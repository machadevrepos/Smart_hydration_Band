#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "app_config.h"
#include "heart_rate.h"
#include "max30101.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

BUILD_ASSERT(HR_SAMPLE_RATE_HZ == MAX30101_SAMPLE_RATE_HZ,
	     "Heart-rate timing must match the sensor sample rate");

/*
 * The MAX30101 sample clock is its own oscillator and can be several percent
 * off 100 Hz (about +5% on the first board), which would scale every bpm by
 * the same error. Count samples against the nRF clock (calibrated LF clock)
 * and pass the measured rate to the estimator.
 */
#define RATE_PERIOD_MS 10000
#define RATE_MIN_HZ 80.0f
#define RATE_MAX_HZ 120.0f

/* Heart-rate status is printed once a second; diagnostics every 5 s. */
#define STATUS_PERIOD_MS 1000
#define DIAG_PERIOD_MS 5000
/* While off the skin, repeat the status only this often. */
#define NO_CONTACT_REPEAT_MS 10000

/* Quality thresholds for the status wording (the estimator rejects below 50). */
#define QUALITY_GOOD 80
#define QUALITY_FAIR 65

/*
 * Skin contact from the raw IR level: ~160000-195000 on wrist and finger,
 * ~1500-3000 off the skin (see APP_CONTACT_IR_MIN). Contact must hold for 1 s before
 * measuring (lets the placement transient pass); 0.1 s below the threshold
 * ends it. Either change restarts the estimator, because the step in the
 * signal would otherwise sit in its 8 s window and can look periodic.
 */
#define CONTACT_ON_SAMPLES 100
#define CONTACT_OFF_SAMPLES 10

struct contact {
	bool on;
	int count;
};

/* Returns true when the contact state changes. */
static bool contact_update(struct contact *c, int32_t ir)
{
	bool above = ir >= APP_CONTACT_IR_MIN;

	if (above == c->on) {
		c->count = 0;
		return false;
	}

	if (++c->count < (above ? CONTACT_ON_SAMPLES : CONTACT_OFF_SAMPLES)) {
		return false;
	}

	c->on = above;
	c->count = 0;
	return true;
}

/* What the status line shows, refreshed from the estimator once a second. */
struct hr_status {
	int64_t contact_since_ms;
	bool have_estimate;
	struct hr_estimate hr;
	int64_t last_no_contact_ms;
};

static const char *quality_word(int quality)
{
	if (quality >= QUALITY_GOOD) {
		return "good";
	}
	if (quality >= QUALITY_FAIR) {
		return "fair";
	}
	return "weak";
}

static void print_header(void)
{
	printk("\n"
	       "Smart Hydration Band - heart rate\n"
	       "Place the sensor on the wrist and keep still.\n"
	       "\n"
	       "  time    heart rate   status\n"
	       "  -----   ----------   ----------------------------\n");
}

/* One line: "  mm:ss   <rate>   <status>". */
static void print_status(const struct contact *contact, struct hr_status *st)
{
	int64_t now = k_uptime_get();
	unsigned int secs = (unsigned int)(now / 1000);
	unsigned int min = secs / 60;
	unsigned int sec = secs % 60;

	if (!contact->on) {
		/* Report once on removal, then only every NO_CONTACT_REPEAT_MS. */
		if (st->last_no_contact_ms != 0 &&
		    now - st->last_no_contact_ms < NO_CONTACT_REPEAT_MS) {
			return;
		}
		st->last_no_contact_ms = now;
		printk("  %02u:%02u   --           no skin contact\n", min, sec);
		return;
	}
	st->last_no_contact_ms = 0;

	if (!st->have_estimate) {
		int waited = (int)((now - st->contact_since_ms) / 1000);
		int left = HR_FIRST_ESTIMATE_S - waited;

		printk("  %02u:%02u   --           measuring, ready in %d s\n", min, sec,
		       left > 1 ? left : 1);
		return;
	}

	const struct hr_estimate *hr = &st->hr;

	if (hr->bpm == 0 && hr->settling) {
		printk("  %02u:%02u   --           settling, keep still (%d)\n", min, sec,
		       hr->quality);
	} else if (hr->bpm == 0) {
		printk("  %02u:%02u   --           no clear pulse, keep still (%d)\n", min, sec,
		       hr->quality);
	} else if (hr->held) {
		printk("  %02u:%02u   %3d bpm      signal unsure (%d), holding last value\n", min,
		       sec, hr->bpm, hr->quality);
	} else {
		printk("  %02u:%02u   %3d bpm      signal %s (%d)\n", min, sec, hr->bpm,
		       quality_word(hr->quality), hr->quality);
	}
}

struct rate_meter {
	int64_t start_ms;
	uint32_t samples;
	float hz;
};

static void rate_meter_restart(struct rate_meter *m)
{
	m->start_ms = k_uptime_get();
	m->samples = 0;
}

static void rate_meter_update(struct rate_meter *m, int count)
{
	int64_t elapsed = k_uptime_get() - m->start_ms;

	m->samples += count;
	if (elapsed < RATE_PERIOD_MS) {
		return;
	}

	float hz = (float)m->samples * 1000.0f / (float)elapsed;

	if (hz > RATE_MIN_HZ && hz < RATE_MAX_HZ) {
		/* Smooth so one late poll does not move the rate. */
		m->hz = m->hz == 0.0f ? hz : 0.7f * m->hz + 0.3f * hz;
		heart_rate_set_sample_rate(m->hz);
	} else {
		LOG_WRN("Implausible MAX30101 sample rate %d Hz", (int)hz);
	}

	rate_meter_restart(m);
}

#if APP_PPG_DIAGNOSTICS
static void print_diagnostics(const struct rate_meter *m)
{
	struct hr_channel_stats st[HR_NUM_CHANNELS];

	for (int c = 0; c < HR_NUM_CHANNELS; c++) {
		heart_rate_get_stats(c, &st[c]);
	}

	/* dc = raw level, ac = pulse size (band-passed RMS), q = periodicity. */
	printk("  diag    %d.%d Hz | red dc %d ac %d q %d | ir dc %d ac %d q %d | "
	       "green dc %d ac %d q %d\n",
	       (int)m->hz, (int)(m->hz * 10.0f) % 10,
	       st[HR_CHANNEL_RED].dc, st[HR_CHANNEL_RED].ac, st[HR_CHANNEL_RED].quality,
	       st[HR_CHANNEL_IR].dc, st[HR_CHANNEL_IR].ac, st[HR_CHANNEL_IR].quality,
	       st[HR_CHANNEL_GREEN].dc, st[HR_CHANNEL_GREEN].ac, st[HR_CHANNEL_GREEN].quality);
}
#endif

int main(void)
{
	static struct max30101_sample samples[MAX30101_FIFO_DEPTH];
	struct rate_meter rate = {.hz = 0.0f};
	struct contact contact = {.on = false};
	struct hr_status status = {0};
	uint32_t sample_index = 0;
	int64_t next_status_ms = STATUS_PERIOD_MS;
#if APP_PPG_DIAGNOSTICS
	int64_t next_diag_ms = DIAG_PERIOD_MS;
#endif
	int ret;

	ret = max30101_init();
	if (ret < 0) {
		LOG_ERR("MAX30101 initialization failed (%d)", ret);
		return ret;
	}

	heart_rate_init();
	print_header();
	rate_meter_restart(&rate);

	for (;;) {
		bool lost = false;
		int count = max30101_read_fifo(samples, ARRAY_SIZE(samples), &lost);

		if (count < 0) {
			LOG_WRN("MAX30101 read failed (%d)", count);
			heart_rate_init();
			status.have_estimate = false;
			status.contact_since_ms = k_uptime_get();
			rate_meter_restart(&rate);
			k_sleep(K_MSEC(APP_POLL_INTERVAL_MS));
			continue;
		}

		/* The estimator assumes evenly spaced samples, so a gap invalidates it. */
		if (lost) {
			LOG_WRN("MAX30101 FIFO overflow, samples lost");
			heart_rate_init();
			status.have_estimate = false;
			status.contact_since_ms = k_uptime_get();
			rate_meter_restart(&rate);
		} else {
			rate_meter_update(&rate, count);
		}

		for (int i = 0; i < count; i++) {
			struct hr_estimate hr;
			const int32_t channels[HR_NUM_CHANNELS] = {
				[HR_CHANNEL_RED] = samples[i].red,
				[HR_CHANNEL_IR] = samples[i].ir,
				[HR_CHANNEL_GREEN] = samples[i].green,
			};

#if APP_RAW_STREAM
			printk("%u,%d,%d,%d\n", sample_index, samples[i].red, samples[i].ir,
			       samples[i].green);
#endif
			sample_index++;

			if (contact_update(&contact, samples[i].ir)) {
				heart_rate_init();
				status.have_estimate = false;
				status.contact_since_ms = k_uptime_get();
			}

			/* Keep feeding so the diagnostics stay live; ignore results off skin. */
			if (heart_rate_process(channels, &hr) && contact.on) {
				status.hr = hr;
				status.have_estimate = true;
			}
		}

		/* Fixed 1 s cadence, independent of FIFO bursts and the sensor clock. */
		if (k_uptime_get() >= next_status_ms) {
			next_status_ms += STATUS_PERIOD_MS;
			print_status(&contact, &status);
		}

#if APP_PPG_DIAGNOSTICS
		if (k_uptime_get() >= next_diag_ms) {
			next_diag_ms += DIAG_PERIOD_MS;
			print_diagnostics(&rate);
		}
#endif

		k_sleep(K_MSEC(APP_POLL_INTERVAL_MS));
	}

	return 0;
}
