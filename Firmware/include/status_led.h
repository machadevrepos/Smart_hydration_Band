#ifndef STATUS_LED_H
#define STATUS_LED_H

enum status_led_color {
	STATUS_LED_WHITE,
	STATUS_LED_GREEN,
	STATUS_LED_OFF,
};

/*
 * Show a solid color.
 *
 * Returns 0 on success, negative errno on failure.
 */
int status_led_set(enum status_led_color color);

#endif /* STATUS_LED_H */
