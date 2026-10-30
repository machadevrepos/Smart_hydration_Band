#ifndef MAX30101_H
#define MAX30101_H

#include <stdbool.h>
#include <stdint.h>

/* Effective sample rate set by CONFIG_MAX30101_SR / CONFIG_MAX30101_SMP_AVE. */
#define MAX30101_SAMPLE_RATE_HZ 100

/* The MAX30101 FIFO holds 32 samples. */
#define MAX30101_FIFO_DEPTH 32

struct max30101_sample {
	int32_t red;
	int32_t ir;
	int32_t green;
};

/*
 * Initialize the MAX30101 sensor.
 *
 * Returns 0 on success, negative errno on failure.
 */
int max30101_init(void);

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

#endif /* MAX30101_H */
