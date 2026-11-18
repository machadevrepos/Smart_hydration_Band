#ifndef HAPTIC_H
#define HAPTIC_H

#include <stdbool.h>
#include <stdint.h>

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

/*
 * True if the motor is off and has been off for at least ms (or has never run).
 * The motor's current pulls the battery voltage down while it runs, so the
 * battery is measured only when this holds. Safe from any thread.
 */
bool haptic_idle_for(uint32_t ms);

#endif /* HAPTIC_H */
