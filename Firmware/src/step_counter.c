#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "step_counter.h"

/* Bosch's BMI270 firmware, as shipped with Zephyr's BMI270 driver. */
#include "bmi270_config_file.h"

LOG_MODULE_REGISTER(step_counter, LOG_LEVEL_INF);

/*
 * Talks to the BMI270 directly instead of through Zephyr's driver. That driver
 * loads the firmware in 256-byte writes, which cannot work on the nRF52832:
 * its I2C DMA moves at most 255 bytes and cannot read from flash. It also has
 * no step-counter API. Here the firmware goes in 128-byte chunks from RAM, and
 * the step counter is enabled the way Bosch's BMI270 Sensor API does it.
 */

#define REG_CHIP_ID         0x00
/* ACC_X, ACC_Y, ACC_Z, each 16-bit little endian. */
#define REG_ACC_DATA        0x0C
#define REG_INTERNAL_STATUS 0x21
#define REG_FEAT_PAGE       0x2F
#define REG_FEATURES        0x30
#define REG_INIT_CTRL       0x59
#define REG_INIT_ADDR_0     0x5B
#define REG_INIT_DATA       0x5E
#define REG_PWR_CONF        0x7C
#define REG_PWR_CTRL        0x7D
#define REG_CMD             0x7E

#define CHIP_ID            0x24
#define CMD_SOFT_RESET     0xB6
#define INIT_CTRL_PREPARE  0x00
#define INIT_CTRL_COMPLETE 0x01
#define STATUS_MESSAGE_MSK 0x0F
#define STATUS_INIT_OK     0x01
#define PWR_CONF_ADV_SAVE  BIT(0)
#define PWR_CTRL_ACC_EN    BIT(2)

/* Feature registers are a 16-byte window at REG_FEATURES, selected by page. */
#define FEATURE_PAGE_LEN      16
#define PAGE_STEP_OUT         0
#define PAGE_STEP_CONFIG      6
/* Step count output: page 0, offset 0, 32 bits, little endian. */
#define STEP_OUT_LEN          4
/* Step counter enable: page 6, offset 0x02 + 1, bit 4. */
#define STEP_ENABLE_OFFSET    0x03
#define STEP_ENABLE_BIT       BIT(4)

/* Must be even: the chip addresses its firmware memory in 16-bit words. */
#define FIRMWARE_CHUNK_LEN 128

#define SOFT_RESET_DELAY_US 2000
/* Minimum gap after a write while advanced power save may be active. */
#define WRITE_DELAY_US      450
#define INIT_POLL_DELAY_MS  10
#define INIT_POLL_TRIES     15

static const struct i2c_dt_spec bmi270 = I2C_DT_SPEC_GET(DT_NODELABEL(bmi270));

/*
 * The count reported is the chip's count plus what it had counted before each
 * restart, so it only grows from power-up even if the chip resets on its own
 * (a supply dip clears its firmware and its count). Main loop only.
 */
static uint32_t count_offset;
static uint32_t last_count;
static int64_t last_read_ms;

/*
 * The total, kept in RAM that start-up does not clear, with its inverse as a
 * check: start-up resets the chip and its count, so without this a watchdog
 * reset would send the app's step count back to 0.
 */
struct kept_steps {
	uint32_t magic;
	uint32_t total;
	uint32_t check;
};

#define KEPT_MAGIC 0x57e90001U

static __noinit struct kept_steps kept;

static void keep_total(uint32_t total)
{
	kept.magic = 0U;
	kept.total = total;
	kept.check = ~total;
	kept.magic = KEPT_MAGIC;
}

/* A wrist cannot take more steps than this per second; more is a bad read. */
#define MAX_STEPS_PER_S  5
#define MAX_STEPS_MARGIN 50

/*
 * After a restart that failed, wait this long before the next: each attempt
 * holds the main loop for about 0.5 s, which overflows the MAX30101 FIFO and
 * restarts heart rate.
 */
#define RESTART_BACKOFF_MS (5 * 60 * 1000)
#define STATUS_NOT_INIT    0x00
static int64_t restart_allowed_ms;

static int write_reg(uint8_t reg, uint8_t value)
{
	int ret = i2c_reg_write_byte_dt(&bmi270, reg, value);

	k_usleep(WRITE_DELAY_US);
	return ret;
}

static int load_firmware(void)
{
	static uint8_t tx[1 + FIRMWARE_CHUNK_LEN];
	const size_t size = sizeof(bmi270_config_file_base);
	int ret;

	ret = write_reg(REG_INIT_CTRL, INIT_CTRL_PREPARE);
	if (ret < 0) {
		return ret;
	}

	for (size_t offset = 0; offset < size; offset += FIRMWARE_CHUNK_LEN) {
		size_t len = MIN(size - offset, FIRMWARE_CHUNK_LEN);
		size_t word = offset / 2;
		const uint8_t addr[3] = {
			REG_INIT_ADDR_0,
			(uint8_t)(word & 0x0F),
			(uint8_t)(word >> 4),
		};

		ret = i2c_write_dt(&bmi270, addr, sizeof(addr));
		if (ret < 0) {
			return ret;
		}
		k_usleep(WRITE_DELAY_US);

		/* One RAM buffer, so the transfer needs no driver concatenation. */
		tx[0] = REG_INIT_DATA;
		memcpy(&tx[1], &bmi270_config_file_base[offset], len);
		ret = i2c_write_dt(&bmi270, tx, len + 1);
		if (ret < 0) {
			return ret;
		}
		k_usleep(WRITE_DELAY_US);
	}

	ret = write_reg(REG_INIT_CTRL, INIT_CTRL_COMPLETE);
	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < INIT_POLL_TRIES; i++) {
		uint8_t status;

		k_msleep(INIT_POLL_DELAY_MS);
		ret = i2c_reg_read_byte_dt(&bmi270, REG_INTERNAL_STATUS, &status);
		if (ret < 0) {
			return ret;
		}
		if ((status & STATUS_MESSAGE_MSK) == STATUS_INIT_OK) {
			return 0;
		}
	}

	LOG_ERR("BMI270 firmware did not start");
	return -EIO;
}

/* Page writes are the whole 16-byte window, as in Bosch's API. */
static int enable_step_counter(void)
{
	uint8_t tx[1 + FEATURE_PAGE_LEN];
	uint8_t check;
	int ret;

	ret = write_reg(REG_FEAT_PAGE, PAGE_STEP_CONFIG);
	if (ret < 0) {
		return ret;
	}

	ret = i2c_burst_read_dt(&bmi270, REG_FEATURES, &tx[1], FEATURE_PAGE_LEN);
	if (ret < 0) {
		return ret;
	}

	tx[0] = REG_FEATURES;
	tx[1 + STEP_ENABLE_OFFSET] |= STEP_ENABLE_BIT;
	ret = i2c_write_dt(&bmi270, tx, sizeof(tx));
	if (ret < 0) {
		return ret;
	}
	k_usleep(WRITE_DELAY_US);

	ret = i2c_reg_read_byte_dt(&bmi270, REG_FEATURES + STEP_ENABLE_OFFSET, &check);
	if (ret < 0) {
		return ret;
	}
	if ((check & STEP_ENABLE_BIT) == 0) {
		LOG_ERR("BMI270 step counter did not enable");
		return -EIO;
	}

	/* Leave the output page selected for step_counter_read(). */
	return write_reg(REG_FEAT_PAGE, PAGE_STEP_OUT);
}

/* Reset the chip, load its firmware and start the step counter from 0. */
static int start_chip(void)
{
	uint8_t value;
	uint8_t pwr_conf;
	int ret;

	if (!i2c_is_ready_dt(&bmi270)) {
		return -ENODEV;
	}

	ret = i2c_reg_read_byte_dt(&bmi270, REG_CHIP_ID, &value);
	if (ret < 0) {
		LOG_ERR("BMI270 not responding at 0x%02x (%d)", bmi270.addr, ret);
		return ret;
	}
	if (value != CHIP_ID) {
		LOG_ERR("BMI270 chip ID 0x%02x, expected 0x%02x", value, CHIP_ID);
		return -EIO;
	}

	ret = i2c_reg_write_byte_dt(&bmi270, REG_CMD, CMD_SOFT_RESET);
	if (ret < 0) {
		return ret;
	}
	k_usleep(SOFT_RESET_DELAY_US);

	/* Advanced power save stays off while loading and configuring. */
	ret = i2c_reg_read_byte_dt(&bmi270, REG_PWR_CONF, &pwr_conf);
	if (ret < 0) {
		return ret;
	}
	ret = write_reg(REG_PWR_CONF, pwr_conf & ~PWR_CONF_ADV_SAVE);
	if (ret < 0) {
		return ret;
	}

	ret = load_firmware();
	if (ret < 0) {
		return ret;
	}

	ret = enable_step_counter();
	if (ret < 0) {
		return ret;
	}

	/* Accelerometer at its reset settings, as in Bosch's step counter example. */
	ret = i2c_reg_read_byte_dt(&bmi270, REG_PWR_CTRL, &value);
	if (ret < 0) {
		return ret;
	}
	ret = write_reg(REG_PWR_CTRL, value | PWR_CTRL_ACC_EN);
	if (ret < 0) {
		return ret;
	}

	return write_reg(REG_PWR_CONF, pwr_conf | PWR_CONF_ADV_SAVE);
}

int step_counter_init(bool try_restore)
{
	count_offset = 0;
	if (try_restore && kept.magic == KEPT_MAGIC && kept.check == ~kept.total) {
		count_offset = kept.total;
	}
	keep_total(count_offset);
	last_count = 0;
	last_read_ms = k_uptime_get();

	return start_chip();
}

int step_counter_check(void)
{
	uint8_t status;
	uint8_t pwr_ctrl;
	int ret;

	ret = i2c_reg_read_byte_dt(&bmi270, REG_INTERNAL_STATUS, &status);
	if (ret < 0) {
		return ret;
	}
	ret = i2c_reg_read_byte_dt(&bmi270, REG_PWR_CTRL, &pwr_ctrl);
	if (ret < 0) {
		return ret;
	}

	/*
	 * A reset turns the accelerometer off (PWR_CTRL reads 0) and leaves the
	 * firmware "not initialized". Only those two are taken as a reset; other
	 * status messages are left alone.
	 */
	if ((pwr_ctrl & PWR_CTRL_ACC_EN) != 0 &&
	    (status & STATUS_MESSAGE_MSK) != STATUS_NOT_INIT) {
		return 0;
	}

	int64_t now = k_uptime_get();

	if (now < restart_allowed_ms) {
		return -EAGAIN;
	}

	LOG_WRN("BMI270 lost its setup (status 0x%02x, power 0x%02x), restarting it", status,
		pwr_ctrl);
	count_offset += last_count;
	last_count = 0;
	last_read_ms = now;

	ret = start_chip();
	if (ret < 0) {
		restart_allowed_ms = k_uptime_get() + RESTART_BACKOFF_MS;
		return ret;
	}
	return 1;
}

int step_counter_read_accel(int16_t xyz[3])
{
	uint8_t rx[6];
	int ret = i2c_burst_read_dt(&bmi270, REG_ACC_DATA, rx, sizeof(rx));

	if (ret < 0) {
		return ret;
	}

	xyz[0] = (int16_t)sys_get_le16(&rx[0]);
	xyz[1] = (int16_t)sys_get_le16(&rx[2]);
	xyz[2] = (int16_t)sys_get_le16(&rx[4]);
	return 0;
}

int step_counter_shutdown(void)
{
	/* Accelerometer, gyro, aux and temperature off: the chip draws a few uA. */
	return write_reg(REG_PWR_CTRL, 0x00);
}

int step_counter_read(uint32_t *steps)
{
	uint8_t rx[STEP_OUT_LEN];
	int ret = i2c_burst_read_dt(&bmi270, REG_FEATURES, rx, sizeof(rx));

	if (ret < 0) {
		return ret;
	}

	uint32_t count = sys_get_le32(rx);
	int64_t now = k_uptime_get();
	int64_t elapsed_s = (now - last_read_ms) / 1000 + 1;

	if (count < last_count) {
		/* The chip restarted its count: carry on from the total so far. */
		count_offset += last_count;
	} else if (count - last_count > MAX_STEPS_PER_S * elapsed_s + MAX_STEPS_MARGIN) {
		/* More steps than a wrist can take since the last read. */
		return -EIO;
	}

	last_count = count;
	last_read_ms = now;
	*steps = count_offset + count;
	keep_total(*steps);
	return 0;
}
