#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "button.h"

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button1), gpios);

static bool stable_down;
static bool last_read_down;

int button_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_get_dt(&button);
	if (ret < 0) {
		return ret;
	}

	/* A button already held at startup is not a press. */
	stable_down = ret > 0;
	last_read_down = stable_down;

	return 0;
}

bool button_pressed(void)
{
	int ret = gpio_pin_get_dt(&button);

	if (ret < 0) {
		return false;
	}

	bool down = ret > 0;
	bool pressed = false;

	if (down == last_read_down && down != stable_down) {
		stable_down = down;
		pressed = down;
	}
	last_read_down = down;

	return pressed;
}
