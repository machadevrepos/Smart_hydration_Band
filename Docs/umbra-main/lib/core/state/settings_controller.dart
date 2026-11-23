import 'package:flutter/foundation.dart';

/// App-wide preferences. In-memory only for now. Swaps for
/// shared_preferences once persistence matters, the read/write shape here
/// shouldn't need to change.
class SettingsController extends ChangeNotifier {
  bool _notificationsEnabled = true;
  bool _hapticsEnabled = true;

  bool get notificationsEnabled => _notificationsEnabled;
  bool get hapticsEnabled => _hapticsEnabled;

  void setNotificationsEnabled(bool value) {
    if (_notificationsEnabled == value) return;
    _notificationsEnabled = value;
    notifyListeners();
  }

  void setHapticsEnabled(bool value) {
    if (_hapticsEnabled == value) return;
    _hapticsEnabled = value;
    notifyListeners();
  }
}
