import 'package:flutter/material.dart';
import '../constants/app_colors.dart';
import '../constants/app_icons.dart';
import '../state/band_controller.dart';

/// Maps [BandLinkState] to the label/icon/color every band status
/// treatment uses, Home's top bar, the band status card, and Band Detail
/// alike, so the states read the same way wherever they show up instead
/// of each screen re-deciding what "reconnecting" looks like.
///
/// In-progress states (scanning, connecting, reconnecting) reuse
/// `AppColors.warning`, the accent at full strength repurposed for
/// "needs a look," not a fourth ad hoc color for "in between."
class BandStatusPresentation {
  const BandStatusPresentation({required this.label, required this.icon, required this.color});

  final String label;
  final IconData icon;
  final Color color;

  factory BandStatusPresentation.of(BandLinkState state) {
    switch (state) {
      case BandLinkState.connected:
        return const BandStatusPresentation(label: 'Connected', icon: AppIcons.bluetooth, color: AppColors.success);
      case BandLinkState.reconnecting:
        return const BandStatusPresentation(label: 'Reconnecting…', icon: AppIcons.bluetooth, color: AppColors.warning);
      case BandLinkState.scanning:
        return const BandStatusPresentation(label: 'Scanning…', icon: AppIcons.bluetooth, color: AppColors.warning);
      case BandLinkState.connecting:
        return const BandStatusPresentation(label: 'Connecting…', icon: AppIcons.bluetooth, color: AppColors.warning);
      case BandLinkState.bluetoothUnavailable:
        return const BandStatusPresentation(label: 'Bluetooth off', icon: AppIcons.bluetoothOff, color: AppColors.textMuted);
      case BandLinkState.permissionDenied:
        return const BandStatusPresentation(label: 'Permission needed', icon: AppIcons.bluetoothOff, color: AppColors.textMuted);
      case BandLinkState.idle:
        return const BandStatusPresentation(label: 'Offline', icon: AppIcons.bluetoothOff, color: AppColors.textMuted);
    }
  }
}
