import 'package:shared_preferences/shared_preferences.dart';

/// Persists the last-connected band's identity across app restarts, so
/// the app can attempt to reconnect automatically on next launch instead
/// of forcing the user to re-pair every time they reopen it.
class BandStorage {
  const BandStorage._();

  static const _deviceIdKey = 'band_last_device_id';
  static const _deviceNameKey = 'band_last_device_name';

  /// Every method here fails closed rather than throwing: losing the
  /// ability to remember a band (a full disk, a platform with no
  /// preferences plugin registered) should never block the connection
  /// state change the caller is in the middle of making.
  static Future<void> saveLastDevice({required String id, required String name}) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_deviceIdKey, id);
      await prefs.setString(_deviceNameKey, name);
    } catch (_) {
      // Reconnect-on-launch simply won't have a device to try next time.
    }
  }

  static Future<({String id, String name})?> loadLastDevice() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final id = prefs.getString(_deviceIdKey);
      if (id == null) return null;
      return (id: id, name: prefs.getString(_deviceNameKey) ?? 'Umbra band');
    } catch (_) {
      return null;
    }
  }

  static Future<void> clearLastDevice() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove(_deviceIdKey);
      await prefs.remove(_deviceNameKey);
    } catch (_) {
      // Nothing to clear if it couldn't be read in the first place.
    }
  }
}
