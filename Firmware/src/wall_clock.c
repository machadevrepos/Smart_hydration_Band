#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "wall_clock.h"

/* UTC milliseconds at uptime 0, and the local offset, guarded by lock. */
static struct k_spinlock lock;
static bool valid;
static int64_t base_ms;
static int16_t tz_offset_min;

/*
 * The time kept in RAM that start-up does not clear, refreshed every main loop
 * pass, with inverse copies to tell it from random RAM after power-on.
 */
struct kept_clock {
	uint32_t magic;
	uint32_t utc_s;
	uint32_t utc_check;
	uint32_t tz;
	uint32_t tz_check;
};

#define KEPT_MAGIC 0xc10c0001U

static __noinit struct kept_clock kept;

static bool tz_valid(int32_t tz)
{
	return tz >= WALL_CLOCK_TZ_MIN && tz <= WALL_CLOCK_TZ_MAX;
}

bool wall_clock_init(bool try_restore)
{
	if (try_restore && kept.magic == KEPT_MAGIC && kept.utc_check == ~kept.utc_s &&
	    kept.tz_check == ~kept.tz && kept.utc_s >= WALL_CLOCK_MIN_UTC &&
	    tz_valid((int32_t)kept.tz)) {
		k_spinlock_key_t key = k_spin_lock(&lock);

		base_ms = (int64_t)kept.utc_s * 1000 - k_uptime_get();
		tz_offset_min = (int16_t)(int32_t)kept.tz;
		valid = true;
		k_spin_unlock(&lock, key);
	}

	kept.magic = 0U;
	wall_clock_keep();

	return valid;
}

int wall_clock_set(uint32_t utc_s, int16_t tz)
{
	if (utc_s < WALL_CLOCK_MIN_UTC || !tz_valid(tz)) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);

	base_ms = (int64_t)utc_s * 1000 - k_uptime_get();
	tz_offset_min = tz;
	valid = true;
	k_spin_unlock(&lock, key);

	return 0;
}

bool wall_clock_now(uint32_t *utc_s)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool ok = valid;
	int64_t ms = base_ms + k_uptime_get();

	k_spin_unlock(&lock, key);

	if (ok) {
		*utc_s = (uint32_t)(ms / 1000);
	}
	return ok;
}

int16_t wall_clock_tz_offset(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	int16_t tz = valid ? tz_offset_min : 0;

	k_spin_unlock(&lock, key);
	return tz;
}

bool wall_clock_local_minute(uint16_t *minute)
{
	uint32_t utc_s;

	if (!wall_clock_now(&utc_s)) {
		return false;
	}

	int64_t local_s = (int64_t)utc_s + (int64_t)wall_clock_tz_offset() * 60;

	*minute = (uint16_t)((local_s / 60) % 1440);
	return true;
}

bool wall_clock_boot_utc(uint32_t *utc_s)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool ok = valid;
	int64_t ms = base_ms;

	k_spin_unlock(&lock, key);

	if (ok) {
		*utc_s = (uint32_t)(ms / 1000);
	}
	return ok;
}

void wall_clock_keep(void)
{
	uint32_t utc_s;

	if (!wall_clock_now(&utc_s)) {
		return;
	}

	uint32_t tz = (uint32_t)(int32_t)wall_clock_tz_offset();

	/* A reset part-way through leaves the magic invalid, so the copy is ignored. */
	kept.magic = 0U;
	kept.utc_s = utc_s;
	kept.utc_check = ~utc_s;
	kept.tz = tz;
	kept.tz_check = ~tz;
	kept.magic = KEPT_MAGIC;
}
