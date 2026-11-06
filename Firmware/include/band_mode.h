#ifndef BAND_MODE_H
#define BAND_MODE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The band's two modes and their water reminders. Daily is the power-up mode
 * (white LED); a button press starts a Night session (green LED) and another
 * ends it. The band works this way with no app or pairing.
 */
enum band_mode {
	BAND_MODE_DAILY,
	BAND_MODE_NIGHT,
};

/*
 * Set the starting mode and the LED. With try_restore (only after a reset that
 * kept RAM), the mode and reminder schedule from before the reset are resumed if
 * a valid copy was kept; *restored says whether that happened. Otherwise the
 * band starts in Daily Mode with a full interval.
 *
 * Returns 0 on success, or the negative errno from the LED update.
 */
int band_mode_init(bool try_restore, bool *restored);

/*
 * Switch mode for a button press: set the LED, buzz and restart the reminder
 * interval.
 *
 * Returns 0 on success, or the negative errno from the LED update.
 */
int band_mode_toggle(void);

enum band_mode band_mode_get(void);

const char *band_mode_name(enum band_mode mode);

/* Current reminder interval in seconds. */
uint32_t band_mode_interval_s(void);

/* Seconds until the next reminder, rounded up. */
uint32_t band_mode_next_reminder_s(void);

/*
 * Call regularly. Fires the double tap when a reminder is due.
 *
 * Returns true when a reminder fired.
 */
bool band_mode_process(void);

#endif /* BAND_MODE_H */
