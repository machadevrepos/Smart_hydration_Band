import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/models/hydration_mode.dart';
import '../../core/models/session_record.dart';
import '../../core/state/band_controller.dart';
import '../../core/state/session_controller.dart';
import '../../core/state/session_store.dart';
import '../../core/utils/band_status_presentation.dart';
import '../../core/utils/date_format.dart';
import '../../core/utils/navigation.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/labeled_stat.dart';
import '../../core/widgets/pressable_scale.dart';
import '../band/band_detail_screen.dart';
import '../checkin/morning_checkin_screen.dart';
import '../session/active_session_screen.dart';
import 'widgets/interval_sheet.dart';
import 'widgets/mode_card.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  HydrationMode _mode = HydrationMode.night;
  final Map<HydrationMode, int> _intervalByMode = {
    HydrationMode.daily: 90,
    HydrationMode.night: 40,
  };

  bool _contentVisible = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) setState(() => _contentVisible = true);
    });
  }

  String get _greeting {
    final hour = DateTime.now().hour;
    if (hour < 5) return 'Good night';
    if (hour < 12) return 'Good morning';
    if (hour < 17) return 'Good afternoon';
    return 'Good evening';
  }

  Future<void> _openIntervalSheet() async {
    final result = await showIntervalSheet(
      context,
      current: _intervalByMode[_mode]!,
    );
    if (result != null) {
      setState(() => _intervalByMode[_mode] = result);
    }
  }

  Future<void> _openCheckIn(SessionRecord session) async {
    final rating = await pushOnce<int>(
      context,
      (_) => MorningCheckinScreen(
        sessionSummary:
            'Last night: Night session, ${session.durationLabel}, ${session.reminderCount} reminders',
      ),
    );
    if (rating != null && mounted) {
      context.read<SessionStore>().attachCheckIn(session.id, rating);
    }
  }

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;
    final duration = reduceMotion ? Duration.zero : AppTheme.animNormal;
    final band = context.watch<BandController>();
    final mostRecentSession = context.watch<SessionStore>().mostRecent;

    return SafeArea(
      bottom: false,
      child: AnimatedOpacity(
        opacity: _contentVisible ? 1 : 0,
        duration: duration,
        curve: AppTheme.motionCurve,
        child: AnimatedSlide(
          offset: _contentVisible ? Offset.zero : const Offset(0, 0.02),
          duration: duration,
          curve: AppTheme.motionCurve,
          child: ListView(
            padding: const EdgeInsets.fromLTRB(
              AppTheme.screenPaddingH,
              AppTheme.spaceM,
              AppTheme.screenPaddingH,
              AppTheme.spaceXxxl,
            ),
            children: [
              _TopBar(state: band.state, battery: band.battery, hasReading: band.lastSeenAt != null),
              const SizedBox(height: AppTheme.spaceXxl),
              Text(_greeting, style: AppTypography.titleL()),
              const SizedBox(height: 4),
              Text('Ready when you are.', style: AppTypography.bodyM()),
              const SizedBox(height: AppTheme.spaceXxl),
              Row(
                children: [
                  Expanded(
                    child: ModeCard(
                      icon: AppIcons.sun,
                      label: 'Daily',
                      descriptor: 'Passive all-day reminders',
                      selected: _mode == HydrationMode.daily,
                      onTap: () => setState(() => _mode = HydrationMode.daily),
                    ),
                  ),
                  const SizedBox(width: AppTheme.spaceM),
                  Expanded(
                    child: ModeCard(
                      icon: AppIcons.moon,
                      label: 'Night',
                      descriptor: "Active while you're out",
                      selected: _mode == HydrationMode.night,
                      onTap: () => setState(() => _mode = HydrationMode.night),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: AppTheme.spaceM),
              _IntervalRow(
                minutes: _intervalByMode[_mode]!,
                onTap: _openIntervalSheet,
              ),
              const SizedBox(height: AppTheme.spaceXl),
              _PrimaryAction(
                mode: _mode,
                intervalMinutes: _intervalByMode[_mode]!,
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              _SectionLabel('Your band'),
              const SizedBox(height: AppTheme.spaceM),
              _BandStatusCard(
                state: band.state,
                battery: band.battery,
                restingHr: band.restingHr,
                lastSeenAt: band.lastSeenAt,
                stale: band.readingsAreStale,
                onTap: () => pushOnce(context, (_) => const BandDetailScreen()),
              ),
              const SizedBox(height: AppTheme.spaceXxxl),
              _SectionLabel('Last night'),
              const SizedBox(height: AppTheme.spaceM),
              if (mostRecentSession != null)
                _LastSessionCard(
                  session: mostRecentSession,
                  onCheckIn: () => _openCheckIn(mostRecentSession),
                )
              else
                const _NoSessionsYetCard(),
            ],
          ),
        ),
      ),
    );
  }
}

class _TopBar extends StatelessWidget {
  const _TopBar({required this.state, required this.battery, required this.hasReading});

  final BandLinkState state;
  final int battery;
  final bool hasReading;

  @override
  Widget build(BuildContext context) {
    final status = BandStatusPresentation.of(state);
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(
          'UMBRA',
          style: AppTypography.labelL().copyWith(
            letterSpacing: 3,
            fontWeight: FontWeight.w600,
          ),
        ),
        Row(
          children: [
            Semantics(
              label: 'Band ${status.label}',
              child: Row(
                children: [
                  Icon(status.icon, size: AppTheme.iconS, color: status.color),
                  const SizedBox(width: AppTheme.spaceXs),
                  Text(status.label, style: AppTypography.labelM()),
                ],
              ),
            ),
            if (hasReading) ...[
              const SizedBox(width: AppTheme.spaceL),
              Semantics(
                label: 'Battery $battery percent',
                child: Row(
                  children: [
                    Icon(
                      battery <= 25 ? AppIcons.batteryLow : AppIcons.batteryFull,
                      size: AppTheme.iconS,
                      color: battery <= 25
                          ? AppColors.error
                          : AppColors.textMuted,
                    ),
                    const SizedBox(width: AppTheme.spaceXs),
                    Text('$battery%', style: AppTypography.labelM()),
                  ],
                ),
              ),
            ],
          ],
        ),
      ],
    );
  }
}

class _IntervalRow extends StatelessWidget {
  const _IntervalRow({required this.minutes, required this.onTap});

  final int minutes;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return PressableScale(
      onTap: onTap,
      semanticLabel:
          'Reminder interval, every $minutes minutes. Double tap to change.',
      child: Container(
        height: AppTheme.minTapTarget,
        padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceL),
        decoration: BoxDecoration(
          color: AppColors.bgSurface,
          borderRadius: BorderRadius.circular(AppTheme.radiusS),
          border: Border.all(color: AppColors.bgHairline),
        ),
        child: Row(
          children: [
            const Icon(
              AppIcons.clock,
              size: AppTheme.iconM,
              color: AppColors.textSecondary,
            ),
            const SizedBox(width: AppTheme.spaceM),
            Expanded(
              child: Text(
                'Reminds every $minutes min',
                style: AppTypography.bodyL(),
              ),
            ),
            const Icon(
              AppIcons.pencil,
              size: AppTheme.iconS,
              color: AppColors.textMuted,
            ),
          ],
        ),
      ),
    );
  }
}

class _PrimaryAction extends StatelessWidget {
  const _PrimaryAction({required this.mode, required this.intervalMinutes});

  final HydrationMode mode;
  final int intervalMinutes;

  void _openSession(BuildContext context) => pushOnce(
    context,
    (_) => ActiveSessionScreen(intervalMinutes: intervalMinutes),
  );

  @override
  Widget build(BuildContext context) {
    // A live session takes priority over whichever mode happens to be
    // selected right now: there's only ever one session, and returning to
    // it has to stay reachable regardless of what the toggle shows.
    final sessionActive = context.watch<SessionController>().isActive;
    if (sessionActive) {
      return PressableScale(
        onTap: () => _openSession(context),
        semanticLabel: 'Session in progress, tap to return',
        child: Container(
          height: AppTheme.minTapTarget + 8,
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceL),
          decoration: BoxDecoration(
            color: AppColors.goldTint,
            borderRadius: BorderRadius.circular(AppTheme.radiusPill),
            border: Border.all(color: AppColors.goldBorder, width: 1.5),
          ),
          child: Row(
            children: [
              const Icon(
                AppIcons.moon,
                size: AppTheme.iconM,
                color: AppColors.gold,
              ),
              const SizedBox(width: AppTheme.spaceM),
              Expanded(
                child: Text(
                  'Session in progress',
                  style: AppTypography.labelL(),
                ),
              ),
              const Icon(
                AppIcons.chevronForward,
                size: AppTheme.iconS,
                color: AppColors.textMuted,
              ),
            ],
          ),
        ),
      );
    }

    if (mode == HydrationMode.night) {
      return AppButton(
        label: 'Start session',
        icon: AppIcons.play,
        onTap: () => _openSession(context),
      );
    }

    // Daily Mode is ambient, not event-based. There's nothing to "start":
    // the band is already reminding on schedule standalone. Reflect that
    // state instead of showing a CTA that has nothing to do.
    return Container(
      height: AppTheme.minTapTarget + 8,
      padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceL),
      decoration: BoxDecoration(
        color: AppColors.bgSurface,
        borderRadius: BorderRadius.circular(AppTheme.radiusPill),
      ),
      child: Row(
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: const BoxDecoration(
              color: AppColors.success,
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: AppTheme.spaceM),
          Expanded(
            child: Text('Daily Mode is active', style: AppTypography.labelL()),
          ),
        ],
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);
  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(text.toUpperCase(), style: AppTypography.labelS());
  }
}

class _BandStatusCard extends StatelessWidget {
  const _BandStatusCard({
    required this.state,
    required this.battery,
    required this.restingHr,
    required this.lastSeenAt,
    required this.stale,
    required this.onTap,
  });

  final BandLinkState state;
  final int battery;
  final int restingHr;
  final DateTime? lastSeenAt;
  final bool stale;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final hasReading = lastSeenAt != null;
    final valueColor = stale ? AppColors.textMuted : AppColors.textPrimary;

    return PressableScale(
      onTap: onTap,
      semanticLabel: 'Band details',
      child: Container(
        padding: const EdgeInsets.all(AppTheme.spaceL),
        decoration: BoxDecoration(
          color: AppColors.bgSurface,
          borderRadius: BorderRadius.circular(AppTheme.radiusM),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Container(
                  width: 44,
                  height: 44,
                  alignment: Alignment.center,
                  decoration: const BoxDecoration(
                    color: AppColors.bgHairline,
                    shape: BoxShape.circle,
                  ),
                  child: const Icon(
                    AppIcons.heart,
                    size: AppTheme.iconM,
                    color: AppColors.gold,
                  ),
                ),
                const SizedBox(width: AppTheme.spaceL),
                LabeledStat(
                  label: 'Resting HR',
                  value: hasReading ? '$restingHr bpm' : 'No data',
                  valueStyle: AppTypography.titleM(color: valueColor),
                ),
                const Spacer(),
                LabeledStat(
                  label: 'Battery',
                  value: hasReading ? '$battery%' : 'No data',
                  alignEnd: true,
                  valueStyle: AppTypography.titleM(color: valueColor),
                ),
                const SizedBox(width: AppTheme.spaceM),
                const Icon(
                  AppIcons.chevronForward,
                  size: AppTheme.iconS,
                  color: AppColors.textMuted,
                ),
              ],
            ),
            if (stale && lastSeenAt != null) ...[
              const SizedBox(height: AppTheme.spaceM),
              const Divider(height: 1, color: AppColors.bgHairline),
              const SizedBox(height: AppTheme.spaceM),
              Row(
                children: [
                  Icon(
                    state == BandLinkState.reconnecting ? AppIcons.bluetooth : AppIcons.bluetoothOff,
                    size: AppTheme.iconS,
                    color: AppColors.textMuted,
                  ),
                  const SizedBox(width: AppTheme.spaceXs),
                  Text(
                    state == BandLinkState.reconnecting
                        ? 'Reconnecting, last seen ${relativeTimeAgoLabel(lastSeenAt!)}'
                        : 'Last seen ${relativeTimeAgoLabel(lastSeenAt!)}',
                    style: AppTypography.bodyS(),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class _LastSessionCard extends StatelessWidget {
  const _LastSessionCard({required this.session, required this.onCheckIn});

  final SessionRecord session;
  final VoidCallback onCheckIn;

  @override
  Widget build(BuildContext context) {
    // Only Night Mode produces a session today (see SessionRecord's doc
    // comment); the Daily Mode branch is exercised directly in
    // HistoryRow's tests since this card follows the identical pattern.
    final title = session.mode == HydrationMode.night
        ? 'Night session'
        : 'Daily tracking';
    final icon = session.mode == HydrationMode.night
        ? AppIcons.moon
        : AppIcons.sun;

    return Container(
      padding: const EdgeInsets.all(AppTheme.spaceL),
      decoration: BoxDecoration(
        color: AppColors.bgSurface,
        borderRadius: BorderRadius.circular(AppTheme.radiusM),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: AppTheme.iconM, color: AppColors.textSecondary),
              const SizedBox(width: AppTheme.spaceS),
              Expanded(child: Text(title, style: AppTypography.titleS())),
              Text(
                relativeDayLabel(session.startedAt),
                style: AppTypography.bodyS(),
              ),
            ],
          ),
          const SizedBox(height: AppTheme.spaceM),
          const Divider(height: 1, color: AppColors.bgHairline),
          const SizedBox(height: AppTheme.spaceM),
          Row(
            children: [
              Expanded(
                child: LabeledStat(
                  label: 'Avg HR',
                  value: '${session.avgHr} bpm',
                ),
              ),
              Expanded(
                child: LabeledStat(
                  label: 'Duration',
                  value: session.durationLabel,
                ),
              ),
              Expanded(
                child: LabeledStat(
                  label: 'Reminders',
                  value: '${session.reminderCount}',
                ),
              ),
            ],
          ),
          const SizedBox(height: AppTheme.spaceM),
          const Divider(height: 1, color: AppColors.bgHairline),
          const SizedBox(height: AppTheme.spaceM),
          if (session.checkInRating != null)
            Row(
              children: [
                const Icon(
                  AppIcons.sparkle,
                  size: AppTheme.iconS,
                  color: AppColors.textMuted,
                ),
                const SizedBox(width: AppTheme.spaceS),
                Text(
                  'Morning check-in: ${session.checkInRating}/10',
                  style: AppTypography.bodyM(),
                ),
              ],
            )
          else
            PressableScale(
              onTap: onCheckIn,
              semanticLabel: 'Rate last night, morning check-in pending',
              child: Row(
                children: [
                  const Icon(
                    AppIcons.sparkle,
                    size: AppTheme.iconS,
                    color: AppColors.gold,
                  ),
                  const SizedBox(width: AppTheme.spaceS),
                  Expanded(
                    child: Text(
                      'Rate last night',
                      style: AppTypography.bodyM(color: AppColors.textPrimary),
                    ),
                  ),
                  const Icon(
                    AppIcons.chevronForward,
                    size: AppTheme.iconS,
                    color: AppColors.textMuted,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

class _NoSessionsYetCard extends StatelessWidget {
  const _NoSessionsYetCard();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(AppTheme.spaceL),
      decoration: BoxDecoration(
        color: AppColors.bgSurface,
        borderRadius: BorderRadius.circular(AppTheme.radiusM),
      ),
      child: Row(
        children: [
          const Icon(
            AppIcons.moon,
            size: AppTheme.iconM,
            color: AppColors.textMuted,
          ),
          const SizedBox(width: AppTheme.spaceM),
          Expanded(
            child: Text(
              'Your nights start here. Nothing tracked yet.',
              style: AppTypography.bodyM(),
            ),
          ),
        ],
      ),
    );
  }
}
