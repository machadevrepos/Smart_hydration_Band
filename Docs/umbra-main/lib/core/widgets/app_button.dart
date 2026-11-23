import 'package:flutter/material.dart';
import '../constants/app_colors.dart';
import '../constants/app_theme.dart';
import '../constants/app_typography.dart';
import 'pressable_scale.dart';

enum AppButtonVariant { primary, secondary }

/// The app's one button primitive. [AppButtonVariant.primary] is solid gold
/// and reserved for the single highest-emphasis action on a screen. See
/// CLAUDE.md: "solid fills are for primary CTAs only, one or two per screen,
/// maximum." Everything else is [AppButtonVariant.secondary].
///
/// A `null` [onTap] renders a genuinely disabled button (dimmed, no press
/// feedback, no haptic) rather than a button that looks tappable but does
/// nothing when tapped. That gap between affordance and behavior is worse
/// than just showing the disabled state honestly.
class AppButton extends StatelessWidget {
  const AppButton({
    super.key,
    required this.label,
    required this.onTap,
    this.variant = AppButtonVariant.primary,
    this.icon,
    this.loading = false,
  });

  final String label;
  final VoidCallback? onTap;
  final AppButtonVariant variant;
  final IconData? icon;
  final bool loading;

  @override
  Widget build(BuildContext context) {
    final isPrimary = variant == AppButtonVariant.primary;
    final disabled = onTap == null;

    final fill = isPrimary ? (disabled ? AppColors.goldMuted : AppColors.gold) : AppColors.bgSurfaceRaised;
    final labelColor = isPrimary ? AppColors.textOnGold : (disabled ? AppColors.textMuted : AppColors.textPrimary);

    return PressableScale(
      onTap: loading ? null : onTap,
      hapticOnTap: true,
      semanticLabel: label,
      child: AnimatedContainer(
        duration: AppTheme.animFast,
        curve: AppTheme.motionCurve,
        width: double.infinity,
        height: AppTheme.minTapTarget + 8,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: fill,
          borderRadius: BorderRadius.circular(AppTheme.radiusPill),
          border: isPrimary ? null : Border.all(color: AppColors.bgHairline),
          boxShadow: isPrimary && !disabled ? AppTheme.goldGlow() : null,
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (loading) ...[
              SizedBox(
                width: AppTheme.iconM,
                height: AppTheme.iconM,
                child: CircularProgressIndicator(strokeWidth: 2, color: labelColor),
              ),
              const SizedBox(width: AppTheme.spaceS),
            ] else if (icon != null) ...[
              Icon(icon, size: AppTheme.iconM, color: labelColor),
              const SizedBox(width: AppTheme.spaceS),
            ],
            Text(label, style: AppTypography.labelL(color: labelColor)),
          ],
        ),
      ),
    );
  }
}
