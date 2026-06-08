import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:watch_app/ble/ble_protocol.dart';
import 'package:watch_app/models/vitals.dart';

void main() {
  test('decodes status payload', () {
    final data = ByteData(13)
      ..setUint32(0, 0x05, Endian.little)
      ..setUint16(4, 25, Endian.little)
      ..setUint32(6, 1234, Endian.little)
      ..setUint8(10, 7)
      ..setUint8(11, WatchBleProtocol.commandWaveformBatch)
      ..setUint8(12, 1);

    final vitals = WatchBleProtocol.decodeStatus(
      Vitals.empty(),
      data.buffer.asUint8List(),
    );

    expect(vitals.flags, 0x05);
    expect(vitals.sampleRate, 25);
    expect(vitals.sampleCount, 1234);
    expect(vitals.commandSeq, 7);
    expect(vitals.lastCommand, WatchBleProtocol.commandWaveformBatch);
    expect(vitals.lastResult, 1);
  });

  test('decodes ten little-endian waveform points', () {
    final data = ByteData(20);
    for (var i = 0; i < 10; i++) {
      data.setInt16(i * 2, i - 5, Endian.little);
    }

    expect(
      WatchBleProtocol.decodeWaveform(data.buffer.asUint8List()),
      <int>[-5, -4, -3, -2, -1, 0, 1, 2, 3, 4],
    );
  });
}
