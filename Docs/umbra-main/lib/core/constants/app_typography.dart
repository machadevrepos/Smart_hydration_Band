import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';
import 'app_colors.dart';

/// Type scale for Umbra. Two families only, per CLAUDE.md's pairing table:
/// Fraunces (display, Latin) and Inter (body/UI, Latin). Arabic counterparts
/// (Amiri / IBM Plex Sans Arabic) are wired the same way once localization
/// lands. Not built out here since this pass is English-first screens.
///
/// The display face appears in exactly two places on the home screen (the
/// greeting and the live HR numeral). Everywhere else uses the UI face.
/// That's deliberate: a display font used everywhere stops reading as
/// special. See CLAUDE.md "Consistency over cleverness".
class AppTypography {
  const AppTypography._();

  static TextStyle _display({
    required double size,
    required double height,
    required FontWeight weight,
    double letterSpacing = 0,
    Color color = AppColors.textPrimary,
  }) =>
      GoogleFonts.fraunces(
        fontSize: size,
        height: height / size,
        fontWeight: weight,
        letterSpacing: letterSpacing,
        color: color,
      );

  static TextStyle _ui({
    required double size,
    required double height,
    required FontWeight weight,
    double letterSpacing = 0,
    Color color = AppColors.textPrimary,
  }) =>
      GoogleFonts.inter(
        fontSize: size,
        height: height / size,
        fontWeight: weight,
        letterSpacing: letterSpacing,
        color: color,
      );

  // ── Display (Fraunces), reserved for hero numerals & the greeting ──────
  static TextStyle displayL({Color color = AppColors.textPrimary}) =>
      _display(size: 44, height: 52, weight: FontWeight.w600, letterSpacing: -0.5, color: color);
  static TextStyle displayM({Color color = AppColors.textPrimary}) =>
      _display(size: 32, height: 40, weight: FontWeight.w600, letterSpacing: -0.3, color: color);
  static TextStyle titleL({Color color = AppColors.textPrimary}) =>
      _display(size: 26, height: 32, weight: FontWeight.w600, letterSpacing: -0.2, color: color);

  // ── Titles (Inter) ────────────────────────────────────────────────────
  static TextStyle titleM({Color color = AppColors.textPrimary}) =>
      _ui(size: 20, height: 26, weight: FontWeight.w600, letterSpacing: -0.1, color: color);
  static TextStyle titleS({Color color = AppColors.textPrimary}) =>
      _ui(size: 17, height: 22, weight: FontWeight.w600, color: color);

  // ── Body (Inter) ──────────────────────────────────────────────────────
  static TextStyle bodyL({Color color = AppColors.textPrimary}) =>
      _ui(size: 17, height: 24, weight: FontWeight.w400, color: color);
  static TextStyle bodyM({Color color = AppColors.textSecondary}) =>
      _ui(size: 15, height: 22, weight: FontWeight.w400, color: color);
  static TextStyle bodyS({Color color = AppColors.textMuted}) =>
      _ui(size: 13, height: 18, weight: FontWeight.w400, color: color);

  // ── Labels (Inter) ────────────────────────────────────────────────────
  static TextStyle labelL({Color color = AppColors.textPrimary}) =>
      _ui(size: 15, height: 20, weight: FontWeight.w500, letterSpacing: 0.1, color: color);
  static TextStyle labelM({Color color = AppColors.textSecondary}) =>
      _ui(size: 13, height: 16, weight: FontWeight.w500, letterSpacing: 0.2, color: color);
  // Eyebrow/section labels, always rendered uppercase by the caller.
  static TextStyle labelS({Color color = AppColors.textMuted}) =>
      _ui(size: 11, height: 14, weight: FontWeight.w600, letterSpacing: 0.6, color: color);
}
