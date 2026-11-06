#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdint.h>

/*
 * Start the BMI270 and its built-in step counter. Takes about 1 s at 100 kHz
 * I2C, mostly loading the chip's firmware. The count starts at 0.
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_init(void);

/*
 * Read the steps counted since step_counter_init().
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_read(uint32_t *steps);

/*
 * Turn the accelerometer off before the band powers off.
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_shutdown(void);

#endif /* STEP_COUNTER_H */
