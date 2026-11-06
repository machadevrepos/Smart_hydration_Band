#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>

#include "app_config.h"
#include "band_mode.h"
#include "battery.h"
#include "ble_service.h"
#include "button.h"
#include "data_log.h"
#include "haptic.h"
#include "heart_rate.h"
#include "max30101.h"
#include "status_led.h"
#include "step_counter.h"
#include "storage.h"
#include "wall_clock.h"
#include "watchdog.h"

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

/* Set once, in prj.conf, and also reported over BLE by the Device Information Service. */
#define FIRMWARE_VERSION CONFIG_BT_DIS_FW_REV_STR

/* Heart-rate status is printed once a second. */
#define STATUS_PERIOD_MS 1000
#define BATTERY_PERIOD_MS 10000
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
/*
 * No sample for this long while reads succeed means the MAX30101 stopped
 * sampling (it makes one every 10 ms), so its settings are written back.
 */
#define SENSOR_STALL_MS 1000

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
	/* False if the MAX30101 did not start: no heart rate, the rest runs. */
	bool sensor_ok;
	/* Consecutive failed FIFO reads; heart rate is paused while non-zero. */
	uint32_t read_errors;
	/* When the FIFO last gave a sample, to notice a sensor that stopped. */
	int64_t last_sample_ms;
	bool stall_reported;
	int64_t contact_since_ms;
	bool have_estimate;
	struct hr_estimate hr;
	int64_t last_no_contact_ms;
	bool steps_ok;
	uint32_t steps;
	uint32_t printed_steps;
	bool battery_ok;
	bool have_battery;
	/* Consecutive readings below APP_BATTERY_OFF_MV. */
	int low_battery_readings;
	uint8_t battery_percent;
};

/*
 * Static: logging is deferred, so these must outlive the printk() call. Only
 * main() writes them.
 */
static char rate_text[12];
static char steps_text[12];
static char status_text[48];

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

static const char *mode_led_name(enum band_mode mode)
{
	return mode == BAND_MODE_NIGHT ? "green" : "white";
}

static void handle_button_press(uint32_t press)
{
	unsigned int secs = (unsigned int)(k_uptime_get() / 1000);
	int led_ret = band_mode_toggle();
	enum band_mode mode = band_mode_get();
	uint32_t interval = band_mode_interval_s();

	printk("  %02u:%02u   button press %u: %s Mode, LED %s%s, buzz, next reminder in %u:%02u\n",
	       secs / 60, secs % 60, press, band_mode_name(mode), mode_led_name(mode),
	       led_ret == 0 ? "" : " FAILED", interval / 60, interval % 60);
}

/* One line per thing band_mode_process() did. */
static void print_mode_events(uint32_t events)
{
	unsigned int secs = (unsigned int)(k_uptime_get() / 1000);
	enum band_mode mode = band_mode_get();
	uint32_t next_s = band_mode_next_reminder_s();

	if (events & BAND_EVENT_MODE_FROM_APP) {
		printk("  %02u:%02u   app: %s Mode, LED %s, buzz\n", secs / 60, secs % 60,
		       band_mode_name(mode), mode_led_name(mode));
	}
	if (events & BAND_EVENT_INTERVALS) {
		struct band_intervals iv;

		band_mode_get_intervals(&iv);
		printk("  %02u:%02u   app: intervals Daily %u min, Night %u min%s\n", secs / 60,
		       secs % 60, iv.daily_min, iv.night_min,
		       (events & BAND_EVENT_SAVE_FAILED) ? ", NOT SAVED to flash" : ", saved");
	}
	if (events & BAND_EVENT_QUIET_HOURS) {
		struct band_quiet_hours q;

		band_mode_get_quiet_hours(&q);
		printk("  %02u:%02u   app: quiet hours %02u:%02u-%02u:%02u%s%s\n", secs / 60,
		       secs % 60, q.start_min / 60, q.start_min % 60, q.end_min / 60,
		       q.end_min % 60, q.start_min == q.end_min ? " (off)" : "",
		       (events & BAND_EVENT_SAVE_FAILED) ? ", NOT SAVED to flash" : ", saved");
	}
	if (events & BAND_EVENT_REMINDER) {
		printk("  %02u:%02u   reminder: double tap (%s Mode)\n", secs / 60, secs % 60,
		       band_mode_name(mode));
	}
	if (events & BAND_EVENT_QUIET_SKIPPED) {
		printk("  %02u:%02u   reminder skipped: quiet hours (Daily Mode)\n", secs / 60,
		       secs % 60);
	}

	printk("  %02u:%02u   next reminder in %u:%02u\n", secs / 60, secs % 60, next_s / 60,
	       next_s % 60);
}

/*
 * Switch the band off on a flat battery, with everything that would keep
 * drawing current quiet first. A button press wakes it (a fresh start in Daily
 * Mode); if the battery is still flat it switches off again a minute later.
 * Does not return.
 */
static void power_off_low_battery(int32_t millivolts)
{
	unsigned int secs = (unsigned int)(k_uptime_get() / 1000);

	printk("  %02u:%02u   battery %d mV: switching off, charge it then press the button\n",
	       secs / 60, secs % 60, millivolts);

	/* Keep the steps and heart rate of the period in progress. */
	data_log_flush();

	haptic_stop();
	(void)status_led_set(STATUS_LED_OFF);
	(void)max30101_shutdown();
	(void)step_counter_shutdown();
	if (button_enable_wake() < 0) {
		printk("  button wake unavailable: use the power switch to restart\n");
	}

	sys_poweroff();
}

static void check_low_battery(struct hr_status *st, int32_t millivolts)
{
	if (millivolts < APP_BATTERY_VALID_MV) {
		/* Not a battery that could still run the band: a measurement fault. */
		st->low_battery_readings = 0;
		return;
	}

	if (millivolts >= APP_BATTERY_OFF_MV) {
		st->low_battery_readings = 0;
		return;
	}

	if (++st->low_battery_readings >= APP_BATTERY_OFF_READINGS) {
		power_off_low_battery(millivolts);
	}
}

static void update_battery(struct hr_status *st)
{
	unsigned int secs = (unsigned int)(k_uptime_get() / 1000);
	uint8_t percent;
	int32_t millivolts;
	int ret;

	if (!st->battery_ok) {
		return;
	}

	ret = battery_read(&percent, &millivolts);
	if (ret < 0) {
		printk("  %02u:%02u   battery read FAILED (%d)\n", secs / 60, secs % 60, ret);
		return;
	}

	printk("  %02u:%02u   battery %u%% (%d mV)\n", secs / 60, secs % 60, percent, millivolts);

	st->have_battery = true;
	st->battery_percent = percent;

	check_low_battery(st, millivolts);
}

/*
 * Send the reading the status line just showed, and add it to the flash log.
 * A reading needs skin contact.
 */
static void publish_telemetry(const struct contact *contact, const struct hr_status *st)
{
	data_log_add_second(contact->on && st->have_estimate ? st->hr.bpm : 0, st->steps_ok,
			    st->steps, band_mode_get() == BAND_MODE_NIGHT);

	struct ble_telemetry telemetry = {
		.heart_rate = contact->on && st->have_estimate ? st->hr.bpm : 0,
		.steps_valid = st->steps_ok,
		.steps = st->steps,
		.night_mode = band_mode_get() == BAND_MODE_NIGHT,
		.battery_valid = st->have_battery,
		.battery_percent = st->battery_percent,
	};

	ble_service_publish(&telemetry);
}

static void print_header(void)
{
	printk("\n"
	       "Smart Hydration Band, firmware " FIRMWARE_VERSION "\n"
	       "\n"
	       "  time    heart rate    steps   status\n"
	       "  -----   ----------   ------   ----------------------------\n");
}

/* Heart-rate columns for the current state. */
static void format_rate(const struct contact *contact, const struct hr_status *st, int64_t now)
{
	const struct hr_estimate *hr = &st->hr;

	snprintk(rate_text, sizeof(rate_text), "--");

	if (!st->sensor_ok) {
		snprintk(status_text, sizeof(status_text), "heart-rate sensor unavailable");
	} else if (st->read_errors != 0) {
		snprintk(status_text, sizeof(status_text), "heart-rate sensor not responding");
	} else if (!contact->on) {
		snprintk(status_text, sizeof(status_text), "no skin contact");
	} else if (!st->have_estimate) {
		int waited = (int)((now - st->contact_since_ms) / 1000);
		int left = HR_FIRST_ESTIMATE_S - waited;

		snprintk(status_text, sizeof(status_text), "measuring, ready in %d s",
			 left > 1 ? left : 1);
	} else if (hr->bpm == 0 && hr->settling) {
		snprintk(status_text, sizeof(status_text), "settling, keep still (%d)",
			 hr->quality);
	} else if (hr->bpm == 0) {
		snprintk(status_text, sizeof(status_text), "no clear pulse, keep still (%d)",
			 hr->quality);
	} else {
		snprintk(rate_text, sizeof(rate_text), "%3d bpm", hr->bpm);
		if (hr->held) {
			snprintk(status_text, sizeof(status_text),
				 "signal unsure (%d), holding last value", hr->quality);
		} else {
			snprintk(status_text, sizeof(status_text), "signal %s (%d)",
				 quality_word(hr->quality), hr->quality);
		}
	}
}

/* One line: "  mm:ss   <rate>   <steps>   <status>". */
static void print_status(const struct contact *contact, struct hr_status *st)
{
	int64_t now = k_uptime_get();
	unsigned int secs = (unsigned int)(now / 1000);
	unsigned int min = secs / 60;
	unsigned int sec = secs % 60;

	if (st->steps_ok) {
		uint32_t steps;

		if (step_counter_read(&steps) == 0) {
			st->steps = steps;
		} else {
			LOG_WRN("BMI270 step read failed");
		}
	}

	if (!contact->on) {
		/*
		 * Off the skin, report once on removal, then only when the step
		 * count changes or every NO_CONTACT_REPEAT_MS.
		 */
		if (st->last_no_contact_ms != 0 && st->steps == st->printed_steps &&
		    now - st->last_no_contact_ms < NO_CONTACT_REPEAT_MS) {
			return;
		}
		st->last_no_contact_ms = now;
	} else {
		st->last_no_contact_ms = 0;
	}

	format_rate(contact, st, now);
	if (st->steps_ok) {
		snprintk(steps_text, sizeof(steps_text), "%u", st->steps);
	} else {
		snprintk(steps_text, sizeof(steps_text), "--");
	}
	st->printed_steps = st->steps;

	printk("  %02u:%02u   %-10s   %6s   %s\n", min, sec, rate_text, steps_text, status_text);
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

/*
 * Drain the MAX30101 FIFO and feed the estimator. A failed read only pauses
 * heart rate: it must never keep the main loop from the button and reminders.
 */
static void poll_heart_rate(struct max30101_sample *samples, int max, struct rate_meter *rate, struct contact *contact,
			    struct hr_status *st)
{
	bool lost = false;
	int count = max30101_read_fifo(samples, max, &lost);

	if (count < 0) {
		/* Logged once per failure run, since the loop retries every poll. */
		if (st->read_errors == 0) {
			LOG_WRN("MAX30101 read failed (%d), heart rate paused", count);
		}
		st->read_errors++;

		/*
		 * No samples, so no reading: drop it rather than send a stale one.
		 * Contact must be found again, which also restarts the estimator.
		 */
		contact->on = false;
		contact->count = 0;
		st->have_estimate = false;
		return;
	}

	if (st->read_errors != 0) {
		LOG_INF("MAX30101 reads recovered after %u failures", st->read_errors);
		st->read_errors = 0;
		st->last_sample_ms = k_uptime_get();
		rate_meter_restart(rate);
		/* Samples from before the gap are not evenly spaced with the rest. */
		return;
	}

	if (count > 0) {
		st->last_sample_ms = k_uptime_get();
		st->stall_reported = false;
	} else if (k_uptime_get() - st->last_sample_ms > SENSOR_STALL_MS) {
		/*
		 * Answering but not sampling: the sensor reset itself (a supply dip
		 * clears its settings). Write them back, retrying each period.
		 */
		int ret = max30101_restore();

		if (!st->stall_reported) {
			st->stall_reported = true;
			LOG_WRN("MAX30101 stopped sampling, settings restored (%d)", ret);
		}
		st->last_sample_ms = k_uptime_get();
		contact->on = false;
		contact->count = 0;
		st->have_estimate = false;
		rate_meter_restart(rate);
		return;
	}

	/* The estimator assumes evenly spaced samples, so a gap invalidates it. */
	if (lost) {
		LOG_WRN("MAX30101 FIFO overflow, samples lost");
		heart_rate_init();
		st->have_estimate = false;
		st->contact_since_ms = k_uptime_get();
		rate_meter_restart(rate);
	} else {
		rate_meter_update(rate, count);
	}

	for (int i = 0; i < count; i++) {
		struct hr_estimate hr;

		if (contact_update(contact, samples[i].ir)) {
			heart_rate_init();
			st->have_estimate = false;
			st->contact_since_ms = k_uptime_get();
		}

		if (contact->on && heart_rate_process(samples[i].ir, &hr)) {
			st->hr = hr;
			st->have_estimate = true;
		}
	}
}

int main(void)
{
	static struct max30101_sample samples[MAX30101_FIFO_DEPTH];
	struct rate_meter rate = {.hz = 0.0f};
	struct contact contact = {.on = false};
	struct hr_status status = {0};
	int led_ret;
	bool button_ok;
	uint32_t button_presses = 0;
	int64_t next_status_ms = STATUS_PERIOD_MS;
	int64_t next_battery_ms = 0;
	int wdt_ret;
	bool mode_restored;
	bool clock_known;
	int ret;

	/* First, so a hang anywhere from here on resets the band. */
	wdt_ret = watchdog_init();

	/*
	 * Each of these is optional: the rest of the band runs if one fails.
	 * Above all, the reminders must keep firing without any sensor.
	 */
	ret = max30101_init();
	status.sensor_ok = ret == 0;
	if (ret < 0) {
		LOG_ERR("MAX30101 unavailable, no heart rate (%d)", ret);
	}

	ret = haptic_init();
	if (ret < 0) {
		LOG_ERR("Haptic motor unavailable (%d)", ret);
	}

	/* The time of day, kept through a watchdog reset; needed for quiet hours. */
	clock_known = wall_clock_init(watchdog_reset_kept_ram());

	/* Before band_mode_init(), which loads the app's intervals from it. */
	ret = storage_init();
	if (ret < 0) {
		LOG_ERR("Flash storage unavailable, using defaults (%d)", ret);
	}

	/* Show the mode as soon as possible; resume it after a watchdog reset. */
	led_ret = band_mode_init(watchdog_reset_kept_ram(), &mode_restored);

	/* Steps and heart rate in flash, for the app to fetch when it connects. */
	ret = data_log_init();
	if (ret < 0) {
		LOG_ERR("Data log unavailable, nothing is stored while disconnected (%d)", ret);
	}

	ret = button_init();
	button_ok = ret == 0;
	if (ret < 0) {
		LOG_ERR("Button unavailable (%d)", ret);
	}

	/* Heart rate still runs if the BMI270 fails; steps then show "--". */
	ret = step_counter_init();
	status.steps_ok = ret == 0;
	if (ret < 0) {
		LOG_ERR("Step counter unavailable (%d)", ret);
	}

	ret = battery_init();
	status.battery_ok = ret == 0;
	if (ret < 0) {
		LOG_ERR("Battery measurement unavailable (%d)", ret);
	}

	ret = ble_service_init();
	if (ret < 0) {
		LOG_ERR("Bluetooth unavailable (%d)", ret);
	}

	/* Loading the BMI270 outlasts the FIFO, so start from an empty one. */
	if (status.sensor_ok) {
		bool ignored;

		(void)max30101_read_fifo(samples, ARRAY_SIZE(samples), &ignored);
	}

	heart_rate_init();
	print_header();

	printk("  last reset: %s\n", watchdog_reset_cause());
	printk("  time of day: %s\n", clock_known ? "kept through the reset"
						 : "not set, no quiet hours until the app sets it");
	if (wdt_ret < 0) {
		LOG_ERR("Watchdog unavailable, a hang will not reset the band (%d)", wdt_ret);
	}

	{
		uint32_t next_s = band_mode_next_reminder_s();
		enum band_mode mode = band_mode_get();

		printk("  %s Mode%s, LED %s%s, next reminder in %u:%02u\n", band_mode_name(mode),
		       mode_restored ? " (resumed after reset)" : "", mode_led_name(mode),
		       led_ret == 0 ? "" : " FAILED", next_s / 60, next_s % 60);
	}
	rate_meter_restart(&rate);
	status.last_sample_ms = k_uptime_get();

	for (;;) {
		if (status.sensor_ok) {
			poll_heart_rate(samples, ARRAY_SIZE(samples), &rate, &contact, &status);
		}

		if (button_ok && button_pressed()) {
			button_presses++;
			handle_button_press(button_presses);
		}

		uint32_t events = band_mode_process();

		if (events != 0) {
			print_mode_events(events);
		}

		if (k_uptime_get() >= next_battery_ms) {
			next_battery_ms = k_uptime_get() + BATTERY_PERIOD_MS;
			update_battery(&status);
		}

		/* Fixed 1 s cadence, independent of FIFO bursts and the sensor clock. */
		if (k_uptime_get() >= next_status_ms) {
			next_status_ms += STATUS_PERIOD_MS;
			print_status(&contact, &status);
			publish_telemetry(&contact, &status);
		}

		wall_clock_keep();
		watchdog_feed();
		k_sleep(K_MSEC(APP_POLL_INTERVAL_MS));
	}

	return 0;
}
