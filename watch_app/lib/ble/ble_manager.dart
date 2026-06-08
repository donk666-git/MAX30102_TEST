import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

import '../models/vitals.dart';
import 'ble_protocol.dart';

class BleManager extends ChangeNotifier {
  final List<ScanResult> scanResults = <ScanResult>[];
  Vitals vitals = Vitals.empty();
  String statusText = 'Idle';
  bool isScanning = false;
  bool isConnecting = false;
  bool isConnected = false;

  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandChar;
  StreamSubscription<List<ScanResult>>? _scanSub;
  StreamSubscription<BluetoothConnectionState>? _connectionSub;
  final List<StreamSubscription<List<int>>> _valueSubs = <StreamSubscription<List<int>>>[];
  Timer? _waveTimer;
  Timer? _reconnectTimer;
  bool _manualDisconnect = false;

  Future<bool> ensureReady() async {
    if (!await FlutterBluePlus.isSupported) {
      statusText = 'Bluetooth unsupported';
      notifyListeners();
      return false;
    }

    if (!kIsWeb && Platform.isAndroid) {
      final statuses = await <Permission>[
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.locationWhenInUse,
      ].request();
      final scanOk = statuses[Permission.bluetoothScan]?.isGranted ?? true;
      final connectOk = statuses[Permission.bluetoothConnect]?.isGranted ?? true;
      final locationOk = statuses[Permission.locationWhenInUse]?.isGranted ?? true;
      if ((!scanOk && !locationOk) || (!connectOk && !locationOk)) {
        statusText = 'Bluetooth permission needed';
        notifyListeners();
        return false;
      }
      await FlutterBluePlus.turnOn();
    }

    await FlutterBluePlus.adapterState
        .where((state) => state == BluetoothAdapterState.on)
        .first
        .timeout(const Duration(seconds: 8));
    return true;
  }

  Future<void> startScan() async {
    if (isScanning) {
      return;
    }

    scanResults.clear();
    statusText = 'Scanning';
    isScanning = true;
    notifyListeners();

    await _scanSub?.cancel();
    _scanSub = FlutterBluePlus.scanResults.listen((results) {
      scanResults
        ..clear()
        ..addAll(results.where(_matchesWatch));
      notifyListeners();
    });

    try {
      await FlutterBluePlus.startScan(
        withServices: [Guid(WatchBleProtocol.serviceUuid)],
        timeout: const Duration(seconds: 8),
      );
      await FlutterBluePlus.isScanning.where((value) => !value).first;
    } finally {
      isScanning = false;
      statusText = scanResults.isEmpty ? 'No devices' : 'Select device';
      notifyListeners();
    }
  }

  Future<bool> connect(BluetoothDevice device) async {
    _manualDisconnect = false;
    _reconnectTimer?.cancel();
    return _connectDevice(device);
  }

  Future<void> disconnect() async {
    _manualDisconnect = true;
    _reconnectTimer?.cancel();
    _waveTimer?.cancel();
    await _clearSubscriptions();
    final device = _device;
    if (device != null) {
      await device.disconnect();
    }
    isConnected = false;
    statusText = 'Disconnected';
    notifyListeners();
  }

  Future<void> syncTime() async {
    final command = _commandChar;
    if (command == null || !isConnected) {
      return;
    }
    await command.write(
      WatchBleProtocol.encodeSetTime(DateTime.now()),
      withoutResponse: false,
    );
  }

  Future<void> requestWaveform() async {
    final command = _commandChar;
    if (command == null || !isConnected) {
      return;
    }
    await command.write(
      WatchBleProtocol.encodeWaveformRequest(),
      withoutResponse: false,
    );
  }

  bool _matchesWatch(ScanResult result) {
    final name = result.advertisementData.advName;
    return name == 'ESP32-Watch' ||
        result.advertisementData.serviceUuids
            .map((uuid) => uuid.toString().toLowerCase())
            .contains(WatchBleProtocol.serviceUuid);
  }

  Future<bool> _connectDevice(BluetoothDevice device) async {
    isConnecting = true;
    statusText = 'Connecting';
    notifyListeners();

    try {
      await FlutterBluePlus.stopScan();
      _device = device;
      await _connectionSub?.cancel();
      _connectionSub = device.connectionState.listen((state) {
        final connected = state == BluetoothConnectionState.connected;
        isConnected = connected;
        if (!connected) {
          _waveTimer?.cancel();
          unawaited(_clearSubscriptions());
          if (!_manualDisconnect) {
            statusText = 'Reconnecting';
            _scheduleReconnect();
          }
        }
        notifyListeners();
      });

      await device.connect(
        license: License.free,
        timeout: const Duration(seconds: 12),
      );
      await _discoverAndSubscribe(device);
      _waveTimer?.cancel();
      _waveTimer = Timer.periodic(
        const Duration(milliseconds: 300),
        (_) => requestWaveform(),
      );
      isConnected = true;
      statusText = 'Connected';
      await syncTime();
      return true;
    } catch (error) {
      statusText = 'Connect failed';
      _scheduleReconnect();
      return false;
    } finally {
      isConnecting = false;
      notifyListeners();
    }
  }

  Future<void> _discoverAndSubscribe(BluetoothDevice device) async {
    await _clearSubscriptions();
    final services = await device.discoverServices();
    final chars = <String, BluetoothCharacteristic>{};
    for (final service in services) {
      if (service.uuid.toString().toLowerCase() != WatchBleProtocol.serviceUuid) {
        continue;
      }
      for (final characteristic in service.characteristics) {
        chars[characteristic.uuid.toString().toLowerCase()] = characteristic;
      }
    }

    _commandChar = chars[WatchBleProtocol.commandUuid];
    await _subscribe(chars[WatchBleProtocol.bpmUuid], (value) {
      vitals = vitals.copyWith(bpm: WatchBleProtocol.decodeFloat(value));
      notifyListeners();
    });
    await _subscribe(chars[WatchBleProtocol.spo2Uuid], (value) {
      vitals = vitals.copyWith(spo2: WatchBleProtocol.decodeFloat(value));
      notifyListeners();
    });
    await _subscribe(chars[WatchBleProtocol.tempUuid], (value) {
      final temp = WatchBleProtocol.decodeTemperature(value);
      vitals = vitals.copyWith(
        sensorTemperatureC: temp.sensor,
        bodyTemperatureC: temp.body,
      );
      notifyListeners();
    });
    await _subscribe(chars[WatchBleProtocol.statusUuid], (value) {
      vitals = WatchBleProtocol.decodeStatus(vitals, value);
      notifyListeners();
    });
    await _subscribe(chars[WatchBleProtocol.waveUuid], (value) {
      vitals = vitals.copyWith(waveform: WatchBleProtocol.decodeWaveform(value));
      notifyListeners();
    });
  }

  Future<void> _subscribe(
    BluetoothCharacteristic? characteristic,
    void Function(List<int>) onValue,
  ) async {
    if (characteristic == null) {
      return;
    }

    final sub = characteristic.onValueReceived.listen(onValue);
    _valueSubs.add(sub);
    await characteristic.setNotifyValue(true);
    if (characteristic.properties.read) {
      onValue(await characteristic.read());
    }
  }

  void _scheduleReconnect() {
    if (_manualDisconnect || _reconnectTimer != null) {
      return;
    }
    _reconnectTimer = Timer.periodic(const Duration(seconds: 3), (timer) async {
      final device = _device;
      if (device == null || isConnected || isConnecting) {
        return;
      }
      final ok = await _connectDevice(device);
      if (ok) {
        timer.cancel();
        _reconnectTimer = null;
      }
    });
  }

  Future<void> _clearSubscriptions() async {
    for (final sub in _valueSubs) {
      await sub.cancel();
    }
    _valueSubs.clear();
    _commandChar = null;
  }

  @override
  void dispose() {
    _manualDisconnect = true;
    unawaited(_scanSub?.cancel());
    unawaited(_connectionSub?.cancel());
    _waveTimer?.cancel();
    _reconnectTimer?.cancel();
    unawaited(_clearSubscriptions());
    super.dispose();
  }
}
