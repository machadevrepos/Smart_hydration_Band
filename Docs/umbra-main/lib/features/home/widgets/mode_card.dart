import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_icons.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/widgets/pressable_scale.dart';

/// One half of the Daily/Night mode segmented selector. Selected state is
/// communicated three ways at once: tint fill, border, and a checkmark
/// badge. Never color alone (CLAUDE.md accessibility rule).
class ModeCard extends StatelessWidget {
  const ModeCard({
    super.key,
    required this.icon,
    required this.label,
    required this.descriptor,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final String descriptor;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.maybeOf(context)?.disableAnimations ?? false;
    final duration = reduceMotion ? Duration.zero : AppTheme.animFast;

    return PressableScale(
      onTap: onTap,
      hapticOnTap: true,
      semanticLabel: '$label mode${selected ? ', selected' : ''}',
      child: AnimatedContainer(
        duration: duration,
        curve: AppTheme.motionCurve,
        padding: const EdgeInsets.all(AppTheme.spaceL),
        decoration: BoxDecoration(
          color: selected ? AppColors.goldTint : AppColors.bgSurface,
          borderRadius: BorderRadius.circular(AppTheme.radiusM),
          border: Border.all(
            color: selected ? AppColors.goldBorder : AppColors.bgHairline,
            width: selected ? 1.5 : 1,
          ),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Icon(icon, size: AppTheme.iconL, color: selected ? AppColors.gold : AppColors.textSecondary),
                AnimatedOpacity(
                  duration: duration,
                  opacity: selected ? 1 : 0,
                  child: Container(
                    width: 18,
                    height: 18,
                    decoration: const BoxDecoration(color: AppColors.gold, shape: BoxShape.circle),
                    child: const Icon(AppIcons.check, size: 11, color: AppColors.textOnGold),
                  ),
                ),
              ],
            ),
            const SizedBox(height: AppTheme.spaceM),
            Text(label, style: AppTypography.titleS(color: selected ? AppColors.textPrimary : AppColors.textSecondary)),
            const SizedBox(height: 2),
            Text(
              descriptor,
              style: AppTypography.bodyS(),
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
            ),
          ],
        ),
      ),
    );
  }
}
