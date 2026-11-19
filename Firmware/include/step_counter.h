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
 * Read the steps counted since step_counter_init(). The count only grows: if
 * the chip restarts its own count, counting carries on from the total. A read
 * showing more steps than a wrist could take since the last one is refused.
 *
 * Returns 0 on success, negative errno on failure.
 */
int step_counter_read(uint32_t *steps);

/*
 * Check that the BMI270 still has its setup, and start it again if it lost it
 * (a supply dip resets it: firmware, step counter and accelerometer off). The
 * restart takes about 0.5 s; counting carries on from the total. Call every few
 * seconds from the main loop.
 *
 * Returns 0 if the chip is fine, 1 if it was restarted, negative errno if it
 * could not be checked or restarted.
 */
int step_counter_check(void);

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
