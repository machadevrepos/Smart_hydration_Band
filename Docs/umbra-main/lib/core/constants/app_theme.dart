import 'package:flutter/material.dart';
import 'app_colors.dart';

/// Spacing, radius, icon size, shadow, and motion tokens. No raw doubles or
/// `Duration(milliseconds: N)` in a widget, everything traces back here.
class AppTheme {
  const AppTheme._();

  // ── Spacing, 4pt base ────────────────────────────────────────────────
  // Sub-scale: the one tight optical gap smaller than the 4pt grid, for a
  // label sitting directly above its value (LabeledStat and friends).
  static const double spaceHairline = 2;
  static const double spaceXs = 4;
  static const double spaceS = 8;
  static const double spaceM = 12;
  static const double spaceL = 16;
  static const double spaceXl = 20;
  static const double spaceXxl = 24;
  static const double spaceXxxl = 32;

  /// The one horizontal screen padding constant, never re-decided per screen.
  static const double screenPaddingH = spaceXl;

  // ── Radius ────────────────────────────────────────────────────────────
  static const double radiusS = 8; // chips, inputs, small tiles
  static const double radiusM = 16; // cards
  static const double radiusL = 24; // sheets, hero cards
  static const double radiusPill = 999; // buttons, segmented controls

  // ── Icon size ─────────────────────────────────────────────────────────
  static const double iconS = 16;
  static const double iconM = 20;
  static const double iconL = 24;
  static const double iconXl = 32;

  // ── Motion, four durations, no exceptions without reason ───────────────
  static const Duration animXs = Duration(milliseconds: 150);
  static const Duration animFast = Duration(milliseconds: 200);
  static const Duration animNormal = Duration(milliseconds: 300);
  static const Duration animSlow = Duration(milliseconds: 500);

  static const Curve motionCurve = Curves.easeOutCubic;

  // ── Elevation, dark surfaces mostly rely on the surface-color step, not
  // heavy shadow; this is the one subtle lift used behind raised sheets.
  static const List<BoxShadow> raisedShadow = [
    BoxShadow(color: Color(0x66000000), blurRadius: 24, offset: Offset(0, 8)),
  ];

  static List<BoxShadow> goldGlow({double blur = 20}) => [
        BoxShadow(color: AppColors.goldGlow, blurRadius: blur, spreadRadius: 1),
      ];

  /// Minimum tap target per accessibility rule, pad, don't grow the icon.
  static const double minTapTarget = 44;
}
