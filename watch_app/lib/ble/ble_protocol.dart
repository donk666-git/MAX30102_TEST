import 'dart:typed_data';

import '../models/vitals.dart';

class WatchBleProtocol {
  const WatchBleProtocol._();

  static const serviceUuid = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
  static const bpmUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310001';
  static const spo2Uuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310002';
  static const tempUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310003';
  static const waveUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310004';
  static const statusUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310005';
  static const commandUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310006';
  static const deviceInfoUuid = '4fafc201-1fb5-459e-8fcc-c5c9c3310007';

  static const commandSetTime = 0x01;
  static const commandWaveformBatch = 0x02;

  static double decodeFloat(List<int> bytes) {
    if (bytes.length < 4) {
      return 0;
    }
    return _byteData(bytes).getFloat32(0, Endian.little);
  }

  static ({double sensor, double body}) decodeTemperature(List<int> bytes) {
    if (bytes.length < 8) {
      return (sensor: 0, body: 0);
    }
    final data = _byteData(bytes);
    return (
      sensor: data.getFloat32(0, Endian.little),
      body: data.getFloat32(4, Endian.little),
    );
  }

  static Vitals decodeStatus(Vitals current, List<int> bytes) {
    if (bytes.length < 13) {
      return current;
    }
    final data = _byteData(bytes);
    return current.copyWith(
      flags: data.getUint32(0, Endian.little),
      sampleRate: data.getUint16(4, Endian.little),
      sampleCount: data.getUint32(6, Endian.little),
      commandSeq: bytes[10],
      lastCommand: bytes[11],
      lastResult: bytes[12],
    );
  }

  static List<int> decodeWaveform(List<int> bytes) {
    final values = <int>[];
    for (var offset = 0; offset + 1 < bytes.length && values.length < 10; offset += 2) {
      values.add(_byteData(bytes).getInt16(offset, Endian.little));
    }
    return values;
  }

  static List<int> encodeSetTime(DateTime now) {
    final seconds = now.toUtc().millisecondsSinceEpoch ~/ 1000;
    final data = ByteData(5);
    data.setUint8(0, commandSetTime);
    data.setUint32(1, seconds, Endian.little);
    return data.buffer.asUint8List();
  }

  static List<int> encodeWaveformRequest() {
    return const [commandWaveformBatch];
  }

  static ByteData _byteData(List<int> bytes) {
    final typed = bytes is Uint8List ? bytes : Uint8List.fromList(bytes);
    return ByteData.sublistView(typed);
  }
}
