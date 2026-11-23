import 'package:flutter/material.dart';

/// Pushes [builder] as a new route, but only if the route the tap
/// originated from is still the current (topmost) one. A rapid double-tap
/// on any button that pushes a screen (the intended tap and an accidental
/// second one before the transition even starts) would otherwise queue two
/// pushes and land two copies of the same screen on the stack; by the time
/// the second tap's handler runs, its route is no longer current, so the
/// guard skips it instead.
Future<T?> pushOnce<T>(BuildContext context, WidgetBuilder builder) {
  final route = ModalRoute.of(context);
  if (route != null && !route.isCurrent) return Future<T?>.value();
  return Navigator.of(context).push<T>(MaterialPageRoute(builder: builder));
}
