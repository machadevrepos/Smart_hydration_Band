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
 * SpO2 spot checks (src/spo2.c), in seconds: the first about 2 min after
 * power-up, then every 30 min while worn (the client's sampling plan), and
 * 5 min after an attempt that failed (movement, weak signal, off the wrist).
 */
#define APP_SPO2_FIRST_S    (2 * 60)
#define APP_SPO2_INTERVAL_S (30 * 60)
#define APP_SPO2_RETRY_S    (5 * 60)
/* A scheduled check that finds the band off the wrist, or no pulse yet, looks again after this. */
#define APP_SPO2_WAIT_S     60

/*
 * SpO2 bench mode, for testing and calibration only: measure as soon as the
 * heart rate locks, then back to back with a 10 s gap, so data comes quickly.
 * The red LED is lit most of the time, so battery life drops. The build prints a
 * warning and the band prints "SpO2 BENCH MODE" at start-up while it is on.
 * Set it to 0 for the delivered firmware.
 */
#define APP_SPO2_BENCH_MODE 1

#if APP_SPO2_BENCH_MODE
#undef APP_SPO2_FIRST_S
#undef APP_SPO2_INTERVAL_S
#undef APP_SPO2_RETRY_S
#undef APP_SPO2_WAIT_S
#define APP_SPO2_FIRST_S    0
#define APP_SPO2_INTERVAL_S 10
#define APP_SPO2_RETRY_S    10
#define APP_SPO2_WAIT_S     2
#endif

/*
 * SpO2 = A * R^2 + B * R + C, R being the ratio of ratios.
 *
 * INTERIM CALIBRATION, NOT CHECKED AGAINST AN OXIMETER: SpO2 = 102.8 - 15 R.
 * No reference oximeter was available, so the line is set so that the R this
 * band reads at rest on a healthy wearer (0.32-0.52 across sessions, depending
 * on how the board sits) shows the normal 95-98%: 99% at R 0.25, 98% at 0.32,
 * 96% at 0.43, 95% at 0.52, 92% at 0.70, 88% at 1.0.
 *
 * Its slope is flatter than a real calibration (Maxim's is about 33% per unit
 * of R here), so a genuine fall in oxygen would show as a smaller fall. Treat
 * readings as a wellness estimate. Replace it with a fit against a fingertip
 * oximeter, in the enclosure, before relying on low readings; see "SpO2
 * calibration" in the README.
 *
 * Published curves tried first, both of which showed 100% at rest on this
 * band, since it reads R 0.32-0.45 in a good fit:
 *   Maxim MAX30101 (MAX32664 default): 1.5958422 R^2 - 34.659664 R + 112.68987
 *   Maxim RD117 (MAX30102), as in RF:  -45.060 R^2 + 30.354 R + 94.845
 */
#define APP_SPO2_CAL_A 0.0f
#define APP_SPO2_CAL_B (-15.0f)
#define APP_SPO2_CAL_C 102.8f

/* Status LED level per color, 0-255. Current and brightness rise with it. */
#define APP_LED_LEVEL 40

#endif /* APP_CONFIG_H */
