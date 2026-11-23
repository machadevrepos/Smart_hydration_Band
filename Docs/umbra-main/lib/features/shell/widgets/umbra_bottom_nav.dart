import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_icons.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/widgets/pressable_scale.dart';

class _NavItem {
  const _NavItem(this.icon, this.label);
  final IconData icon;
  final String label;
}

const List<_NavItem> _kNavItems = [
  _NavItem(AppIcons.home, 'Home'),
  _NavItem(AppIcons.history, 'History'),
  _NavItem(AppIcons.insights, 'Insights'),
  _NavItem(AppIcons.settings, 'Settings'),
];

class UmbraBottomNav extends StatelessWidget {
  const UmbraBottomNav({super.key, required this.activeIndex, required this.onTap});

  final int activeIndex;
  final ValueChanged<int> onTap;

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).padding.bottom;

    return Container(
      padding: EdgeInsets.only(top: AppTheme.spaceS, bottom: bottomInset + AppTheme.spaceXs),
      decoration: const BoxDecoration(
        color: AppColors.bgSurfaceRaised,
        border: Border(top: BorderSide(color: AppColors.bgHairline)),
      ),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceEvenly,
        children: List.generate(_kNavItems.length, (i) {
          final item = _kNavItems[i];
          final active = i == activeIndex;
          final color = active ? AppColors.gold : AppColors.textMuted;

          return PressableScale(
            onTap: () => onTap(i),
            semanticLabel: item.label,
            child: ConstrainedBox(
              constraints: const BoxConstraints(minWidth: AppTheme.minTapTarget, minHeight: AppTheme.minTapTarget),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(item.icon, size: AppTheme.iconL, color: color),
                  const SizedBox(height: 3),
                  Text(item.label, style: AppTypography.labelS(color: color)),
                ],
              ),
            ),
          );
        }),
      ),
    );
  }
}
