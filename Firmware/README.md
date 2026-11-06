# Smart Hydration Band Firmware

Zephyr firmware for the Smart Hydration Band prototype (Umbra). This first
stage brings up the MAX30101 optical sensor and computes heart rate, and counts
steps with the BMI270.

## Hardware

| Signal | Connection |
| --- | --- |
| MCU | BMD-350-A-R (nRF52832 module) |
| MAX30101 | I2C address `0x57` |
| I2C SDA | P0.26 |
| I2C SCL | P0.27 |
| MAX30101 INT | not connected (polling only) |
| BMI270 | I2C address `0x68`, built-in step counter polled once a second |
| BMI270 CSB | P0.23, driven high (no pull-up on the PCB) |
| BMI270 INT1, INT2 | P0.30, P0.31 (not used yet) |
| Haptic motor | P0.24, gate of low-side switch Q1 (on = high) |
| Status LED | WS2812 D4, data on P0.25 |
| Push button | SW2, P0.14 to GND (internal pull-up) |

The board target is `nrf52dk/nrf52832` with `boards/nrf52dk_nrf52832.overlay`.
The MAX30101 driver in nRF Connect SDK v3.3.1 (Zephyr 4.3) takes its sensor
settings from devicetree, so they are in the overlay: IR only (multi-LED mode,
slot 1), 100 Hz, no averaging, 411 us pulse width (18-bit; the driver default is
69 us / 15-bit), IR LED current `0xC0` with a 62.5 pA/LSB ADC range. The
older Kconfig-based driver copied in `Docs/MAX30101` does not match this SDK.
A dedicated BMD-350 board definition can replace this later; the overlay keeps
the pin mapping explicit for now.

The BMD-350 has no 32.768 kHz crystal, so `prj.conf` selects the internal RC
low-frequency clock.

## Project layout

```text
Firmware/
├── boards/nrf52dk_nrf52832.overlay   PCB pin map and MAX30101 node
├── include/
│   ├── app_config.h                  reminder intervals, motor timing, tuning
│   ├── heart_rate.h                  PPG heart-rate estimator API
│   ├── step_counter.h                BMI270 step counter API
│   ├── status_led.h                  status LED API
│   ├── button.h                      push button API
│   ├── haptic.h                      motor pattern API
│   ├── band_mode.h                   Daily and Night modes, reminders
│   ├── ble_service.h                 BLE link to the app
│   ├── battery.h                     battery measurement API
│   ├── watchdog.h                    hardware watchdog API
│   └── max30101.h                    sensor wrapper API
├── src/
│   ├── main.c                        initialization and FIFO polling loop
│   ├── max30101.c                    Zephyr driver wrapper, FIFO pointer handling
│   ├── heart_rate.c                  band-pass + windowed autocorrelation HR
│   ├── step_counter.c                BMI270 firmware load and step counter
│   ├── status_led.c                  WS2812 colors
│   ├── button.c                      button polling and debounce
│   ├── haptic.c                      motor patterns in the background
│   ├── band_mode.c                   mode, LED color and reminder timing
│   ├── ble_service.c                 advertising, services, app commands
│   ├── battery.c                     battery voltage to percent
│   └── watchdog.c                    hardware watchdog, reset reason
└── prj.conf
```

`max30101.c` owns the Zephyr sensor API. The Zephyr driver's
`sensor_sample_fetch()` pops one frame from the FIFO without checking that a
new one is waiting, so the wrapper first reads the FIFO write/read pointers
and overflow counter, then fetches exactly the samples the sensor produced.
The estimator relies on evenly spaced samples (10 ms each), so polling jitter
does not affect the BPM; a FIFO overflow resets the estimator. `heart_rate.c` owns the signal
algorithm and has no sensor dependency, so it can be reused by a later SpO2 or
PPG path. `main.c` only wires them together.

## Build and flash

From an nRF Connect SDK terminal:

```bash
west build -b nrf52dk/nrf52832
west flash
```

In VS Code with the nRF Connect extension, use **Add build configuration**,
select board `nrf52dk/nrf52832`, then Build and Flash. The overlay in `boards/`
is applied automatically.

Open the Segger RTT viewer (or the nRF Connect extension's RTT terminal) to see
the output. Logs are on RTT channel 0.

Skin contact is detected from the raw IR level (`APP_CONTACT_IR_MIN` in
`include/app_config.h`). The first reading comes about 11 s after contact
(1 s contact debounce, 8 s window, 2 s filter settling). The monitor shows one
line per second while the sensor is on the skin:

```text
Smart Hydration Band, firmware 1.0.0

  time    heart rate    steps   status
  -----   ----------   ------   ----------------------------
  00:01   --                0   no skin contact
  00:04   --                0   measuring, ready in 11 s
  00:15    83 bpm           0   signal good (85)
  00:30    84 bpm           0   signal fair (72)
  00:41    84 bpm           0   signal unsure (44), holding last value
  00:47   --                0   no clear pulse, keep still (31)
  00:52   --               12   no skin contact
```

- `signal` is the periodicity of the last 8 s (0-100): good is 80 and above,
  fair 65-79, weak 50-64. The first reading needs 60 (a marginal first lock
  can be on half the rate); after that, below 50 the last value is held for up
  to 5 s, then the line shows `no clear pulse` with the score. `settling` means
  the signal level is still decaying after placement.
- Off the skin the status is printed once, then when the step count changes or
  every 10 s.
- Warnings and errors (FIFO overflow, I2C failures) still appear as normal
  Zephyr log lines.

## Step counter

`src/step_counter.c` uses the step counter built into the BMI270: it loads
Bosch's firmware, enables the counter and reads the 32-bit count once a second.
It talks to the chip over I2C directly with `CONFIG_BMI270=n`, because Zephyr's
driver loads the firmware in 256-byte writes that the nRF52832's I2C cannot
send, and has no step-counter API. The firmware blob comes from the Zephyr tree
(`drivers/sensor/bosch/bmi270/bmi270_config_file.h`). If the BMI270 fails to
start, an error is logged, steps show `--` and heart rate keeps running.

The count starts at 0 at power-up. Daily reset, wrist-specific tuning and use of
the interrupt lines are not implemented, and accuracy on the wrist is not yet
measured.

## Modes, LED, button and haptics

The band works with no app or pairing:

| Mode | LED | Reminder |
| --- | --- | --- |
| Daily, at power-up | White | Double tap every 90 min |
| Night | Green | Double tap every 35 min |

The reminders, LED, button and BLE do not depend on the sensors. If the
MAX30101 does not start at power-up, the error is logged, the status line shows
`heart-rate sensor unavailable`, no heart rate is sent, and everything else runs. If MAX30101 reads start failing
later (loose connection, I2C fault), heart rate pauses with the status
`heart-rate sensor not responding` and resumes, after contact is found again,
when reads recover; the main loop goes on serving the button and reminders
throughout. The first failure and the recovery are each logged once. If the
sensor keeps answering but makes no samples for 1 s (it reset itself, for example
on a supply dip, and lost its settings), the settings the driver wrote at
start-up, read back once then, are written again and sampling restarts; this
repeats every second until samples return, logged once.

A button press switches mode, gives one buzz and restarts the interval. Each
press and reminder prints a line over RTT. Intervals are the two values at the top of
`include/app_config.h`, in seconds, with the motor timings just below them.

Motor patterns run on the system work queue because the main loop must keep
draining the sensor FIFO. Not implemented yet: app control of mode and interval,
quiet hours (needs the time of day), and skipping reminders off the wrist.

The WS2812 uses Zephyr's `worldsemi,ws2812-spi` driver on SPIM2: MOSI on P0.25,
SCK on P0.22 (unconnected, but the SPIM stalls without a clock pin). `APP_LED_LEVEL` in `include/app_config.h` sets the brightness.
Each press prints a line over RTT with its time, number and result. `data sent`
means the SPI transfer finished, not that an LED received it. On P0.25 each color
bit lasts 2 us: a 0 is a 250 ns high pulse and a 1 is 750 ns. At `APP_LED_LEVEL` 40
each update is a 48 us burst, in green-red-blue order, each channel sending
`00101000` when on. This has not yet been confirmed on the board.

## Watchdog

The nRF52832 hardware watchdog resets the band if it is not fed for 10 s.
`main()` starts it first and feeds it on every loop pass (about every 40 ms),
but only while the system work queue, which runs the motor patterns and the
Bluetooth host, has also checked in within the last 5 s. A hang in either one,
or a fatal error that halts the CPU, therefore ends in a reset with reminders
running, instead of a silent band. The watchdog pauses while a debugger halts
the CPU.

A reset that keeps RAM (watchdog, fatal error, software reset, debugger) resumes
the same mode on the same reminder schedule: `band_mode.c` keeps the mode and
the time to the next reminder in no-init RAM, each with its inverse as a check,
and the start-up line says `(resumed after reset)`. Without this a Night session
would silently drop to Daily after a reset, and a band that kept resetting would
never reach a reminder. Power-on, brown-out and the power switch start fresh in
Daily Mode.

After every start the RTT output shows why the chip reset, for example
`last reset: power-on` or `last reset: watchdog`. Any `watchdog` in a soak test
means something hung and needs investigating.

## BLE

The band advertises as `Umbra`, with no pairing, and runs the same with no app
connected. It asks the phone for a 200-400 ms connection interval to save power;
the phone may choose differently.

| Service | Characteristic | UUID | Access | Value |
| --- | --- | --- | --- | --- |
| Heart Rate | Measurement | `0x2A37` | Notify | Standard: flags byte, then bpm. Sent with each reading |
| Battery | Battery Level | `0x2A19` | Read, notify | 0-100 %, on change |
| Device Information | Firmware Revision | `0x2A26` | Read | Firmware version string, for example `1.0.0` |
| Umbra `1ab10000-fde9-42a3-ab5b-82416f9bca6e` | Steps | `1ab10001-...` | Read, notify | 32-bit little endian, on change |
| | Mode | `1ab10002-...` | Read, notify | 1 byte: 0 Daily, 1 Night, on change |
| | Haptic | `1ab10003-...` | Write | 1 byte: 1 fires the double tap |

An app should subscribe, then read Battery, Steps and Mode once for their current
values. The Umbra characteristic UUIDs share the service UUID's other digits.

The battery percentage uses a typical LiPo curve and has not been calibrated.
Battery is measured and printed every 10 s, and a reading taken while the motor runs may
dip.

**Low battery.** After six readings in a row (one minute) below 3.3 V, under 5%
left, the band switches itself off: motor and LED off, MAX30101 and BMI270 shut
down, then nRF52 system off. A flat LiPo otherwise ends in brown-out resets and
erratic behavior. The LED going dark is the sign. Charge it, then press the
button (or cycle the power switch) to start again; if the battery is still
under 3.3 V it switches off again after a minute. Readings under 2.5 V are taken
as a measurement fault and ignored, so a broken divider cannot keep the band off.
The thresholds are in `include/app_config.h`. Because the battery reading is not
calibrated, compare the RTT millivolts with a multimeter before shipping.

When it's off this way, the WS2812 still draws its idle current (roughly 1 mA)
from the 3.3 V regulator, so the battery keeps draining slowly. For storage or
shipping, use the power switch (SW1), which disconnects the battery.

**Transition:** the current app reads only the Nordic UART Service, so that stays
until the app uses the services above. It sends a JSON line a second, only once
subscribed, and accepts text containing `trigger_haptic`:
`{"session_active":false,"heart_rate":84,"steps":120,"battery":78}`. Then remove
it from `src/ble_service.c` and drop `CONFIG_BT_NUS`.

`heart_rate` is in every line; `0` means there is no reading right now (off the
wrist, settling, sensor fault). The app must show that as "no reading" rather than
"0 bpm", and must not keep the last value. On the Heart Rate Service, no reading
means no notification, so an app using it should treat a few seconds without one
the same way.

Anyone nearby can connect, since the client's requirement is no pairing, so they
could also trigger a buzz.

## Heart-rate algorithm

No open-source MAX30101 algorithm is built for the wrist: Maxim's RF algorithm
(MAXREFDES117) and SparkFun's beat detector are tuned for IR on a fingertip,
and Maxim's wrist algorithm runs closed-source on the MAX32664 sensor hub. That
hub's documentation says its default wrist algorithm uses the green LED and
needs an accelerometer for motion. This firmware follows the same approach
without the motion part:

- **IR only.** Green is normally best on the wrist, but the MAX30101 green LED
  needs a 4.5-5.5 V LED supply and this board gives it 3.3 V (see Hardware
  caveats). A wrist recording showed a clear IR pulse and no usable green, and
  red is unused until SpO2 is added, so both are off to save power.
- **Band-pass 0.9-3 Hz**, decimate to 50 Hz. Chosen from a raw wrist recording
  (`Docs/Raw_logs.md`): the pulse is nearly a sine at 1.4 Hz with slow lines at
  0.2-0.7 Hz. Lower corners locked on half the rate; first-differencing, used
  earlier, boosted noise. The cost is a weaker signal below about 50 bpm.
- **Normalized autocorrelation of an 8 s window every second**, over 40-200
  bpm. The output rate is steady and does not depend on individual beats being
  found.
- **Measured sample rate.** The MAX30101 runs from its own oscillator; on the
  first board it ran about 5% fast (~105 Hz), which would read every heart rate
  5% low. `main.c` counts samples against the calibrated nRF clock over 10 s
  windows and passes the measured rate to the estimator.
- **Choosing the rate.** The shortest lag with a peak within 90% of the best
  one, or the peak closest to the last rate (within 20 bpm). A lower peak at a
  shorter lag is the second bump of each beat, not the rate, so the half lag
  is taken only if its peak reaches 80% of the chosen one. A different rate must
  persist for 5 s before it shows, or 10 s against a tracked peak, which also
  recovers from a wrong first lock. Then parabolic interpolation and the median
  of the last 3 estimates.
- **Artifact handling.** One-second segments over 2x the window's median level
  are zeroed (motion bursts), and a window whose oldest 2 s are over 2.5x its
  newest 2 s is rejected (the board settling after placement).

Synthetic check (pulse with dicrotic notch, beat-to-beat interval jitter and
amplitude variation, noise, 0.1 Hz and 0.27 Hz baseline drift): worst average
error under 2 bpm from 45 to 190 bpm, including drift 60x the pulse amplitude.
When the pulse is too weak it reports `--` rather than a wrong value. A 70 to
140 bpm ramp lags by about 5 bpm because of the window. This is not a wrist
accuracy result; validate against a chest strap or ECG. Arm movement is not
handled yet; that needs the BMI270.

## Tuning

- `APP_POLL_INTERVAL_MS` in `include/app_config.h` is how often the FIFO is
  drained (40 ms, about 4 samples). Keep it well under 320 ms, the time the
  32-sample FIFO takes to fill at 100 Hz.
- The sample rate is fixed at 100 Hz. `src/max30101.c` fails the build if the
  overlay's `acq-mode`, `led-slot`, `smp-sr`, `smp-ave` or `led-pw` change,
  because the heart-rate timing depends on them; update `MAX30101_SAMPLE_RATE_HZ` and `HR_SAMPLE_RATE_HZ`
  together if you change it.
- The IR LED current is the second byte of `led-pa` in the overlay, 0.2 mA per
  LSB. The raw level must stay below the 18-bit limit of 262143.
- Estimator constants (window, quality threshold, tracking range, hold time)
  are at the top of `src/heart_rate.c`.

## First board results

At rest, wrist (IR channel, led-pa 0x60, adc-rge 8192): locked 19 s after start, then 82-87
bpm continuously with quality 76-89 (IR dc ~172000, ac 40-70 counts). Finger:
74-82 bpm once settled, lower quality, one half-rate reading (46 bpm) during
settling that the first-lock threshold now rejects. Sensor sample rate measured
105.3-105.4 Hz. No reference device was recorded, so these show stability, not
accuracy.

A later session with the same settings gave no reading on the wrist in the
10 s measured, and 66-71 bpm on a finger with a jump to 138 (the rule above).
The wrist pulse is small (IR ac 40-70 counts on dc ~172000) and depends on
placement. IR current was then doubled, with no visible gain.

Replaying a 22 s raw wrist recording through the estimator: the previous
filter gave 2 live readings out of 14, the current one 14 out of 14, at 80-86
bpm around a 1.4 Hz (84 bpm) spectral peak. This is one short recording with
no reference device, used to choose the filter, so it is not an accuracy
result. Firmware with this filter has not yet run on the board.

## Hardware caveats

**Green LED supply.** The MAX30101 datasheet specifies VLED+ 3.1-5.0 V for the
red and IR LEDs but 4.5-5.5 V for the green LED. This board ties VLED+ to
+3.3 V, so the green LED cannot reach its programmed current: on the first
board green read ~16000 counts on a fingertip and ~6000 on the wrist with no
usable pulse, at the same setting that gives strong red/IR. Wrist products use
green, so the next PCB revision should feed VLED+ (pins 9-10) from a 5 V boost
converter (sized for LED pulses up to 50 mA, with local bulk capacitance).
Until then heart rate comes from IR.

**Sensor VDD: delivery blocker until checked on the unit.**

The MAX30101 has a 1.8 V analog supply (`VDD`, pin 11; 1.7-2.0 V, absolute
maximum 2.2 V) and a separate 3.1-5.0 V LED supply (`VLED+`, pins 9-10). The
KiCad schematic in `PCB/kicad_project` ties pin 11 to `+3.3V` together with pins
9-10, and its only regulator is the AP2112K-3.3: there is no 1.8 V rail. A
sensor run at 3.3 V on VDD is 1.1 V over its absolute maximum. It may work on the
bench and then drift or fail in use, and firmware cannot detect or correct it.

Before a unit ships:

1. Power the band from its battery, sensor fitted.
2. Measure pin 11 of the MAX30101 (or its VDD bypass capacitor) to GND.
3. 1.7-2.0 V: the unit is reworked and fine. Around 3.3 V: rework it first with
   a 1.8 V LDO (for example the AP2112K-1.8 or TLV70018, input from +3.3 V)
   feeding pin 11 only, with a 1 uF bypass capacitor at the pin, and VLED+ left
   on +3.3 V. Then repeat the check. A sensor that has already run at 3.3 V
   should be replaced.
4. Update the schematic and PCB so later units do not need the rework.

The firmware keeps the reminders running if the sensor fails (see Modes), but
heart rate is lost.

## Scope

Implemented: heart rate from IR at 1 Hz, BMI270 step counting, Daily and Night
modes with the LED, button and haptic reminders, BLE services and haptic command,
battery level, RTT output.

Not implemented yet: SpO2, skin temperature, app control of mode and interval, flash logging, motion-artifact
handling for heart rate, power management.
