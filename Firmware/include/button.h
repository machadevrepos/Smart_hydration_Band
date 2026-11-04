#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>

/*
 * Configure the push button.
 *
 * Returns 0 on success, negative errno on failure.
 */
int button_init(void);

/*
 * Poll the button; call this regularly (every 20-50 ms). The input must read
 * the same twice in a row to count, which filters contact bounce.
 *
 * Returns true once per press, when the button goes down.
 */
bool button_pressed(void);

/* The debounced state: true while the button is held down. */
bool button_is_down(void);

#endif /* BUTTON_H */
