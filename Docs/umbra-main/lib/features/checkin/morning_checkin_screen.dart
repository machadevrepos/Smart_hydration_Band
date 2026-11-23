import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/settings_controller.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/pressable_scale.dart';

/// The single morning prompt the whole app sends: one notification, one
/// question, no follow-ups. See side_notes.md, "Just one notification.
/// Don't add more prompts than this." Reachable either by tapping that 9am
/// notification (TODO: wire once flutter_local_notifications lands) or,
/// as a fallback for a missed/dismissed notification, from Home's last-
/// session card.
class MorningCheckinScreen extends StatefulWidget {
  const MorningCheckinScreen({super.key, required this.sessionSummary});

  final String sessionSummary;

  @override
  State<MorningCheckinScreen> createState() => _MorningCheckinScreenState();
}

class _MorningCheckinScreenState extends State<MorningCheckinScreen> {
  int? _rating;
  bool _submitted = false;

  Future<void> _submit() async {
    if (_rating == null) return;
    if (context.read<SettingsController>().hapticsEnabled) {
      HapticFeedback.selectionClick();
    }
    setState(() => _submitted = true);
    await Future.delayed(AppTheme.animSlow);
    if (mounted) Navigator.of(context).pop(_rating);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
          child: _submitted ? const _SubmittedConfirmation() : _buildForm(context),
        ),
      ),
    );
  }

  Widget _buildForm(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.end,
          children: [
            PressableScale(
              onTap: () => Navigator.of(context).pop(),
              semanticLabel: 'Not now',
              child: Container(
                height: AppTheme.minTapTarget,
                padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM),
                alignment: Alignment.center,
                child: Text('Not now', style: AppTypography.labelL(color: AppColors.textMuted)),
              ),
            ),
          ],
        ),
        const SizedBox(height: AppTheme.spaceXl),
        Text('MORNING CHECK-IN', style: AppTypography.labelS()),
        const SizedBox(height: AppTheme.spaceS),
        Text('How do you feel?', style: AppTypography.titleL()),
        const SizedBox(height: AppTheme.spaceS),
        Text(widget.sessionSummary, style: AppTypography.bodyM()),
        const SizedBox(height: AppTheme.spaceXxxl),
        _RatingGrid(value: _rating, onChanged: (v) => setState(() => _rating = v)),
        const Spacer(),
        AppButton(label: 'Save', onTap: _rating == null ? null : _submit),
        const SizedBox(height: AppTheme.spaceL),
      ],
    );
  }
}

class _RatingGrid extends StatelessWidget {
  const _RatingGrid({required this.value, required this.onChanged});
  final int? value;
  final ValueChanged<int> onChanged;

  Widget _row(List<int> numbers) {
    return Row(
      children: [
        for (final n in numbers) ...[
          if (n != numbers.first) const SizedBox(width: AppTheme.spaceS),
          Expanded(child: _RatingCircle(number: n, selected: value == n, onTap: () => onChanged(n))),
        ],
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        _row(const [1, 2, 3, 4, 5]),
        const SizedBox(height: AppTheme.spaceS),
        _row(const [6, 7, 8, 9, 10]),
        const SizedBox(height: AppTheme.spaceL),
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text('Rough night', style: AppTypography.bodyS()),
            Text('Felt great', style: AppTypography.bodyS()),
          ],
        ),
      ],
    );
  }
}

class _RatingCircle extends StatelessWidget {
  const _RatingCircle({required this.number, required this.selected, required this.onTap});
  final int number;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;

    return PressableScale(
      onTap: onTap,
      hapticOnTap: true,
      semanticLabel: 'Rate $number out of 10${selected ? ', selected' : ''}',
      child: AnimatedContainer(
        duration: reduceMotion ? Duration.zero : AppTheme.animFast,
        curve: AppTheme.motionCurve,
        height: 44,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: selected ? AppColors.goldTint : AppColors.bgSurface,
          shape: BoxShape.circle,
          border: Border.all(color: selected ? AppColors.goldBorder : AppColors.bgHairline, width: selected ? 1.5 : 1),
        ),
        child: Text(
          '$number',
          style: AppTypography.labelL(color: selected ? AppColors.textPrimary : AppColors.textSecondary),
        ),
      ),
    );
  }
}

class _SubmittedConfirmation extends StatelessWidget {
  const _SubmittedConfirmation();

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;

    return Center(
      child: TweenAnimationBuilder<double>(
        tween: Tween(begin: 0.85, end: 1.0),
        duration: reduceMotion ? Duration.zero : AppTheme.animNormal,
        curve: AppTheme.motionCurve,
        builder: (context, scale, child) => Transform.scale(scale: scale, child: child),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 72,
              height: 72,
              alignment: Alignment.center,
              decoration: BoxDecoration(color: AppColors.goldTint, shape: BoxShape.circle, border: Border.all(color: AppColors.gold, width: 1.5)),
              child: const Icon(AppIcons.check, size: AppTheme.iconXl, color: AppColors.gold),
            ),
            const SizedBox(height: AppTheme.spaceL),
            Text('Noted.', style: AppTypography.titleM()),
          ],
        ),
      ),
    );
  }
}
