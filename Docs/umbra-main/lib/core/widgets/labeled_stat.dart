import 'package:flutter/material.dart';
import '../constants/app_theme.dart';
import '../constants/app_typography.dart';

/// A small uppercase label over a value, the recurring stat-tile pattern
/// used in session summaries and dashboards. Built once so every stat in
/// the app lines up the same way instead of being re-laid-out per screen.
class LabeledStat extends StatelessWidget {
  const LabeledStat({super.key, required this.label, required this.value, this.alignEnd = false, this.valueStyle});

  final String label;
  final String value;
  final bool alignEnd;

  /// Defaults to [AppTypography.titleS]; pass [AppTypography.titleM] where
  /// the stat needs to read as more prominent (e.g. a card's headline
  /// numbers vs. a secondary recap row).
  final TextStyle? valueStyle;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: alignEnd ? CrossAxisAlignment.end : CrossAxisAlignment.start,
      children: [
        Text(label.toUpperCase(), style: AppTypography.labelS()),
        const SizedBox(height: AppTheme.spaceHairline),
        Text(value, style: valueStyle ?? AppTypography.titleS()),
      ],
    );
  }
}
