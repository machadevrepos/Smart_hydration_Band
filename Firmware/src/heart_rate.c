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
 * 1. Band-pass 0.9-3 Hz to remove drift and noise. Recorded wrist IR has slow
 *    lines at 0.2-0.7 Hz; passing them locked onto half the rate (84 bpm read
 *    as 42), so the corner sits above them at the cost of a weaker signal
 *    below about 50 bpm.
 * 2. Decimate to 50 Hz. The wrist pulse is nearly a sine, so differencing here
 *    would only boost noise: on the same recording it gave 2 live readings out
 *    of 14 against 14 out of 14 without it.
 * 3. Normalized autocorrelation of the 8 s window over lags for 40-200 bpm.
 *    The peak height (0-1) is the signal quality; below Q_MIN there is no
 *    reliable rhythm and no new reading is made.
 *    One-second segments far louder than the rest (motion bursts) are zeroed
 *    first, and a window whose level is still decaying (the board settling
 *    after placement) is rejected.
 * 4. Take the shortest lag whose peak is nearly as high as the best one, or
 *    the peak closest to the previous rate, then half that lag if its peak is
 *    also strong. A rate far from the previous one must persist before it is
 *    shown. Parabolic interpolation refines the lag.
 * 5. Median of the last 3 estimates.
 */

#if HR_SAMPLE_RATE_HZ != 100
#error "Band-pass coefficients in heart_rate.c are computed for 100 Hz"
#endif

#define DECIMATION 2
#define FS_DEC (HR_SAMPLE_RATE_HZ / DECIMATION)
#define WINDOW_S 8
#define WINDOW_LEN (WINDOW_S * FS_DEC)
#define SETTLE_LEN (2 * FS_DEC)

_Static_assert((WINDOW_LEN + SETTLE_LEN + FS_DEC) / FS_DEC == HR_FIRST_ESTIMATE_S,
	       "HR_FIRST_ESTIMATE_S must match the window and settling time");

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
#define Q_START 0.6f
/*
 * With no previous rate, take the shortest lag with a peak this close to the
 * best. A lower peak at a shorter lag is a harmonic of the pulse shape (the
 * second bump of each beat), not the rate: following it read 66 bpm as 138.
 */
#define PICK_FRAC 0.9f
/*
 * A peak at half the lag this high relative to the chosen one means the chosen
 * lag is two periods. Lower values read a strong second bump as double the rate.
 */
#define HALF_LAG_FRAC 0.8f
/* A peak this close to the best can be followed from the previous rate... */
#define TRACK_FRAC 0.7f
/* ...if it is within this many bpm of it. */
#define TRACK_BPM 20.0f
/* Seconds a different rate with no tracked peak must persist before it shows. */
#define JUMP_S 5
/* Seconds the shortest-lag pick may disagree with a tracked peak before it wins. */
#define RELOCK_S 10
/* Seconds to hold the last value through poor signal before reporting 0. */
#define HOLD_S 5

/* Rejected as still settling if the oldest 2 s are this much louder than the newest. */
#define SETTLING_LEN (2 * FS_DEC)
#define SETTLING_RATIO 2.5f
/* A 1 s segment this much louder than the median segment is zeroed as a burst. */
#define SEGMENT_LEN FS_DEC
#define SEGMENT_COUNT WINDOW_S
#define BURST_RATIO 2.0f

#define MEDIAN_SIZE 3

/*
 * Band-pass filter: two cascaded 2nd-order Butterworth high-pass sections at
 * 0.9 Hz and one 2nd-order Butterworth low-pass at 3 Hz. Coefficients are
 * from the RBJ audio-EQ cookbook with Q = 1/sqrt(2), for 100 Hz.
 */
struct biquad {
	float b0, b1, b2, a1, a2;
	float z1, z2;
};

#define HP_0_9HZ {9.608026442e-01f, -1.921605288e+00f, 9.608026442e-01f, \
		  -1.920068265e+00f, 9.231423118e-01f}
#define LP_3HZ   {7.820208033e-03f, 1.564041607e-02f, 7.820208033e-03f, \
		  -1.734725769e+00f, 7.660066009e-01f}

static const float filter_coeffs[][5] = {HP_0_9HZ, HP_0_9HZ, LP_3HZ};

#define NUM_SECTIONS (sizeof(filter_coeffs) / sizeof(filter_coeffs[0]))

static struct biquad filter[NUM_SECTIONS];
static int32_t baseline;
/* Ring buffer of filtered, decimated samples. */
static float ring[WINDOW_LEN];
static float acf[LAG_MAX + 2];
static bool have_baseline;
static int decimate_count;
/* Next write position in the ring, and decimated samples seen (saturating). */
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
/* Shortest-lag pick that disagrees with last_bpm, and for how many seconds. */
static int differ_lag;
static int differ_count;

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

static bool is_peak(int k)
{
	return acf[k] > acf[k - 1] && acf[k] >= acf[k + 1];
}

static float rms(const float *x, int len)
{
	float sum = 0.0f;

	for (int i = 0; i < len; i++) {
		sum += x[i] * x[i];
	}

	return sqrtf(sum / len);
}

/* True if the oldest samples of the window are much louder than the newest. */
static bool window_settling(void)
{
	float head = rms(window, SETTLING_LEN);
	float tail = rms(&window[WINDOW_LEN - SETTLING_LEN], SETTLING_LEN);

	return tail > 0.0f && head > SETTLING_RATIO * tail;
}

/* Zero the 1 s segments far above the median level of the window. */
static void mask_bursts(void)
{
	float level[SEGMENT_COUNT];
	float sorted[SEGMENT_COUNT] = {0.0f};

	for (int s = 0; s < SEGMENT_COUNT; s++) {
		level[s] = rms(&window[s * SEGMENT_LEN], SEGMENT_LEN);

		/* Insertion sort, the list is short. */
		int i = s;

		while (i > 0 && sorted[i - 1] > level[s]) {
			sorted[i] = sorted[i - 1];
			i--;
		}
		sorted[i] = level[s];
	}

	float median = 0.5f * (sorted[SEGMENT_COUNT / 2 - 1] + sorted[SEGMENT_COUNT / 2]);

	for (int s = 0; s < SEGMENT_COUNT; s++) {
		if (median > 0.0f && level[s] > BURST_RATIO * median) {
			for (int i = 0; i < SEGMENT_LEN; i++) {
				window[s * SEGMENT_LEN + i] = 0.0f;
			}
		}
	}
}

/*
 * Normalized autocorrelation of the window for lags LAG_MIN-1 .. LAG_MAX+1.
 * Sets *settling and returns the height of the highest peak.
 */
static float compute_acf(bool *settling)
{
	int start = ring_idx;
	float mean = 0.0f;

	for (int i = 0; i < WINDOW_LEN; i++) {
		window[i] = ring[(start + i) % WINDOW_LEN];
	}

	*settling = window_settling();
	mask_bursts();

	for (int i = 0; i < WINDOW_LEN; i++) {
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

		acf[k] = denom > 0.0f ? sum / denom : 0.0f;
	}

	float best = 0.0f;

	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (is_peak(k) && acf[k] > best) {
			best = acf[k];
		}
	}

	return best;
}

static int quality_percent(float best)
{
	return best > 0.0f ? (int)(best * 100.0f + 0.5f) : 0;
}

static void restart_tracking(void)
{
	history_count = 0;
	history_idx = 0;
	differ_lag = 0;
	differ_count = 0;
}

static void report_bad(struct hr_estimate *out)
{
	bad_count++;
	if (bad_count > HOLD_S) {
		restart_tracking();
		last_bpm = 0.0f;
	}

	out->bpm = (int)(last_bpm + 0.5f);
	out->held = last_bpm > 0.0f;
}

/* The strong peak at half of lag k, or 0 if there is none. */
static int find_half_lag(int k)
{
	for (int h = LAG_MIN; h <= LAG_MAX; h++) {
		if (is_peak(h) && abs(2 * h - k) <= 2 && acf[h] >= HALF_LAG_FRAC * acf[k]) {
			return h;
		}
	}

	return 0;
}

/*
 * The peak closest to the last rate, or 0. A peak with a strong peak at half
 * its lag is two periods of a faster rate, not this rate.
 */
static int find_tracked_lag(float best)
{
	int tracked = 0;
	float tracked_diff = TRACK_BPM;

	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (!is_peak(k) || acf[k] < TRACK_FRAC * best) {
			continue;
		}

		float diff = fabsf(lag_to_bpm(k) - last_bpm);

		if (diff <= tracked_diff && find_half_lag(k) == 0) {
			tracked_diff = diff;
			tracked = k;
		}
	}

	return tracked;
}

static void estimate(struct hr_estimate *out)
{
	float best = compute_acf(&out->settling);

	out->quality = quality_percent(best);

	if (out->settling || best < (last_bpm > 0.0f ? Q_MIN : Q_START)) {
		report_bad(out);
		return;
	}

	/* Shortest lag with a peak close to the best. */
	int pick = 0;

	for (int k = LAG_MIN; k <= LAG_MAX; k++) {
		if (is_peak(k) && acf[k] >= PICK_FRAC * best) {
			pick = k;
			break;
		}
	}

	int half = find_half_lag(pick);

	if (half != 0) {
		pick = half;
	}

	int lag = pick;

	if (last_bpm > 0.0f) {
		int tracked = find_tracked_lag(best);

		if (fabsf(lag_to_bpm(pick) - last_bpm) <= TRACK_BPM) {
			differ_lag = 0;
			differ_count = 0;
		} else if (differ_lag != 0 && abs(pick - differ_lag) <= 2) {
			differ_count++;
		} else {
			differ_lag = pick;
			differ_count = 1;
		}

		if (tracked != 0 && differ_count < RELOCK_S) {
			lag = tracked;
		} else if (tracked == 0 && differ_count < JUMP_S) {
			/* A different rate has not persisted yet: keep showing the last one. */
			bad_count = 0;
			out->bpm = (int)(last_bpm + 0.5f);
			out->held = true;
			return;
		} else {
			restart_tracking();
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
	for (size_t i = 0; i < NUM_SECTIONS; i++) {
		filter[i] = (struct biquad){
			.b0 = filter_coeffs[i][0],
			.b1 = filter_coeffs[i][1],
			.b2 = filter_coeffs[i][2],
			.a1 = filter_coeffs[i][3],
			.a2 = filter_coeffs[i][4],
		};
	}
	baseline = 0;
	have_baseline = false;
	decimate_count = 0;

	ring_idx = 0;
	ready_count = 0;
	since_estimate = 0;

	restart_tracking();
	last_bpm = 0.0f;
	bad_count = 0;
}

void heart_rate_set_sample_rate(float hz)
{
	fs_dec = hz / DECIMATION;
}

bool heart_rate_process(int32_t sample, struct hr_estimate *out)
{
	/*
	 * Subtracting the first sample removes the large DC step the high-pass
	 * would otherwise ring on, and keeps the float input precise. The sign
	 * is flipped because more blood means less light at the photodiode.
	 */
	if (!have_baseline) {
		baseline = sample;
		have_baseline = true;
	}

	float filtered = (float)(baseline - sample);

	for (size_t i = 0; i < NUM_SECTIONS; i++) {
		filtered = biquad_step(&filter[i], filtered);
	}

	if (++decimate_count < DECIMATION) {
		return false;
	}
	decimate_count = 0;

	ring[ring_idx] = filtered;
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

	estimate(out);

	return true;
}
