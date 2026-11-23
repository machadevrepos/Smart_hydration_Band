import 'package:shared_preferences/shared_preferences.dart';

/// Persists whether the user has ever reached the end of the onboarding →
/// permissions → band pairing setup flow, so Splash can route straight to
/// Home on subsequent launches instead of replaying setup every time.
class OnboardingStorage {
  const OnboardingStorage._();

  static const _completedKey = 'onboarding_completed';

  /// Fails closed (returns false) rather than throwing: if the flag can't
  /// be read, the app falls back to showing onboarding again, which is
  /// safe, rather than blocking startup.
  static Future<bool> hasCompletedOnboarding() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      return prefs.getBool(_completedKey) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<void> setCompleted() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setBool(_completedKey, true);
    } catch (_) {
      // Onboarding will simply replay next launch if this couldn't be saved.
    }
  }
}
