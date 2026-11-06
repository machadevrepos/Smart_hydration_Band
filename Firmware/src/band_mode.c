#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "app_config.h"
#include "band_mode.h"
#include "haptic.h"
#include "status_led.h"

/* The pattern must finish before the next one starts. */
BUILD_ASSERT(APP_DAILY_REMINDER_S >= 5 && APP_NIGHT_REMINDER_S >= 5,
	     "Reminder intervals must be at least 5 seconds");

static enum band_mode mode;
static int64_t next_reminder_ms;

/*
 * Mode and time to the next reminder, kept in RAM that start-up does not clear.
 * After a watchdog or software reset the band comes back in the same mode on
 * the same schedule, instead of falling back to Daily and restarting the wait
 * (a band that kept resetting would otherwise never reach its reminder). Each
 * value is stored with its bitwise inverse so a kept copy can be told from the
 * random RAM contents after power-on.
 */
struct kept_state {
	uint32_t magic;
	uint32_t mode;
	uint32_t mode_check;
	uint32_t remaining_ms;
	uint32_t remaining_check;
};

#define KEPT_MAGIC 0x0b5e0001U

static __noinit struct kept_state kept;

uint32_t band_mode_interval_s(void)
{
	return mode == BAND_MODE_NIGHT ? APP_NIGHT_REMINDER_S : APP_DAILY_REMINDER_S;
}

static void restart_interval(void)
{
	next_reminder_ms = k_uptime_get() + (int64_t)band_mode_interval_s() * 1000;
}

static void save_state(void)
{
	int64_t remaining = next_reminder_ms - k_uptime_get();
	uint32_t remaining_ms = remaining > 0 ? (uint32_t)remaining : 0U;

	/* A reset part-way through leaves the magic invalid, so the copy is ignored. */
	kept.magic = 0U;
	kept.mode = (uint32_t)mode;
	kept.mode_check = ~(uint32_t)mode;
	kept.remaining_ms = remaining_ms;
	kept.remaining_check = ~remaining_ms;
	kept.magic = KEPT_MAGIC;
}

static bool restore_state(void)
{
	if (kept.magic != KEPT_MAGIC || kept.mode_check != ~kept.mode ||
	    kept.remaining_check != ~kept.remaining_ms ||
	    (kept.mode != BAND_MODE_DAILY && kept.mode != BAND_MODE_NIGHT)) {
		return false;
	}

	mode = (enum band_mode)kept.mode;
	next_reminder_ms = k_uptime_get() +
			   MIN((int64_t)kept.remaining_ms, (int64_t)band_mode_interval_s() * 1000);

	return true;
}

static int show_mode(void)
{
	return status_led_set(mode == BAND_MODE_NIGHT ? STATUS_LED_GREEN : STATUS_LED_WHITE);
}

int band_mode_init(bool try_restore, bool *restored)
{
	*restored = try_restore && restore_state();

	if (!*restored) {
		mode = BAND_MODE_DAILY;
		restart_interval();
	}
	save_state();

	return show_mode();
}

int band_mode_toggle(void)
{
	mode = mode == BAND_MODE_NIGHT ? BAND_MODE_DAILY : BAND_MODE_NIGHT;
	restart_interval();
	save_state();
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

uint32_t band_mode_next_reminder_s(void)
{
	int64_t remaining = next_reminder_ms - k_uptime_get();

	return remaining > 0 ? (uint32_t)((remaining + 999) / 1000) : 0U;
}

bool band_mode_process(void)
{
	bool due = k_uptime_get() >= next_reminder_ms;

	if (due) {
		next_reminder_ms += (int64_t)band_mode_interval_s() * 1000;
		haptic_double_tap();
	}

	/* Cheap RAM writes, so the kept copy is never more than one pass old. */
	save_state();

	return due;
}
