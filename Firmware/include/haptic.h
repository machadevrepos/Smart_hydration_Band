#ifndef HAPTIC_H
#define HAPTIC_H

/*
 * Configure the motor output, off.
 *
 * Returns 0 on success, negative errno on failure.
 */
int haptic_init(void);

/*
 * Patterns run in the background and return immediately, so the caller keeps
 * draining the sensor FIFO. A new pattern replaces one still running. They do
 * nothing if haptic_init() failed.
 */

/* One buzz: confirms a button press. */
void haptic_buzz(void);

/* Two short firm pulses: the reminder to drink water. */
void haptic_double_tap(void);

/* Stop any pattern and leave the motor off. Waits for a running step to end. */
void haptic_stop(void);

#endif /* HAPTIC_H */
