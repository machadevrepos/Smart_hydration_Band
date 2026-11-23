import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';
import '../../../core/constants/app_theme.dart';
import '../../../core/constants/app_typography.dart';
import '../../../core/models/session_record.dart';
import '../../../core/utils/date_format.dart';
import '../../../core/widgets/pressable_scale.dart';

/// A single-series bar chart of morning ratings, oldest to newest, left to
/// right. One series needs no legend, the section header above it names
/// it. Exactly one bar is ever highlighted (defaults to the most recent)
/// and only that bar carries a direct value label, replacing hover with a
/// tap on mobile rather than labeling every bar.
class RatingTrendChart extends StatefulWidget {
  const RatingTrendChart({super.key, required this.sessions});

  /// Oldest first, must all have a non-null [SessionRecord.checkInRating].
  final List<SessionRecord> sessions;

  @override
  State<RatingTrendChart> createState() => _RatingTrendChartState();
}

class _RatingTrendChartState extends State<RatingTrendChart> {
  late int _selected = widget.sessions.length - 1;

  static const double _barAreaHeight = 120;

  @override
  Widget build(BuildContext context) {
    final selectedSession = widget.sessions[_selected];

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text('${selectedSession.checkInRating}/10', style: AppTypography.titleM(color: AppColors.gold)),
            Text(relativeDayLabel(selectedSession.startedAt), style: AppTypography.bodyS()),
          ],
        ),
        const SizedBox(height: AppTheme.spaceM),
        SizedBox(
          height: _barAreaHeight,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: List.generate(widget.sessions.length, (i) {
              final session = widget.sessions[i];
              final selected = i == _selected;
              final heightFactor = (session.checkInRating! / 10).clamp(0.04, 1.0);

              return Expanded(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceXs),
                  child: PressableScale(
                    onTap: () => setState(() => _selected = i),
                    semanticLabel: '${relativeDayLabel(session.startedAt)}, rated ${session.checkInRating} out of 10',
                    child: Align(
                      alignment: Alignment.bottomCenter,
                      child: FractionallySizedBox(
                        heightFactor: heightFactor,
                        widthFactor: 1,
                        child: AnimatedContainer(
                          duration: AppTheme.animFast,
                          curve: AppTheme.motionCurve,
                          decoration: BoxDecoration(
                            color: selected ? AppColors.gold : AppColors.bgHairline,
                            borderRadius: const BorderRadius.vertical(top: Radius.circular(AppTheme.radiusS)),
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              );
            }),
          ),
        ),
        const SizedBox(height: AppTheme.spaceS),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(relativeDayLabel(widget.sessions.first.startedAt), style: AppTypography.bodyS()),
            Text(relativeDayLabel(widget.sessions.last.startedAt), style: AppTypography.bodyS()),
          ],
        ),
      ],
    );
  }
}
