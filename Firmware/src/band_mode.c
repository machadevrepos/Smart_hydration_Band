#include <zephyr/kernel.h>

#include "app_config.h"
#include "band_mode.h"
#include "haptic.h"
#include "status_led.h"

/* The pattern must finish before the next one starts. */
BUILD_ASSERT(APP_DAILY_REMINDER_S >= 5 && APP_NIGHT_REMINDER_S >= 5,
	     "Reminder intervals must be at least 5 seconds");

static enum band_mode mode;
static int64_t next_reminder_ms;

uint32_t band_mode_interval_s(void)
{
	return mode == BAND_MODE_NIGHT ? APP_NIGHT_REMINDER_S : APP_DAILY_REMINDER_S;
}

static void restart_interval(void)
{
	next_reminder_ms = k_uptime_get() + (int64_t)band_mode_interval_s() * 1000;
}

static int show_mode(void)
{
	return status_led_set(mode == BAND_MODE_NIGHT ? STATUS_LED_GREEN : STATUS_LED_WHITE);
}

int band_mode_init(void)
{
	mode = BAND_MODE_DAILY;
	restart_interval();

	return show_mode();
}

int band_mode_toggle(void)
{
	mode = mode == BAND_MODE_NIGHT ? BAND_MODE_DAILY : BAND_MODE_NIGHT;
	restart_interval();
	haptic_buzz();

	return show_mode();
}

enum band_mode band_mode_get(void)
{
	return mode;
}

const char *band_mode_name(enum band_mode m)
{
	return m == BAND_MODE_NIGHT ? "Night" : "Daily";
}

bool band_mode_process(void)
{
	if (k_uptime_get() < next_reminder_ms) {
		return false;
	}

	next_reminder_ms += (int64_t)band_mode_interval_s() * 1000;
	haptic_double_tap();

	return true;
}
