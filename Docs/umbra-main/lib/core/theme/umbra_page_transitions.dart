import 'package:flutter/material.dart';
import '../constants/app_theme.dart';
import 'umbra_back_gesture.dart';

/// The one page transition used everywhere, on every platform: a fade
/// through with a slight scale-up on entry. Replaces the default platform
/// slide (iOS push / Android fade-upward) so navigation feels like one
/// deliberate system rather than whatever the OS defaults to.
/// See CLAUDE.md "Motion & micro-interactions".
///
/// [enableEdgeSwipeBack] additionally wires up a real edge-swipe-to-pop
/// gesture via [UmbraBackGestureDetector]. That's iOS-only in practice
/// (see `app_theme_data.dart`): Android's edge-swipe-back is owned by the
/// OS itself (system gesture navigation, and Android 13+'s predictive
/// back with its own live preview, both wired through
/// `PredictiveBackPageTransitionsBuilder`), so layering our own
/// touch-recognizer on the same screen edge would race the platform's own
/// gesture arbitration instead of cooperating with it. iOS has no such
/// system-owned layer, its back-swipe *is* the app-level gesture, which is
/// exactly what `CupertinoPageTransitionsBuilder` normally provides and
/// what this reimplements to keep our own visual instead of Cupertino's.
class UmbraPageTransitionsBuilder extends PageTransitionsBuilder {
  const UmbraPageTransitionsBuilder({this.enableEdgeSwipeBack = false});

  final bool enableEdgeSwipeBack;

  // Named, not Flutter's undocumented 450ms default. Every duration in
  // this app traces to an AppTheme token.
  @override
  Duration get transitionDuration => AppTheme.animNormal;

  @override
  Widget buildTransitions<T>(
    PageRoute<T> route,
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
    Widget child,
  ) {
    final content = enableEdgeSwipeBack ? UmbraBackGestureDetector<T>(route: route, child: child) : child;

    final reduceMotion = MediaQuery.maybeOf(context)?.disableAnimations ?? false;
    if (reduceMotion) return content;

    // While a finger is actively dragging the page, the curve must be
    // linear so the fade/scale tracks 1:1 with the drag instead of easing
    // against it: same reasoning as Cupertino's `linearTransition`.
    final curve = (enableEdgeSwipeBack && route.popGestureInProgress) ? Curves.linear : AppTheme.motionCurve;
    final curved = CurvedAnimation(parent: animation, curve: curve);

    return FadeTransition(
      opacity: curved,
      child: ScaleTransition(
        scale: Tween<double>(begin: 0.98, end: 1.0).animate(curved),
        child: content,
      ),
    );
  }
}
