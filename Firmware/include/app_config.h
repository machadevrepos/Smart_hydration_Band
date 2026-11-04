#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * How often main.c drains the MAX30101 FIFO. The FIFO holds 32 samples
 * (320 ms at 100 Hz), so this must stay well below that or samples are lost.
 */
#define APP_POLL_INTERVAL_MS 40

/* Sample rate seen by the heart-rate detector; main.c checks it against the sensor. */
#define HR_SAMPLE_RATE_HZ 100

/*
 * Raw IR level (ADC counts) above which the sensor is taken to be on the skin.
 * Measured ~160000-195000 on skin and ~1500-3000 off it at IR led-pa 0x60 with
 * adc-rge 8192. IR 0xC0 with adc-rge 16384 should read about the same (double
 * the light, half the gain). Scale it if the ratio of the two changes.
 */
#define APP_CONTACT_IR_MIN 50000

/*
 * Set to 1 to add a line every 5 s with the measured sample rate and, per
 * channel, the raw level, pulse size and periodicity. Use it when tuning LED
 * currents or debugging contact.
 */
#define APP_PPG_DIAGNOSTICS 0

/* Status LED level per color, 0-255. Current and brightness rise with it. */
#define APP_LED_LEVEL 40

/* Set to 1 to stream raw PPG samples as CSV: sample_index,red,ir,green. */
#define APP_RAW_STREAM 0

#endif /* APP_CONFIG_H */
