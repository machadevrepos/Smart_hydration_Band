#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "max30101.h"

LOG_MODULE_REGISTER(max30101, LOG_LEVEL_INF);

#define MAX30101_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(maxim_max30101)

BUILD_ASSERT(DT_NODE_EXISTS(MAX30101_NODE), "No okay maxim,max30101 node in devicetree");

/*
 * max30101_read_one() below and the heart-rate timing rely on this exact driver
 * configuration: one FIFO channel (IR) at 100 Hz.
 */
BUILD_ASSERT(DT_ENUM_HAS_VALUE(MAX30101_NODE, acq_mode, multi_led), "Multi-LED mode is required");
BUILD_ASSERT(DT_PROP_BY_IDX(MAX30101_NODE, led_slot, 0) == 2 &&
		     DT_PROP_BY_IDX(MAX30101_NODE, led_slot, 1) == 0 &&
		     DT_PROP_BY_IDX(MAX30101_NODE, led_slot, 2) == 0 &&
		     DT_PROP_BY_IDX(MAX30101_NODE, led_slot, 3) == 0,
	     "led-slot must be <2 0 0 0> (IR only)");
BUILD_ASSERT(DT_PROP(MAX30101_NODE, smp_sr) == MAX30101_SAMPLE_RATE_HZ &&
		     DT_PROP(MAX30101_NODE, smp_ave) == 1,
	     "MAX30101_SAMPLE_RATE_HZ must match smp-sr with no averaging");
BUILD_ASSERT(DT_PROP(MAX30101_NODE, led_pw) == 411, "led-pw must be 411 (18-bit samples)");

/* FIFO pointer registers, read as one burst: FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR. */
#define REG_FIFO_WR_PTR 0x04
#define FIFO_PTR_MASK   0x1f

static const struct device *const max30101_dev = DEVICE_DT_GET(MAX30101_NODE);
static const struct i2c_dt_spec max30101_i2c = I2C_DT_SPEC_GET(MAX30101_NODE);

int max30101_init(void)
{
	if (!device_is_ready(max30101_dev)) {
		LOG_ERR("MAX30101 device not ready (check wiring, I2C address and supply)");
		return -ENODEV;
	}

	return 0;
}

/* Fetch one frame through the Zephyr driver. This pops one sample from the FIFO. */
static int max30101_read_one(int32_t *ir)
{
	struct sensor_value value;
	int ret;

	ret = sensor_sample_fetch(max30101_dev);
	if (ret < 0) {
		return ret;
	}

	ret = sensor_channel_get(max30101_dev, SENSOR_CHAN_IR, &value);
	if (ret < 0) {
		return ret;
	}

	*ir = value.val1;

	return 0;
}

int max30101_read_fifo(int32_t *ir, int max, bool *lost)
{
	uint8_t ptr[3];
	int available;
	int ret;

	if (ir == NULL || lost == NULL || max <= 0) {
		return -EINVAL;
	}

	/*
	 * The Zephyr driver reads FIFO_DATA without checking whether a sample
	 * is waiting, so check the pointers first. OVF_COUNTER is cleared when
	 * a sample is popped, so it must be read before draining.
	 */
	ret = i2c_burst_read_dt(&max30101_i2c, REG_FIFO_WR_PTR, ptr, sizeof(ptr));
	if (ret < 0) {
		return ret;
	}

	uint8_t wr = ptr[0] & FIFO_PTR_MASK;
	uint8_t ovf = ptr[1] & FIFO_PTR_MASK;
	uint8_t rd = ptr[2] & FIFO_PTR_MASK;

	/* WR == RD means empty, or full if samples have overflowed. */
	*lost = ovf != 0;
	available = *lost ? MAX30101_FIFO_DEPTH : (wr - rd) & FIFO_PTR_MASK;

	if (available > max) {
		available = max;
	}

	for (int i = 0; i < available; i++) {
		ret = max30101_read_one(&ir[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return available;
}
