import 'package:flutter/material.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/pressable_scale.dart';
import 'band_pairing_screen.dart';

enum _PermissionState { notAsked, granted }

class PermissionsScreen extends StatefulWidget {
  const PermissionsScreen({super.key});

  @override
  State<PermissionsScreen> createState() => _PermissionsScreenState();
}

class _PermissionsScreenState extends State<PermissionsScreen> {
  // TODO: wire to real requests (permission_handler for Bluetooth,
  // flutter_local_notifications / UNUserNotificationCenter for
  // notifications) once we're past the design pass. This screen currently
  // mocks the granted state locally so the flow can be reviewed end to end.
  _PermissionState _bluetooth = _PermissionState.notAsked;
  _PermissionState _notifications = _PermissionState.notAsked;

  void _continue() {
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const BandPairingScreen()));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const SizedBox(height: AppTheme.spaceXxl),
              Text('A couple of permissions', style: AppTypography.titleL()),
              const SizedBox(height: AppTheme.spaceS),
              Text(
                'Umbra needs these to work fully. You can change either any time in Settings.',
                style: AppTypography.bodyM(),
              ),
              const SizedBox(height: AppTheme.spaceXxl),
              _PermissionRow(
                icon: AppIcons.bluetooth,
                title: 'Bluetooth',
                description: 'Connects to your band for live HR and battery status.',
                state: _bluetooth,
                onAllow: () => setState(() => _bluetooth = _PermissionState.granted),
              ),
              const SizedBox(height: AppTheme.spaceM),
              _PermissionRow(
                icon: AppIcons.sparkle,
                title: 'Notifications',
                description: 'For your single 9am morning check-in, nothing else.',
                state: _notifications,
                onAllow: () => setState(() => _notifications = _PermissionState.granted),
              ),
              const Spacer(),
              AppButton(label: 'Continue', onTap: _continue),
              const SizedBox(height: AppTheme.spaceM),
              Center(
                child: PressableScale(
                  onTap: _continue,
                  semanticLabel: 'Maybe later',
                  child: Container(
                    height: AppTheme.minTapTarget,
                    padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM),
                    alignment: Alignment.center,
                    child: Text('Maybe later', style: AppTypography.labelL(color: AppColors.textMuted)),
                  ),
                ),
              ),
              const SizedBox(height: AppTheme.spaceM),
            ],
          ),
        ),
      ),
    );
  }
}

class _PermissionRow extends StatelessWidget {
  const _PermissionRow({
    required this.icon,
    required this.title,
    required this.description,
    required this.state,
    required this.onAllow,
  });

  final IconData icon;
  final String title;
  final String description;
  final _PermissionState state;
  final VoidCallback onAllow;

  @override
  Widget build(BuildContext context) {
    final granted = state == _PermissionState.granted;

    return Container(
      padding: const EdgeInsets.all(AppTheme.spaceL),
      decoration: BoxDecoration(color: AppColors.bgSurface, borderRadius: BorderRadius.circular(AppTheme.radiusM)),
      child: Row(
        children: [
          Container(
            width: 44,
            height: 44,
            alignment: Alignment.center,
            decoration: const BoxDecoration(color: AppColors.bgHairline, shape: BoxShape.circle),
            child: Icon(icon, size: AppTheme.iconM, color: AppColors.textSecondary),
          ),
          const SizedBox(width: AppTheme.spaceL),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(title, style: AppTypography.titleS()),
                const SizedBox(height: 2),
                Text(description, style: AppTypography.bodyS(), maxLines: 2, overflow: TextOverflow.ellipsis),
              ],
            ),
          ),
          const SizedBox(width: AppTheme.spaceM),
          if (granted)
            const Icon(AppIcons.check, size: AppTheme.iconM, color: AppColors.success)
          else
            PressableScale(
              onTap: onAllow,
              hapticOnTap: true,
              semanticLabel: 'Allow $title',
              child: Container(
                height: 36,
                padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AppColors.bgSurfaceRaised,
                  borderRadius: BorderRadius.circular(AppTheme.radiusPill),
                  border: Border.all(color: AppColors.bgHairline),
                ),
                child: Text('Allow', style: AppTypography.labelM(color: AppColors.textPrimary)),
              ),
            ),
        ],
      ),
    );
  }
}
