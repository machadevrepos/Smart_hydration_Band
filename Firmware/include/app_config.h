#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* Water reminder intervals in seconds: Daily 90 min, Night 35 min. */
#define APP_DAILY_REMINDER_S 5400
#define APP_NIGHT_REMINDER_S 2100

/* Reminder: two pulses with a gap. Button press: one buzz. All in ms. */
#define APP_HAPTIC_TAP_MS     200
#define APP_HAPTIC_TAP_GAP_MS 200
#define APP_HAPTIC_BUZZ_MS    300

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
 * Low battery: below APP_BATTERY_OFF_MV for APP_BATTERY_OFF_READINGS readings in
 * a row (10 s apart, so a motor dip never counts) the band switches itself off
 * to protect the LiPo and avoid brown-out resets. 3.3 V is under 5% left.
 * Readings below APP_BATTERY_VALID_MV are taken as a measurement fault, not a
 * flat battery, so a broken divider cannot keep switching the band off.
 * Check the measured voltage against a multimeter before relying on this.
 */
#define APP_BATTERY_OFF_MV       3300
#define APP_BATTERY_OFF_READINGS 6
#define APP_BATTERY_VALID_MV     2500

/* Status LED level per color, 0-255. Current and brightness rise with it. */
#define APP_LED_LEVEL 40

#endif /* APP_CONFIG_H */
