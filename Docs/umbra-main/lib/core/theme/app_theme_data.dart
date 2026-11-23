import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../constants/app_colors.dart';
import '../constants/app_typography.dart';
import 'umbra_page_transitions.dart';

/// System status bar / Android navigation bar styling to match the app's
/// permanently-dark chrome. Without this, Android in particular falls back
/// to whatever the OS default is (dark icons on some devices, a mismatched
/// light system nav bar pill on others) since we never use a Material
/// `AppBar` anywhere for `Scaffold` to derive a style from automatically.
/// One global call at startup is enough: the app has no light-surface
/// screens for this to need to vary per-screen.
///
/// `statusBarBrightness` (iOS) and `statusBarIconBrightness` (Android) are
/// confusingly inverted in Flutter's own API: iOS describes the bar's own
/// brightness (dark bar -> light icons), Android describes the icons
/// directly (light icons). Both are set here so each platform gets light
/// icons against our black bar.
const SystemUiOverlayStyle umbraSystemUiOverlayStyle = SystemUiOverlayStyle(
  statusBarColor: Colors.transparent,
  statusBarBrightness: Brightness.dark,
  statusBarIconBrightness: Brightness.light,
  systemNavigationBarColor: AppColors.bgVoid,
  systemNavigationBarIconBrightness: Brightness.light,
  systemNavigationBarDividerColor: Colors.transparent,
);

/// The app's single `ThemeData`. Beyond `AppColors`/`AppTypography`/
/// `AppTheme` (used directly by our own widgets), this maps the same tokens
/// onto Flutter's `ColorScheme`/`TextTheme` so any stock Material widget we
/// reach for later (SnackBar, TextField, Dialog) inherits the same system
/// instead of falling back to Material defaults.
ThemeData buildUmbraTheme() {
  final textTheme = TextTheme(
    displayLarge: AppTypography.displayL(),
    displayMedium: AppTypography.displayM(),
    displaySmall: AppTypography.titleL(),
    headlineLarge: AppTypography.titleL(),
    headlineMedium: AppTypography.titleM(),
    headlineSmall: AppTypography.titleS(),
    titleLarge: AppTypography.titleL(),
    titleMedium: AppTypography.titleM(),
    titleSmall: AppTypography.titleS(),
    bodyLarge: AppTypography.bodyL(),
    bodyMedium: AppTypography.bodyM(),
    bodySmall: AppTypography.bodyS(),
    labelLarge: AppTypography.labelL(),
    labelMedium: AppTypography.labelM(),
    labelSmall: AppTypography.labelS(),
  );

  return ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    scaffoldBackgroundColor: AppColors.bgVoid,
    canvasColor: AppColors.bgVoid,
    textTheme: textTheme,
    colorScheme: const ColorScheme.dark(
      surface: AppColors.bgSurface,
      surfaceContainerHighest: AppColors.bgSurfaceRaised,
      primary: AppColors.gold,
      onPrimary: AppColors.textOnGold,
      secondary: AppColors.goldMuted,
      onSecondary: AppColors.textOnGold,
      onSurface: AppColors.textPrimary,
      error: AppColors.error,
      onError: AppColors.textPrimary,
      outline: AppColors.bgHairline,
    ),
    dividerColor: AppColors.bgHairline,
    dividerTheme: const DividerThemeData(color: AppColors.bgHairline, thickness: 1, space: 1),
    // Tactile feedback comes from PressableScale, not Material ink. A
    // ripple on top of our own press-scale would be a second, competing
    // feedback system on every tap.
    splashFactory: NoSplash.splashFactory,
    highlightColor: Colors.transparent,
    splashColor: Colors.transparent,
    pageTransitionsTheme: const PageTransitionsTheme(
      builders: {
        // Only iOS gets the edge-swipe-back gesture: see the class doc on
        // UmbraPageTransitionsBuilder for why Android deliberately keeps
        // Flutter's own predictive-back builder instead of our custom one.
        TargetPlatform.iOS: UmbraPageTransitionsBuilder(enableEdgeSwipeBack: true),
        TargetPlatform.android: PredictiveBackPageTransitionsBuilder(),
        TargetPlatform.macOS: UmbraPageTransitionsBuilder(),
        TargetPlatform.windows: UmbraPageTransitionsBuilder(),
        TargetPlatform.linux: UmbraPageTransitionsBuilder(),
      },
    ),
    bottomSheetTheme: const BottomSheetThemeData(
      backgroundColor: Colors.transparent,
      surfaceTintColor: Colors.transparent,
      modalBarrierColor: Color(0x99000000),
    ),
    snackBarTheme: SnackBarThemeData(
      backgroundColor: AppColors.bgSurfaceRaised,
      contentTextStyle: AppTypography.bodyM(color: AppColors.textPrimary),
      behavior: SnackBarBehavior.floating,
    ),
  );
}
