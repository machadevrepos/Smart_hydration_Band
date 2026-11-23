import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/models/session_record.dart';
import '../../core/state/session_store.dart';
import '../../core/widgets/labeled_stat.dart';
import 'widgets/rating_trend_chart.dart';

const int _kMinRatedSessionsForPattern = 3;
const Duration _kShortNightThreshold = Duration(hours: 2);

class InsightsScreen extends StatelessWidget {
  const InsightsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final history = context.watch<SessionStore>().history;
    final rated = history.where((s) => s.checkInRating != null).toList().reversed.toList(); // oldest first

    return SafeArea(
      bottom: false,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
        child: ListView(
          padding: const EdgeInsets.only(top: AppTheme.spaceM, bottom: AppTheme.spaceXxxl),
          children: [
            Text('Insights', style: AppTypography.titleL()),
            const SizedBox(height: AppTheme.spaceXs),
            Text('Patterns from your tracked nights.', style: AppTypography.bodyM()),
            const SizedBox(height: AppTheme.spaceXxl),
            if (rated.length < _kMinRatedSessionsForPattern)
              _NotEnoughData(rated: rated.length, needed: _kMinRatedSessionsForPattern)
            else ...[
              _SummaryRow(rated: rated),
              const SizedBox(height: AppTheme.spaceXxxl),
              Text('MORNING RATING', style: AppTypography.labelS()),
              const SizedBox(height: AppTheme.spaceM),
              Container(
                padding: const EdgeInsets.all(AppTheme.spaceL),
                decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
                child: RatingTrendChart(sessions: rated.length > 8 ? rated.sublist(rated.length - 8) : rated),
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              Text('PATTERN', style: AppTypography.labelS()),
              const SizedBox(height: AppTheme.spaceM),
              _DurationPatternCard(rated: rated),
            ],
          ],
        ),
      ),
    );
  }
}

class _SummaryRow extends StatelessWidget {
  const _SummaryRow({required this.rated});
  final List<SessionRecord> rated;

  @override
  Widget build(BuildContext context) {
    final avg = rated.map((s) => s.checkInRating!).reduce((a, b) => a + b) / rated.length;
    final best = rated.map((s) => s.checkInRating!).reduce((a, b) => a > b ? a : b);

    return Container(
      padding: const EdgeInsets.all(AppTheme.spaceL),
      decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
      child: Row(
        children: [
          Expanded(child: LabeledStat(label: 'Nights tracked', value: '${rated.length}')),
          Expanded(child: LabeledStat(label: 'Avg rating', value: '${avg.toStringAsFixed(1)}/10')),
          Expanded(child: LabeledStat(label: 'Best morning', value: '$best/10')),
        ],
      ),
    );
  }
}

class _DurationPatternCard extends StatelessWidget {
  const _DurationPatternCard({required this.rated});
  final List<SessionRecord> rated;

  @override
  Widget build(BuildContext context) {
    final shorter = rated.where((s) => s.duration < _kShortNightThreshold).toList();
    final longer = rated.where((s) => s.duration >= _kShortNightThreshold).toList();

    if (shorter.isEmpty || longer.isEmpty) {
      return Container(
        padding: const EdgeInsets.all(AppTheme.spaceL),
        decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
        child: Row(
          children: [
            const Icon(AppIcons.sparkle, size: AppTheme.iconM, color: AppColors.textMuted),
            const SizedBox(width: AppTheme.spaceM),
            Expanded(child: Text('Track a wider mix of night lengths to see a pattern here.', style: AppTypography.bodyM())),
          ],
        ),
      );
    }

    double avgOf(List<SessionRecord> list) => list.map((s) => s.checkInRating!).reduce((a, b) => a + b) / list.length;
    final shorterAvg = avgOf(shorter);
    final longerAvg = avgOf(longer);
    final hours = _kShortNightThreshold.inHours;

    return Container(
      padding: const EdgeInsets.all(AppTheme.spaceL),
      decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Icon(AppIcons.sparkle, size: AppTheme.iconM, color: AppColors.gold),
          const SizedBox(width: AppTheme.spaceM),
          Expanded(
            child: Text(
              'Nights under $hours hours score noticeably better the next morning: '
              '${shorterAvg.toStringAsFixed(1)}/10 average, versus ${longerAvg.toStringAsFixed(1)}/10 on longer nights.',
              style: AppTypography.bodyM(color: AppColors.textPrimary),
            ),
          ),
        ],
      ),
    );
  }
}

class _NotEnoughData extends StatelessWidget {
  const _NotEnoughData({required this.rated, required this.needed});
  final int rated;
  final int needed;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(top: AppTheme.spaceXxxl),
      child: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(AppIcons.insights, size: AppTheme.iconXl, color: AppColors.textMuted),
            const SizedBox(height: AppTheme.spaceL),
            Text('Patterns take a few mornings to show.', style: AppTypography.bodyL(color: AppColors.textSecondary)),
            const SizedBox(height: AppTheme.spaceXs),
            Text('${needed - rated} more morning check-in${needed - rated == 1 ? '' : 's'} to go.', style: AppTypography.bodyS()),
          ],
        ),
      ),
    );
  }
}
