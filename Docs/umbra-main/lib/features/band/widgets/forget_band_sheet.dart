import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/widgets/app_button.dart';

/// Forgetting the band is reversible (re-pairing takes a minute) and
/// doesn't touch tracked history, so a one-tap confirm is proportionate.
/// See CLAUDE.md "Destructive actions confirm proportionally to their
/// severity": this isn't the "type the band's name" tier.
Future<bool> showForgetBandSheet(BuildContext context) async {
  final result = await showModalBottomSheet<bool>(
    context: context,
    backgroundColor: Colors.transparent,
    builder: (context) => const _ForgetBandSheet(),
  );
  return result ?? false;
}

class _ForgetBandSheet extends StatelessWidget {
  const _ForgetBandSheet();

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).padding.bottom;

    return Container(
      decoration: const BoxDecoration(
        color: AppColors.bgSurfaceRaised,
        borderRadius: BorderRadius.vertical(top: Radius.circular(AppTheme.radiusL)),
      ),
      padding: EdgeInsets.only(
        left: AppTheme.screenPaddingH,
        right: AppTheme.screenPaddingH,
        top: AppTheme.spaceM,
        bottom: bottomInset + AppTheme.spaceL,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Center(
            child: Container(
              width: 36,
              height: 4,
              decoration: BoxDecoration(color: AppColors.bgHairline, borderRadius: BorderRadius.circular(AppTheme.radiusS)),
            ),
          ),
          const SizedBox(height: AppTheme.spaceL),
          Text('Forget this band?', style: AppTypography.titleM()),
          const SizedBox(height: AppTheme.spaceXs),
          Text(
            'Umbra will stop syncing live readings. Your tracked history stays right where it is, and you can re-pair any time. The band keeps buzzing on schedule either way.',
            style: AppTypography.bodyM(),
          ),
          const SizedBox(height: AppTheme.spaceXl),
          AppButton(label: 'Forget band', onTap: () => Navigator.of(context).pop(true)),
          const SizedBox(height: AppTheme.spaceM),
          AppButton(
            label: 'Cancel',
            variant: AppButtonVariant.secondary,
            onTap: () => Navigator.of(context).pop(false),
          ),
        ],
      ),
    );
  }
}
