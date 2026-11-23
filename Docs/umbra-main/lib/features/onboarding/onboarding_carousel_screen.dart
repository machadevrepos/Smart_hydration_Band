import 'package:flutter/material.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/widgets/app_button.dart';
import '../../core/widgets/pressable_scale.dart';
import 'permissions_screen.dart';
import 'widgets/onboarding_background.dart';

class _OnboardingPageData {
  const _OnboardingPageData({required this.mood, required this.headline, required this.body});
  final OnboardingMood mood;
  final String headline;
  final String body;
}

const List<_OnboardingPageData> _kPages = [
  _OnboardingPageData(
    mood: OnboardingMood.calm,
    headline: 'Umbra reminds you to drink water.',
    body: 'The band buzzes. You drink water. That\'s it. No logging, no tapping, no setup.',
  ),
  _OnboardingPageData(
    mood: OnboardingMood.night,
    headline: 'Built for the night out.',
    body: 'Night Mode buzzes every 40 minutes by default while you\'re out. Adjust it any time.',
  ),
  _OnboardingPageData(
    mood: OnboardingMood.morning,
    headline: 'Wake up better.',
    body: 'One check-in at 9am. See what helped, and what didn\'t, over time.',
  ),
];

class OnboardingCarouselScreen extends StatefulWidget {
  const OnboardingCarouselScreen({super.key});

  @override
  State<OnboardingCarouselScreen> createState() => _OnboardingCarouselScreenState();
}

class _OnboardingCarouselScreenState extends State<OnboardingCarouselScreen> {
  final _pageController = PageController();
  int _page = 0;

  bool get _isLastPage => _page == _kPages.length - 1;

  void _next() {
    if (_isLastPage) {
      _finish();
      return;
    }
    _pageController.nextPage(duration: AppTheme.animNormal, curve: AppTheme.motionCurve);
  }

  void _finish() {
    Navigator.of(context).pushReplacement(MaterialPageRoute(builder: (_) => const PermissionsScreen()));
  }

  @override
  void dispose() {
    _pageController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: SafeArea(
        child: Stack(
          children: [
            PageView.builder(
              controller: _pageController,
              itemCount: _kPages.length,
              onPageChanged: (i) => setState(() => _page = i),
              itemBuilder: (context, i) => _OnboardingPage(data: _kPages[i]),
            ),
            Positioned(
              top: AppTheme.spaceS,
              right: AppTheme.screenPaddingH,
              child: AnimatedOpacity(
                opacity: _isLastPage ? 0 : 1,
                duration: AppTheme.animFast,
                child: PressableScale(
                  onTap: _isLastPage ? null : _finish,
                  semanticLabel: 'Skip introduction',
                  child: Container(
                    height: AppTheme.minTapTarget,
                    padding: const EdgeInsets.symmetric(horizontal: AppTheme.spaceM),
                    alignment: Alignment.center,
                    child: Text('Skip', style: AppTypography.labelL(color: AppColors.textSecondary)),
                  ),
                ),
              ),
            ),
            Positioned(
              left: AppTheme.screenPaddingH,
              right: AppTheme.screenPaddingH,
              bottom: AppTheme.spaceXl,
              child: Column(
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: List.generate(_kPages.length, (i) {
                      final active = i == _page;
                      return AnimatedContainer(
                        duration: AppTheme.animFast,
                        curve: AppTheme.motionCurve,
                        margin: const EdgeInsets.symmetric(horizontal: 3),
                        width: active ? 20 : 6,
                        height: 6,
                        decoration: BoxDecoration(
                          color: active ? AppColors.gold : AppColors.bgHairline,
                          borderRadius: BorderRadius.circular(AppTheme.radiusPill),
                        ),
                      );
                    }),
                  ),
                  const SizedBox(height: AppTheme.spaceXl),
                  AppButton(label: _isLastPage ? 'Get started' : 'Continue', onTap: _next),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _OnboardingPage extends StatelessWidget {
  const _OnboardingPage({required this.data});
  final _OnboardingPageData data;

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        Positioned.fill(child: OnboardingBackground(mood: data.mood)),
        Positioned(
          left: AppTheme.screenPaddingH,
          right: AppTheme.screenPaddingH,
          bottom: 148,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(data.headline, style: AppTypography.titleL()),
              const SizedBox(height: AppTheme.spaceM),
              Text(data.body, style: AppTypography.bodyL(color: AppColors.textSecondary)),
            ],
          ),
        ),
      ],
    );
  }
}
