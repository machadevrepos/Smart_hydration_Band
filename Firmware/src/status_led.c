#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>

#include "app_config.h"
#include "status_led.h"

static const struct device *const led_strip = DEVICE_DT_GET(DT_NODELABEL(led_strip));

int status_led_init(void)
{
	return device_is_ready(led_strip) ? 0 : -ENODEV;
}

int status_led_set(enum status_led_color color)
{
	struct led_rgb pixel = {.g = APP_LED_LEVEL};

	if (color == STATUS_LED_WHITE) {
		pixel.r = APP_LED_LEVEL;
		pixel.b = APP_LED_LEVEL;
	}

	return led_strip_update_rgb(led_strip, &pixel, 1);
}
