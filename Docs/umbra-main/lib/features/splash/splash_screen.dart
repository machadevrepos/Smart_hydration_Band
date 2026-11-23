import 'package:flutter/material.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/onboarding_storage.dart';
import '../onboarding/onboarding_carousel_screen.dart';
import '../shell/main_shell.dart';

/// The one-time brand moment on launch. Deliberately the single place in
/// the app that leans on the display face at real size and lets an
/// animation run purely for the entrance itself. Everywhere else,
/// animation has to justify itself against a state change (see CLAUDE.md
/// "No animation exists purely for delight with no functional purpose").
/// A splash reveal is the one legitimate exception: its function *is* the
/// brand moment.
class SplashScreen extends StatefulWidget {
  const SplashScreen({super.key});

  @override
  State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen> with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final Animation<double> _markOpacity;
  late final Animation<double> _markScale;
  late final Animation<double> _ruleWidth;

  static const _entranceDuration = Duration(milliseconds: 900);
  static const _holdDuration = Duration(milliseconds: 550);

  @override
  void initState() {
    super.initState();

    final reduceMotion = WidgetsBinding.instance.platformDispatcher.accessibilityFeatures.disableAnimations;

    _controller = AnimationController(
      vsync: this,
      duration: reduceMotion ? Duration.zero : _entranceDuration,
    );

    _markOpacity = CurvedAnimation(
      parent: _controller,
      curve: const Interval(0.0, 0.65, curve: Curves.easeOut),
    );
    _markScale = Tween<double>(begin: 0.92, end: 1.0).animate(
      CurvedAnimation(parent: _controller, curve: const Interval(0.0, 0.65, curve: Curves.easeOutCubic)),
    );
    _ruleWidth = CurvedAnimation(
      parent: _controller,
      curve: const Interval(0.5, 1.0, curve: Curves.easeOut),
    );

    _scheduleNavigation(reduceMotion);
  }

  Future<void> _scheduleNavigation(bool reduceMotion) async {
    // Kicked off alongside the entrance animation, not after it, so the
    // preferences read doesn't add its own delay on top of the brand hold.
    final hasCompletedOnboardingFuture = OnboardingStorage.hasCompletedOnboarding();
    try {
      await _controller.forward();
    } on TickerCanceled {
      return;
    }
    if (!mounted) return;
    await Future.delayed(reduceMotion ? const Duration(milliseconds: 300) : _holdDuration);
    if (!mounted) return;
    final hasCompletedOnboarding = await hasCompletedOnboardingFuture;
    if (!mounted) return;
    Navigator.of(context).pushReplacement(
      MaterialPageRoute(
        builder: (_) => hasCompletedOnboarding ? const MainShell() : const OnboardingCarouselScreen(),
      ),
    );
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: Center(
        child: AnimatedBuilder(
          animation: _controller,
          builder: (context, child) {
            return Opacity(
              opacity: _markOpacity.value,
              child: Transform.scale(
                scale: _markScale.value,
                child: child,
              ),
            );
          },
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                'UMBRA',
                style: AppTypography.displayM().copyWith(letterSpacing: 8),
              ),
              const SizedBox(height: AppTheme.spaceM),
              AnimatedBuilder(
                animation: _ruleWidth,
                builder: (context, _) {
                  return SizedBox(
                    width: 64,
                    child: Align(
                      alignment: Alignment.center,
                      child: FractionallySizedBox(
                        widthFactor: _ruleWidth.value.clamp(0.0, 1.0),
                        child: const _Rule(),
                      ),
                    ),
                  );
                },
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _Rule extends StatelessWidget {
  const _Rule();

  @override
  Widget build(BuildContext context) {
    return Container(height: 1.5, color: AppColors.gold);
  }
}
