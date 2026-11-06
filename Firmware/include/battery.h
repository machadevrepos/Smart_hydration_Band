#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

/*
 * Prepare the battery measurement.
 *
 * Returns 0 on success, negative errno on failure.
 */
int battery_init(void);

/*
 * Measure the battery. The percentage comes from a typical LiPo discharge curve,
 * so it is approximate.
 *
 * Returns 0 on success, negative errno on failure.
 */
int battery_read(uint8_t *percent, int32_t *millivolts);

#endif /* BATTERY_H */
