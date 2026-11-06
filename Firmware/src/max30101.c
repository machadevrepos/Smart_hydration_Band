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

#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA     0x0C
#define REG_SLOT_1_2    0x11
#define MODE_SHDN       BIT(7)

/*
 * The settings the Zephyr driver wrote at boot, read back once so they can be
 * written again if the sensor resets on its own (a supply dip) and stops
 * sampling. The driver stays the one source of the configuration.
 */
static struct {
	bool valid;
	uint8_t fifo_mode_spo2[3]; /* 0x08-0x0A */
	uint8_t led_pa[3];         /* 0x0C-0x0E */
	uint8_t slots[2];          /* 0x11-0x12 */
} saved;

static const struct device *const max30101_dev = DEVICE_DT_GET(MAX30101_NODE);
static const struct i2c_dt_spec max30101_i2c = I2C_DT_SPEC_GET(MAX30101_NODE);

int max30101_init(void)
{
	if (!device_is_ready(max30101_dev)) {
		LOG_ERR("MAX30101 device not ready (check wiring, I2C address and supply)");
		return -ENODEV;
	}

	if (i2c_burst_read_dt(&max30101_i2c, REG_FIFO_CONFIG, saved.fifo_mode_spo2,
			      sizeof(saved.fifo_mode_spo2)) == 0 &&
	    i2c_burst_read_dt(&max30101_i2c, REG_LED1_PA, saved.led_pa, sizeof(saved.led_pa)) == 0 &&
	    i2c_burst_read_dt(&max30101_i2c, REG_SLOT_1_2, saved.slots, sizeof(saved.slots)) == 0) {
		saved.valid = true;
	} else {
		LOG_WRN("MAX30101 settings not saved; a sensor reset will not be recovered");
	}

	return 0;
}

int max30101_restore(void)
{
	static const uint8_t clear_fifo[] = {REG_FIFO_WR_PTR, 0, 0, 0};
	int ret;

	if (!saved.valid) {
		return -ENODATA;
	}

	/* Everything but the mode first; writing the mode starts sampling. */
	ret = i2c_reg_write_byte_dt(&max30101_i2c, REG_FIFO_CONFIG, saved.fifo_mode_spo2[0]);
	if (ret == 0) {
		ret = i2c_reg_write_byte_dt(&max30101_i2c, REG_SPO2_CONFIG,
					    saved.fifo_mode_spo2[2]);
	}
	if (ret == 0) {
		ret = i2c_burst_write_dt(&max30101_i2c, REG_LED1_PA, saved.led_pa,
					 sizeof(saved.led_pa));
	}
	if (ret == 0) {
		ret = i2c_burst_write_dt(&max30101_i2c, REG_SLOT_1_2, saved.slots,
					 sizeof(saved.slots));
	}
	if (ret == 0) {
		/* FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR to 0: start from an empty FIFO. */
		ret = i2c_write_dt(&max30101_i2c, clear_fifo, sizeof(clear_fifo));
	}
	if (ret == 0) {
		ret = i2c_reg_write_byte_dt(&max30101_i2c, REG_MODE_CONFIG,
					    saved.fifo_mode_spo2[1]);
	}

	return ret;
}

int max30101_shutdown(void)
{
	/* MODE_CONFIG SHDN: LEDs and sampling off, registers kept, under 1 uA. */
	return i2c_reg_update_byte_dt(&max30101_i2c, REG_MODE_CONFIG, MODE_SHDN, MODE_SHDN);
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
