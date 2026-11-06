#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * Default water reminder intervals in minutes: Daily 90, Night 35. The app can
 * change them (1-240 min); the values it sets are kept in flash and replace
 * these until it sets 0, which restores the default.
 */
#define APP_DAILY_REMINDER_MIN 90
#define APP_NIGHT_REMINDER_MIN 35

/*
 * Default quiet hours, local time in minutes after midnight: no Daily Mode
 * reminders from 23:00 to 07:00. Night Mode reminders always fire, since a
 * night out runs through these hours. Needs the time from the app; until the
 * app has set it after power-on, there are no quiet hours. The app can change
 * them; start equal to end turns them off.
 */
#define APP_QUIET_START_MIN (23 * 60)
#define APP_QUIET_END_MIN   (7 * 60)

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

/*
 * SpO2 spot checks (src/spo2.c): the first about 2 min after power-up, then
 * every 30 min while worn (the client's sampling plan), and 5 min after an
 * attempt that failed (movement, weak signal, off the wrist).
 */
#define APP_SPO2_FIRST_MIN    2
#define APP_SPO2_INTERVAL_MIN 30
#define APP_SPO2_RETRY_MIN    5

/*
 * SpO2 = A - B * R, R being the ratio of ratios. 110 - 25 R is the textbook
 * starting point; it is NOT calibrated for this board and enclosure. Fit A and
 * B from R (printed over RTT) against a reference fingertip oximeter; see
 * "SpO2 calibration" in the README.
 */
#define APP_SPO2_CAL_A 110.0f
#define APP_SPO2_CAL_B 25.0f

/* Status LED level per color, 0-255. Current and brightness rise with it. */
#define APP_LED_LEVEL 40

#endif /* APP_CONFIG_H */
