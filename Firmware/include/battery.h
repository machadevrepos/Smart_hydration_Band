#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Prepare the battery measurement.
 *
 * Returns 0 on success, negative errno on failure.
 */
int battery_init(void);

/*
 * Measure the battery. The percentage comes from a typical LiPo discharge curve,
 * so it is approximate, and it follows every dip in the voltage: show the
 * gauge's value instead.
 *
 * Returns 0 on success, negative errno on failure.
 */
int battery_read(uint8_t *percent, int32_t *millivolts);

/*
 * Start the gauge. With try_restore (only after a reset that kept RAM) the
 * percentage shown before the reset is resumed if a valid copy was kept.
 *
 * Returns true if a shown percentage is already known.
 */
bool battery_gauge_init(bool try_restore);

/*
 * Feed one reading, taken with the motor off and settled, every 10 s. Returns
 * the percentage to show: it goes down by at most 1% a minute and only when the
 * battery does, and up only while charging, 1% at most every 100 s (see
 * battery.c).
 */
uint8_t battery_gauge_update(int32_t millivolts);

/* The filtered voltage behind the shown percentage, for logs. */
int32_t battery_gauge_filtered_mv(void);

/* True while the voltage says the battery is charging. */
bool battery_gauge_charging(void);

#endif /* BATTERY_H */
