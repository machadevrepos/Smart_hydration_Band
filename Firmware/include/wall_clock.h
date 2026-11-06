#ifndef WALL_CLOCK_H
#define WALL_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Time of day, set by the app. The band has no battery-backed clock: after
 * power-on the time is unknown until the app writes it, and it then runs from
 * the nRF52 clock (about +-500 ppm, under 45 s a day). A watchdog or software
 * reset keeps it, losing only the few seconds the reset takes.
 */

/* Earliest UTC time accepted (2024-01-01), to reject an unset phone clock. */
#define WALL_CLOCK_MIN_UTC 1704067200U

/* Accepted local offset from UTC in minutes: UTC-12:00 to UTC+14:00. */
#define WALL_CLOCK_TZ_MIN (-720)
#define WALL_CLOCK_TZ_MAX 840

/*
 * Start the clock. With try_restore (only after a reset that kept RAM) the
 * time from before the reset is resumed if a valid copy was kept.
 *
 * Returns true if the time is known.
 */
bool wall_clock_init(bool try_restore);

/*
 * Set the time: seconds since 1970 UTC and the local offset from UTC in
 * minutes (daylight saving included). Safe from any thread.
 *
 * Returns 0, or -EINVAL if a value is outside the accepted range.
 */
int wall_clock_set(uint32_t utc_s, int16_t tz_offset_min);

/* Current UTC seconds. Returns false, leaving *utc_s alone, if not set. */
bool wall_clock_now(uint32_t *utc_s);

/* Local offset from UTC in minutes, 0 if never set. */
int16_t wall_clock_tz_offset(void);

/* Local minute of the day, 0-1439. Returns false if the time is not set. */
bool wall_clock_local_minute(uint16_t *minute);

/* UTC seconds at which this boot's uptime was 0. Returns false if not set. */
bool wall_clock_boot_utc(uint32_t *utc_s);

/* Refresh the copy kept for a reset. Call from every main loop pass. */
void wall_clock_keep(void);

#endif /* WALL_CLOCK_H */
