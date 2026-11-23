import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import '../models/hydration_mode.dart';
import '../models/session_record.dart';

// Representative values for the live-reading demo. Real values stream in
// over BLE from the band's PPG sensor once that layer exists.
const List<int> kMockHrSequence = [78, 82, 91, 87, 95, 84, 90, 101];

// HRV (ms), indexed alongside kMockHrSequence: it trends down as HR rises,
// same demo cadence, same eventual swap for a real BLE reading.
const List<int> kMockHrvSequence = [62, 58, 50, 54, 46, 57, 51, 44];

/// Owns the live Night session: start time, elapsed ticking, mock HR
/// readings, and the reminder countdown, independent of any screen's
/// lifecycle. Navigating away from Active Session used to cancel its
/// `Timer` on `dispose()`, silently killing the session despite the back
/// button's own label claiming it "keeps running." Now the session lives
/// here, one level up, the same way a real session tracker keeps counting
/// while you background the app.
class SessionController extends ChangeNotifier {
  DateTime? _startedAt;
  Timer? _ticker;

  int elapsedSeconds = 0;
  int remindersFired = 0;
  int intervalMinutes = 40;

  int _hrIndex = 0;
  int _hrTickCounter = 0;
  int _hrSum = kMockHrSequence.first;
  int _hrSamples = 1;
  int _hrvSum = kMockHrvSequence.first;
  int _hrvSamples = 1;

  // Demo-seeded countdown so the full "ring fills, reminder fires, resets"
  // loop is observable within a short test session instead of the real
  // configured interval (20-90 minutes depending on mode). Swap for the
  // band's actual next-reminder timestamp once BLE telemetry exists.
  static const int demoReminderSeconds = 90;
  int secondsUntilReminder = demoReminderSeconds;

  bool get isActive => _startedAt != null;
  DateTime? get startedAt => _startedAt;
  int get hr => kMockHrSequence[_hrIndex];
  int get avgHr => (_hrSum / _hrSamples).round();
  int get hrv => kMockHrvSequence[_hrIndex];
  int get avgHrv => (_hrvSum / _hrvSamples).round();

  // SpO2 and motion context don't need per-tick simulation fidelity the way
  // HR/HRV do (they're the two values a wrist PPG naturally streams
  // continuously); a steady reading is representative until real BLE
  // telemetry lands.
  int get spo2 => 97;
  String get motion => 'Active';

  void startSession({required int intervalMinutes}) {
    if (isActive) return; // already running: resuming, not restarting
    _startedAt = DateTime.now();
    this.intervalMinutes = intervalMinutes;
    elapsedSeconds = 0;
    remindersFired = 0;
    _hrIndex = 0;
    _hrTickCounter = 0;
    _hrSum = kMockHrSequence.first;
    _hrSamples = 1;
    _hrvSum = kMockHrvSequence.first;
    _hrvSamples = 1;
    secondsUntilReminder = demoReminderSeconds;
    _ticker = Timer.periodic(const Duration(seconds: 1), (_) => _onTick());
    notifyListeners();
  }

  void _onTick() {
    elapsedSeconds++;
    _hrTickCounter++;
    if (_hrTickCounter >= 4) {
      _hrTickCounter = 0;
      _hrIndex = (_hrIndex + 1) % kMockHrSequence.length;
      _hrSum += hr;
      _hrSamples++;
      _hrvSum += hrv;
      _hrvSamples++;
    }
    secondsUntilReminder--;
    if (secondsUntilReminder <= 0) {
      remindersFired++;
      HapticFeedback.mediumImpact();
      secondsUntilReminder = demoReminderSeconds;
    }
    notifyListeners();
  }

  /// Stops the ticker and returns the finished record for the caller to
  /// hand to `SessionStore`. This controller doesn't know about the store
  /// itself, callers compose the two rather than one controller reaching
  /// into another.
  SessionRecord endSession() {
    assert(isActive, 'endSession called with no active session');
    final record = SessionRecord(
      id: _startedAt!.microsecondsSinceEpoch.toString(),
      mode: HydrationMode.night,
      startedAt: _startedAt!,
      endedAt: DateTime.now(),
      avgHr: avgHr,
      avgHrv: avgHrv,
      avgSpo2: spo2,
      motion: motion,
      reminderCount: remindersFired,
    );
    _ticker?.cancel();
    _ticker = null;
    _startedAt = null;
    notifyListeners();
    return record;
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }
}
