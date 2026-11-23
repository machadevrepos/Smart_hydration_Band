import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';
import 'package:permission_handler/permission_handler.dart';

import '../ble/band_ble_constants.dart';
import '../ble/band_storage.dart';
import '../ble/band_telemetry.dart';

/// Where the band link currently stands. The pairing screen and the band
/// status card both read this instead of re-deriving it from lower-level
/// state, so there's exactly one place that decides what "connecting" or
/// "reconnecting" means.
enum BandLinkState {
  /// Not connected and not trying: either never paired, or the user
  /// explicitly forgot the band.
  idle,

  /// Bluetooth itself is off, unauthorized, or unsupported at the OS level.
  bluetoothUnavailable,

  /// Scan/connect permission was denied.
  permissionDenied,

  scanning,
  connecting,
  connected,

  /// Was connected, lost the link (band powered off, out of range, app
  /// backgrounded through a system-killed radio), and is retrying with
  /// backoff. The band itself keeps buzzing on schedule regardless, see
  /// side_notes.md: "reconnection handling should be graceful, never
  /// blocking core reminder behavior."
  reconnecting,
}

class BandScanMatch {
  const BandScanMatch({required this.id, required this.name, required this.rssi});
  final String id;
  final String name;
  final int rssi;
}

/// Band connection state shared between Home's status pill, Band Detail,
/// and the pairing flow. Owns the real BLE link end to end: scanning,
/// connecting, reconnect-with-backoff on an unexpected drop, and
/// persisting the last-paired device so the app reconnects quietly on its
/// own next launch.
///
/// Every entry point into `flutter_reactive_ble`/`permission_handler` is
/// wrapped so a platform error (Bluetooth off, permission denied, no
/// plugin registered at all, e.g. in a widget test) changes [state]
/// instead of throwing, per the product requirement that losing the band
/// link, or Bluetooth itself, must never take the app down with it.
class BandController extends ChangeNotifier {
  BandController() {
    _bootstrap();
  }

  final FlutterReactiveBle _ble = FlutterReactiveBle();
  final BandTelemetryLineReader _telemetryReader = BandTelemetryLineReader();

  BandLinkState _state = BandLinkState.idle;
  BleStatus _adapterStatus = BleStatus.unknown;
  String? _deviceId;
  String _deviceName = '';
  int _battery = 0;
  int _restingHr = 0;
  DateTime? _lastSeenAt;
  bool _userInitiatedDisconnect = false;
  int _reconnectAttempt = 0;
  final List<BandScanMatch> _scanResults = [];

  StreamSubscription<BleStatus>? _statusSub;
  StreamSubscription<DiscoveredDevice>? _scanSub;
  StreamSubscription<ConnectionStateUpdate>? _connectionSub;
  StreamSubscription<List<int>>? _telemetrySub;
  Timer? _reconnectTimer;

  static const _reconnectBackoff = [2, 5, 10, 20, 30];

  BandLinkState get state => _state;
  bool get connected => _state == BandLinkState.connected;
  bool get bluetoothOn => _adapterStatus == BleStatus.ready;
  int get battery => _battery;
  int get restingHr => _restingHr;
  String get deviceName => _deviceName.isEmpty ? 'Umbra band' : _deviceName;
  List<BandScanMatch> get scanResults => List.unmodifiable(_scanResults);

  /// When a reading last actually arrived from the band, `null` if it
  /// never has this app lifetime. Distinguishes "no data yet" from "the
  /// numbers on screen are cached from before the link dropped."
  DateTime? get lastSeenAt => _lastSeenAt;

  /// [battery]/[restingHr] reflect a live reading right now vs. a cached
  /// value from before the link dropped. Screens use this to decide
  /// whether to show the numbers as current or mark them stale.
  bool get readingsAreStale => !connected && _lastSeenAt != null;

  Future<void> _bootstrap() async {
    _statusSub = _ble.statusStream.listen(_onAdapterStatus, onError: (_) {});
    try {
      final saved = await BandStorage.loadLastDevice();
      if (saved != null) {
        _deviceId = saved.id;
        _deviceName = saved.name;
      }
    } catch (_) {
      // Preferences unavailable, e.g. a test harness with no plugin
      // registered: nothing to restore, the rest of the app still works.
    }
  }

  void _onAdapterStatus(BleStatus status) {
    final wasReady = _adapterStatus == BleStatus.ready;
    _adapterStatus = status;

    if (status != BleStatus.ready) {
      _reconnectTimer?.cancel();
      unawaited(stopScan());
      if (_state == BandLinkState.connected || _state == BandLinkState.reconnecting) {
        // The band itself doesn't care, it keeps firing reminders on its
        // own schedule. This only affects what the app can show.
        _state = BandLinkState.reconnecting;
      } else if (_state != BandLinkState.idle) {
        _state = BandLinkState.bluetoothUnavailable;
      }
      notifyListeners();
      return;
    }

    // Bluetooth just became ready (including the first-ever ready signal
    // at cold start): pick back up with the last-known device, if any.
    if (!wasReady && _deviceId != null && _state != BandLinkState.connected) {
      _reconnectAttempt = 0;
      _connectToKnownDevice();
    }
    notifyListeners();
  }

  Future<void> _connectToKnownDevice() async {
    final id = _deviceId;
    final name = _deviceName;
    if (id == null) return;
    _userInitiatedDisconnect = false;
    _state = BandLinkState.connecting;
    notifyListeners();
    await _connect(id, name);
  }

  Future<void> _connect(String id, String name) async {
    await _connectionSub?.cancel();
    try {
      _connectionSub = _ble
          .connectToDevice(id: id, connectionTimeout: BandBleConstants.connectionTimeout)
          .listen((update) => _onConnectionUpdate(id, name, update), onError: (_) => _onLinkDropped(id, name));
    } catch (_) {
      _onLinkDropped(id, name);
    }
  }

  void _onConnectionUpdate(String id, String name, ConnectionStateUpdate update) {
    switch (update.connectionState) {
      case DeviceConnectionState.connecting:
        _state = BandLinkState.connecting;
        notifyListeners();
      case DeviceConnectionState.connected:
        _deviceId = id;
        _deviceName = name;
        _reconnectAttempt = 0;
        _state = BandLinkState.connected;
        _lastSeenAt = DateTime.now();
        unawaited(BandStorage.saveLastDevice(id: id, name: name));
        _subscribeTelemetry(id);
        notifyListeners();
      case DeviceConnectionState.disconnecting:
        break;
      case DeviceConnectionState.disconnected:
        _onLinkDropped(id, name);
    }
  }

  void _onLinkDropped(String id, String name) {
    _telemetrySub?.cancel();
    _telemetrySub = null;
    if (_userInitiatedDisconnect) {
      _state = BandLinkState.idle;
    } else {
      _state = BandLinkState.reconnecting;
      _scheduleReconnect(id, name);
    }
    notifyListeners();
  }

  void _scheduleReconnect(String id, String name) {
    _reconnectTimer?.cancel();
    final delay = _reconnectBackoff[_reconnectAttempt.clamp(0, _reconnectBackoff.length - 1)];
    _reconnectAttempt++;
    _reconnectTimer = Timer(Duration(seconds: delay), () {
      if (_state == BandLinkState.reconnecting && bluetoothOn) _connect(id, name);
    });
  }

  void _subscribeTelemetry(String id) {
    _telemetryReader.reset();
    final characteristic = QualifiedCharacteristic(
      characteristicId: BandBleConstants.telemetryCharacteristicUuid,
      serviceId: BandBleConstants.serviceUuid,
      deviceId: id,
    );
    try {
      _telemetrySub = _ble.subscribeToCharacteristic(characteristic).listen(
        (bytes) {
          for (final reading in _telemetryReader.add(bytes)) {
            _applyTelemetry(reading);
          }
        },
        onError: (_) {
          // The characteristic doesn't exist on whatever answered, or
          // firmware isn't streaming yet: the link itself stays up, we
          // simply never receive readings until it does.
        },
      );
    } catch (_) {
      // Same as above: no telemetry, connection unaffected.
    }
  }

  void _applyTelemetry(BandTelemetry reading) {
    _lastSeenAt = DateTime.now();
    var changed = false;
    if (reading.battery != null && reading.battery != _battery) {
      _battery = reading.battery!;
      changed = true;
    }
    if (reading.heartRate != null && reading.heartRate != _restingHr) {
      _restingHr = reading.heartRate!;
      changed = true;
    }
    if (changed) notifyListeners();
  }

  Future<void> startScan() async {
    if (_state == BandLinkState.scanning) return;

    final granted = await _ensurePermissions();
    if (!granted) {
      _state = BandLinkState.permissionDenied;
      notifyListeners();
      return;
    }
    if (!bluetoothOn) {
      _state = BandLinkState.bluetoothUnavailable;
      notifyListeners();
      return;
    }

    _scanResults.clear();
    _state = BandLinkState.scanning;
    notifyListeners();

    try {
      // Filters on advertised name, not service UUID: see the comment on
      // BandBleConstants.advertisedNamePrefixes. Location services aren't
      // required since the scan never derives location from results (the
      // manifest declares `neverForLocation` for the same reason).
      _scanSub = _ble
          .scanForDevices(withServices: const [], requireLocationServicesEnabled: false)
          .listen(_onScanResult, onError: (_) => stopScan());
    } catch (_) {
      _state = BandLinkState.idle;
      notifyListeners();
    }
  }

  void _onScanResult(DiscoveredDevice device) {
    final name = device.name;
    final matchesBand = BandBleConstants.advertisedNamePrefixes.any(
      (prefix) => name.toLowerCase().startsWith(prefix),
    );
    if (!matchesBand) return;

    final match = BandScanMatch(id: device.id, name: name, rssi: device.rssi);
    final existingIndex = _scanResults.indexWhere((d) => d.id == device.id);
    if (existingIndex == -1) {
      _scanResults.add(match);
    } else {
      _scanResults[existingIndex] = match;
    }
    notifyListeners();
  }

  Future<void> stopScan() async {
    await _scanSub?.cancel();
    _scanSub = null;
    if (_state == BandLinkState.scanning) {
      _state = BandLinkState.idle;
      notifyListeners();
    }
  }

  Future<void> connectTo(BandScanMatch device) async {
    await stopScan();
    _userInitiatedDisconnect = false;
    _reconnectAttempt = 0;
    _state = BandLinkState.connecting;
    notifyListeners();
    await _connect(device.id, device.name);
  }

  Future<bool> _ensurePermissions() async {
    try {
      if (Platform.isAndroid) {
        final scan = await Permission.bluetoothScan.request();
        final connect = await Permission.bluetoothConnect.request();
        return scan.isGranted && connect.isGranted;
      }
      if (Platform.isIOS) {
        final status = await Permission.bluetooth.request();
        return status.isGranted;
      }
      return true;
    } catch (_) {
      // Permission plugin unavailable: fail closed, not crash.
      return false;
    }
  }

  Future<void> forget() async {
    // Deliberately doesn't await the teardown below: the user is waiting
    // on this to flip the UI to "offline" right now, and neither
    // cancelling a BLE subscription nor clearing storage should be able
    // to stall that on a slow or unresponsive platform. _userInitiatedDisconnect
    // is set first so a late connection-state event arriving after this
    // still resolves to idle rather than triggering a reconnect.
    _userInitiatedDisconnect = true;
    _reconnectTimer?.cancel();
    final telemetrySub = _telemetrySub;
    _telemetrySub = null;
    if (telemetrySub != null) unawaited(telemetrySub.cancel());
    // Cancelling the connection stream is how flutter_reactive_ble tears
    // down the link, there's no separate disconnect() call.
    final connectionSub = _connectionSub;
    _connectionSub = null;
    if (connectionSub != null) unawaited(connectionSub.cancel());
    unawaited(BandStorage.clearLastDevice());
    _deviceId = null;
    _deviceName = '';
    _battery = 0;
    _restingHr = 0;
    _lastSeenAt = null;
    _state = BandLinkState.idle;
    notifyListeners();
  }

  /// Test-only seam: real pairing needs live BLE hardware, which widget
  /// tests don't have, so this lets a test reach a connected state
  /// deterministically without touching the platform channel at all. Not
  /// referenced from any production code path.
  void debugConnectForTesting({String name = 'Umbra-4F21', int battery = 82, int restingHr = 68}) {
    _deviceId = 'debug-device';
    _deviceName = name;
    _battery = battery;
    _restingHr = restingHr;
    _lastSeenAt = DateTime.now();
    _state = BandLinkState.connected;
    notifyListeners();
  }

  @override
  void dispose() {
    _statusSub?.cancel();
    _scanSub?.cancel();
    _connectionSub?.cancel();
    _telemetrySub?.cancel();
    _reconnectTimer?.cancel();
    super.dispose();
  }
}
