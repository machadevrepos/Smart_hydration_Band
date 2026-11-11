#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdint.h>

/*
 * Start the BMI270 and its built-in step counter. Takes about 0.5 s at
 * 400 kHz I2C, mostly loading the chip's firmware (the waits between chunks
 * are fixed). The count starts at 0.
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
 * The accelerometer's reset range is +-8 g, so one g is this many counts. The
 * firmware leaves the range at its reset value.
 */
#define STEP_COUNTER_ACCEL_PER_G 4096

/*
 * Read the latest acceleration on the three axes, in counts
 * (STEP_COUNTER_ACCEL_PER_G per g). Used to tell whether the wrist is still.
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_read_accel(int16_t xyz[3]);

/*
 * Turn the accelerometer off before the band powers off.
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_shutdown(void);

#endif /* STEP_COUNTER_H */
