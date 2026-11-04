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
| Haptic motor | P0.24 (not used yet) |
| Status LED | WS2812 D4, data on P0.25 |
| Push button | SW2, P0.14 to GND (internal pull-up) |

The board target is `nrf52dk/nrf52832` with `boards/nrf52dk_nrf52832.overlay`.
The MAX30101 driver in nRF Connect SDK v3.3.1 (Zephyr 4.3) takes its sensor
settings from devicetree, so they are in the overlay: multi-LED mode with
red/IR/green in slots 1-3, 100 Hz, no averaging, 411 us pulse width (18-bit;
the driver default is 69 us / 15-bit), LED current `[60 C0 60]` with a 62.5
pA/LSB ADC range. The
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
│   ├── app_config.h                  sample rate, raw stream, detector tuning
│   ├── heart_rate.h                  PPG heart-rate estimator API
│   ├── step_counter.h                BMI270 step counter API
│   ├── status_led.h                  status LED API
│   ├── button.h                      push button API
│   └── max30101.h                    sensor wrapper API
├── src/
│   ├── main.c                        initialization and FIFO polling loop
│   ├── max30101.c                    Zephyr driver wrapper, FIFO pointer handling
│   ├── heart_rate.c                  band-pass + windowed autocorrelation HR
│   ├── step_counter.c                BMI270 firmware load and step counter
│   ├── status_led.c                  WS2812 colors
│   └── button.c                      button polling and debounce
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
Smart Hydration Band - heart rate and steps
For heart rate, place the sensor on the wrist and keep still.

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

For tuning, set `APP_PPG_DIAGNOSTICS` to `1` in `include/app_config.h` to add a
line every 5 s:

```text
  diag    105.4 Hz | red dc 84197 ac 10 q 29 | ir dc 172660 ac 57 q 89 | green dc 4803 ac 37 q 14
```

- The rate is the measured sensor sample rate (0.0 for the first 10 s).
- `dc` is the raw level, `ac` the RMS pulse size after the band-pass, both in
  ADC counts, and `q` the channel's periodicity. The estimate uses the channel
  with the highest `q`. Off the skin IR `dc` is a few thousand; above ~240000
  the LED current is too high (18-bit limit 262143). A larger `ac` at the same
  `dc` means a stronger pulse.

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

## Status LED and button

Bring-up behavior: the LED is white at power-up and each button press swaps it
between white and green. Client modes (white for Daily, green for Night, and a
buzz on each press) are not implemented yet.

The WS2812 uses Zephyr's `worldsemi,ws2812-spi` driver on SPIM2: MOSI on P0.25,
SCK on P0.22 (unconnected, but the SPIM stalls without a clock pin). `APP_LED_LEVEL` in `include/app_config.h` sets the brightness.
Each press prints a line over RTT with its time, number and result. `data sent`
means the SPI transfer finished, not that an LED received it. On P0.25 each color
bit lasts 2 us: a 0 is a 250 ns high pulse and a 1 is 750 ns. At `APP_LED_LEVEL` 40
each update is a 48 us burst, in green-red-blue order, each channel sending
`00101000` when on. This has not yet been confirmed on the board.

## Heart-rate algorithm

No open-source MAX30101 algorithm is built for the wrist: Maxim's RF algorithm
(MAXREFDES117) and SparkFun's beat detector are tuned for IR on a fingertip,
and Maxim's wrist algorithm runs closed-source on the MAX32664 sensor hub. That
hub's documentation says its default wrist algorithm uses the green LED and
needs an accelerometer for motion. This firmware follows the same approach
without the motion part:

- **Best of three channels.** Each second the estimator runs on red, IR and
  green and uses the channel with the clearest rhythm. Green is normally best
  on the wrist, but the MAX30101 green LED needs a 4.5-5.5 V LED supply and this
  board gives it 3.3 V (see Hardware caveat). With green unusable the estimator
  falls back to IR or red.
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
- `APP_PPG_DIAGNOSTICS` set to `1` adds the per-channel diagnostics line
  described above.
- `APP_RAW_STREAM` set to `1` prints `sample_index,red,ir,green` over RTT for
  signal inspection (sample index x 10 ms = sensor time). Use it to check
  LED-current headroom and waveform amplitude.
- LED currents are `led-pa` in the overlay, `[red IR green]` as hex bytes,
  0.2 mA per LSB (`[60 60 60]` = ~19 mA each). If quality stays low at rest,
  raise the green current (third byte) while `green` stays below ~240000; the
  18-bit limit is 262143. VLED is 3.3 V, close to the green LED's needs, so
  very high green currents may not be reached.
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
Until then heart rate comes from IR or red.

**Sensor VDD.**

The MAX30101 has a 1.8 V analog supply (`VDD`, pin 11; 1.7-2.0 V, absolute
maximum 2.2 V) and a separate 3.1-5.0 V LED supply (`VLED+`, pins 9-10). The
KiCad netlist in `PCB/kicad_project` ties pin 11 to `+3.3V` together with pins
9-10. That exceeds the VDD absolute maximum. Before powering the board with the
sensor fitted, verify the netlist and, if needed, add a 1.8 V regulator for VDD
with a 1 uF bypass capacitor.

## Scope

Implemented: MAX30101 initialization, FIFO-pointer polling, sample-rate
measurement, 1 Hz heart-rate estimation on the best of red/IR/green, RTT
diagnostics, BMI270 step counting (not yet measured on the board).

Not implemented yet: SpO2, BLE, haptic reminders, motion-artifact handling for
heart rate, power management. Those come after the heart-rate path is verified on the PCB.
