#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdbool.h>

/*
 * Hardware watchdog. The band's one job is to keep buzzing, so if the main loop
 * or the system work queue (motor patterns, Bluetooth host) stops, the chip
 * resets instead of sitting silent with the LED on.
 */

/*
 * Start the watchdog. Call first in main(): from here on watchdog_feed() must
 * run at least every WATCHDOG_TIMEOUT_MS. It cannot be stopped until reset.
 *
 * Returns 0 on success, negative errno on failure (then nothing resets the band).
 */
int watchdog_init(void);

/*
 * Call from every main loop pass. Feeds the watchdog only while the system work
 * queue is also running, so a stall in either one resets the band.
 */
void watchdog_feed(void);

/*
 * Why the chip last reset, in a few words ("power-on", "watchdog", ...). The
 * hardware reason is read and cleared on the first call and kept for later ones.
 */
const char *watchdog_reset_cause(void);

/*
 * True when the last reset kept RAM contents (watchdog, fatal error, software
 * reset, debugger), so state saved in no-init RAM before it can be trusted.
 */
bool watchdog_reset_kept_ram(void);

#endif /* WATCHDOG_H */
