# MAX30101 heart-rate improvement — proposed design

## Goal
Obtain trustworthy wrist pulse-rate estimates and evidence for accuracy on the existing nRF52832 and MAX30101 hardware. Begin with reliable data capture; do not claim an accuracy improvement until compared against a reference on real recordings.

## Approaches
1. **Improve acquisition and compare detectors (recommended).** Keep the existing Zephyr initialization, add FIFO availability and overflow handling appropriate to the installed driver, process every actual sample with sample-derived timing, and replay captured green and IR signals through the existing detector and a suitable reference. This preserves a baseline and makes algorithm choice measurable.
2. **Replace the detector immediately.** Port a public reference algorithm, while still needing to fix capture, tune for this optical layout, and validate on the wrist. Faster to get a different algorithm, but its superiority is unproven and it may depend on different sampling and LED settings.
3. **Motion-aware tracking now.** Integrate the BMI270 and a motion-compensated estimator. Potentially useful for exercise, but it adds synchronization and tuning before basic optical signal quality is understood.

## Proposed behavior
- Poll FIFO often enough, determine new-sample availability using MAX30101 pointers and overflow information, drain available frames in order, and detect/reset after lost or uncertain samples. Handle pointer wrap/full ambiguity according to the datasheet. Avoid competing reads or writes with the Zephyr driver's configuration and fetch state.
- Assign each frame timing based on configured effective sample rate and sequence; never treat arrival time or identical ADC values as proof that a frame is new or duplicated. Record gaps and reset detector state when timing is uncertain.
- Preserve red, IR and green acquisition for diagnostic comparison. Consider green as the initial wrist candidate, but select it only after viewing real waveforms; expose raw data without implying serial print times are sensor timestamps.
- Make BPM limits and refractory timing consistent with the intended measurement range. Reset stale estimates after interruptions or loss of reliable contact. Do not report invalid readings as valid; use conservative quality criteria that can be tuned against recordings, without claiming they detect all motion artifacts.
- Compare the existing detector with a documented reference using the same samples and effective rate. Retain the simpler approach unless measurements justify replacing it; preserve license and sampling assumptions for any imported code.
- Do not alter SpO2, BLE, or motion tracking in this stage. Do not flash or power the custom PCB for sensor testing until VDD and interface levels are verified safe.

## Validation
- Automated tests on actual production logic: FIFO pointer wrap, multiple samples, duplicate values, overflow/gap handling, sample timing, accepted pulse intervals, no-contact behavior, and detector reset. Synthetic tests demonstrate behavior, not clinical or wrist accuracy.
- Build with the installed nRF Connect SDK. Record raw samples and diagnostics from safe hardware, compare green and IR at rest and during intended movement against a synchronized ECG or validated chest strap, and report error, availability and false readings. Establish target BPM range, conditions, and acceptable error before choosing a final detector.
- CPU, RAM, and energy optimization require representative measurements; an x86 host benchmark cannot establish nRF52832 power consumption. No performance gain is assumed.

## Open validation inputs
- Confirm the physical test board, the MAX30101 VDD correction, and available reference heart-rate device.
- Define intended resting/exercise range, measurement conditions, and acceptance criteria. Until then, any threshold or accuracy target remains provisional.
