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
 * Measured ~160000-195000 on skin and ~1500-3000 off it at led-pa 0x60.
 * Scale it with the IR LED current if that changes.
 */
#define APP_CONTACT_IR_MIN 50000

/* Set to 1 to stream raw PPG samples as CSV: sample_index,red,ir,green. */
#define APP_RAW_STREAM 0

#endif /* APP_CONFIG_H */
