#ifndef MAX30101_H
#define MAX30101_H

#include <stdbool.h>
#include <stdint.h>

/* Effective sample rate set by smp-sr and smp-ave in the devicetree. */
#define MAX30101_SAMPLE_RATE_HZ 100

/* The MAX30101 FIFO holds 32 samples. */
#define MAX30101_FIFO_DEPTH 32

/*
 * Initialize the MAX30101 sensor.
 *
 * Returns 0 on success, negative errno on failure.
 */
int max30101_init(void);

/* One FIFO sample: both LED slots, taken in the same 10 ms period. */
struct max30101_sample {
	int32_t ir;
	/* Ambient-cancelled level only; the red LED is off unless measuring SpO2. */
	int32_t red;
};

/*
 * Read every new sample from the sensor FIFO, oldest first.
 *
 * Only samples the sensor has actually produced are returned, so consecutive
 * samples are exactly 1/MAX30101_SAMPLE_RATE_HZ apart unless *lost is
 * non-zero. *lost is set when the FIFO overflowed since the previous call;
 * samples were dropped and the caller should treat the stream as broken.
 *
 * Returns the number of samples stored (0..max), or negative errno.
 */
int max30101_read_fifo(struct max30101_sample *samples, int max, bool *lost);

/*
 * Set the red LED current, 0.2 mA per step (0 = off). Only the SpO2
 * measurement lights it; heart rate uses IR.
 *
 * Returns 0 on success, negative errno on failure.
 */
int max30101_set_red_current(uint8_t pa);

/*
 * Write back the settings the driver configured at start-up, empty the FIFO and
 * restart sampling. For a sensor that reset on its own and stopped sampling.
 *
 * Returns 0 on success, -ENODATA if the settings could not be saved at start-up,
 * or another negative errno.
 */
int max30101_restore(void);

/*
 * Put the sensor in its shutdown mode before the band powers off. Works over
 * I2C even if the Zephyr driver did not start.
 *
 * Returns 0 on success, negative errno on failure.
 */
int max30101_shutdown(void);

#endif /* MAX30101_H */
