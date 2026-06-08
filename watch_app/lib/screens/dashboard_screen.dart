import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';

import '../ble/ble_manager.dart';
import '../ble/ble_protocol.dart';
import '../models/vitals.dart';

class DashboardScreen extends StatelessWidget {
  const DashboardScreen({super.key, required this.manager});

  final BleManager manager;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: manager,
      builder: (context, _) {
        final vitals = manager.vitals;
        return Scaffold(
          appBar: AppBar(
            title: const Text('ESP32-Watch'),
            actions: [
              IconButton(
                tooltip: 'Sync time',
                icon: const Icon(Icons.schedule),
                onPressed: manager.isConnected ? manager.syncTime : null,
              ),
              IconButton(
                tooltip: 'Disconnect',
                icon: const Icon(Icons.link_off),
                onPressed: () async {
                  await manager.disconnect();
                  if (context.mounted) {
                    Navigator.of(context).pop();
                  }
                },
              ),
            ],
          ),
          body: SafeArea(
            child: ListView(
              padding: const EdgeInsets.all(16),
              children: [
                _StatusStrip(manager: manager, vitals: vitals),
                const SizedBox(height: 14),
                GridView.count(
                  crossAxisCount: MediaQuery.sizeOf(context).width > 680 ? 4 : 2,
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  crossAxisSpacing: 10,
                  mainAxisSpacing: 10,
                  childAspectRatio: 1.45,
                  children: [
                    _MetricCard(
                      label: 'BPM',
                      value: vitals.bpm > 0 ? vitals.bpm.toStringAsFixed(0) : '--',
                      icon: Icons.favorite,
                      color: const Color(0xFFFF5A6B),
                    ),
                    _MetricCard(
                      label: 'SpO2',
                      value: vitals.spo2 > 0 ? '${vitals.spo2.toStringAsFixed(1)}%' : '--',
                      icon: Icons.bloodtype,
                      color: const Color(0xFF5AB9FF),
                    ),
                    _MetricCard(
                      label: vitals.bodyValid ? 'Body' : 'Temp',
                      value: _formatTemp(
                        vitals.bodyValid ? vitals.bodyTemperatureC : vitals.sensorTemperatureC,
                        vitals.bodyValid || vitals.tempValid,
                      ),
                      icon: Icons.thermostat,
                      color: const Color(0xFFFFC857),
                    ),
                    _MetricCard(
                      label: 'Rate',
                      value: '${vitals.sampleRate}/s',
                      icon: Icons.speed,
                      color: const Color(0xFF49D17D),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                _WaveformPanel(values: vitals.waveform),
                const SizedBox(height: 16),
                _DetailPanel(vitals: vitals),
              ],
            ),
          ),
        );
      },
    );
  }

  static String _formatTemp(double value, bool valid) {
    return valid ? '${value.toStringAsFixed(1)}C' : '--';
  }
}

class _StatusStrip extends StatelessWidget {
  const _StatusStrip({required this.manager, required this.vitals});

  final BleManager manager;
  final Vitals vitals;

  @override
  Widget build(BuildContext context) {
    final color = manager.isConnected ? const Color(0xFF49D17D) : const Color(0xFFFFC857);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: const Color(0xFF171C24),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF2B3440)),
      ),
      child: Row(
        children: [
          Icon(manager.isConnected ? Icons.bluetooth_connected : Icons.bluetooth_searching, color: color),
          const SizedBox(width: 10),
          Expanded(child: Text(manager.statusText)),
          Text(vitals.fingerPresent ? 'Finger ON' : 'Finger OFF'),
        ],
      ),
    );
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({
    required this.label,
    required this.value,
    required this.icon,
    required this.color,
  });

  final String label;
  final String value;
  final IconData icon;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF171C24),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF2B3440)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Row(
            children: [
              Icon(icon, color: color, size: 20),
              const SizedBox(width: 8),
              Text(label, style: Theme.of(context).textTheme.labelLarge),
            ],
          ),
          FittedBox(
            alignment: Alignment.centerLeft,
            fit: BoxFit.scaleDown,
            child: Text(
              value,
              style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                    fontWeight: FontWeight.w700,
                    color: Colors.white,
                  ),
            ),
          ),
        ],
      ),
    );
  }
}

class _WaveformPanel extends StatelessWidget {
  const _WaveformPanel({required this.values});

  final List<int> values;

  @override
  Widget build(BuildContext context) {
    final spots = <FlSpot>[
      for (var i = 0; i < values.length; i++) FlSpot(i.toDouble(), values[i].toDouble()),
    ];
    return Container(
      height: 260,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF171C24),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF2B3440)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.monitor_heart, color: Color(0xFFFF5A6B), size: 20),
              const SizedBox(width: 8),
              Text('PPG', style: Theme.of(context).textTheme.titleMedium),
            ],
          ),
          const SizedBox(height: 10),
          Expanded(
            child: LineChart(
              LineChartData(
                minX: 0,
                maxX: 9,
                gridData: const FlGridData(show: true),
                titlesData: const FlTitlesData(show: false),
                borderData: FlBorderData(show: false),
                lineBarsData: [
                  LineChartBarData(
                    spots: spots.isEmpty ? const [FlSpot(0, 0), FlSpot(9, 0)] : spots,
                    isCurved: true,
                    color: const Color(0xFFFF5A6B),
                    barWidth: 2,
                    dotData: const FlDotData(show: false),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _DetailPanel extends StatelessWidget {
  const _DetailPanel({required this.vitals});

  final Vitals vitals;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF171C24),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF2B3440)),
      ),
      child: Column(
        children: [
          _DetailRow(label: 'MAX30102', value: _ok(vitals.maxPresent, vitals.maxInitialized)),
          _DetailRow(label: 'MAX30205', value: _ok(vitals.tempPresent, vitals.tempInitialized)),
          _DetailRow(label: 'Pulse', value: vitals.pulseDetected ? 'Detected' : '--'),
          _DetailRow(label: 'Time', value: vitals.timeValid ? 'Synced' : 'Waiting'),
          _DetailRow(label: 'Command', value: _commandText(vitals)),
          _DetailRow(label: 'Samples', value: '${vitals.sampleCount}'),
        ],
      ),
    );
  }

  static String _ok(bool present, bool initialized) {
    if (!present) {
      return 'Missing';
    }
    return initialized ? 'OK' : 'Init';
  }

  static String _commandText(Vitals vitals) {
    final command = switch (vitals.lastCommand) {
      WatchBleProtocol.commandSetTime => 'time',
      WatchBleProtocol.commandWaveformBatch => 'wave',
      _ => '--',
    };
    final result = switch (vitals.lastResult) {
      1 => 'ok',
      2 => 'bad length',
      3 => 'unknown',
      4 => 'failed',
      5 => 'busy',
      _ => 'none',
    };
    return '#${vitals.commandSeq} $command $result';
  }
}

class _DetailRow extends StatelessWidget {
  const _DetailRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          Expanded(
            child: Text(label, style: TextStyle(color: Colors.white.withValues(alpha: 0.64))),
          ),
          Text(value, style: const TextStyle(fontWeight: FontWeight.w600)),
        ],
      ),
    );
  }
}
