#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "app_config.h"
#include "max30101.h"
#include "spo2.h"
#include "step_counter.h"

LOG_MODULE_REGISTER(spo2, LOG_LEVEL_INF);

#if APP_SPO2_BENCH_MODE
#warning "SpO2 bench mode is on (APP_SPO2_BENCH_MODE in app_config.h): not for delivery"
#endif

#if MAX30101_SAMPLE_RATE_HZ != 100
#error "SpO2 timing and filter coefficients are for 100 Hz"
#endif

/* Timing, in samples at 100 Hz. */
#define SKIP_AFTER_LED_CHANGE 20  /* The level jumps when the red current changes. */
#define AGC_SAMPLES           100 /* Level measured to set the red current. */
#define AGC_MAX_ROUNDS        3
#define SETTLE_SAMPLES        200 /* Filters settle before the first window. */
#define WINDOW_SAMPLES        400 /* One 4 s window, several beats. */
/*
 * Windows are measured until TARGET_GOOD_WINDOWS are good (about 30 s on a
 * still, well-fitted wrist) or MAX_WINDOWS have been measured (about 55 s, for a
 * loose fit that rejects some). Fewer than MIN_GOOD_WINDOWS good gives no value.
 */
#define MAX_WINDOWS           12
#define TARGET_GOOD_WINDOWS   6
#define MIN_GOOD_WINDOWS      4
/* Give up early when this many windows are done and fewer than 2 were good. */
#define GIVE_UP_WINDOWS       5
/* A measurement that has not finished by then has lost its samples. */
#define TIMEOUT_MS            80000

/* Red LED current, 0.2 mA per step: start at ~19 mA, aim for this ADC level. */
#define RED_PA_START  0x60
#define RED_PA_MIN    0x08
#define RED_PA_MAX    0xFF
#define RED_TARGET_DC 120000.0f
/* Within this share of the target the current is left alone. */
#define AGC_TOLERANCE 0.15f

/* Usable DC range of the 18-bit ADC (full scale 262143). */
#define DC_MIN 20000.0f
#define DC_MAX 250000.0f

/*
 * Window acceptance. The method follows R. Fraczkiewicz's open-source
 * MAX30102 algorithm ("MAX30102_by_RF", the usual replacement for Maxim's
 * RD117 reference code): DC is the mean, the pulse is taken from the detrended
 * (here band-passed) signal, and red and IR must correlate (Pearson). RF asks
 * for 0.8 on a fingertip. A wrist in a loose enclosure correlates less, so a
 * window needs 0.70 here, and the result is not taken from any single window
 * but pooled over all good ones (see conclude()), which averages the noise
 * that a lower threshold lets in.
 *
 * The wrist pulse on this board is small (IR AC 150-280 ppm of DC at rest held
 * firmly, less when loose), so the perfusion floor is low.
 */
#define MIN_CORRELATION 0.70f    /* Red and IR pulses must move together. */
/*
 * RF's second check: the signal must repeat at the pulse rate (its normalized
 * autocorrelation peak, "min_autocorrelation_ratio" 0.5 in RF). The heart-rate
 * estimator already computes exactly that for the IR channel every second, as
 * its quality, so a window needs its mean quality at or above this. On the
 * wrist, 65 (the "fair" level) is used: a band shifting on the skin gives red
 * and IR swings that correlate well (R near 1, about 85%) while the heart rate
 * shows "no clear pulse" (quality 20-50); this check throws those windows out.
 */
#define MIN_PULSE_QUALITY 65
#define MIN_PERFUSION   0.00002f /* AC/DC, 20 ppm. */
#define MAX_MOTION_MG   100      /* Largest swing on any axis within a window. */
/* Valid R range, as in the RF algorithm (0.02-1.84). */
#define RATIO_MIN       0.02f
#define RATIO_MAX       1.84f
/*
 * Good windows further than this share from the median R are left out of the
 * pooled result (a window can pass the checks and still carry an artifact).
 * At least MIN_GOOD_WINDOWS must remain.
 */
#define AGREE_FRACTION  0.30f

/*
 * Reported range. At rest, a wrist reading under 80% is far more likely a bad
 * signal than real, and showing it would alarm the wearer, so such a result
 * counts as an unstable signal and is retried instead of reported.
 */
#define SPO2_MIN_PERCENT 80
#define SPO2_MAX_PERCENT 100
/*
 * A pooled R at or above this is treated as an unstable signal whatever the
 * calibration says. Red and IR swinging by the same fraction (R near 1) is the
 * signature of the band moving on the skin rather than of blood, and no
 * healthy wearer at rest reads it (this band: 0.32-0.52). The published curves
 * put R 1.0 at about 80%, so this keeps the same cut-off with the flatter
 * interim calibration, which would show it as 88%.
 */
#define RATIO_MAX_RESULT 1.0f

/* Band-pass as in heart_rate.c: 2x high-pass 0.9 Hz, low-pass 3 Hz, at 100 Hz. */
struct biquad {
	float b0, b1, b2, a1, a2;
	float z1, z2;
};

static const float filter_coeffs[][5] = {
	{9.608026442e-01f, -1.921605288e+00f, 9.608026442e-01f, -1.920068265e+00f,
	 9.231423118e-01f},
	{9.608026442e-01f, -1.921605288e+00f, 9.608026442e-01f, -1.920068265e+00f,
	 9.231423118e-01f},
	{7.820208033e-03f, 1.564041607e-02f, 7.820208033e-03f, -1.734725769e+00f,
	 7.660066009e-01f},
};

#define NUM_SECTIONS ARRAY_SIZE(filter_coeffs)

struct channel_filter {
	struct biquad section[NUM_SECTIONS];
	float baseline;
};

enum state {
	STATE_IDLE,
	STATE_AGC,
	STATE_SETTLE,
	STATE_COLLECT,
};

/* Why a window was rejected, for the summary. */
enum reject {
	REJECT_NONE,
	REJECT_MOTION,
	REJECT_WEAK,
	REJECT_UNSTABLE,
	REJECT_NO_PULSE,
	REJECT_COUNT,
};

/* Main loop only. */
static bool sensor_ok;
static enum state state = STATE_IDLE;
static int64_t next_due_ms;
static int64_t started_ms;
static uint8_t red_pa;
static uint8_t last_good_pa = RED_PA_START;
static int agc_rounds;
static int sample_count;
static int window_index;
static uint32_t pending_events;

static struct channel_filter red_filter;
static struct channel_filter ir_filter;

/* Running sums for the AGC pass or the current window. */
static struct {
	double dc_red;
	double dc_ir;
	double red2;
	double ir2;
	double red_ir;
	bool accel_seen;
	int16_t accel_min[3];
	int16_t accel_max[3];
	/* Heart-rate quality over the window: sum and number of main loop passes. */
	int32_t pulse_quality_sum;
	int32_t pulse_quality_passes;
} acc;

/*
 * Each good window: its R, and its DC-normalized sums (red x IR, IR^2, red^2,
 * pulse parts only) so the windows can be pooled into one estimate.
 */
static struct {
	float ratio;
	double sxy;
	double sxx;
	double syy;
} good[MAX_WINDOWS];
static int good_windows;
static int rejects[REJECT_COUNT];

/* Shared with other threads. */
static atomic_t requested;
static struct k_spinlock result_lock;
static struct spo2_result result;

const char *spo2_status_name(enum spo2_status status)
{
	switch (status) {
	case SPO2_STATUS_OK:
		return "ok";
	case SPO2_STATUS_MEASURING:
		return "measuring";
	case SPO2_STATUS_NO_CONTACT:
		return "not on the skin";
	case SPO2_STATUS_MOTION:
		return "too much movement";
	case SPO2_STATUS_WEAK:
		return "pulse too weak";
	case SPO2_STATUS_UNSTABLE:
		return "unstable signal";
	case SPO2_STATUS_SENSOR:
		return "sensor unavailable";
	default:
		return "none";
	}
}

static void set_status(enum spo2_status status)
{
	k_spinlock_key_t key = k_spin_lock(&result_lock);

	result.status = status;
	k_spin_unlock(&result_lock, key);
}

void spo2_get_result(struct spo2_result *out)
{
	k_spinlock_key_t key = k_spin_lock(&result_lock);

	*out = result;
	k_spin_unlock(&result_lock, key);
}

void spo2_request(void)
{
	atomic_set(&requested, 1);
}

bool spo2_measuring(void)
{
	return state != STATE_IDLE;
}

void spo2_init(bool ok)
{
	sensor_ok = ok;
	next_due_ms = k_uptime_get() + (int64_t)APP_SPO2_FIRST_S * 1000;
	set_status(ok ? SPO2_STATUS_NONE : SPO2_STATUS_SENSOR);
}

static void filter_reset(struct channel_filter *f, int32_t first)
{
	for (size_t i = 0; i < NUM_SECTIONS; i++) {
		f->section[i] = (struct biquad){
			.b0 = filter_coeffs[i][0],
			.b1 = filter_coeffs[i][1],
			.b2 = filter_coeffs[i][2],
			.a1 = filter_coeffs[i][3],
			.a2 = filter_coeffs[i][4],
		};
	}
	f->baseline = (float)first;
}

static float filter_step(struct channel_filter *f, int32_t x)
{
	float y = (float)x - f->baseline;

	for (size_t i = 0; i < NUM_SECTIONS; i++) {
		struct biquad *s = &f->section[i];
		float out = s->b0 * y + s->z1;

		s->z1 = s->b1 * y - s->a1 * out + s->z2;
		s->z2 = s->b2 * y - s->a2 * out;
		y = out;
	}
	return y;
}

static void clear_sums(void)
{
	acc.dc_red = 0.0;
	acc.dc_ir = 0.0;
	acc.red2 = 0.0;
	acc.ir2 = 0.0;
	acc.red_ir = 0.0;
	acc.accel_seen = false;
	acc.pulse_quality_sum = 0;
	acc.pulse_quality_passes = 0;
}

static int set_red(uint8_t pa)
{
	int ret = max30101_set_red_current(pa);

	if (ret == 0) {
		red_pa = pa;
	}
	return ret;
}

static void print_red_current(const char *what, float dc_red, float dc_ir)
{
	printk("  SpO2: %s, red LED %u.%u mA (red level %d, IR level %d)\n", what, red_pa / 5,
	       (red_pa * 2) % 10, (int)dc_red, (int)dc_ir);
}

static float median(float *v, int n)
{
	for (int i = 1; i < n; i++) {
		float x = v[i];
		int j = i;

		while (j > 0 && v[j - 1] > x) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = x;
	}
	return (n % 2) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

/*
 * SpO2 from R with the calibration curve in app_config.h (an interim line by
 * default), before the 80-100% limits.
 */
static float spo2_from_ratio(float r)
{
	/*
	 * SpO2 must fall as R rises. A quadratic only does that on one side of its
	 * turning point, so R is held at that point beyond it: below it for a
	 * downward curve such as RD117's (peak at R 0.34), above it for an upward
	 * one such as the default (turning point at R 10.9, never reached).
	 */
	if (APP_SPO2_CAL_A != 0.0f) {
		float turn = -APP_SPO2_CAL_B / (2.0f * APP_SPO2_CAL_A);

		r = APP_SPO2_CAL_A < 0.0f ? MAX(r, turn) : MIN(r, turn);
	}
	return (APP_SPO2_CAL_A * r + APP_SPO2_CAL_B) * r + APP_SPO2_CAL_C;
}

/* End the measurement: red LED off, result stored, next one scheduled. */
static void finish(enum spo2_status status, float ratio, uint8_t quality)
{
	int64_t now = k_uptime_get();

	(void)set_red(0);
	state = STATE_IDLE;

	k_spinlock_key_t key = k_spin_lock(&result_lock);

	result.status = status;
	result.ratio_milli = ratio > 0.0f ? (uint16_t)MIN(ratio * 1000.0f + 0.5f, 65535.0f) : 0;
	if (status == SPO2_STATUS_OK) {
		float spo2 = CLAMP(spo2_from_ratio(ratio), (float)SPO2_MIN_PERCENT,
				   (float)SPO2_MAX_PERCENT);

		result.percent = (uint8_t)(spo2 + 0.5f);
		result.quality = quality;
		result.uptime_ms = now;
	}
	k_spin_unlock(&result_lock, key);

	next_due_ms = now + (int64_t)(status == SPO2_STATUS_OK ? APP_SPO2_INTERVAL_S
							      : APP_SPO2_RETRY_S) *
				    1000;
	pending_events |= SPO2_EVENT_DONE;
}

/*
 * Judge one 4 s window and keep it if it is good.
 *
 * R is the ratio of ratios (red AC/DC) / (IR AC/DC). RF and Maxim take each AC
 * as the RMS (or peak-to-peak) of its own channel, so noise in the red channel,
 * which has the weaker pulse, adds to red AC and pushes R up and SpO2 down as
 * the fit gets looser. Here red AC is instead the part of the red pulse that
 * follows the IR pulse: the least-squares slope of red against IR,
 *
 *   R = (sum red*IR / sum IR^2) * (DC IR / DC red),
 *
 * which equals the RMS ratio for a clean signal (correlation 1) and is not
 * raised by red noise. The RMS-based R is printed too, for comparison.
 */
static void close_window(void)
{
	float n = (float)WINDOW_SAMPLES;
	float dc_red = (float)(acc.dc_red / n);
	float dc_ir = (float)(acc.dc_ir / n);
	float ac_red = sqrtf((float)(acc.red2 / n));
	float ac_ir = sqrtf((float)(acc.ir2 / n));
	float denom = sqrtf((float)(acc.red2 * acc.ir2));
	float corr = denom > 0.0f ? (float)(acc.red_ir / denom) : 0.0f;
	float pi_red = dc_red > 0.0f ? ac_red / dc_red : 0.0f;
	float pi_ir = dc_ir > 0.0f ? ac_ir / dc_ir : 0.0f;
	float ratio_rms = pi_ir > 0.0f ? pi_red / pi_ir : 0.0f;
	float ratio = (acc.ir2 > 0.0 && dc_red > 0.0f)
			      ? (float)(acc.red_ir / acc.ir2) * dc_ir / dc_red
			      : 0.0f;
	int motion_mg = -1;
	int pulse_quality = acc.pulse_quality_passes > 0
				    ? acc.pulse_quality_sum / acc.pulse_quality_passes
				    : 0;
	enum reject why = REJECT_NONE;

	if (acc.accel_seen) {
		int range = 0;

		for (int a = 0; a < 3; a++) {
			range = MAX(range, acc.accel_max[a] - acc.accel_min[a]);
		}
		motion_mg = range * 1000 / STEP_COUNTER_ACCEL_PER_G;
	}

	if (motion_mg > MAX_MOTION_MG) {
		why = REJECT_MOTION;
	} else if (dc_red < DC_MIN || dc_red > DC_MAX || dc_ir < DC_MIN || dc_ir > DC_MAX ||
		   pi_red < MIN_PERFUSION || pi_ir < MIN_PERFUSION) {
		why = REJECT_WEAK;
	} else if (pulse_quality < MIN_PULSE_QUALITY) {
		why = REJECT_NO_PULSE;
	} else if (corr < MIN_CORRELATION || ratio < RATIO_MIN || ratio > RATIO_MAX) {
		why = REJECT_UNSTABLE;
	}

	/*
	 * Out of range only on a rejected window: bound it so the line stays readable
	 * (a negative R or a huge one from a near-flat IR window).
	 */
	ratio = CLAMP(ratio, 0.0f, 99.0f);
	ratio_rms = CLAMP(ratio_rms, 0.0f, 99.0f);
	corr = MAX(corr, 0.0f);

	/* One line per window: what calibration and threshold tuning need. */
	printk("  SpO2 window %d: R %d.%03d (RMS %d.%03d), PI red %d ppm, IR %d ppm, corr %d%%, "
	       "pulse %d, move %d mg, DC red %d IR %d, %s\n",
	       window_index + 1, (int)ratio, (int)(ratio * 1000.0f) % 1000, (int)ratio_rms,
	       (int)(ratio_rms * 1000.0f) % 1000, (int)(pi_red * 1e6f), (int)(pi_ir * 1e6f),
	       (int)(corr * 100.0f), pulse_quality, motion_mg, (int)dc_red, (int)dc_ir,
	       why == REJECT_NONE       ? "good"
	       : why == REJECT_MOTION   ? "rejected: movement"
	       : why == REJECT_WEAK     ? "rejected: weak or out of range"
	       : why == REJECT_NO_PULSE ? "rejected: no clear pulse"
					: "rejected: red and IR differ");

	rejects[why]++;
	if (why == REJECT_NONE && good_windows < MAX_WINDOWS) {
		double norm_xy = (double)dc_red * dc_ir;
		double norm_xx = (double)dc_ir * dc_ir;
		double norm_yy = (double)dc_red * dc_red;

		good[good_windows].ratio = ratio;
		good[good_windows].sxy = acc.red_ir / norm_xy;
		good[good_windows].sxx = acc.ir2 / norm_xx;
		good[good_windows].syy = acc.red2 / norm_yy;
		good_windows++;
	}
}

/*
 * After the last window: decide the result. The good windows near the median R
 * are pooled into one least-squares fit of red against IR, which weights each
 * window by the strength of its pulse and gives one R from 16-50 s of signal,
 * steadier than the median of separate 4 s estimates.
 */
static void conclude(void)
{
	if (good_windows < MIN_GOOD_WINDOWS) {
		enum spo2_status status = SPO2_STATUS_UNSTABLE;
		float sorted[MAX_WINDOWS];
		/*
		 * No clear pulse while the accelerometer is quiet is the band shifting
		 * on the skin: the same advice as movement (keep still, fit the band).
		 */
		int moved = rejects[REJECT_MOTION] + rejects[REJECT_NO_PULSE];

		if (moved >= rejects[REJECT_WEAK] && moved >= rejects[REJECT_UNSTABLE]) {
			status = SPO2_STATUS_MOTION;
		} else if (rejects[REJECT_WEAK] >= rejects[REJECT_UNSTABLE]) {
			status = SPO2_STATUS_WEAK;
		}
		for (int i = 0; i < good_windows; i++) {
			sorted[i] = good[i].ratio;
		}
		printk("  SpO2 summary: %d of %d windows good (moved %d, no clear pulse %d, weak %d, "
		       "red/IR differ %d)\n",
		       good_windows, window_index, rejects[REJECT_MOTION], rejects[REJECT_NO_PULSE],
		       rejects[REJECT_WEAK], rejects[REJECT_UNSTABLE]);
		finish(status, good_windows > 0 ? median(sorted, good_windows) : 0.0f, 0);
		return;
	}

	float sorted[MAX_WINDOWS];

	for (int i = 0; i < good_windows; i++) {
		sorted[i] = good[i].ratio;
	}
	float r_med = median(sorted, good_windows);
	float spread = r_med > 0.0f ? (sorted[good_windows - 1] - sorted[0]) / r_med : 0.0f;
	double sxy = 0.0;
	double sxx = 0.0;
	double syy = 0.0;
	int agree = 0;

	for (int i = 0; i < good_windows; i++) {
		if (fabsf(good[i].ratio - r_med) <= AGREE_FRACTION * r_med) {
			sxy += good[i].sxy;
			sxx += good[i].sxx;
			syy += good[i].syy;
			agree++;
		}
	}

	float r = sxx > 0.0 ? (float)(sxy / sxx) : 0.0f;
	float corr = (sxx > 0.0 && syy > 0.0) ? (float)(sxy / sqrt(sxx * syy)) : 0.0f;

	corr = MAX(corr, 0.0f);

	/* The whole measurement on one line, for calibration notes. */
	printk("  SpO2 summary: %d of %d windows good, %d pooled, R %d.%03d (median %d.%03d, "
	       "spread %d%%), corr %d%%\n",
	       good_windows, window_index, agree, (int)r, (int)(r * 1000.0f) % 1000, (int)r_med,
	       (int)(r_med * 1000.0f) % 1000, (int)(spread * 100.0f + 0.5f),
	       (int)(corr * 100.0f));

	if (agree < MIN_GOOD_WINDOWS || r < RATIO_MIN || r > RATIO_MAX) {
		finish(SPO2_STATUS_UNSTABLE, r_med, 0);
		return;
	}

	if (r >= RATIO_MAX_RESULT || spo2_from_ratio(r) < SPO2_MIN_PERCENT) {
		printk("  SpO2 R %d.%03d is outside a resting reading (R under 1, SpO2 %d%% or more): "
		       "treated as an unstable signal\n",
		       (int)r, (int)(r * 1000.0f) % 1000, SPO2_MIN_PERCENT);
		finish(SPO2_STATUS_UNSTABLE, r, 0);
		return;
	}

	/* Share of measured windows that were used, times how well red and IR matched. */
	float quality = 100.0f * (float)agree / (float)window_index * corr;

	finish(SPO2_STATUS_OK, r, (uint8_t)CLAMP(quality + 0.5f, 0.0f, 100.0f));
}

static void start(int64_t now)
{
	agc_rounds = 0;
	sample_count = 0;
	window_index = 0;
	good_windows = 0;
	for (size_t i = 0; i < ARRAY_SIZE(rejects); i++) {
		rejects[i] = 0;
	}
	clear_sums();
	started_ms = now;

	if (set_red(last_good_pa) < 0) {
		finish(SPO2_STATUS_SENSOR, 0.0f, 0);
		return;
	}

	state = STATE_AGC;
	set_status(SPO2_STATUS_MEASURING);
	pending_events |= SPO2_EVENT_STARTED;
}

/* AGC pass done: set the red current for the target level, or move on. */
static void agc_done(void)
{
	float dc_red = (float)(acc.dc_red / AGC_SAMPLES);
	float dc_ir = (float)(acc.dc_ir / AGC_SAMPLES);
	float scale = dc_red > 1.0f ? RED_TARGET_DC / dc_red : 4.0f;
	bool close = fabsf(scale - 1.0f) <= AGC_TOLERANCE;

	if (!close && agc_rounds < AGC_MAX_ROUNDS) {
		uint8_t pa = (uint8_t)CLAMP((float)red_pa * scale + 0.5f, (float)RED_PA_MIN,
					    (float)RED_PA_MAX);

		agc_rounds++;
		if (pa != red_pa) {
			if (set_red(pa) < 0) {
				finish(SPO2_STATUS_SENSOR, 0.0f, 0);
				return;
			}
			sample_count = 0;
			clear_sums();
			return;
		}
	}

	/* Start the next measurement here, so it skips most of this adjustment. */
	last_good_pa = red_pa;
	print_red_current("measuring, keep still", dc_red, dc_ir);
	state = STATE_SETTLE;
	sample_count = 0;
}

void spo2_add_sample(const struct max30101_sample *s)
{
	switch (state) {
	case STATE_AGC:
		if (++sample_count <= SKIP_AFTER_LED_CHANGE) {
			return;
		}
		acc.dc_red += s->red;
		acc.dc_ir += s->ir;
		if (sample_count >= SKIP_AFTER_LED_CHANGE + AGC_SAMPLES) {
			agc_done();
		}
		return;

	case STATE_SETTLE:
		if (sample_count == 0) {
			filter_reset(&red_filter, s->red);
			filter_reset(&ir_filter, s->ir);
		}
		(void)filter_step(&red_filter, s->red);
		(void)filter_step(&ir_filter, s->ir);
		if (++sample_count >= SETTLE_SAMPLES) {
			state = STATE_COLLECT;
			sample_count = 0;
			clear_sums();
		}
		return;

	case STATE_COLLECT: {
		float red_ac = filter_step(&red_filter, s->red);
		float ir_ac = filter_step(&ir_filter, s->ir);

		acc.dc_red += s->red;
		acc.dc_ir += s->ir;
		acc.red2 += (double)red_ac * red_ac;
		acc.ir2 += (double)ir_ac * ir_ac;
		acc.red_ir += (double)red_ac * ir_ac;

		if (++sample_count >= WINDOW_SAMPLES) {
			close_window();
			sample_count = 0;
			clear_sums();
			window_index++;
			/*
			 * Stop as soon as the result is settled: enough good windows, all
			 * windows done, or too many rejected to be worth going on (keeps
			 * the red LED from burning on a wrist that keeps moving).
			 */
			if (good_windows >= TARGET_GOOD_WINDOWS || window_index >= MAX_WINDOWS ||
			    good_windows + (MAX_WINDOWS - window_index) < MIN_GOOD_WINDOWS ||
			    (window_index >= GIVE_UP_WINDOWS && good_windows < 2)) {
				conclude();
			}
		}
		return;
	}

	default:
		return;
	}
}

void spo2_sensor_gap(void)
{
	if (state != STATE_IDLE) {
		finish(SPO2_STATUS_SENSOR, 0.0f, 0);
	}
}

/* Per main loop pass while collecting: heart-rate quality and the motion range. */
static void track_inputs(const struct spo2_inputs *in)
{
	if (state != STATE_COLLECT) {
		return;
	}

	acc.pulse_quality_sum += CLAMP(in->pulse_quality, 0, 100);
	acc.pulse_quality_passes++;

	if (!in->accel_ok) {
		return;
	}

	for (int a = 0; a < 3; a++) {
		if (!acc.accel_seen) {
			acc.accel_min[a] = in->accel[a];
			acc.accel_max[a] = in->accel[a];
		} else {
			acc.accel_min[a] = MIN(acc.accel_min[a], in->accel[a]);
			acc.accel_max[a] = MAX(acc.accel_max[a], in->accel[a]);
		}
	}
	acc.accel_seen = true;
}

uint32_t spo2_process(const struct spo2_inputs *in)
{
	int64_t now = k_uptime_get();
	uint32_t events;

	if (state == STATE_IDLE) {
		bool asked = atomic_set(&requested, 0) != 0;

		if (asked || now >= next_due_ms) {
			if (!sensor_ok) {
				if (asked) {
					finish(SPO2_STATUS_SENSOR, 0.0f, 0);
				}
				next_due_ms = now + (int64_t)APP_SPO2_INTERVAL_S * 1000;
			} else if (!in->contact) {
				/* Asked: answer at once. Scheduled: look again shortly. */
				if (asked) {
					finish(SPO2_STATUS_NO_CONTACT, 0.0f, 0);
				} else {
					next_due_ms = now + (int64_t)APP_SPO2_WAIT_S * 1000;
				}
			} else if (!in->pulse && !asked) {
				/* Wait for the heart-rate lock, which shows there is a pulse. */
				next_due_ms = now + (int64_t)APP_SPO2_WAIT_S * 1000;
			} else {
				start(now);
			}
		}
	} else if (!in->contact) {
		finish(SPO2_STATUS_NO_CONTACT, 0.0f, 0);
	} else if (now - started_ms > TIMEOUT_MS) {
		finish(SPO2_STATUS_SENSOR, 0.0f, 0);
	} else {
		track_inputs(in);
	}

	events = pending_events;
	pending_events = 0;
	return events;
}
