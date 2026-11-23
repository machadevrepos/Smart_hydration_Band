import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/widgets/app_button.dart';

/// A quick one-tap confirm, not a typed confirmation, because ending a
/// session is reversible and low-stakes: it saves what was tracked and
/// stops reminders, it doesn't delete anything. See CLAUDE.md "Destructive
/// actions confirm proportionally to their severity."
Future<bool> showEndSessionSheet(BuildContext context, {required String elapsedLabel}) async {
  final result = await showModalBottomSheet<bool>(
    context: context,
    backgroundColor: Colors.transparent,
    builder: (context) => _EndSessionSheet(elapsedLabel: elapsedLabel),
  );
  return result ?? false;
}

class _EndSessionSheet extends StatelessWidget {
  const _EndSessionSheet({required this.elapsedLabel});
  final String elapsedLabel;

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
          Text('End session?', style: AppTypography.titleM()),
          const SizedBox(height: AppTheme.spaceXs),
          Text(
            'You\'ve been tracking for $elapsedLabel. It\'ll be saved to your history.',
            style: AppTypography.bodyM(),
          ),
          const SizedBox(height: AppTheme.spaceXl),
          AppButton(label: 'End session', onTap: () => Navigator.of(context).pop(true)),
          const SizedBox(height: AppTheme.spaceM),
          AppButton(
            label: 'Keep going',
            variant: AppButtonVariant.secondary,
            onTap: () => Navigator.of(context).pop(false),
          ),
        ],
      ),
    );
  }
}
