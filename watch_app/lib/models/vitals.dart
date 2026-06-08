class Vitals {
  const Vitals({
    required this.bpm,
    required this.spo2,
    required this.sensorTemperatureC,
    required this.bodyTemperatureC,
    required this.flags,
    required this.sampleRate,
    required this.sampleCount,
    required this.commandSeq,
    required this.lastCommand,
    required this.lastResult,
    required this.waveform,
  });

  factory Vitals.empty() {
    return const Vitals(
      bpm: 0,
      spo2: 0,
      sensorTemperatureC: 0,
      bodyTemperatureC: 0,
      flags: 0,
      sampleRate: 0,
      sampleCount: 0,
      commandSeq: 0,
      lastCommand: 0,
      lastResult: 0,
      waveform: <int>[],
    );
  }

  final double bpm;
  final double spo2;
  final double sensorTemperatureC;
  final double bodyTemperatureC;
  final int flags;
  final int sampleRate;
  final int sampleCount;
  final int commandSeq;
  final int lastCommand;
  final int lastResult;
  final List<int> waveform;

  bool get maxPresent => _flag(0);
  bool get maxInitialized => _flag(1);
  bool get fingerPresent => _flag(2);
  bool get pulseDetected => _flag(3);
  bool get saturated => _flag(4);
  bool get settling => _flag(5);
  bool get measurementActive => _flag(6);
  bool get tempPresent => _flag(7);
  bool get tempInitialized => _flag(8);
  bool get tempValid => _flag(9);
  bool get tempContact => _flag(10);
  bool get bodyValid => _flag(11);
  bool get timeValid => _flag(12);

  bool _flag(int bit) => (flags & (1 << bit)) != 0;

  Vitals copyWith({
    double? bpm,
    double? spo2,
    double? sensorTemperatureC,
    double? bodyTemperatureC,
    int? flags,
    int? sampleRate,
    int? sampleCount,
    int? commandSeq,
    int? lastCommand,
    int? lastResult,
    List<int>? waveform,
  }) {
    return Vitals(
      bpm: bpm ?? this.bpm,
      spo2: spo2 ?? this.spo2,
      sensorTemperatureC: sensorTemperatureC ?? this.sensorTemperatureC,
      bodyTemperatureC: bodyTemperatureC ?? this.bodyTemperatureC,
      flags: flags ?? this.flags,
      sampleRate: sampleRate ?? this.sampleRate,
      sampleCount: sampleCount ?? this.sampleCount,
      commandSeq: commandSeq ?? this.commandSeq,
      lastCommand: lastCommand ?? this.lastCommand,
      lastResult: lastResult ?? this.lastResult,
      waveform: waveform ?? this.waveform,
    );
  }
}
