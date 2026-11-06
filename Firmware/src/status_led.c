#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>

#include "app_config.h"
#include "status_led.h"

static const struct device *const led_strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));

int status_led_set(enum status_led_color color)
{
	struct led_rgb pixel = {0};

	if (!device_is_ready(led_strip)) {
		return -ENODEV;
	}

	switch (color) {
	case STATUS_LED_WHITE:
		pixel.r = APP_LED_LEVEL;
		pixel.g = APP_LED_LEVEL;
		pixel.b = APP_LED_LEVEL;
		break;
	case STATUS_LED_GREEN:
		pixel.g = APP_LED_LEVEL;
		break;
	case STATUS_LED_OFF:
	default:
		break;
	}

	return led_strip_update_rgb(led_strip, &pixel, 1);
}
