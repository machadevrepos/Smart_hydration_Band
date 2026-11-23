import 'package:flutter/material.dart';

/// Dark-first palette. Every color used in a widget must come from here,
/// never a raw `Color(0x...)` inline. See CLAUDE.md "Visual identity".
class AppColors {
  const AppColors._();

  // ── Background ────────────────────────────────────────────────────────
  static const Color bgVoid = Color(0xFF000000);
  static const Color bgSurface = Color(0xFF0E0E10);
  static const Color bgSurfaceRaised = Color(0xFF17161A);
  static const Color bgHairline = Color(0xFF232226);


  // ── Gold accent, spend it deliberately ─────────────────────────────────
  static const Color gold = Color(0xFFC9A227);
  static const Color goldMuted = Color(0xFF8C6D1F);
  static const Color champagne = Color(0xFFE8D9A0);
  // 14% opacity glow, composited over bgVoid/bgSurface for active-state halos.
  static const Color goldGlow = Color(0x24C9A227);
  // 12% tint for chip/selected-card fills, never a solid fill outside CTAs.
  static const Color goldTint = Color(0x1FC9A227);
  // 40%, selected-card border. Stronger than the tint so a selected state
  // reads at a glance without leaning on a solid gold fill.
  static const Color goldBorder = Color(0x66C9A227);

  // ── Text, warm off-white, not pure white ───────────────────────────────
  static const Color textPrimary = Color(0xFFF5F1E8);
  static const Color textSecondary = Color(0xB3F5F1E8); // 70%
  static const Color textMuted = Color(0x73F5F1E8); // 45%
  static const Color textOnGold = Color(0xFF16140C);

  // ── Status, jewel tones ─────────────────────────────────────────────────
  static const Color success = Color(0xFF4E9F6E);
  static const Color successTint = Color(0x1F4E9F6E);
  static const Color warning = gold;
  static const Color warningTint = goldTint;
  static const Color error = Color(0xFFC0453F);
  static const Color errorTint = Color(0x1FC0453F);
}
