#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog, LOG_LEVEL_INF);

/*
 * Reset after 10 s without a feed. The main loop feeds every ~40 ms; its
 * slowest pass is about 1 s (two I2C transfers timing out on a stuck bus), and
 * start-up before the first feed takes about 2 s (BMI270 firmware load).
 */
#define WATCHDOG_TIMEOUT_MS 10000

/* The work queue proves it is alive this often... */
#define HEARTBEAT_PERIOD_MS 1000
/* ...and is taken as stalled after this long without it. */
#define WORKQ_STALL_MS 5000

static const struct device *const wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));
static int channel = -1;

/* k_uptime_get_32() of the last heartbeat; unsigned subtraction handles wrap. */
static atomic_t workq_alive_ms;
static bool stall_reported;

static void heartbeat_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat_handler);

static void heartbeat_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	atomic_set(&workq_alive_ms, (atomic_val_t)k_uptime_get_32());
	(void)k_work_schedule(&heartbeat_work, K_MSEC(HEARTBEAT_PERIOD_MS));
}

int watchdog_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.window = {.min = 0, .max = WATCHDOG_TIMEOUT_MS},
		.callback = NULL,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ret;

	if (!device_is_ready(wdt)) {
		return -ENODEV;
	}

	ret = wdt_install_timeout(wdt, &cfg);
	if (ret < 0) {
		return ret;
	}
	channel = ret;

	atomic_set(&workq_alive_ms, (atomic_val_t)k_uptime_get_32());
	(void)k_work_schedule(&heartbeat_work, K_MSEC(HEARTBEAT_PERIOD_MS));

	/* Pause while a debugger halts the CPU, so breakpoints do not reset it. */
	ret = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret < 0) {
		channel = -1;
		return ret;
	}

	return 0;
}

void watchdog_feed(void)
{
	if (channel < 0) {
		return;
	}

	uint32_t since = k_uptime_get_32() - (uint32_t)atomic_get(&workq_alive_ms);

	if (since > WORKQ_STALL_MS) {
		/* Stop feeding: the reset is the recovery. */
		if (!stall_reported) {
			stall_reported = true;
			printk("  system work queue stalled for %u ms, watchdog will reset\n", since);
		}
		return;
	}

	(void)wdt_feed(wdt, channel);
}

const char *watchdog_reset_cause(void)
{
	uint32_t cause = 0;

	if (hwinfo_get_reset_cause(&cause) < 0) {
		return "unknown";
	}
	(void)hwinfo_clear_reset_cause();

	/* Several bits can be set; report the most telling one. */
	if (cause & RESET_WATCHDOG) {
		return "watchdog";
	}
	if (cause & RESET_CPU_LOCKUP) {
		return "CPU lockup";
	}
	if (cause & RESET_SOFTWARE) {
		return "software (fatal error or reboot)";
	}
	if (cause & RESET_BROWNOUT) {
		return "brown-out";
	}
	if (cause & RESET_PIN) {
		return "reset pin";
	}
	if (cause & RESET_DEBUG) {
		return "debugger";
	}
	if (cause & RESET_LOW_POWER_WAKE) {
		return "wake from system off";
	}
	if (cause == 0) {
		/* The nRF52 sets no reason bit on power-on. */
		return "power-on";
	}

	return "other";
}
