import 'package:flutter/material.dart';
import '../constants/app_colors.dart';
import '../constants/app_theme.dart';
import 'pressable_scale.dart';

/// The app's one toggle primitive: a pill track with a sliding knob,
/// styled from the same selection language as `ModeCard`/rating circles
/// (tint + border when on) rather than Material's default `Switch`, so a
/// toggle feels like it belongs to the same system as everything else.
class AppSwitch extends StatelessWidget {
  const AppSwitch({super.key, required this.value, required this.onChanged, this.semanticLabel});

  final bool value;
  final ValueChanged<bool> onChanged;
  final String? semanticLabel;

  static const double _width = 46;
  static const double _height = 26;
  static const double _knobSize = 20;
  static const double _padding = 3;

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;
    final duration = reduceMotion ? Duration.zero : AppTheme.animFast;

    return PressableScale(
      onTap: () => onChanged(!value),
      hapticOnTap: true,
      semanticLabel: semanticLabel,
      child: AnimatedContainer(
        duration: duration,
        curve: AppTheme.motionCurve,
        width: _width,
        height: _height,
        padding: const EdgeInsets.all(_padding),
        decoration: BoxDecoration(
          color: value ? AppColors.goldTint : AppColors.bgHairline,
          borderRadius: BorderRadius.circular(AppTheme.radiusPill),
          border: Border.all(color: value ? AppColors.goldBorder : Colors.transparent, width: 1.5),
        ),
        child: AnimatedAlign(
          duration: duration,
          curve: AppTheme.motionCurve,
          alignment: value ? Alignment.centerRight : Alignment.centerLeft,
          child: Container(
            width: _knobSize,
            height: _knobSize,
            decoration: BoxDecoration(
              color: value ? AppColors.gold : AppColors.textSecondary,
              shape: BoxShape.circle,
            ),
          ),
        ),
      ),
    );
  }
}
