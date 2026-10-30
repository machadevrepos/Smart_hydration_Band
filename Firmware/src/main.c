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

/* How often the per-channel diagnostics line is printed. */
#define DIAG_PERIOD_MS 5000

/*
 * Skin contact from the raw IR level: ~160000-195000 on wrist and finger at
 * led-pa 0x60, ~1500-3000 off the skin. Contact must hold for 1 s before
 * measuring (lets the placement transient pass); 0.1 s below the threshold
 * ends it. Either change restarts the estimator, because the step in the
 * signal would otherwise sit in its 8 s window and can look periodic.
 */
#define CONTACT_ON_SAMPLES 100
#define CONTACT_OFF_SAMPLES 10
#define NO_CONTACT_PERIOD_MS 1000

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

static const char *const channel_names[HR_NUM_CHANNELS] = {"red", "ir", "green"};

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

static void print_diagnostics(const struct rate_meter *m)
{
	struct hr_channel_stats st[HR_NUM_CHANNELS];

	for (int c = 0; c < HR_NUM_CHANNELS; c++) {
		heart_rate_get_stats(c, &st[c]);
	}

	/* dc = raw level, ac = pulse size (band-passed RMS), q = periodicity. */
	LOG_INF("PPG %d.%d Hz | red dc %d ac %d q %d | ir dc %d ac %d q %d | "
		"green dc %d ac %d q %d",
		(int)m->hz, (int)(m->hz * 10.0f) % 10,
		st[0].dc, st[0].ac, st[0].quality, st[1].dc, st[1].ac, st[1].quality,
		st[2].dc, st[2].ac, st[2].quality);
}

int main(void)
{
	static struct max30101_sample samples[MAX30101_FIFO_DEPTH];
	struct rate_meter rate = {.hz = 0.0f};
	struct contact contact = {.on = false};
	uint32_t sample_index = 0;
	int64_t next_diag_ms = DIAG_PERIOD_MS;
	int64_t next_no_contact_ms = 0;
	int ret;

	ret = max30101_init();
	if (ret < 0) {
		LOG_ERR("MAX30101 initialization failed (%d)", ret);
		return ret;
	}

	heart_rate_init();
	LOG_INF("Place the sensor against the skin to start heart-rate measurement");
	rate_meter_restart(&rate);

	for (;;) {
		bool lost = false;
		int count = max30101_read_fifo(samples, ARRAY_SIZE(samples), &lost);

		if (count < 0) {
			LOG_WRN("MAX30101 read failed (%d)", count);
			heart_rate_init();
			rate_meter_restart(&rate);
			k_sleep(K_MSEC(APP_POLL_INTERVAL_MS));
			continue;
		}

		/* The estimator assumes evenly spaced samples, so a gap invalidates it. */
		if (lost) {
			LOG_WRN("MAX30101 FIFO overflow, samples lost");
			heart_rate_init();
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
				if (contact.on) {
					LOG_INF("Skin contact detected, first reading in ~10 s");
				} else {
					LOG_INF("Skin contact lost");
				}
			}

			/* Keep feeding so the diagnostics stay live; discard results. */
			if (!heart_rate_process(channels, &hr) || !contact.on) {
				continue;
			}

			if (hr.bpm == 0) {
				LOG_INF("HR: --  (best %s, quality %d)", channel_names[hr.channel],
					hr.quality);
			} else {
				LOG_INF("HR: %d bpm%s  (%s, quality %d)", hr.bpm,
					hr.held ? " (held)" : "", channel_names[hr.channel],
					hr.quality);
			}
		}

		if (!contact.on && k_uptime_get() >= next_no_contact_ms) {
			next_no_contact_ms = k_uptime_get() + NO_CONTACT_PERIOD_MS;
			LOG_INF("HR: --  (no skin contact)");
		}

		if (k_uptime_get() >= next_diag_ms) {
			next_diag_ms += DIAG_PERIOD_MS;
			print_diagnostics(&rate);
		}

		k_sleep(K_MSEC(APP_POLL_INTERVAL_MS));
	}

	return 0;
}
