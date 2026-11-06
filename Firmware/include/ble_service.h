#ifndef BLE_SERVICE_H
#define BLE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * BLE link to the phone app. No pairing: the app finds the band by its advertised
 * name. The band works the same with or without a connection. The services and
 * their formats are documented in the README.
 */

struct ble_telemetry {
	/* Heart rate in bpm, or 0 when there is no reading. */
	int heart_rate;
	bool steps_valid;
	uint32_t steps;
	/* True during a Night session. */
	bool night_mode;
	bool battery_valid;
	uint8_t battery_percent;
	/* Latest SpO2 in percent while it is recent, 0 when there is none. */
	uint8_t spo2;
};

/*
 * Start Bluetooth and advertising.
 *
 * Returns 0 on success, negative errno on failure.
 */
int ble_service_init(void);

/*
 * Update the values and notify a connected app of what changed. Returns at once:
 * the sending happens on the BLE thread, because it can block on the radio and
 * the caller must keep draining the sensor FIFO. Does nothing if
 * ble_service_init() failed.
 */
void ble_service_publish(const struct ble_telemetry *telemetry);

#endif /* BLE_SERVICE_H */
