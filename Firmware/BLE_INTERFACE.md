# Umbra band: BLE interface for the app

This is the contract between the band's firmware (version 1.1.0 and later) and
the phone app. It lists every Bluetooth service and characteristic the band
exposes, the exact byte layout of each value, what the band does when the app
writes one, and the order the app should do things in on each connection.

All multi-byte numbers are **little endian**. "Minutes after midnight" is
local time. There is no pairing or bonding: any phone can connect, and the
band accepts one connection at a time.

The band works fully on its own. Everything here is optional for the band, and
nothing the app does (or fails to do) stops the water reminders.

## 1. Finding the band

| What | Value |
| --- | --- |
| Advertised name | `Umbra` (match the prefix `umbra`, ignoring case) |
| Advertised 16-bit services | Heart Rate `0x180D`, Battery `0x180F` |
| Scan response | Umbra service UUID `1ab10000-fde9-42a3-ab5b-82416f9bca6e` |
| Pairing | None. Connect directly. |
| Connections | One. While a phone is connected the band does not advertise. |

Every band currently advertises the same name. Remember the band's device ID
after the first connection to reconnect to the same one.

## 2. Services and characteristics

The Umbra characteristic UUIDs are the service UUID with the last digit of the
first group changed: `1ab1000N-fde9-42a3-ab5b-82416f9bca6e`.

| # | Characteristic | UUID | Properties | Size |
| --- | --- | --- | --- | --- |
| | **Heart Rate service** `0x180D` | | | |
| 1 | Heart Rate Measurement | `0x2A37` | Notify | 2 bytes |
| | **Battery service** `0x180F` | | | |
| 2 | Battery Level | `0x2A19` | Read, Notify | 1 byte |
| | **Device Information service** `0x180A` | | | |
| 3 | Firmware Revision String | `0x2A26` | Read | text |
| | **Umbra service** `1ab10000-…` | | | |
| 4 | Steps | `1ab10001-…` | Read, Notify | 4 bytes |
| 5 | Mode | `1ab10002-…` | Read, Write, Notify | 1 byte |
| 6 | Haptic | `1ab10003-…` | Write, Write without response | 1 byte |
| 7 | Intervals | `1ab10004-…` | Read, Write | 4 bytes |
| 8 | Time | `1ab10005-…` | Read, Write | 6 bytes |
| 9 | Quiet hours | `1ab10006-…` | Read, Write | 4 bytes |
| 10 | Log control | `1ab10007-…` | Read, Write | read 12, write 4 bytes |
| 11 | Log data | `1ab10008-…` | Notify | 4 or a multiple of 20 bytes |
| 12 | SpO2 | `1ab10009-…` | Read, Write, Notify | read 10, write 1 byte |
| | **Nordic UART service** `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | | | |
| 13 | RX (app to band) | `6e400002-…` | Write | text |
| 14 | TX (band to app) | `6e400003-…` | Notify | text lines |

A write with the wrong length is rejected with ATT error `0x0D` (Invalid
Attribute Value Length). A write with a value out of range is rejected with
`0x13` (Value Not Allowed); the band then changes nothing.

### 2.1 Heart Rate Measurement (`0x2A37`)

The standard Bluetooth Heart Rate format: byte 0 is flags, byte 1 is the heart
rate in bpm. Sent once a second **only while there is a reading**. When the band
is off the wrist, still settling, or the sensor has a fault, notifications stop.
**Treat about 3 seconds without one as "no reading"** and show that, not the last
value.

### 2.2 Battery Level (`0x2A19`)

0-100 %. Notified when it changes, measured every 10 s. It is an estimate from the
battery voltage. Below about 5% (3.3 V for a minute) the band switches itself off;
the LED going dark is the sign.

### 2.3 Firmware Revision String (`0x2A26`)

The firmware version as text, for example `1.0.0`. Show it on a band or about
screen; it identifies which build a band is running.

### 2.4 Steps (`1ab10001`)

uint32: steps counted **since the band was last switched on**. It starts from 0 at
every power-on, so do not use it as a daily total. Notified when it changes. For
step history, use the log (section 3).

### 2.5 Mode (`1ab10002`)

| Value | Mode | LED | Default reminder |
| --- | --- | --- | --- |
| `0` | Daily | White | Every 90 min |
| `1` | Night | Green | Every 35 min |

- **Read** gives the current mode. **Notify** fires when it changes, whether from
  the band's button or from the app.
- **Write** `0` or `1` to switch. The band applies it within about 40 ms, exactly
  like a button press: the LED changes, the band buzzes once, and the reminder
  interval starts over. Writing the mode the band is already in does nothing.
- The band always powers up in Daily Mode. The mode is not kept through power-off.

The user can also switch modes with the button on the band at any time. **Always
update the app's UI from the Mode notification**, not from what the app last wrote.

### 2.6 Haptic (`1ab10003`)

Write `1` to make the band give its reminder double tap now (for example a "test
buzz" button or the demo). Any other value is rejected.

A second command within 2 seconds of the last one is ignored (the write still
succeeds). This stops anyone from buzzing the band nonstop.

### 2.7 Intervals (`1ab10004`)

| Bytes | Type | Meaning |
| --- | --- | --- |
| 0-1 | uint16 | Daily Mode reminder interval, minutes |
| 2-3 | uint16 | Night Mode reminder interval, minutes |

- Accepted values: **1 to 240**. The app's picker should offer 20, 30, 45 and 60.
  **`0` means "use the default"** (Daily 90, Night 35).
- **Read** gives the intervals in effect.
- **Write** both at once. To change only one, read first, change one field, write
  both back. The band saves them in flash, so they survive power-off.
- If the interval of the **current** mode changes, the wait to the next reminder
  starts over from now with the new interval.

Examples:

| Intent | Bytes written |
| --- | --- |
| Daily 60, Night 30 | `3C 00 1E 00` |
| Daily 90, Night 35 (defaults) | `00 00 00 00` or `5A 00 23 00` |
| Change Night to 45, keep Daily at 90 | `5A 00 2D 00` |

### 2.8 Time (`1ab10005`)

| Bytes | Type | Meaning |
| --- | --- | --- |
| 0-3 | uint32 | Current time, seconds since 1970-01-01 UTC (Unix time) |
| 4-5 | int16 | Local offset from UTC in minutes, daylight saving included |

The band has no clock that survives power-off. It needs the time for quiet hours
and to timestamp the log. **Write it on every connection**, first thing, and again
when the phone's time zone changes.

- Times before 2024-01-01 and offsets outside -720 to +840 are rejected.
- **Read** gives the band's current time and offset; UTC reads `0` if it has not
  been set since power-on.
- The band's clock drifts up to about 45 s a day, so writing it on each connection
  keeps it close.

Examples, for 2026-11-06 12:00:00 UTC (`1793966400` = `0x6AEDC140`):

| Place | Offset | Bytes written |
| --- | --- | --- |
| Dubai (UTC+4) | +240 | `40 C1 ED 6A F0 00` |
| New York, winter (UTC-5) | -300 | `40 C1 ED 6A D4 FE` |

### 2.9 Quiet hours (`1ab10006`)

| Bytes | Type | Meaning |
| --- | --- | --- |
| 0-1 | uint16 | Start, minutes after local midnight, 0-1439 |
| 2-3 | uint16 | End, minutes after local midnight, 0-1439 |

During quiet hours, **Daily Mode** reminders are skipped. Night Mode reminders
always fire, because a night out runs through these hours. The window may cross
midnight. **Start equal to end turns quiet hours off.** The default is 23:00 to
07:00 (`64 05 A4 01`). Kept in flash.

Quiet hours only work once the band knows the time (section 2.8). After a
power-on, until the app connects, the band buzzes through the night.

### 2.10 Log control (`1ab10007`) and Log data (`1ab10008`)

See section 3 for how to use them.

**Read Log control** (12 bytes):

| Bytes | Type | Meaning |
| --- | --- | --- |
| 0-3 | uint32 | Oldest sequence number still stored |
| 4-7 | uint32 | Next sequence number (records are oldest up to next - 1; equal means empty) |
| 8-9 | uint16 | Capacity in records (1024) |
| 10 | uint8 | Record length in bytes (20) |
| 11 | uint8 | Format version (2) |

**Write Log control** (4 bytes): uint32, the first sequence number to send. `0`
means "from the oldest".

**Log data notifications** come in two kinds, told apart by length:

- **A multiple of 20 bytes:** one or more records, in sequence order. Use the
  record length read from Log control rather than a fixed 20.
- **Exactly 4 bytes:** the end marker. A uint32: the sequence number to ask for
  next time. No more records follow.

**Record layout** (20 bytes, format version 2):

| Bytes | Type | Field | Meaning |
| --- | --- | --- | --- |
| 0-3 | uint32 | seq | Record number. Starts at 1, only grows, never reused. |
| 4-7 | uint32 | time | Start of the period. If flag bit 0 is set: Unix time UTC. If not: seconds since the band booted (boot number `boot`). |
| 8-9 | uint16 | steps | Steps counted in the period. Valid only if flag bit 2 is set. |
| 10-11 | uint16 | boot | Which power-on (boot) of the band made this record. |
| 12 | uint8 | hr_avg | Mean heart rate in bpm over the period's readings. `0` = no reading in the period. |
| 13 | uint8 | hr_max | Highest heart rate in bpm. `0` = no reading. |
| 14 | uint8 | duration | Length of the period in tens of seconds (`30` = 5 min, `6` = 1 min). |
| 15 | uint8 | flags | Bit 0: time is UTC. Bit 1: Night Mode. Bit 2: steps valid. Others: 0. |
| 16 | uint8 | spo2 | SpO2 in % measured during the period. `0` = no reading in the period. |
| 17 | uint8 | spo2_quality | Quality of that SpO2 reading, 0-100. |
| 18-19 | uint16 | reserved | 0. |

Example: `D2 04 00 00 40 C1 ED 6A 38 01 07 00 52 61 1E 05 61 55 00 00` is record
1234, starting 2026-11-06 12:00:00 UTC, 312 steps, boot 7, mean 82 bpm, peak 97
bpm, 5 minutes, Daily Mode, time UTC, steps valid, SpO2 97% with quality 85.

Format version 1 (firmware 1.0.0) had 16-byte records without the SpO2 bytes.

### 2.11 SpO2 (`1ab10009`)

SpO2 is measured in **spot checks**, the way wrist devices do it: 30-55 seconds
with the wrist still, every 30 minutes while the band is worn, 5 minutes after an
attempt that failed, and whenever the app asks. It is a wellness estimate, not a
medical reading.

**Read / Notify** (10 bytes):

| Bytes | Type | Meaning |
| --- | --- | --- |
| 0 | uint8 | Status of the latest attempt (table below) |
| 1 | uint8 | Last valid SpO2 in %, `0` if there has been none since power-on |
| 2 | uint8 | Quality of that reading, 0-100 |
| 3 | uint8 | Reserved, 0 |
| 4-7 | uint32 | When that reading was taken, Unix time UTC; `0` if the time was not set |
| 8-9 | uint16 | Latest attempt's ratio R x 1000 (for calibration; the app can ignore it) |

| Status | Meaning |
| --- | --- |
| `0` | No measurement yet |
| `1` | OK: byte 1 is a fresh reading |
| `2` | Measuring now (30-55 s) |
| `3` | Not on the skin |
| `4` | Too much movement, or the band shifting on the skin: ask the user to keep still and check the fit |
| `5` | Pulse too weak |
| `6` | Unstable signal |
| `7` | Sensor unavailable |

A failed attempt keeps the last valid reading in bytes 1-7, so the app can still
show it with its age. Notified whenever any byte changes, including when a
measurement starts (status 2) and when it ends.

**Write** `1` to measure now. Use this for a "measure SpO2" button: tell the user to
keep still, show "measuring" while the status is 2, then the result or the reason
it failed. A request while a measurement is running is ignored.

### 2.12 Nordic UART (legacy)

Kept for the current app; it can be dropped once the app uses the services above.

- **TX** sends one JSON line per second once the app subscribes, ending in `\n`.
  A line can arrive split over several notifications; join them up to the `\n`.
  Example: `{"session_active":false,"heart_rate":84,"steps":120,"battery":78,"spo2":97}`.
  - `session_active`: `true` in Night Mode.
  - `heart_rate`: always present. **`0` means no reading right now. Show "no
    reading", never "0 bpm", and do not keep showing the old value.**
  - `spo2`: always present. The last SpO2 reading while it is recent (up to 35
    minutes old), `0` when there is none. Show `0` as "no reading".
  - `steps` and `battery` are left out if the band cannot measure them.
- **RX** accepts the text `trigger_haptic`, which works like writing `1` to Haptic.

## 3. What the app should do on each connection

1. **Connect** to the band (scan for the name `Umbra`). Let the phone negotiate
   the largest MTU; iOS does this by itself (185 bytes or more). A bigger MTU
   makes the log transfer about 12 times faster.
2. **Discover** the services above.
3. **Write Time** (section 2.8).
4. **Subscribe** to Heart Rate Measurement, Battery Level, Steps, Mode, SpO2 and
   Log data.
5. **Read** Mode, Intervals, Quiet hours, Battery Level, Steps, SpO2 and Firmware Revision
   once, for their current values. After that the notifications keep them current.
6. **Sync the log:**
   1. Take the "next sequence number" the app saved after the last sync (0 the
      first time).
   2. Write it to Log control.
   3. Store every record received on Log data. Use `seq` as the unique key, so a
      record received twice is not stored twice.
   4. When the 4-byte end marker arrives, save its value as the next sequence
      number for next time. Save it only after the records are stored.
   5. If the connection drops before the end marker arrives, nothing is lost: the
      next sync starts from the saved number again.
7. **Live:** show heart rate from its notifications (section 2.1), battery, steps
   mode and the last SpO2 with its time. The mode selector writes Mode, the
   interval picker writes Intervals, the "buzz" button writes Haptic, and a
   "measure SpO2" button writes `1` to SpO2.

When the app's saved number is older than the oldest record still stored (the
band was away long enough to wrap its 1024-record ring), the transfer simply
starts at the oldest record. A jump in `seq` tells the app that records in
between were overwritten.

During a log transfer the band asks for a 15-30 ms connection interval, then
200-400 ms again to save power. The phone may decline; the transfer still works,
only more slowly. 1024 records take a few seconds at a large MTU.

## 4. Reading the log

- **One record per period:** 5 minutes in Daily Mode, 1 minute in Night Mode. A mode
  change ends the period early, so a short record can appear (see `duration`).
- **Daily steps:** add up `steps` (when flag bit 2 is set) over the records of the
  day, using each record's local date.
- **Heart rate over time:** plot `hr_avg` (and `hr_max`) per record, skipping zeros.
- **SpO2 over time:** records with a non-zero `spo2`, about one every 30 minutes
  while the band is worn.
- **Night sessions:** consecutive records with flag bit 1 set.
- **Records without UTC time (flag bit 0 clear):** these were made after a power-on
  while no phone had set the time, and the band was switched off again before a
  phone connected. Their `time` is seconds since that boot. The app can drop them,
  or place them before the first UTC record of a later boot if it wants to estimate.
  Records from the boot that is still running are always sent with UTC once the
  time is set.
- **`boot` changes** mean the band was switched off and on (or reset) in between.

How much the band keeps: 1024 records, about 3.5 days of Daily Mode or 17 hours of
Night Mode, kept through power-off and battery-empty.

## 5. Code examples (Dart)

Encoding the writes with `dart:typed_data`:

```dart
import 'dart:typed_data';

Uint8List encodeTime(DateTime now) {
  final b = ByteData(6);
  b.setUint32(0, now.toUtc().millisecondsSinceEpoch ~/ 1000, Endian.little);
  b.setInt16(4, now.timeZoneOffset.inMinutes, Endian.little);
  return b.buffer.asUint8List();
}

Uint8List encodeIntervals(int dailyMin, int nightMin) {
  final b = ByteData(4);
  b.setUint16(0, dailyMin, Endian.little); // 0 = default (90)
  b.setUint16(2, nightMin, Endian.little); // 0 = default (35)
  return b.buffer.asUint8List();
}

Uint8List encodeQuietHours(int startMin, int endMin) {
  final b = ByteData(4);
  b.setUint16(0, startMin, Endian.little); // 23:00 = 1380
  b.setUint16(2, endMin, Endian.little); //   07:00 = 420
  return b.buffer.asUint8List();
}

Uint8List encodeLogRequest(int fromSeq) {
  final b = ByteData(4);
  b.setUint32(0, fromSeq, Endian.little); // 0 = from the oldest
  return b.buffer.asUint8List();
}

final modeDaily = Uint8List.fromList([0]);
final modeNight = Uint8List.fromList([1]);
final buzz = Uint8List.fromList([1]);
final measureSpo2 = Uint8List.fromList([1]);
```

Decoding Log data notifications:

```dart
class LogRecord {
  LogRecord(this.seq, this.time, this.steps, this.boot, this.hrAvg, this.hrMax,
      this.durationS, this.flags, this.spo2, this.spo2Quality);
  final int seq, time, steps, boot, hrAvg, hrMax, durationS, flags, spo2, spo2Quality;
  bool get timeIsUtc => flags & 0x01 != 0;
  bool get night => flags & 0x02 != 0;
  bool get stepsValid => flags & 0x04 != 0;
}

/// [recordLen] is byte 10 of Log control (20 in format version 2).
/// Returns the records, or null with [endNextSeq] set for the end marker.
List<LogRecord>? decodeLogData(
    List<int> value, int recordLen, void Function(int) endNextSeq) {
  final b = ByteData.sublistView(Uint8List.fromList(value));
  if (value.length == 4) {
    endNextSeq(b.getUint32(0, Endian.little));
    return null;
  }
  final records = <LogRecord>[];
  for (var o = 0; o + recordLen <= value.length; o += recordLen) {
    records.add(LogRecord(
      b.getUint32(o, Endian.little),
      b.getUint32(o + 4, Endian.little),
      b.getUint16(o + 8, Endian.little),
      b.getUint16(o + 10, Endian.little),
      b.getUint8(o + 12),
      b.getUint8(o + 13),
      b.getUint8(o + 14) * 10,
      b.getUint8(o + 15),
      recordLen >= 20 ? b.getUint8(o + 16) : 0,
      recordLen >= 20 ? b.getUint8(o + 17) : 0,
    ));
  }
  return records;
}
```

## 6. Changes needed in the current app

The current app (`umbra-main`) reads only the Nordic UART JSON line and writes
nothing to the band. To use this interface:

1. **Heart rate:** show `heart_rate: 0` (or no Heart Rate notification for about
   3 s) as "no reading", not "0 bpm" and not the old value.
2. **Time:** write Time on every connection (section 2.8).
3. **Mode selector:** write Mode; update the UI from the Mode notification, since
   the button on the band also changes it.
4. **Interval picker:** write Intervals; read it on connect to show the band's
   values (they are kept on the band, so another phone may have changed them).
5. **History:** sync the log on every connection (section 3, step 6) and keep the
   records in the app's own storage.
6. **Buzz / demo button:** write `1` to Haptic (or send `trigger_haptic` on UART).
7. **SpO2:** show the last reading and its time from the SpO2 characteristic, a
   "measure now" button (section 2.11), and SpO2 in the history (record byte 16).
8. **Optional:** a quiet hours setting (section 2.9), and the firmware version on
   a band screen (section 2.3).

## 7. Behavior summary

| Situation | What the band does |
| --- | --- |
| No app ever connected | Daily Mode, white LED, double tap every 90 min. Button switches to Night (green, every 35 min) and back. No quiet hours (no time). Log keeps recording. |
| App connected | The same, plus live data and app control. |
| Worn | SpO2 spot check every 30 min (30-55 s, keep still); the red LED is lit only then. |
| App disconnects | Keeps going on its own, keeps logging, advertises again. |
| Band reset by its watchdog | Comes back within ~10 s in the same mode, same reminder schedule, same time. |
| Battery under ~5% | Saves the log, switches off (LED dark). Charge, then press the button. |
| Band switched off and on | Daily Mode, default schedule. Intervals and quiet hours kept; time lost until the app connects; steps start from 0; log kept. |
