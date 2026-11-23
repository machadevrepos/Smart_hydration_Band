import 'package:flutter/material.dart';
import '../../../core/constants/app_colors.dart';

/// Atmospheric background for one onboarding page. Backed by the generated
/// plates from `image-prompts.md` (onboarding_01/02/03); composition (glow
/// position, mood) was requested to match each page's copy.
enum OnboardingMood { calm, night, morning }

class OnboardingBackground extends StatelessWidget {
  const OnboardingBackground({super.key, required this.mood});

  final OnboardingMood mood;

  @override
  Widget build(BuildContext context) {
    final asset = switch (mood) {
      OnboardingMood.calm => 'assets/images/onboarding_01_calm.png',
      OnboardingMood.night => 'assets/images/onboarding_02_night.png',
      OnboardingMood.morning => 'assets/images/onboarding_03_morning.png',
    };
    return Container(
      color: AppColors.bgVoid,
      child: Image.asset(asset, fit: BoxFit.cover, width: double.infinity, height: double.infinity),
    );
  }
}
