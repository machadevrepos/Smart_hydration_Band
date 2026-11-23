import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/session_controller.dart';
import '../../core/state/session_store.dart';
import '../../core/utils/date_format.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/labeled_stat.dart';
import '../../core/widgets/pressable_scale.dart';
import '../../core/widgets/radial_countdown.dart';
import '../../core/widgets/status_chip.dart';
import 'widgets/end_session_sheet.dart';

enum _HrZone { resting, elevated, high }

_HrZone _zoneFor(int bpm) {
  if (bpm < 90) return _HrZone.resting;
  if (bpm < 116) return _HrZone.elevated;
  return _HrZone.high;
}

/// Live view onto [SessionController]. The session itself lives one level
/// up and keeps running whether or not this screen is on screen, so
/// "back to Home, session keeps running" is actually true. This screen
/// starts a session if none is active yet, or simply resumes displaying
/// one that's already in progress.
class ActiveSessionScreen extends StatefulWidget {
  const ActiveSessionScreen({super.key, required this.intervalMinutes});

  final int intervalMinutes;

  @override
  State<ActiveSessionScreen> createState() => _ActiveSessionScreenState();
}

class _ActiveSessionScreenState extends State<ActiveSessionScreen> {
  // Resolved once, here, and reused everywhere below rather than calling
  // context.read<SessionController>() again in dispose() or in a listener
  // callback that might fire after this element has been deactivated by a
  // pop. Provider's own lookup still walks the element tree even with
  // listen: false, and that walk isn't safe once the element is inactive:
  // "Looking up a deactivated widget's ancestor is unsafe."
  late final SessionController _session;
  late int _lastSeenReminders;

  @override
  void initState() {
    super.initState();
    _session = context.read<SessionController>();
    _lastSeenReminders = _session.remindersFired;
    _session.addListener(_onSessionChanged);
    if (!_session.isActive) {
      // Starting fires notifyListeners(), which can't happen synchronously
      // while this element is still in its first build (Provider would try
      // to mark itself dirty mid-build and throw). Deferring to the post-
      // frame callback is a no-op for the resume path since only the
      // fresh-start branch reaches here at all.
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _session.startSession(intervalMinutes: widget.intervalMinutes);
      });
    }
  }

  @override
  void dispose() {
    _session.removeListener(_onSessionChanged);
    super.dispose();
  }

  // The ticker lives in SessionController and fires its haptic regardless
  // of whether this screen is mounted (that's the point). The SnackBar is
  // a visual bonus only shown while this screen happens to be the one
  // visible, detected here rather than assumed from a rebuild.
  void _onSessionChanged() {
    if (_session.remindersFired > _lastSeenReminders) {
      _lastSeenReminders = _session.remindersFired;
      if (mounted) {
        ScaffoldMessenger.of(context)
          ..hideCurrentSnackBar()
          ..showSnackBar(
            const SnackBar(
              content: Text('Reminder sent. Time to drink water.'),
            ),
          );
      }
    }
  }

  String _elapsedLabel(int elapsedSeconds) {
    final h = elapsedSeconds ~/ 3600;
    final m = (elapsedSeconds % 3600) ~/ 60;
    if (h > 0) return '${h}h ${m}m';
    final s = elapsedSeconds % 60;
    return m > 0 ? '${m}m ${s}s' : '${s}s';
  }

  String _countdownLabel(int secondsUntilReminder) {
    final m = secondsUntilReminder ~/ 60;
    final s = secondsUntilReminder % 60;
    return m > 0 ? '${m}m ${s}s' : '${s}s';
  }

  Future<void> _endSession() async {
    final confirmed = await showEndSessionSheet(
      context,
      elapsedLabel: _elapsedLabel(_session.elapsedSeconds),
    );
    if (!confirmed || !mounted) return;

    final record = _session.endSession();
    context.read<SessionStore>().addSession(record);
    if (mounted) Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final session = context.watch<SessionController>();
    final zone = _zoneFor(session.hr);
    final zoneVisual = switch (zone) {
      _HrZone.resting => (
        label: 'Resting',
        color: AppColors.success,
        tint: AppColors.successTint,
        icon: AppIcons.heart,
      ),
      _HrZone.elevated => (
        label: 'Elevated',
        color: AppColors.gold,
        tint: AppColors.goldTint,
        icon: AppIcons.heart,
      ),
      _HrZone.high => (
        label: 'High',
        color: AppColors.error,
        tint: AppColors.errorTint,
        icon: AppIcons.heart,
      ),
    };
    final ringProgress =
        1 -
        (session.secondsUntilReminder / SessionController.demoReminderSeconds);
    final startedAtLabel = session.startedAt == null
        ? ''
        : shortTimeLabel(session.startedAt!);

    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(
            horizontal: AppTheme.screenPaddingH,
          ),
          child: Column(
            children: [
              const SizedBox(height: AppTheme.spaceS),
              Row(
                children: [
                  PressableScale(
                    onTap: () => Navigator.of(context).pop(),
                    semanticLabel: 'Back to Home, session keeps running',
                    child: Container(
                      width: AppTheme.minTapTarget,
                      height: AppTheme.minTapTarget,
                      alignment: Alignment.centerLeft,
                      child: const Icon(
                        AppIcons.chevronBack,
                        size: AppTheme.iconL,
                        color: AppColors.textSecondary,
                      ),
                    ),
                  ),
                  Expanded(
                    child: Center(
                      child: _LiveModeBadge(
                        label:
                            'NIGHT MODE · EVERY ${session.intervalMinutes} MIN',
                      ),
                    ),
                  ),
                  const SizedBox(width: AppTheme.minTapTarget),
                ],
              ),
              const Spacer(),
              RadialCountdown(
                progress: ringProgress,
                size: 232,
                child: AnimatedSwitcher(
                  duration: AppTheme.animFast,
                  child: Column(
                    key: ValueKey(session.hr),
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Text(
                        '${session.hr}',
                        style: AppTypography.displayL(color: AppColors.gold),
                      ),
                      Text('BPM', style: AppTypography.labelS()),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: AppTheme.spaceXl),
              StatusChip(
                label: zoneVisual.label,
                icon: zoneVisual.icon,
                color: zoneVisual.color,
                tint: zoneVisual.tint,
              ),
              const SizedBox(height: AppTheme.spaceM),
              Text(
                'Next reminder in ${_countdownLabel(session.secondsUntilReminder)}',
                style: AppTypography.bodyM(),
              ),
              const SizedBox(height: AppTheme.spaceXs),
              Text(
                'Started $startedAtLabel, ${_elapsedLabel(session.elapsedSeconds)} elapsed',
                style: AppTypography.bodyS(),
              ),
              const Spacer(),
              Container(
                padding: const EdgeInsets.all(AppTheme.spaceL),
                decoration: BoxDecoration(
                  color: AppColors.bgSurface,
                  borderRadius: BorderRadius.circular(AppTheme.radiusM),
                ),
                child: Row(
                  children: [
                    Expanded(
                      child: LabeledStat(label: 'HRV', value: '${session.hrv} ms'),
                    ),
                    Expanded(
                      child: LabeledStat(label: 'SpO2', value: '${session.spo2}%'),
                    ),
                    Expanded(
                      child: LabeledStat(label: 'Motion', value: session.motion),
                    ),
                  ],
                ),
              ),
              const SizedBox(height: AppTheme.spaceXl),
              AppButton(
                label: 'End session',
                variant: AppButtonVariant.secondary,
                onTap: _endSession,
              ),
              const SizedBox(height: AppTheme.spaceL),
            ],
          ),
        ),
      ),
    );
  }
}

class _LiveModeBadge extends StatefulWidget {
  const _LiveModeBadge({required this.label});
  final String label;

  @override
  State<_LiveModeBadge> createState() => _LiveModeBadgeState();
}

class _LiveModeBadgeState extends State<_LiveModeBadge>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final bool _reduceMotion;

  @override
  void initState() {
    super.initState();
    _reduceMotion = WidgetsBinding
        .instance
        .platformDispatcher
        .accessibilityFeatures
        .disableAnimations;
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1400),
    );
    if (!_reduceMotion) _controller.repeat(reverse: true);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        FadeTransition(
          opacity: _reduceMotion
              ? const AlwaysStoppedAnimation(1)
              : Tween(begin: 0.35, end: 1.0).animate(
                  CurvedAnimation(parent: _controller, curve: Curves.easeInOut),
                ),
          child: Container(
            width: 6,
            height: 6,
            decoration: const BoxDecoration(
              color: AppColors.success,
              shape: BoxShape.circle,
            ),
          ),
        ),
        const SizedBox(width: AppTheme.spaceS),
        Flexible(
          child: Text(
            widget.label,
            style: AppTypography.labelS(),
            overflow: TextOverflow.ellipsis,
          ),
        ),
      ],
    );
  }
}
