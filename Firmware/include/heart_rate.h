#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdbool.h>
#include <stdint.h>

struct hr_estimate {
	/* Heart rate in bpm, or 0 when there is no reliable reading. */
	int bpm;
	/* Periodicity of the last 8 s, 0-100. Below 50 is rejected. */
	int quality;
	/* True when bpm is the last good value, held through a short bad stretch. */
	bool held;
	/* True when the signal is still decaying after placement; no new reading. */
	bool settling;
};

/* Seconds of samples before the first estimate (window plus filter settling). */
#define HR_FIRST_ESTIMATE_S 11

/* Reset the estimator. Call after any gap in the sample stream. */
void heart_rate_init(void);

/*
 * Set the measured sensor sample rate. The MAX30101 runs from its own
 * oscillator, which can be a few percent off the nominal HR_SAMPLE_RATE_HZ;
 * this is used to convert periods to bpm.
 */
void heart_rate_set_sample_rate(float hz);

/*
 * Feed one raw IR sample.
 *
 * Returns true once per second, starting HR_FIRST_ESTIMATE_S seconds after
 * heart_rate_init(), with a new estimate in *out.
 */
bool heart_rate_process(int32_t sample, struct hr_estimate *out);

#endif /* HEART_RATE_H */
