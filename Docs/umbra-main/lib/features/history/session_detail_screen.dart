import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/models/hydration_mode.dart';
import '../../core/state/session_store.dart';
import '../../core/utils/date_format.dart';
import '../../core/utils/navigation.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/labeled_stat.dart';
import '../../core/widgets/pressable_scale.dart';
import '../checkin/morning_checkin_screen.dart';

/// Reads the session live from [SessionStore] by id rather than taking a
/// snapshot, so attaching a check-in rating from this screen is reflected
/// immediately without a stale local copy to keep in sync.
class SessionDetailScreen extends StatelessWidget {
  const SessionDetailScreen({super.key, required this.sessionId});

  final String sessionId;

  Future<void> _rate(BuildContext context, String durationLabel, int reminderCount) async {
    final rating = await pushOnce<int>(
      context,
      (_) => MorningCheckinScreen(sessionSummary: 'Last night: Night session, $durationLabel, $reminderCount reminders'),
    );
    if (rating != null && context.mounted) {
      context.read<SessionStore>().attachCheckIn(sessionId, rating);
    }
  }

  @override
  Widget build(BuildContext context) {
    final store = context.watch<SessionStore>();
    final matches = store.history.where((s) => s.id == sessionId);
    final session = matches.isEmpty ? null : matches.first;

    if (session == null) {
      // The record could vanish if history is ever pruned while this
      // screen is open; back out rather than render a broken detail view.
      return const Scaffold(body: SizedBox.shrink());
    }

    final isNight = session.mode == HydrationMode.night;

    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const SizedBox(height: AppTheme.spaceS),
              PressableScale(
                onTap: () => Navigator.of(context).pop(),
                semanticLabel: 'Back to History',
                child: Container(
                  width: AppTheme.minTapTarget,
                  height: AppTheme.minTapTarget,
                  alignment: Alignment.centerLeft,
                  child: const Icon(AppIcons.chevronBack, size: AppTheme.iconL, color: AppColors.textSecondary),
                ),
              ),
              const SizedBox(height: AppTheme.spaceL),
              Row(
                children: [
                  Icon(isNight ? AppIcons.moon : AppIcons.sun, size: AppTheme.iconL, color: AppColors.gold),
                  const SizedBox(width: AppTheme.spaceM),
                  Text(isNight ? 'Night session' : 'Daily tracking', style: AppTypography.titleL()),
                ],
              ),
              const SizedBox(height: AppTheme.spaceS),
              Text(
                '${relativeDayLabel(session.startedAt)}, ${shortTimeLabel(session.startedAt)} to ${shortTimeLabel(session.endedAt)}',
                style: AppTypography.bodyM(),
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              Container(
                padding: const EdgeInsets.all(AppTheme.spaceL),
                decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
                child: Row(
                  children: [
                    Expanded(child: LabeledStat(label: 'Avg HR', value: '${session.avgHr} bpm', valueStyle: AppTypography.titleM())),
                    Expanded(child: LabeledStat(label: 'Duration', value: session.durationLabel, valueStyle: AppTypography.titleM())),
                    Expanded(child: LabeledStat(label: 'Reminders', value: '${session.reminderCount}', valueStyle: AppTypography.titleM())),
                  ],
                ),
              ),
              const SizedBox(height: AppTheme.spaceM),
              Container(
                padding: const EdgeInsets.all(AppTheme.spaceL),
                decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
                child: Row(
                  children: [
                    Expanded(child: LabeledStat(label: 'Avg HRV', value: '${session.avgHrv} ms', valueStyle: AppTypography.titleM())),
                    Expanded(child: LabeledStat(label: 'Avg SpO2', value: '${session.avgSpo2}%', valueStyle: AppTypography.titleM())),
                    Expanded(child: LabeledStat(label: 'Motion', value: session.motion, valueStyle: AppTypography.titleM())),
                  ],
                ),
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              Text('MORNING CHECK-IN', style: AppTypography.labelS()),
              const SizedBox(height: AppTheme.spaceM),
              if (session.checkInRating != null)
                Container(
                  padding: const EdgeInsets.all(AppTheme.spaceL),
                  decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
                  child: Row(
                    children: [
                      const Icon(AppIcons.sparkle, size: AppTheme.iconM, color: AppColors.gold),
                      const SizedBox(width: AppTheme.spaceM),
                      Text('${session.checkInRating}/10', style: AppTypography.titleM()),
                      const SizedBox(width: AppTheme.spaceS),
                      Text('felt that morning', style: AppTypography.bodyM()),
                    ],
                  ),
                )
              else
                AppButton(
                  label: 'Rate this night',
                  variant: AppButtonVariant.secondary,
                  onTap: () => _rate(context, session.durationLabel, session.reminderCount),
                ),
              const SizedBox(height: AppTheme.spaceL),
            ],
          ),
        ),
      ),
    );
  }
}
