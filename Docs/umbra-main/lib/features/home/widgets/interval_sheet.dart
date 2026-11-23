import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_icons.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/widgets/pressable_scale.dart';

const List<int> kIntervalOptions = [20, 30, 45, 60];

/// Presents the interval picker as a modal sheet and resolves to the chosen
/// minute value, or null if dismissed without a change.
Future<int?> showIntervalSheet(BuildContext context, {required int current}) {
  return showModalBottomSheet<int>(
    context: context,
    backgroundColor: Colors.transparent,
    isScrollControlled: true,
    builder: (context) => _IntervalSheet(current: current),
  );
}

class _IntervalSheet extends StatelessWidget {
  const _IntervalSheet({required this.current});

  final int current;

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
          Text('Reminder interval', style: AppTypography.titleM()),
          const SizedBox(height: AppTheme.spaceXs),
          Text('How often the band buzzes.', style: AppTypography.bodyM()),
          const SizedBox(height: AppTheme.spaceL),
          ...List.generate(kIntervalOptions.length, (i) {
            final minutes = kIntervalOptions[i];
            final selected = minutes == current;
            return Column(
              children: [
                PressableScale(
                  onTap: () => Navigator.of(context).pop(minutes),
                  hapticOnTap: true,
                  semanticLabel: '$minutes minutes${selected ? ', selected' : ''}',
                  child: Container(
                    height: AppTheme.minTapTarget,
                    alignment: Alignment.center,
                    child: Row(
                      children: [
                        Expanded(
                          child: Text(
                            'Every $minutes minutes',
                            style: AppTypography.bodyL(color: selected ? AppColors.textPrimary : AppColors.textSecondary),
                          ),
                        ),
                        if (selected) const Icon(AppIcons.check, size: AppTheme.iconM, color: AppColors.gold),
                      ],
                    ),
                  ),
                ),
                if (i != kIntervalOptions.length - 1) const Divider(height: 1, color: AppColors.bgHairline),
              ],
            );
          }),
        ],
      ),
    );
  }
}
