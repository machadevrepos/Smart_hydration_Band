#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

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
