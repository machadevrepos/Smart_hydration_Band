import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

/// BLE identifiers and scan filters for the band, pending the real
/// firmware spec.
///
/// No production hardware exists yet, see side_notes.md's "BLE / firmware
/// context": the feasibility report only fixes an MCU family (ESP32 or
/// equivalent) and a JSON telemetry shape, not real service or
/// characteristic UUIDs. These default to the Nordic UART Service, the
/// standard choice for an ESP32 prototype streaming JSON text over BLE
/// notify, and every place that depends on them is isolated to this file
/// so swapping in real firmware UUIDs later is a one-file change.
class BandBleConstants {
  const BandBleConstants._();

  static final Uuid serviceUuid = Uuid.parse('6e400001-b5a3-f393-e0a9-e50e24dcca9e');
  static final Uuid telemetryCharacteristicUuid = Uuid.parse('6e400003-b5a3-f393-e0a9-e50e24dcca9e');
  static final Uuid commandCharacteristicUuid = Uuid.parse('6e400002-b5a3-f393-e0a9-e50e24dcca9e');

  /// Scanning filters on advertised name rather than [serviceUuid]. Some
  /// minimal ESP32 firmware omits service UUIDs from the legacy 31-byte
  /// advertisement packet to save space, so a service-UUID scan filter can
  /// silently find nothing even with the band nearby and advertising.
  /// Matches the "HYDRO-WR-001"-style device id from the feasibility
  /// report's example payload, case-insensitively, plus the app's own name.
  static const List<String> advertisedNamePrefixes = ['umbra', 'hydro'];

  static const Duration scanTimeout = Duration(seconds: 12);
  static const Duration connectionTimeout = Duration(seconds: 15);
}
