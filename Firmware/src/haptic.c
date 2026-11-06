#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "app_config.h"
#include "haptic.h"

static const struct gpio_dt_spec motor = GPIO_DT_SPEC_GET(DT_NODELABEL(haptic_motor), gpios);

/* Durations in ms, alternating motor on and off, starting on. */
static const uint16_t pattern_buzz[] = {APP_HAPTIC_BUZZ_MS};
static const uint16_t pattern_double_tap[] = {APP_HAPTIC_TAP_MS, APP_HAPTIC_TAP_GAP_MS,
					      APP_HAPTIC_TAP_MS};

static const uint16_t *pattern;
static size_t pattern_len;
static size_t pattern_step;
static bool motor_ready;
static K_MUTEX_DEFINE(start_lock);

static void pattern_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(pattern_work, pattern_work_handler);

static void pattern_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (pattern_step >= pattern_len) {
		(void)gpio_pin_set_dt(&motor, 0);
		return;
	}

	(void)gpio_pin_set_dt(&motor, (pattern_step % 2U) == 0U);
	(void)k_work_schedule(&pattern_work, K_MSEC(pattern[pattern_step]));
	pattern_step++;
}

static void pattern_start(const uint16_t *steps, size_t len)
{
	static struct k_work_sync sync;

	if (!motor_ready) {
		return;
	}

	/* Both the main loop and BLE commands can start a pattern. */
	k_mutex_lock(&start_lock, K_FOREVER);

	(void)k_work_cancel_delayable_sync(&pattern_work, &sync);

	pattern = steps;
	pattern_len = len;
	pattern_step = 0;

	(void)k_work_schedule(&pattern_work, K_NO_WAIT);

	k_mutex_unlock(&start_lock);
}

int haptic_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&motor)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&motor, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	motor_ready = true;
	return 0;
}

void haptic_stop(void)
{
	static struct k_work_sync sync;

	if (!motor_ready) {
		return;
	}

	k_mutex_lock(&start_lock, K_FOREVER);
	(void)k_work_cancel_delayable_sync(&pattern_work, &sync);
	(void)gpio_pin_set_dt(&motor, 0);
	k_mutex_unlock(&start_lock);
}

void haptic_buzz(void)
{
	pattern_start(pattern_buzz, ARRAY_SIZE(pattern_buzz));
}

void haptic_double_tap(void)
{
	pattern_start(pattern_double_tap, ARRAY_SIZE(pattern_double_tap));
}
