import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/band_controller.dart';
import '../../core/utils/band_status_presentation.dart';
import '../../core/utils/date_format.dart';
import '../../core/utils/navigation.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/labeled_stat.dart';
import '../../core/widgets/pressable_scale.dart';
import '../../core/widgets/status_chip.dart';
import '../onboarding/band_pairing_screen.dart';
import 'widgets/forget_band_sheet.dart';

class BandDetailScreen extends StatelessWidget {
  const BandDetailScreen({super.key});

  Future<void> _forget(BuildContext context, BandController band) async {
    final confirmed = await showForgetBandSheet(context);
    if (confirmed) await band.forget();
  }

  Future<void> _reconnect(BuildContext context) async {
    await pushOnce(context, (_) => const BandPairingScreen());
  }

  @override
  Widget build(BuildContext context) {
    final band = context.watch<BandController>();
    final status = BandStatusPresentation.of(band.state);
    final hasPairedDevice = band.state == BandLinkState.connected || band.state == BandLinkState.reconnecting;
    final hasReading = band.lastSeenAt != null;
    final valueColor = band.readingsAreStale ? AppColors.textMuted : AppColors.textPrimary;

    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const SizedBox(height: AppTheme.spaceS),
              PressableScale(
                onTap: () => Navigator.of(context).pop(),
                semanticLabel: 'Back',
                child: Container(
                  width: AppTheme.minTapTarget,
                  height: AppTheme.minTapTarget,
                  alignment: Alignment.centerLeft,
                  child: const Icon(AppIcons.chevronBack, size: AppTheme.iconL, color: AppColors.textSecondary),
                ),
              ),
              const SizedBox(height: AppTheme.spaceL),
              Center(
                child: Column(
                  children: [
                    Container(
                      width: 88,
                      height: 88,
                      alignment: Alignment.center,
                      decoration: BoxDecoration(
                        color: AppColors.bgSurface,
                        shape: BoxShape.circle,
                        border: Border.all(color: band.connected ? AppColors.goldBorder : AppColors.bgHairline, width: 1.5),
                      ),
                      child: Icon(AppIcons.moon, size: AppTheme.iconXl, color: band.connected ? AppColors.gold : AppColors.textMuted),
                    ),
                    const SizedBox(height: AppTheme.spaceL),
                    Text(band.deviceName, style: AppTypography.titleL()),
                    const SizedBox(height: AppTheme.spaceS),
                    StatusChip(
                      label: status.label,
                      icon: status.icon,
                      color: status.color,
                      tint: band.connected ? AppColors.successTint : AppColors.bgHairline,
                    ),
                    if (band.readingsAreStale && band.lastSeenAt != null) ...[
                      const SizedBox(height: AppTheme.spaceS),
                      Text('Last seen ${relativeTimeAgoLabel(band.lastSeenAt!)}', style: AppTypography.bodyS()),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              Container(
                padding: const EdgeInsets.all(AppTheme.spaceL),
                decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
                child: Row(
                  children: [
                    Expanded(
                      child: LabeledStat(
                        label: 'Battery',
                        value: hasReading ? '${band.battery}%' : 'No data',
                        valueStyle: AppTypography.titleM(color: valueColor),
                      ),
                    ),
                    Expanded(
                      child: LabeledStat(
                        label: 'Resting HR',
                        value: hasReading ? '${band.restingHr} bpm' : 'No data',
                        valueStyle: AppTypography.titleM(color: valueColor),
                      ),
                    ),
                    Expanded(child: LabeledStat(label: 'Firmware', value: '1.2.0', valueStyle: AppTypography.titleM())),
                  ],
                ),
              ),
              const SizedBox(height: AppTheme.spaceL),
              Text(
                'Umbra works standalone even when the app is closed. Reminders keep firing on schedule whether the band is connected here or not.',
                style: AppTypography.bodyS(),
              ),
              const Spacer(),
              if (hasPairedDevice)
                AppButton(
                  label: 'Forget this band',
                  variant: AppButtonVariant.secondary,
                  onTap: () => _forget(context, band),
                )
              else
                AppButton(label: 'Reconnect', icon: AppIcons.bluetooth, onTap: () => _reconnect(context)),
              const SizedBox(height: AppTheme.spaceL),
            ],
          ),
        ),
      ),
    );
  }
}
