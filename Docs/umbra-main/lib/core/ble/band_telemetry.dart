import 'dart:convert';

/// A single decoded reading from the band, matching the JSON telemetry
/// shape in the feasibility report. `gsr` is intentionally not modeled,
/// see side_notes.md: "GSR is explicitly dropped for v1."
class BandTelemetry {
  const BandTelemetry({this.deviceId, this.heartRate, this.spo2, this.battery, this.sessionActive});

  final String? deviceId;
  final int? heartRate;
  final int? spo2;
  final int? battery;
  final bool? sessionActive;

  static BandTelemetry? tryParse(String line) {
    if (line.trim().isEmpty) return null;
    try {
      final decoded = jsonDecode(line);
      if (decoded is! Map<String, dynamic>) return null;
      return BandTelemetry(
        deviceId: decoded['device_id'] as String?,
        heartRate: (decoded['heart_rate'] as num?)?.toInt(),
        spo2: (decoded['spo2'] as num?)?.toInt(),
        battery: (decoded['battery'] as num?)?.toInt(),
        sessionActive: decoded['session_active'] as bool?,
      );
    } catch (_) {
      // Malformed or partial packet, most commonly one JSON payload
      // straddling two BLE notifications when it exceeds the connection's
      // MTU. Dropping a single reading is harmless, the next one arrives
      // on the band's own telemetry schedule regardless.
      return null;
    }
  }
}

/// Buffers raw notify bytes into newline-delimited JSON lines.
///
/// A single JSON telemetry payload can arrive split across more than one
/// BLE notification once it exceeds the negotiated MTU, so this
/// reassembles fragments by byte-buffering until a line terminator shows
/// up, the standard framing for UART-style BLE telemetry.
class BandTelemetryLineReader {
  final List<int> _buffer = [];

  Iterable<BandTelemetry> add(List<int> chunk) sync* {
    _buffer.addAll(chunk);
    while (true) {
      final newlineIndex = _buffer.indexOf(0x0A); // '\n'
      if (newlineIndex == -1) break;
      final lineBytes = _buffer.sublist(0, newlineIndex);
      _buffer.removeRange(0, newlineIndex + 1);
      final line = utf8.decode(lineBytes, allowMalformed: true);
      final telemetry = BandTelemetry.tryParse(line);
      if (telemetry != null) yield telemetry;
    }
    // Guards against a runaway buffer if the peripheral never sends a
    // line terminator, e.g. a firmware bug: drop it rather than growing
    // unbounded for the life of the connection.
    if (_buffer.length > 4096) _buffer.clear();
  }

  void reset() => _buffer.clear();
}
