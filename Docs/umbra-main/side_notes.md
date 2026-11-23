# Umbra, Side Notes

Working reference distilled from the client brief and feasibility report in
`Project AI-Powered Smart Hydration & Wellness Wristband (Brad)/`. Read this
alongside `CLAUDE.md` (design system) before building screens, this file
covers *what the product is and does*; `CLAUDE.md` covers *how it should
look and feel*. Don't duplicate design rules here; don't invent product
features here either, if it's not in the source docs or confirmed by the
user, it's an open question below, not a default.

## What Umbra is

A passive behavior-change wristband that reminds people to drink water.
Core loop: **band buzzes → user drinks water → that's it.** No logging, no
tapping, no setup. Pitched as "a Whoop for nightlife", not preachy,
not anti-drinking, just a smart nudge so you feel better the next morning.

**Non-negotiable:** the band works completely standalone, with zero app
pairing or setup out of the box. The app *enhances* the experience, live
data, history, adjusting intervals, but the haptic reminder itself must
never depend on the app being open or even installed.

Client: Brad. Contractor for hardware/firmware/feasibility: M. Bilal Javid
(the two PDFs in the project folder, one is the client's own spec email,
the other is the hardware contractor's feasibility report for the physical
band/PCB/enclosure, not for the app we're building here).

## Two modes

| Mode | Default interval | Adjustable options |
|---|---|---|
| Night Mode (active while drinking) | **40 min** | 20 / 30 / 45 / 60 min |
| Daily Mode (passive all-day hydration) | **90 min** | 20 / 30 / 45 / 60 min |

Note: the client's original doc said Night default 40 min with 20/30/40/60
options; a later "few key reminders" follow-up changed this to 35 min
default with 20/30/45/60 options; then Brad's build-107 TestFlight review
(2026-09-07) asked for the default back to 40 min for the beta, keeping
the 20/30/45/60 options from the second message. Treat this as the current
authoritative combination — 40 min default, 20/30/45/60 options — unless
a later message supersedes it again.

Haptic pattern: **double tap, two short, firm pulses**, strong enough to
feel clearly in a loud bar or festival environment. This is a firmware/
hardware detail, but it matters for any in-app haptic preview or reminder
simulation the app might offer.

## Sensors, v1 scope

HR (essential), HRV (derived from HR, no new sensor), full PPG waveform
(firmware only), respiration rate (derived from HR), skin temperature,
SpO2, accelerometer (movement context, steps, sleep detection, logged
passively, **not featured prominently in v1 UI**).

**GSR is explicitly dropped for v1**, client called it out twice ("too
noisy, will add in v2" / "prob don't need GSR"). The hardware contractor's
feasibility report still includes a full GSR subsystem (sensor, PCB
section, electrodes, example BLE payload with a `gsr` field), that's the
contractor's own scope-of-work boilerplate and conflicts with the client's
direct instruction. **Follow the client, not the feasibility report, on
this:** don't design any v1 app screen around GSR/sweat data.

## App, "ruthlessly simple" (client's own words)

- Mode selector, Daily or Night
- Interval adjustment, 20 / 30 / 45 / 60 minutes
- Live HR display, with HR zone (resting / elevated / high) shown via
  color coding, but per `CLAUDE.md`, color alone can't be the only signal,
  pair it with a label/icon too
- Session tracking, start/end a night out (or a daily-mode session)
- Session history, past sessions with biometric averages
- Single morning check-in notification at 9am, "How do you feel 1,10?",
  correlated against the previous session's data. **Just one notification.**
  Don't add more prompts than this; the client was specific about "single."
- Battery level, BLE connection status
- Steps and sleep logged passively in background, data exists, stays out
  of the primary UI in v1
- Insights, over time, surface what patterns precede the best/worst
  mornings (this is the one screen that leans slightly more "analytical,"
  keep it in the same restrained visual language, not a dashboard)
- Apple Watch companion: mirrors the active session, shows HR zone, fires
  haptic reminders on-wrist, syncs session data back to the phone

App aesthetic per the client, verbatim: **"dark background, premium feel,
clean and minimal. Think Oura app meets nightlife. Not medical looking."**
This is exactly what `CLAUDE.md`'s design system already encodes (dark-first,
one gold accent, generous negative space), no translation needed, the
constitution already fits the brief.

## Explicitly out of scope (client said so directly)

- Custom PCB (breadboard/module prototype is fine for v1)
- Injection-molded enclosure (3D printed is fine)
- App Store submission
- Backend/server infrastructure
- GSR (see above)

## BLE / firmware context (not what this codebase builds, but shapes the app's data layer)

- MCU: ESP32 or equivalent, BLE-capable
- Band has an **onboard fallback**: if BLE drops, it keeps firing reminders
  on the default interval independently, the app's BLE layer should be
  built assuming the band doesn't need it to function, only to enhance
 , reconnection handling should be graceful, never blocking core reminder
  behavior
- Example telemetry shape from the feasibility report (drop the `gsr` key
  per the v1 scope decision above):
  ```json
  { "device_id": "HYDRO-WR-001", "heart_rate": 92, "spo2": 97, "battery": 78, "session_active": true }
  ```
- Example command shape: `{ "command": "trigger_haptic", "pattern": "hydration_reminder", "duration": 3 }`

## Open questions / things to flag rather than assume

- **Platform mismatch:** the client's brief explicitly asks for a native
  **SwiftUI iOS app + WatchOS companion**, built from scratch in Xcode, with
  a documented Swift BLE manager class. This repository is a **Flutter**
  project. If that's a deliberate change of direction (e.g. cross-platform
  now matters more than matching the brief literally), fine, but don't
  silently assume it's resolved. The BLE integration approach is decided
  for the Flutter side now (see "BLE integration, decided" below), but the
  "Apple Watch companion" deliverable specifically still hinges on this,
  a Watch app is native-only regardless of what the phone app is built in.
- **Dubai/bilingual scope vs. the brief:** `CLAUDE.md` frames Umbra as a
  Dubai launch with mandatory Arabic/English bilingual support from day one.
  Brad's brief never mentions localization and describes delivery to New
  York, plausibly the NY prototype is for the client demo/investor round
  and Dubai is the actual go-to-market, but that's inference, not stated
  fact. Don't let the nightlife/alcohol framing in the brief clash with
  UAE cultural norms without checking, worth a direct question if it
  becomes relevant to copy or feature framing.
- Interval option sets differ slightly across the two client messages (see
  Modes table above), resolved in favor of the later message, but flagging
  since the source docs disagree.
- No data model, state management choice, or folder structure is decided
  yet, `CLAUDE.md` says to document that decision here once made. Nothing
  to record yet; `lib/` is still the default `flutter create` scaffold.

## BLE integration, decided

`flutter_reactive_ble` (BSD, free for commercial use), not
`flutter_blue_plus`: the latter changed its license to require a paid
commercial tier for any for-profit use, including development and
testing, which applies to Umbra. See `lib/core/state/band_controller.dart`
and `lib/core/ble/`.

- `BandController` owns the real link end to end: scan, connect,
  reconnect-with-backoff on an unexpected drop, and persisting the last
  paired device (`shared_preferences`, via `band_storage.dart`) so the app
  reconnects on its own the next time it's opened, without the user
  re-pairing.
- No real firmware exists yet, so `band_ble_constants.dart` defaults to
  the Nordic UART Service UUIDs (the standard choice for an ESP32
  prototype streaming JSON text over BLE notify) and scans by advertised
  name prefix rather than service UUID. Both are isolated to that one
  file, swapping in the real firmware's UUIDs later is a one-file change.
- Telemetry is parsed against the feasibility report's example JSON shape
  (`device_id`, `heart_rate`, `spo2`, `battery`, `session_active`), with
  `gsr` never read, per the v1 scope decision above.
- Every BLE/permission entry point is wrapped so a platform error changes
  state instead of throwing: Bluetooth off, permission denied, the band
  itself powering off mid-session, or no BLE plugin available at all (as
  in a widget test) all become a visible app state, never a crash. This
  matches the standalone-band requirement above: losing the link can't
  take the app down or block the band's own reminders.

### Device status, available app-wide

`BandController` is registered once, at the `MultiProvider` root in
`main.dart`, the same tier as `SessionController`/`SessionStore`/
`SettingsController`. Any screen or future controller reads it the same
way, `context.watch<BandController>()` to rebuild on change or
`context.read<BandController>()` for a one-off action, no per-screen
wiring needed. This is the one source of truth for hardware/connection
state, nothing else in the app should track a second copy of it.

Public contract other features can rely on:

- `state` (`BandLinkState`): `idle` (never paired, or user forgot the
  band) · `bluetoothUnavailable` · `permissionDenied` · `scanning` ·
  `connecting` · `connected` · `reconnecting` (was connected, dropped,
  retrying with backoff, the band itself keeps buzzing regardless).
- `connected` (bool): shorthand for `state == BandLinkState.connected`.
- `battery`, `restingHr` (int): most recent readings. Hold their last
  known value after a disconnect, they don't reset to 0, check
  `lastSeenAt`/`readingsAreStale` before treating them as current.
- `deviceName` (String): the paired band's advertised name, falls back to
  a generic "Umbra band" label.
- `lastSeenAt` (DateTime?): when a reading last actually arrived, `null`
  if the app has never connected to a band this install.
- `readingsAreStale` (bool): true when not connected but a previous
  reading exists, the signal to show cached data as cached rather than
  live.
- `scanResults` (`List<BandScanMatch>`): live candidates during
  `startScan()`, pairing-flow use only.

`BandStatusPresentation.of(state)` (`lib/core/utils/`) is the single
place that turns a `BandLinkState` into a label/icon/color, Home's top
bar, the band status card, and Band Detail all call it rather than each
re-deciding what "reconnecting" looks like. Any new screen that shows
band status should call it too instead of adding a fourth switch
statement.

Not wired up yet, deliberately: `SessionController`'s live HR/HRV during
an active session are still the mock demo sequence, not real `heart_rate`
telemetry from `BandController`. `heart_rate` is already a confirmed
field and could be wired now; HRV isn't in the example payload at all
(side_notes above: "derived from HR, no new sensor") so whether it
arrives as a firmware-computed field or needs computing app-side from raw
samples is still open. Don't wire one without the other without checking,
that's a product decision (does a real session now show real HR, unblocked
by an unrelated open question) more than a technical one.

## Timeline (for context, not a hard deadline the app has to hit)

Client's stated project start: 3 June 2026, 3-week duration. Contractor's
feasibility report allocates 60 hours / 3 weeks to app development
specifically (BLE layer gets the largest single slice at 14 hours). Milestone
4 in the client's own doc is "prototype arrives in New York and functions
correctly", the hardware side has its own independent timeline; nothing here
implies this Flutter app is on the same 3-week clock unless the user says so.
