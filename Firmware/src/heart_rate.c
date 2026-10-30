#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "app_config.h"
#include "heart_rate.h"

/*
 * Windowed autocorrelation heart-rate estimator for wrist PPG.
 *
 * Beat-by-beat peak picking misses beats on a weak wrist signal and then
 * reports half the rate, and it only reports when a beat is found. Instead,
 * every second this measures the dominant period of the last 8 s of signal:
 *
 * 1. Band-pass 0.5-4 Hz (30-240 bpm) to remove drift and noise.
 * 2. Decimate to 50 Hz and take the first difference. The difference
 *    emphasises the sharp systolic upstroke and suppresses the remaining
 *    baseline drift from breathing and contact pressure.
 * 3. Normalized autocorrelation of the 8 s window over lags for 40-200 bpm.
 *    The peak height (0-1) is the signal quality; below Q_MIN there is no
 *    reliable rhythm and no new reading is made.
 * 4. Choose the peak closest to the previous rate among the near-best ones,
 *    then switch to half the lag if that peak is also strong (a lag of two
 *    periods means half the rate). Parabolic interpolation refines the lag.
 * 5. Median of the last 3 estimates.
 *
 * Steps 1-3 run on the red, IR and green channels; the estimate uses the one
 * with the highest quality. Green is normally best on the wrist, but only when
 * its LED is driven properly (it needs a 4.5-5.5 V LED supply).
 */

#if HR_SAMPLE_RATE_HZ != 100
#error "Band-pass coefficients in heart_rate.c are computed for 100 Hz"
#endif

#define DECIMATION 2
#define FS_DEC (HR_SAMPLE_RATE_HZ / DECIMATION)
#define WINDOW_S 8
#define WINDOW_LEN (WINDOW_S * FS_DEC)
#define SETTLE_LEN (2 * FS_DEC)

#define MIN_BPM 40
#define MAX_BPM 200
#define LAG_MIN (FS_DEC * 60 / MAX_BPM)
#define LAG_MAX (FS_DEC * 60 / MIN_BPM + 1)

/* Minimum autocorrelation peak accepted as a heart rhythm. */
#define Q_MIN 0.5f
/*
 * Minimum for the first reading, when there is no previous rate to track.
 * A marginal first lock can be on half the rate (seen at q 0.51 on a finger).
 */
#define Q_START 0.65f
/* Peaks within this fraction of the best one compete on tracking. */
#define CANDIDATE_FRAC 0.85f
/* Tracking only follows a peak within this many bpm of the last estimate. */
#define TRACK_BPM 20.0f
/* A peak at half the lag this strong means the chosen lag is two periods. */
#define HARMONIC_FRAC 0.7f
/* Seconds to hold the last value through poor signal before reporting 0. */
#define HOLD_S 5

#define MEDIAN_SIZE 3

/* Smoothing for the diagnostic DC and AC levels (time constant ~1 s). */
#define STATS_ALPHA 0.01f

/*
 * Band-pass filter: two cascaded 2nd-order Butterworth high-pass sections at
 * 0.5 Hz and one 2nd-order Butterworth low-pass at 4 Hz. Coefficients are
 * from the RBJ audio-EQ cookbook with Q = 1/sqrt(2), for 100 Hz.
 */
struct biquad {
	float b0, b1, b2, a1, a2;
	float z1, z2;
};

#define HP_0_5HZ {9.780304792e-01f, -1.956060958e+00f, 9.780304792e-01f, \
		  -1.955578240e+00f, 9.565436765e-01f}
#define LP_4HZ   {1.335920003e-02f, 2.671840006e-02f, 1.335920003e-02f, \
		  -1.647459981e+00f, 7.008967812e-01f}

static const float filter_coeffs[][5] = {HP_0_5HZ, HP_0_5HZ, LP_4HZ};

#define NUM_SECTIONS (sizeof(filter_coeffs) / sizeof(filter_coeffs[0]))

struct channel {
	struct biquad filter[NUM_SECTIONS];
	int32_t baseline;
	float prev_filtered;
	/* Ring buffer of differenced, decimated samples. */
	float ring[WINDOW_LEN];
	float acf[LAG_MAX + 2];
	float best;
	float dc;
	float ac_power;
};

static struct channel channels[HR_NUM_CHANNELS];
static bool have_baseline;
static int decimate_count;
/* Next write position in each ring, and decimated samples seen (saturating). */
static int ring_idx;
static int ready_count;
static int since_estimate;
static float fs_dec = FS_DEC;

static float window[WINDOW_LEN];

static float history[MEDIAN_SIZE];
static int history_count;
static int history_idx;
static float last_bpm;
static int bad_count;

/* Transposed direct form II, which keeps float32 rounding error low. */
static float biquad_step(struct biquad *f, float x)
{
	float y = f->b0 * x + f->z1;

	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;

	return y;
}

static float median_history(void)
{
	float sorted[MEDIAN_SIZE];

	for (int i = 0; i < history_count; i++) {
		sorted[i] = history[i];
	}

	for (int i = 0; i < history_count - 1; i++) {
		for (int j = i + 1; j < history_count; j++) {
			if (sorted[i] > sorted[j]) {
				float tmp = sorted[i];

				sorted[i] = sorted[j];
				sorted[j] = tmp;
			}
		}
	}

	return sorted[history_count / 2];
}

static float lag_to_bpm(float lag)
{
	return fs_dec * 60.0f / lag;
}

static bool is_peak(const float *acf, int k)
{
	return acf[k] > acf[k - 1] && acf[k] >= acf[k + 1];
}

/*
 * Normalized autocorrelation of one channel's window for lags
 * LAG_MIN-1 .. LAG_MAX+1, and the height of its highest peak.
 */
static void compute_acf(struct channel *ch)
{
	int start = ring_idx;
	float mean = 0.0f;

	for (int i = 0; i < WINDOW_LEN; i++) {
		window[i] = ch->ring[(start + i) % WINDOW_LEN];
		mean += window[i];
	}
	mean /= WINDOW_LEN;

	float energy = 0.0f;

	for (int i = 0; i < WINDOW_LEN; i++) {
		window[i] -= mean;
		energy += window[i] * window[i];
	}

	/*
	 * For lag k the overlapping parts are window[0..N-k) and window[k..N).
	 * Their energies are the total minus the k samples each one drops.
	 */
	float energy_head = energy;
	float energy_tail = energy;

	for (int k = 1; k <= LAG_MAX + 1; k++) {
		energy_head -= window[WINDOW_LEN - k] * window[WINDOW_LEN - k];
		energy_tail -= window[k - 1] * window[k - 1];

		if (k < LAG_MIN - 1) {
			continue;
		}

		float sum = 0.0f;

		for (int i = 0; i < WINDOW_LEN - k; i++) {
			sum += window[i] * window[i + k];
		}

		float denom = sqrtf(energy_head * energy_tail);

		ch->acf[k] = denom > 0.0f ? sum / denom : 0.0f;
	}

	ch->best = 0.0f;
	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (is_peak(ch->acf, k) && ch->acf[k] > ch->best) {
			ch->best = ch->acf[k];
		}
	}
}

static int quality_percent(float best)
{
	return best > 0.0f ? (int)(best * 100.0f + 0.5f) : 0;
}

static void report_bad(struct hr_estimate *out)
{
	bad_count++;
	if (bad_count > HOLD_S) {
		history_count = 0;
		history_idx = 0;
		last_bpm = 0.0f;
	}

	out->bpm = (int)(last_bpm + 0.5f);
	out->held = last_bpm > 0.0f;
}

static void estimate(struct hr_estimate *out)
{
	enum hr_channel chosen = HR_CHANNEL_RED;

	for (int c = 0; c < HR_NUM_CHANNELS; c++) {
		compute_acf(&channels[c]);
		if (channels[c].best > channels[chosen].best) {
			chosen = c;
		}
	}

	const float *acf = channels[chosen].acf;
	float best = channels[chosen].best;

	out->channel = chosen;
	out->quality = quality_percent(best);

	if (best < (last_bpm > 0.0f ? Q_MIN : Q_START)) {
		report_bad(out);
		return;
	}

	/* Shortest near-best lag, or the one closest to the last rate. */
	int lag = 0;
	int tracked = 0;
	float tracked_diff = TRACK_BPM;

	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (!is_peak(acf, k) || acf[k] < CANDIDATE_FRAC * best) {
			continue;
		}
		if (lag == 0) {
			lag = k;
		}
		if (last_bpm > 0.0f) {
			float diff = fabsf(lag_to_bpm(k) - last_bpm);

			if (diff <= tracked_diff) {
				tracked_diff = diff;
				tracked = k;
			}
		}
	}
	if (tracked != 0) {
		lag = tracked;
	}

	/* Sub-harmonic correction. */
	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (is_peak(acf, k) && abs(2 * k - lag) <= 2 && acf[k] >= HARMONIC_FRAC * acf[lag]) {
			lag = k;
			break;
		}
	}

	float curvature = acf[lag - 1] - 2.0f * acf[lag] + acf[lag + 1];
	float offset = curvature < 0.0f ? 0.5f * (acf[lag - 1] - acf[lag + 1]) / curvature : 0.0f;
	float bpm = lag_to_bpm((float)lag + offset);

	history[history_idx % MEDIAN_SIZE] = bpm;
	history_idx++;
	if (history_count < MEDIAN_SIZE) {
		history_count++;
	}

	last_bpm = median_history();
	bad_count = 0;

	out->bpm = (int)(last_bpm + 0.5f);
	out->held = false;
}

void heart_rate_init(void)
{
	for (int c = 0; c < HR_NUM_CHANNELS; c++) {
		struct channel *ch = &channels[c];

		for (size_t i = 0; i < NUM_SECTIONS; i++) {
			ch->filter[i] = (struct biquad){
				.b0 = filter_coeffs[i][0],
				.b1 = filter_coeffs[i][1],
				.b2 = filter_coeffs[i][2],
				.a1 = filter_coeffs[i][3],
				.a2 = filter_coeffs[i][4],
			};
		}
		ch->baseline = 0;
		ch->prev_filtered = 0.0f;
		ch->best = 0.0f;
		ch->dc = 0.0f;
		ch->ac_power = 0.0f;
	}
	have_baseline = false;
	decimate_count = 0;

	ring_idx = 0;
	ready_count = 0;
	since_estimate = 0;

	history_count = 0;
	history_idx = 0;
	last_bpm = 0.0f;
	bad_count = 0;
}

void heart_rate_set_sample_rate(float hz)
{
	fs_dec = hz / DECIMATION;
}

bool heart_rate_process(const int32_t samples[HR_NUM_CHANNELS], struct hr_estimate *out)
{
	/*
	 * Subtracting the first sample removes the large DC step the high-pass
	 * would otherwise ring on, and keeps the float input precise. The sign
	 * is flipped because more blood means less light at the photodiode.
	 */
	if (!have_baseline) {
		for (int c = 0; c < HR_NUM_CHANNELS; c++) {
			channels[c].baseline = samples[c];
			channels[c].dc = (float)samples[c];
		}
		have_baseline = true;
	}

	bool keep = ++decimate_count >= DECIMATION;

	if (keep) {
		decimate_count = 0;
	}

	for (int c = 0; c < HR_NUM_CHANNELS; c++) {
		struct channel *ch = &channels[c];
		float filtered = (float)(ch->baseline - samples[c]);

		for (size_t i = 0; i < NUM_SECTIONS; i++) {
			filtered = biquad_step(&ch->filter[i], filtered);
		}

		ch->dc += STATS_ALPHA * ((float)samples[c] - ch->dc);
		ch->ac_power += STATS_ALPHA * (filtered * filtered - ch->ac_power);

		if (keep) {
			ch->ring[ring_idx] = filtered - ch->prev_filtered;
			ch->prev_filtered = filtered;
		}
	}

	if (!keep) {
		return false;
	}

	ring_idx = (ring_idx + 1) % WINDOW_LEN;

	/* Wait for the filter to settle and the window to fill, then estimate once a second. */
	if (ready_count < WINDOW_LEN + SETTLE_LEN) {
		ready_count++;
		return false;
	}
	if (++since_estimate < FS_DEC) {
		return false;
	}
	since_estimate = 0;

	if (out != NULL) {
		estimate(out);
	}

	return out != NULL;
}

void heart_rate_get_stats(enum hr_channel channel, struct hr_channel_stats *stats)
{
	const struct channel *ch = &channels[channel];

	stats->dc = (int32_t)ch->dc;
	stats->ac = (int32_t)sqrtf(ch->ac_power);
	stats->quality = quality_percent(ch->best);
}
