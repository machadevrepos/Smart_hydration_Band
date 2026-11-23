import 'package:flutter/foundation.dart';
import '../models/hydration_mode.dart';
import '../models/session_record.dart';

/// Shared session history: Home reads the most recent entry for its "Last
/// night" card, Active Session writes a new entry when a session ends, and
/// History/Insights (once built) read the full list. One store instead of
/// each screen re-deriving its own copy of "what happened."
///
/// In-memory only for now. Swaps for a local database (sqflite/drift) once
/// persistence matters, the read/write shape here shouldn't need to change.
class SessionStore extends ChangeNotifier {
  SessionStore() {
    _seedMockHistory();
  }

  final List<SessionRecord> _history = [];

  /// Newest first.
  List<SessionRecord> get history => List.unmodifiable(_history);

  SessionRecord? get mostRecent => _history.isEmpty ? null : _history.first;

  void addSession(SessionRecord record) {
    _history.insert(0, record);
    notifyListeners();
  }

  void attachCheckIn(String sessionId, int rating) {
    final index = _history.indexWhere((s) => s.id == sessionId);
    if (index == -1) return;
    _history[index] = _history[index].copyWith(checkInRating: rating);
    notifyListeners();
  }

  // Two weeks of representative nights so History reads as a real list and
  // Insights has enough rated sessions to show an actual pattern rather
  // than an empty state. `seed-1` (yesterday, unrated) is kept exactly as
  // it was: it's what the check-in flow tests exercise.
  void _seedMockHistory() {
    SessionRecord night({
      required String id,
      required int daysAgo,
      required int startHour,
      required int startMinute,
      required Duration duration,
      required int avgHr,
      required int avgHrv,
      required int avgSpo2,
      required int reminderCount,
      int? checkInRating,
    }) {
      final day = DateTime.now().subtract(Duration(days: daysAgo));
      final start = DateTime(day.year, day.month, day.day, startHour, startMinute);
      return SessionRecord(
        id: id,
        mode: HydrationMode.night,
        startedAt: start,
        endedAt: start.add(duration),
        avgHr: avgHr,
        avgHrv: avgHrv,
        avgSpo2: avgSpo2,
        // Every seeded night here is Night Mode, out and about, so a single
        // fixed label is honest rather than a per-call param for a value
        // that never actually varies among these seeds.
        motion: 'Active',
        reminderCount: reminderCount,
        checkInRating: checkInRating,
      );
    }

    _history.addAll([
      night(id: 'seed-1', daysAgo: 1, startHour: 23, startMinute: 42, duration: const Duration(hours: 2, minutes: 33), avgHr: 88, avgHrv: 52, avgSpo2: 97, reminderCount: 3),
      night(id: 'seed-2', daysAgo: 2, startHour: 22, startMinute: 50, duration: const Duration(hours: 1, minutes: 45), avgHr: 84, avgHrv: 56, avgSpo2: 98, reminderCount: 3, checkInRating: 8),
      night(id: 'seed-3', daysAgo: 3, startHour: 23, startMinute: 10, duration: const Duration(hours: 2, minutes: 40), avgHr: 95, avgHrv: 44, avgSpo2: 96, reminderCount: 2, checkInRating: 5),
      night(id: 'seed-4', daysAgo: 4, startHour: 22, startMinute: 30, duration: const Duration(hours: 1, minutes: 50), avgHr: 82, avgHrv: 59, avgSpo2: 98, reminderCount: 4, checkInRating: 8),
      night(id: 'seed-5', daysAgo: 5, startHour: 23, startMinute: 0, duration: const Duration(hours: 2, minutes: 15), avgHr: 90, avgHrv: 49, avgSpo2: 97, reminderCount: 3, checkInRating: 6),
      night(id: 'seed-6', daysAgo: 6, startHour: 22, startMinute: 40, duration: const Duration(hours: 1, minutes: 20), avgHr: 79, avgHrv: 61, avgSpo2: 98, reminderCount: 2, checkInRating: 8),
      night(id: 'seed-7', daysAgo: 7, startHour: 23, startMinute: 20, duration: const Duration(hours: 3, minutes: 10), avgHr: 97, avgHrv: 42, avgSpo2: 95, reminderCount: 4, checkInRating: 4),
      night(id: 'seed-8', daysAgo: 8, startHour: 22, startMinute: 55, duration: const Duration(hours: 1, minutes: 55), avgHr: 86, avgHrv: 54, avgSpo2: 97, reminderCount: 3, checkInRating: 7),
      night(id: 'seed-9', daysAgo: 9, startHour: 23, startMinute: 5, duration: const Duration(hours: 2, minutes: 50), avgHr: 93, avgHrv: 46, avgSpo2: 96, reminderCount: 5, checkInRating: 5),
    ]);
  }
}
