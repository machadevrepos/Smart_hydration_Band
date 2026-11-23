import 'dart:async';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:provider/provider.dart';
import '../../core/ble/band_ble_constants.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/band_controller.dart';
import '../../core/state/onboarding_storage.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/pressable_scale.dart';
import '../../core/widgets/pulse_radar.dart';
import '../shell/main_shell.dart';

class BandPairingScreen extends StatefulWidget {
  const BandPairingScreen({super.key});

  @override
  State<BandPairingScreen> createState() => _BandPairingScreenState();
}

class _BandPairingScreenState extends State<BandPairingScreen> {
  late final BandController _band;
  Timer? _scanTimeoutTimer;
  Timer? _handoffTimer;
  bool _timedOut = false;
  bool _connectedHandled = false;

  @override
  void initState() {
    super.initState();
    _band = context.read<BandController>();
    WidgetsBinding.instance.addPostFrameCallback((_) => _startScan());
  }

  void _startScan() {
    final band = _band;
    if (band.connected) return;
    _timedOut = false;
    band.startScan();
    _scanTimeoutTimer?.cancel();
    _scanTimeoutTimer = Timer(BandBleConstants.scanTimeout, () {
      if (_band.state == BandLinkState.scanning && _band.scanResults.isEmpty) {
        _band.stopScan();
        if (mounted) setState(() => _timedOut = true);
      }
    });
  }

  void _connect(BandScanMatch device) {
    _scanTimeoutTimer?.cancel();
    _band.connectTo(device);
  }

  void _handleConnected() {
    if (_connectedHandled) return;
    _connectedHandled = true;
    _handoffTimer = Timer(const Duration(milliseconds: 700), () {
      if (mounted) _goHome();
    });
  }

  void _goHome() {
    OnboardingStorage.setCompleted();
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const MainShell()));
  }

  @override
  void dispose() {
    _scanTimeoutTimer?.cancel();
    _handoffTimer?.cancel();
    _band.stopScan();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final band = context.watch<BandController>();
    final connected = band.state == BandLinkState.connected;
    if (connected) _handleConnected();

    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const SizedBox(height: AppTheme.spaceXxl),
              Text('Find your band', style: AppTypography.titleL()),
              const SizedBox(height: AppTheme.spaceS),
              Text(
                "Make sure it's charged and nearby. You can skip this. Umbra works standalone.",
                style: AppTypography.bodyM(),
              ),
              const Spacer(),
              Center(
                child: AnimatedSwitcher(
                  duration: AppTheme.animNormal,
                  child: connected
                      ? _ConnectedMark(key: const ValueKey('connected'))
                      : PulseRadar(
                          key: const ValueKey('radar'),
                          child: Icon(_stageIcon(band.state), size: AppTheme.iconL, color: AppColors.gold),
                        ),
                ),
              ),
              const SizedBox(height: AppTheme.spaceXxl),
              Center(child: _StageLabel(state: band.state, timedOut: _timedOut)),
              const SizedBox(height: AppTheme.spaceXl),
              AnimatedSwitcher(
                duration: AppTheme.animFast,
                child: band.scanResults.isNotEmpty
                    ? Column(
                        key: const ValueKey('devices'),
                        children: [
                          for (final device in band.scanResults) ...[
                            _DeviceCard(
                              device: device,
                              connecting: band.state == BandLinkState.connecting,
                              onTap: band.state == BandLinkState.connecting ? null : () => _connect(device),
                            ),
                            const SizedBox(height: AppTheme.spaceM),
                          ],
                        ],
                      )
                    : const SizedBox(key: ValueKey('empty'), height: 0),
              ),
              const Spacer(),
              if (!connected) ...[
                _PrimaryAction(state: band.state, timedOut: _timedOut, onRetry: _startScan),
                const SizedBox(height: AppTheme.spaceM),
                Center(
                  child: PressableScale(
                    onTap: _goHome,
                    semanticLabel: 'Skip for now',
                    child: Container(
                      height: AppTheme.minTapTarget,
                      padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM),
                      alignment: Alignment.center,
                      child: Text('Skip for now', style: AppTypography.labelL(color: AppColors.textMuted)),
                    ),
                  ),
                ),
                const SizedBox(height: AppTheme.spaceM),
              ] else
                const SizedBox(height: AppTheme.minTapTarget + 8 + AppTheme.spaceM + AppTheme.minTapTarget + AppTheme.spaceM),
            ],
          ),
        ),
      ),
    );
  }

  IconData _stageIcon(BandLinkState state) {
    return switch (state) {
      BandLinkState.bluetoothUnavailable => AppIcons.bluetoothOff,
      BandLinkState.permissionDenied => AppIcons.bluetoothOff,
      _ => AppIcons.bluetooth,
    };
  }
}

/// The screen's one primary action, its label and behavior both follow
/// from [state]: retrying a scan, opening system settings for a denied
/// permission, or nothing at all while a scan or connection is already
/// underway (the radar above is the only feedback needed then).
class _PrimaryAction extends StatelessWidget {
  const _PrimaryAction({required this.state, required this.timedOut, required this.onRetry});

  final BandLinkState state;
  final bool timedOut;
  final VoidCallback onRetry;

  @override
  Widget build(BuildContext context) {
    switch (state) {
      case BandLinkState.permissionDenied:
        return AppButton(label: 'Open Settings', icon: AppIcons.bluetooth, onTap: openAppSettings);
      case BandLinkState.bluetoothUnavailable:
        return AppButton(label: 'Try again', icon: AppIcons.bluetooth, onTap: onRetry);
      case BandLinkState.connecting:
        return const AppButton(label: 'Connecting', onTap: null, loading: true);
      case BandLinkState.scanning:
        if (timedOut) return AppButton(label: 'Try again', icon: AppIcons.bluetooth, onTap: onRetry);
        return const AppButton(label: 'Scanning', onTap: null, loading: true);
      case BandLinkState.idle:
      case BandLinkState.reconnecting:
      case BandLinkState.connected:
        return AppButton(label: 'Try again', icon: AppIcons.bluetooth, onTap: onRetry);
    }
  }
}

class _StageLabel extends StatelessWidget {
  const _StageLabel({required this.state, required this.timedOut});
  final BandLinkState state;
  final bool timedOut;

  @override
  Widget build(BuildContext context) {
    final text = switch (state) {
      BandLinkState.bluetoothUnavailable => 'Turn on Bluetooth to find your band',
      BandLinkState.permissionDenied => 'Umbra needs Bluetooth permission to find your band',
      BandLinkState.scanning => timedOut ? "Didn't find a band nearby" : 'Scanning for nearby bands…',
      BandLinkState.connecting => 'Connecting…',
      BandLinkState.reconnecting => 'Reconnecting…',
      BandLinkState.connected => 'Connected',
      BandLinkState.idle => 'Ready to scan',
    };
    return AnimatedSwitcher(
      duration: AppTheme.animFast,
      child: Text(
        text,
        key: ValueKey(text),
        textAlign: TextAlign.center,
        style: AppTypography.bodyL(color: AppColors.textSecondary),
      ),
    );
  }
}

class _DeviceCard extends StatelessWidget {
  const _DeviceCard({required this.device, required this.connecting, required this.onTap});

  final BandScanMatch device;
  final bool connecting;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return PressableScale(
      onTap: onTap,
      hapticOnTap: true,
      semanticLabel: 'Connect to ${device.name}',
      child: Container(
        padding: const EdgeInsets.all(AppTheme.spaceL),
        decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
        child: Row(
          children: [
            Container(
              width: 40,
              height: 40,
              alignment: Alignment.center,
              decoration: const BoxDecoration(color: AppColors.bgHairline, shape: BoxShape.circle),
              child: const Icon(AppIcons.moon, size: AppTheme.iconM, color: AppColors.gold),
            ),
            const SizedBox(width: AppTheme.spaceL),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(device.name, style: AppTypography.titleS()),
                  const SizedBox(height: 2),
                  Text(_signalLabel(device.rssi), style: AppTypography.bodyS()),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  String _signalLabel(int rssi) {
    final strength = rssi >= -60 ? 'Strong signal' : (rssi >= -75 ? 'Good signal' : 'Weak signal');
    return 'Nearby · $strength';
  }
}

class _ConnectedMark extends StatelessWidget {
  const _ConnectedMark({super.key});

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;
    return TweenAnimationBuilder<double>(
      tween: Tween(begin: 0.85, end: 1.0),
      duration: reduceMotion ? Duration.zero : AppTheme.animNormal,
      curve: AppTheme.motionCurve,
      builder: (context, scale, child) => Transform.scale(scale: scale, child: child),
      child: Container(
        width: 88,
        height: 88,
        alignment: Alignment.center,
        decoration: BoxDecoration(color: AppColors.successTint, shape: BoxShape.circle, border: Border.all(color: AppColors.success, width: 1.5)),
        child: const Icon(AppIcons.check, size: AppTheme.iconXl, color: AppColors.success),
      ),
    );
  }
}
