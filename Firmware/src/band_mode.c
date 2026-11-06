#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "app_config.h"
#include "band_mode.h"
#include "haptic.h"
#include "status_led.h"
#include "storage.h"

LOG_MODULE_REGISTER(band_mode, LOG_LEVEL_INF);

BUILD_ASSERT(APP_DAILY_REMINDER_MIN >= BAND_INTERVAL_MIN &&
		     APP_DAILY_REMINDER_MIN <= BAND_INTERVAL_MAX &&
		     APP_NIGHT_REMINDER_MIN >= BAND_INTERVAL_MIN &&
		     APP_NIGHT_REMINDER_MIN <= BAND_INTERVAL_MAX,
	     "Default reminder intervals must be in the accepted range");

#define STORAGE_KEY_INTERVALS "intervals"

/* Main loop only, apart from the locked interval fields below. */
static enum band_mode mode;
static int64_t next_reminder_ms;

/*
 * Intervals in effect, and a pending request from the app. Both are guarded
 * by lock, since BLE writes arrive on the Bluetooth thread.
 */
static struct k_spinlock lock;
static struct band_intervals intervals = {APP_DAILY_REMINDER_MIN, APP_NIGHT_REMINDER_MIN};
static struct band_intervals requested_intervals;
static bool intervals_pending;

/* Mode asked for by the app, or -1. */
static atomic_t requested_mode = ATOMIC_INIT(-1);

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

static uint32_t interval_min_for(enum band_mode m)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t min = m == BAND_MODE_NIGHT ? intervals.night_min : intervals.daily_min;

	k_spin_unlock(&lock, key);
	return min;
}

uint32_t band_mode_interval_s(void)
{
	return interval_min_for(mode) * 60U;
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

static bool intervals_valid(const struct band_intervals *iv)
{
	return iv->daily_min >= BAND_INTERVAL_MIN && iv->daily_min <= BAND_INTERVAL_MAX &&
	       iv->night_min >= BAND_INTERVAL_MIN && iv->night_min <= BAND_INTERVAL_MAX;
}

static void load_intervals(void)
{
	struct band_intervals stored;
	int ret = storage_load(STORAGE_KEY_INTERVALS, &stored, sizeof(stored));

	if (ret == 0 && intervals_valid(&stored)) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		intervals = stored;
		k_spin_unlock(&lock, key);
	} else if (ret != -ENOENT && ret != -ENODEV) {
		LOG_WRN("Stored intervals unreadable (%d), using defaults", ret);
	}
}

static int show_mode(void)
{
	return status_led_set(mode == BAND_MODE_NIGHT ? STATUS_LED_GREEN : STATUS_LED_WHITE);
}

int band_mode_init(bool try_restore, bool *restored)
{
	load_intervals();

	*restored = try_restore && restore_state();

	if (!*restored) {
		mode = BAND_MODE_DAILY;
		restart_interval();
	}
	save_state();

	return show_mode();
}

static int switch_to(enum band_mode new_mode)
{
	mode = new_mode;
	restart_interval();
	save_state();
	haptic_buzz();

	return show_mode();
}

int band_mode_toggle(void)
{
	return switch_to(mode == BAND_MODE_NIGHT ? BAND_MODE_DAILY : BAND_MODE_NIGHT);
}

void band_mode_request(enum band_mode m)
{
	atomic_set(&requested_mode, (atomic_val_t)m);
}

int band_mode_request_intervals(const struct band_intervals *iv)
{
	struct band_intervals wanted = {
		.daily_min = iv->daily_min != 0 ? iv->daily_min : APP_DAILY_REMINDER_MIN,
		.night_min = iv->night_min != 0 ? iv->night_min : APP_NIGHT_REMINDER_MIN,
	};

	if (!intervals_valid(&wanted)) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	requested_intervals = wanted;
	intervals_pending = true;
	k_spin_unlock(&lock, key);

	return 0;
}

void band_mode_get_intervals(struct band_intervals *iv)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	*iv = intervals_pending ? requested_intervals : intervals;
	k_spin_unlock(&lock, key);
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

/* Apply app requests. Returns BAND_EVENT_* flags. */
static uint32_t apply_requests(void)
{
	uint32_t events = 0;
	atomic_val_t wanted_mode = atomic_set(&requested_mode, -1);

	if ((wanted_mode == BAND_MODE_DAILY || wanted_mode == BAND_MODE_NIGHT) &&
	    wanted_mode != (atomic_val_t)mode) {
		int ret = switch_to((enum band_mode)wanted_mode);

		if (ret < 0) {
			LOG_WRN("LED update failed (%d)", ret);
		}
		events |= BAND_EVENT_MODE_FROM_APP;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	bool pending = intervals_pending;
	struct band_intervals old = intervals;
	struct band_intervals now = requested_intervals;

	if (pending) {
		intervals = now;
		intervals_pending = false;
	}
	k_spin_unlock(&lock, key);

	if (!pending) {
		return events;
	}

	events |= BAND_EVENT_INTERVALS;

	/* A shorter or longer wait applies from now, not from the last reminder. */
	if ((mode == BAND_MODE_DAILY && now.daily_min != old.daily_min) ||
	    (mode == BAND_MODE_NIGHT && now.night_min != old.night_min)) {
		restart_interval();
	}

	if (storage_save(STORAGE_KEY_INTERVALS, &now, sizeof(now)) < 0) {
		events |= BAND_EVENT_SAVE_FAILED;
	}

	return events;
}

uint32_t band_mode_process(void)
{
	uint32_t events = apply_requests();

	if (k_uptime_get() >= next_reminder_ms) {
		next_reminder_ms += (int64_t)band_mode_interval_s() * 1000;
		haptic_double_tap();
		events |= BAND_EVENT_REMINDER;
	}

	/* Cheap RAM writes, so the kept copy is never more than one pass old. */
	save_state();

	return events;
}
