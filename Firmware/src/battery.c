#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>

#include "battery.h"

/* The PCB divides the battery by R10 (800k) over R11 (2M) into P0.28. */
#define DIVIDER_TOTAL_KOHM  2800
#define DIVIDER_BOTTOM_KOHM 2000

static const struct adc_dt_spec adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

/* Typical LiPo at light load: voltage falling, with percent at each point. */
static const struct {
	int16_t millivolts;
	uint8_t percent;
} curve[] = {
	{4200, 100}, {4060, 90}, {3980, 80}, {3920, 70}, {3870, 60}, {3820, 50},
	{3790, 40},  {3770, 30}, {3740, 20}, {3680, 10}, {3450, 5},  {3000, 0},
};

static uint8_t millivolts_to_percent(int32_t mv)
{
	if (mv >= curve[0].millivolts) {
		return curve[0].percent;
	}

	for (size_t i = 1; i < ARRAY_SIZE(curve); i++) {
		if (mv >= curve[i].millivolts) {
			int32_t span_mv = curve[i - 1].millivolts - curve[i].millivolts;
			int32_t span_percent = curve[i - 1].percent - curve[i].percent;

			return curve[i].percent +
			       (mv - curve[i].millivolts) * span_percent / span_mv;
		}
	}

	return 0;
}

int battery_init(void)
{
	if (!adc_is_ready_dt(&adc)) {
		return -ENODEV;
	}

	return adc_channel_setup_dt(&adc);
}

int battery_read(uint8_t *percent, int32_t *millivolts)
{
	int16_t raw = 0;
	struct adc_sequence sequence = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int ret = adc_sequence_init_dt(&adc, &sequence);

	if (ret < 0) {
		return ret;
	}

	ret = adc_read_dt(&adc, &sequence);
	if (ret < 0) {
		return ret;
	}

	int32_t mv = MAX(raw, 0);

	ret = adc_raw_to_millivolts_dt(&adc, &mv);
	if (ret < 0) {
		return ret;
	}

	mv = mv * DIVIDER_TOTAL_KOHM / DIVIDER_BOTTOM_KOHM;

	*millivolts = mv;
	*percent = millivolts_to_percent(mv);

	return 0;
}

/*
 * Battery gauge: the percentage shown to the wearer and the app.
 *
 * The voltage under the band's load moves with what the band is doing (the
 * WS2812, the sensor LEDs, the radio), and the curve above is steep where a
 * LiPo spends most of its time: around 3.85 V, 5 mV is 1%. Shown directly, the
 * percentage jumps by several points and back. So the gauge works like a
 * phone's:
 *
 * - main.c only takes readings with the motor off and settled (the motor's
 *   current alone moved it by 6% or more).
 * - A median of the last GAUGE_MEDIAN_LEN readings removes single odd ones,
 *   and a slow average (GAUGE_EMA_WEIGHT per reading, about a minute) the
 *   noise; the curve turns that into a target percentage.
 * - The shown value moves towards the target 1% at a time, at most once per
 *   GAUGE_STEP_READINGS readings, and only after the target has been on that
 *   side the whole time. Down needs the target below it for that long.
 * - Up only while charging, 1% at most once per GAUGE_RISE_STEP_READINGS. There
 *   is no charger status line to the nRF52, so charging is a target at least
 *   GAUGE_RISE_MARGIN above the shown value for GAUGE_RISE_READINGS readings in
 *   a row; it ends when the target falls below the shown value (unplugged).
 *   Otherwise the shown value never goes up, so a reading that recovers after a
 *   busy stretch cannot bounce it.
 *
 * Readings are 10 s apart (main.c), so: down at most 1% a minute, up at most 1%
 * every 100 s (0-100% in about 2.8 h, close to the MCP73831's ~200 mA charge of
 * a 500 mAh cell; under charge the voltage reads high, so the target is ahead
 * of the real charge and this keeps the display from racing it), and charging
 * is recognised after 2 minutes. A watchdog reset keeps the shown value; a
 * power-on starts from the measured voltage.
 */
#define GAUGE_MEDIAN_LEN         5
#define GAUGE_EMA_WEIGHT         0.2f
#define GAUGE_STEP_READINGS      6
#define GAUGE_RISE_STEP_READINGS 10
#define GAUGE_RISE_MARGIN        5
#define GAUGE_RISE_READINGS      12

static struct {
	int32_t recent_mv[GAUGE_MEDIAN_LEN];
	int count;
	int next;
	float filtered_mv;
	bool have_shown;
	uint8_t shown;
	bool charging;
	/* Readings in a row with the target below / above the shown value. */
	int below;
	int above;
	/* Readings in a row with the target GAUGE_RISE_MARGIN or more above it. */
	int rise_seen;
} gauge;

/* The shown value, kept through a reset that keeps RAM, with its inverse as a check. */
struct kept_gauge {
	uint32_t magic;
	uint32_t percent;
	uint32_t check;
};

#define KEPT_MAGIC 0xba770001U

static __noinit struct kept_gauge kept;

static void keep_shown(void)
{
	kept.magic = 0U;
	kept.percent = gauge.shown;
	kept.check = ~(uint32_t)gauge.shown;
	kept.magic = KEPT_MAGIC;
}

bool battery_gauge_init(bool try_restore)
{
	if (try_restore && kept.magic == KEPT_MAGIC && kept.check == ~kept.percent &&
	    kept.percent <= 100U) {
		gauge.shown = (uint8_t)kept.percent;
		gauge.have_shown = true;
	}
	return gauge.have_shown;
}

static int32_t recent_median(void)
{
	int32_t sorted[GAUGE_MEDIAN_LEN];

	for (int i = 0; i < gauge.count; i++) {
		int32_t x = gauge.recent_mv[i];
		int j = i;

		while (j > 0 && sorted[j - 1] > x) {
			sorted[j] = sorted[j - 1];
			j--;
		}
		sorted[j] = x;
	}

	return sorted[gauge.count / 2];
}

uint8_t battery_gauge_update(int32_t millivolts)
{
	gauge.recent_mv[gauge.next] = millivolts;
	gauge.next = (gauge.next + 1) % GAUGE_MEDIAN_LEN;
	if (gauge.count < GAUGE_MEDIAN_LEN) {
		gauge.count++;
	}

	int32_t median_mv = recent_median();

	if (gauge.count == 1) {
		gauge.filtered_mv = (float)median_mv;
	} else {
		gauge.filtered_mv += GAUGE_EMA_WEIGHT * ((float)median_mv - gauge.filtered_mv);
	}

	uint8_t target = millivolts_to_percent((int32_t)(gauge.filtered_mv + 0.5f));

	if (!gauge.have_shown) {
		gauge.shown = target;
		gauge.have_shown = true;
	} else if (target < gauge.shown) {
		gauge.charging = false;
		gauge.above = 0;
		gauge.rise_seen = 0;
		if (++gauge.below >= GAUGE_STEP_READINGS) {
			gauge.shown--;
			gauge.below = 0;
		}
	} else if (target > gauge.shown) {
		gauge.below = 0;
		if (!gauge.charging) {
			gauge.rise_seen =
				target >= gauge.shown + GAUGE_RISE_MARGIN ? gauge.rise_seen + 1 : 0;
			if (gauge.rise_seen >= GAUGE_RISE_READINGS) {
				gauge.charging = true;
				gauge.above = 0;
			}
		}
		if (gauge.charging && ++gauge.above >= GAUGE_RISE_STEP_READINGS) {
			gauge.shown++;
			gauge.above = 0;
		}
	} else {
		gauge.below = 0;
		gauge.above = 0;
		if (!gauge.charging) {
			gauge.rise_seen = 0;
		}
	}

	keep_shown();
	return gauge.shown;
}

int32_t battery_gauge_filtered_mv(void)
{
	return (int32_t)(gauge.filtered_mv + 0.5f);
}

bool battery_gauge_charging(void)
{
	return gauge.charging;
}
