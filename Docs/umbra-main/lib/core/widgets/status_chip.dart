import 'package:flutter/material.dart';
import '../constants/app_theme.dart';
import '../constants/app_typography.dart';

/// Tinted status/category chip: fill is the semantic color at ~12%
/// opacity, full-strength color for icon + text. Never a solid fill; solid
/// fills are reserved for primary CTAs. Icon is mandatory, not optional:
/// color alone can't carry the state (CLAUDE.md accessibility rule).
class StatusChip extends StatelessWidget {
  const StatusChip({
    super.key,
    required this.label,
    required this.icon,
    required this.color,
    required this.tint,
  });

  final String label;
  final IconData icon;
  final Color color;
  final Color tint;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM, vertical: AppTheme.spaceXs),
      decoration: BoxDecoration(
        color: tint,
        borderRadius: BorderRadius.circular(AppTheme.radiusPill),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: AppTheme.iconS, color: color),
          const SizedBox(width: AppTheme.spaceXs),
          Text(label, style: AppTypography.labelM(color: color)),
        ],
      ),
    );
  }
}
