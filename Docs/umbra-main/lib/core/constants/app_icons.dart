import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

/// Single icon source of truth: every icon in the app is `AppIcons.xxx`,
/// never `Icons.xxx` / `CupertinoIcons.xxx` inline. Keeps a future icon-set
/// swap or RTL-mirroring fix a one-file change.
///
/// RTL note (see CLAUDE.md "Not everything flips"): [chevronForward] and
/// [chevronBack] are directional and must mirror under RTL. Wrap them in
/// a widget that respects `Directionality`, don't hardcode `Transform.flip`.
/// [play], [heart], [moon], [sun], [bolt] represent real-world objects with
/// inherent orientation and must never mirror.
class AppIcons {
  const AppIcons._();

  // Navigation
  static const IconData home = CupertinoIcons.house_fill;
  static const IconData history = CupertinoIcons.time;
  static const IconData insights = CupertinoIcons.chart_bar_alt_fill;
  static const IconData settings = CupertinoIcons.gear_alt_fill;

  // Directional: mirrors in RTL
  static const IconData chevronForward = CupertinoIcons.chevron_forward;
  static const IconData chevronBack = CupertinoIcons.chevron_back;

  // Object icons: never mirror
  static const IconData play = CupertinoIcons.play_fill;
  static const IconData stop = CupertinoIcons.stop_fill;
  static const IconData heart = CupertinoIcons.heart_fill;
  static const IconData moon = CupertinoIcons.moon_stars_fill;
  static const IconData sun = CupertinoIcons.sun_max_fill;
  static const IconData bolt = CupertinoIcons.bolt_fill;

  // Status & metrics
  static const IconData droplet = Icons.water_drop_rounded;
  static const IconData bluetooth = CupertinoIcons.antenna_radiowaves_left_right;
  static const IconData bluetoothOff = CupertinoIcons.wifi_slash;
  static const IconData batteryFull = CupertinoIcons.battery_100;
  static const IconData batteryLow = CupertinoIcons.battery_25;
  static const IconData clock = CupertinoIcons.clock_fill;
  static const IconData pencil = CupertinoIcons.pencil;
  static const IconData check = CupertinoIcons.checkmark_alt;
  static const IconData sparkle = CupertinoIcons.sparkles;
}
