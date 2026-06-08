import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../ble/ble_manager.dart';
import 'dashboard_screen.dart';

class ScanScreen extends StatefulWidget {
  const ScanScreen({super.key, required this.manager});

  final BleManager manager;

  @override
  State<ScanScreen> createState() => _ScanScreenState();
}

class _ScanScreenState extends State<ScanScreen> {
  @override
  void initState() {
    super.initState();
    _start();
  }

  Future<void> _start() async {
    if (await widget.manager.ensureReady()) {
      await widget.manager.startScan();
    }
  }

  @override
  void dispose() {
    widget.manager.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.manager,
      builder: (context, _) {
        return Scaffold(
          appBar: AppBar(
            title: const Text('Watch'),
            actions: [
              IconButton(
                tooltip: 'Scan',
                icon: widget.manager.isScanning
                    ? const SizedBox(
                        width: 20,
                        height: 20,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.refresh),
                onPressed: widget.manager.isScanning ? null : widget.manager.startScan,
              ),
            ],
          ),
          body: SafeArea(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Padding(
                  padding: const EdgeInsets.fromLTRB(20, 12, 20, 8),
                  child: Text(
                    widget.manager.statusText,
                    style: Theme.of(context).textTheme.titleMedium,
                  ),
                ),
                Expanded(
                  child: ListView.separated(
                    itemCount: widget.manager.scanResults.length,
                    separatorBuilder: (_, __) => const Divider(height: 1),
                    itemBuilder: (context, index) {
                      final result = widget.manager.scanResults[index];
                      return _DeviceTile(
                        result: result,
                        busy: widget.manager.isConnecting,
                        onTap: () async {
                          final ok = await widget.manager.connect(result.device);
                          if (!context.mounted || !ok) {
                            return;
                          }
                          await Navigator.of(context).push(
                            MaterialPageRoute<void>(
                              builder: (_) => DashboardScreen(manager: widget.manager),
                            ),
                          );
                        },
                      );
                    },
                  ),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _DeviceTile extends StatelessWidget {
  const _DeviceTile({
    required this.result,
    required this.busy,
    required this.onTap,
  });

  final ScanResult result;
  final bool busy;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final name = result.advertisementData.advName.isEmpty
        ? 'ESP32-Watch'
        : result.advertisementData.advName;
    return ListTile(
      leading: const Icon(Icons.watch),
      title: Text(name),
      subtitle: Text('${result.device.remoteId}   RSSI ${result.rssi}'),
      trailing: busy
          ? const SizedBox(
              width: 20,
              height: 20,
              child: CircularProgressIndicator(strokeWidth: 2),
            )
          : const Icon(Icons.chevron_right),
      onTap: busy ? null : onTap,
    );
  }
}
