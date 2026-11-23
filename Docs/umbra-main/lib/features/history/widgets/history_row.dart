import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_icons.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/models/hydration_mode.dart';
import '../../../core/models/session_record.dart';
import '../../../core/utils/date_format.dart';
import '../../../core/widgets/pressable_scale.dart';

class HistoryRow extends StatelessWidget {
  const HistoryRow({super.key, required this.session, required this.onTap});

  final SessionRecord session;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final isNight = session.mode == HydrationMode.night;
    final title = isNight ? 'Night session' : 'Daily tracking';

    return PressableScale(
      onTap: onTap,
      semanticLabel: '$title, ${relativeDayLabel(session.startedAt)}',
      child: Container(
        padding: const EdgeInsets.all(AppTheme.spaceL),
        decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(isNight ? AppIcons.moon : AppIcons.sun, size: AppTheme.iconM, color: AppColors.textSecondary),
                const SizedBox(width: AppTheme.spaceS),
                Expanded(child: Text(title, style: AppTypography.titleS())),
                Text(relativeDayLabel(session.startedAt), style: AppTypography.bodyS()),
                const SizedBox(width: AppTheme.spaceXs),
                const Icon(AppIcons.chevronForward, size: AppTheme.iconS, color: AppColors.textMuted),
              ],
            ),
            const SizedBox(height: AppTheme.spaceXs),
            Text(
              '${session.avgHr} bpm avg · ${session.durationLabel} · ${session.reminderCount} reminders',
              style: AppTypography.bodyS(color: AppColors.textSecondary),
            ),
            if (session.checkInRating != null) ...[
              const SizedBox(height: AppTheme.spaceXs),
              Row(
                children: [
                  const Icon(AppIcons.sparkle, size: AppTheme.iconS, color: AppColors.textMuted),
                  const SizedBox(width: AppTheme.spaceXs),
                  Text('Felt ${session.checkInRating}/10', style: AppTypography.bodyS()),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}
