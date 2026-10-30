#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdbool.h>
#include <stdint.h>

/* Channels fed to heart_rate_process(), in this order. */
enum hr_channel {
	HR_CHANNEL_RED,
	HR_CHANNEL_IR,
	HR_CHANNEL_GREEN,
	HR_NUM_CHANNELS,
};

struct hr_estimate {
	/* Heart rate in bpm, or 0 when there is no reliable reading. */
	int bpm;
	/* Periodicity of the last 8 s on the chosen channel, 0-100. Below 50 is rejected. */
	int quality;
	/* True when bpm is the last good value, held through a short bad stretch. */
	bool held;
	/* Channel with the clearest rhythm, used for this estimate. */
	enum hr_channel channel;
};

/* Per-channel signal levels for diagnostics and LED-current tuning. */
struct hr_channel_stats {
	/* Raw level (ADC counts, averaged over ~1 s). */
	int32_t dc;
	/* RMS of the 0.5-4 Hz band-passed signal (ADC counts): the pulse size. */
	int32_t ac;
	/* Periodicity of the last 8 s, 0-100. */
	int quality;
};

/* Reset the estimator. Call after any gap in the sample stream. */
void heart_rate_init(void);

/*
 * Set the measured sensor sample rate. The MAX30101 runs from its own
 * oscillator, which can be a few percent off the nominal HR_SAMPLE_RATE_HZ;
 * this is used to convert periods to bpm.
 */
void heart_rate_set_sample_rate(float hz);

/*
 * Feed one raw PPG sample per channel, indexed by enum hr_channel.
 *
 * Returns true once per second, after the first 10 s, with a new estimate in
 * *out.
 */
bool heart_rate_process(const int32_t samples[HR_NUM_CHANNELS], struct hr_estimate *out);

/* Latest diagnostics for one channel; quality is updated once per second. */
void heart_rate_get_stats(enum hr_channel channel, struct hr_channel_stats *stats);

#endif /* HEART_RATE_H */
