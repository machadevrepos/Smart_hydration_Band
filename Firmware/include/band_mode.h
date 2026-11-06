#ifndef BAND_MODE_H
#define BAND_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

/*
 * The band's two modes and their water reminders. Daily is the power-up mode
 * (white LED); a button press starts a Night session (green LED) and another
 * ends it. The band works this way with no app or pairing. The app can also
 * set the mode, the two reminder intervals and the quiet hours; the intervals
 * and quiet hours are kept in flash.
 */
enum band_mode {
	BAND_MODE_DAILY,
	BAND_MODE_NIGHT,
};

/* Reminder intervals in minutes. */
struct band_intervals {
	uint16_t daily_min;
	uint16_t night_min;
};

/*
 * Quiet hours, local minutes after midnight (0-1439). Daily Mode reminders that
 * fall from start up to end are skipped; the window may cross midnight. Start
 * equal to end means no quiet hours.
 */
struct band_quiet_hours {
	uint16_t start_min;
	uint16_t end_min;
};

#define BAND_MINUTES_PER_DAY 1440

/* Accepted interval range in minutes. The app offers 20, 30, 45 and 60. */
#define BAND_INTERVAL_MIN 1
#define BAND_INTERVAL_MAX 240

/* What band_mode_process() did, for the caller to report. */
#define BAND_EVENT_REMINDER      BIT(0) /* Double tap fired. */
#define BAND_EVENT_MODE_FROM_APP BIT(1) /* Mode changed by an app request. */
#define BAND_EVENT_INTERVALS     BIT(2) /* Intervals changed by an app request. */
#define BAND_EVENT_SAVE_FAILED   BIT(3) /* A new setting could not be stored in flash. */
#define BAND_EVENT_QUIET_SKIPPED BIT(4) /* A reminder fell in quiet hours: no buzz. */
#define BAND_EVENT_QUIET_HOURS   BIT(5) /* Quiet hours changed by an app request. */

/*
 * Load the stored intervals, set the starting mode and the LED. With
 * try_restore (only after a reset that kept RAM), the mode and reminder
 * schedule from before the reset are resumed if a valid copy was kept;
 * *restored says whether that happened. Otherwise the band starts in Daily
 * Mode with a full interval. Call after storage_init().
 *
 * Returns 0 on success, or the negative errno from the LED update.
 */
int band_mode_init(bool try_restore, bool *restored);

/*
 * Switch mode for a button press: set the LED, buzz and restart the reminder
 * interval. Main loop only.
 *
 * Returns 0 on success, or the negative errno from the LED update.
 */
int band_mode_toggle(void);

/*
 * Ask for a mode from the app. Safe from any thread; band_mode_process()
 * applies it like a button press (LED, buzz, interval restart). Asking for the
 * current mode does nothing.
 */
void band_mode_request(enum band_mode mode);

/*
 * Ask for new intervals from the app. Safe from any thread. A field of 0 means
 * the default (Daily 90, Night 35 minutes). band_mode_process() applies and
 * stores them; if the current mode's interval changed, its wait restarts.
 *
 * Returns 0, or -EINVAL if a value is outside BAND_INTERVAL_MIN..MAX.
 */
int band_mode_request_intervals(const struct band_intervals *intervals);

/* The intervals in effect, or the ones just requested. Safe from any thread. */
void band_mode_get_intervals(struct band_intervals *intervals);

/*
 * Ask for new quiet hours from the app. Safe from any thread;
 * band_mode_process() applies and stores them.
 *
 * Returns 0, or -EINVAL if a value is 1440 or more.
 */
int band_mode_request_quiet_hours(const struct band_quiet_hours *quiet);

/* The quiet hours in effect, or the ones just requested. Safe from any thread. */
void band_mode_get_quiet_hours(struct band_quiet_hours *quiet);

enum band_mode band_mode_get(void);

const char *band_mode_name(enum band_mode mode);

/* Current mode's reminder interval in seconds. */
uint32_t band_mode_interval_s(void);

/* Seconds until the next reminder, rounded up. */
uint32_t band_mode_next_reminder_s(void);

/*
 * Call regularly from the main loop. Applies app requests and fires the
 * double tap when a reminder is due.
 *
 * Returns a mask of BAND_EVENT_* flags, 0 if nothing happened.
 */
uint32_t band_mode_process(void);

#endif /* BAND_MODE_H */
