#ifndef SPO2_H
#define SPO2_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "max30101.h"

/*
 * SpO2 spot checks from the MAX30101 red and IR channels, after the
 * open-source RF algorithm for the MAX3010x with changes for a loosely worn
 * wrist band. A measurement takes 30-55 s with the wrist still: the red LED is
 * turned on and its current adjusted, then 4 s windows are measured until six
 * are good (at most twelve). Windows with movement, a weak pulse or red and IR
 * pulses that do not match are thrown out; the good ones are pooled into one
 * ratio of ratios
 *
 *   R = (red AC / red DC) / (IR AC / IR DC)
 *
 * which the calibration in app_config.h (interim, not oximeter-checked) turns
 * into SpO2. With too few good windows no value is reported. Each window prints
 * a diagnostic line over RTT.
 *
 * Measurements run every APP_SPO2_INTERVAL_S seconds while the band is worn,
 * sooner after a failed attempt, and on request from the app.
 */

enum spo2_status {
	SPO2_STATUS_NONE = 0,       /* No measurement yet. */
	SPO2_STATUS_OK = 1,         /* percent is valid. */
	SPO2_STATUS_MEASURING = 2,  /* A measurement is running. */
	SPO2_STATUS_NO_CONTACT = 3, /* Not on the skin. */
	SPO2_STATUS_MOTION = 4,     /* Too much movement. */
	SPO2_STATUS_WEAK = 5,       /* Pulse too weak in red or IR. */
	SPO2_STATUS_UNSTABLE = 6,   /* Red and IR disagree, or windows disagree. */
	SPO2_STATUS_SENSOR = 7,     /* Sensor unavailable or its data stream broke. */
};

/*
 * status and ratio_milli describe the latest attempt (or one running); percent,
 * quality and uptime_ms are the last valid reading, kept when a later attempt
 * fails, so the app can show it with its age.
 */
struct spo2_result {
	enum spo2_status status;
	/* Last valid SpO2 in percent, 0 if there has been none. */
	uint8_t percent;
	/* 0-100 for that reading: share of windows used times red/IR correlation. */
	uint8_t quality;
	/* Latest attempt's R times 1000, 0 if no window was good (calibration). */
	uint16_t ratio_milli;
	/* k_uptime_get() of the last valid reading, 0 if none. */
	int64_t uptime_ms;
};

/* What the main loop tells spo2_process() each pass. */
struct spo2_inputs {
	/* Skin contact from the IR level. */
	bool contact;
	/* A heart-rate reading is showing (there is a pulse to measure). */
	bool pulse;
	/*
	 * The heart-rate estimator's latest signal quality, 0-100 (its normalized
	 * autocorrelation peak: how clearly the IR signal repeats at the pulse
	 * rate), 0 when there is no estimate.
	 */
	int pulse_quality;
	/* Acceleration, valid only if accel_ok. */
	bool accel_ok;
	int16_t accel[3];
};

/* Events from spo2_process(). */
#define SPO2_EVENT_STARTED BIT(0)
#define SPO2_EVENT_DONE    BIT(1)

/* Set up. Without a working sensor, measurements report SPO2_STATUS_SENSOR. */
void spo2_init(bool sensor_ok);

/* Ask for a measurement now. Safe from any thread. */
void spo2_request(void);

/* True while a measurement is running and wants samples and acceleration. */
bool spo2_measuring(void);

/* Feed each sensor sample while measuring. Main loop only. */
void spo2_add_sample(const struct max30101_sample *sample);

/* The sample stream broke (FIFO overflow, read failure, sensor restore). */
void spo2_sensor_gap(void);

/* Call every main loop pass. Returns SPO2_EVENT_* flags. */
uint32_t spo2_process(const struct spo2_inputs *in);

/* The latest result, or the running state. Safe from any thread. */
void spo2_get_result(struct spo2_result *result);

/* A short word for a status, for logs. */
const char *spo2_status_name(enum spo2_status status);

#endif /* SPO2_H */
