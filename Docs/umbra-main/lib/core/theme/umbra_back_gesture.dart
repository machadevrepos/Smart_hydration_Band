import 'dart:math';
import 'package:flutter/gestures.dart';
import 'package:flutter/widgets.dart';

/// Re-implements the edge-swipe-to-pop gesture that
/// `CupertinoPageTransitionsBuilder` gets "for free" but that a custom
/// [PageTransitionsBuilder] does not. Flutter only wires that gesture up
/// inside its own private `_CupertinoBackGestureDetector`, which isn't
/// exported, so a brand-specific transition has to bring its own or lose
/// the gesture entirely. This mirrors Flutter's own mechanics (same edge
/// width, fling threshold, and animation duration, pulled from
/// `cupertino/route.dart`) rather than approximating them, so the feel
/// matches what iOS users already expect.
///
/// Wraps [child] with a left-edge (right-edge in RTL) drag listener. While
/// a drag is active, [route]'s own transition [AnimationController] is
/// driven directly by the finger; on release, it either flings the page
/// closed (calling [route]'s pop) or snaps back open.
class UmbraBackGestureDetector<T> extends StatefulWidget {
  const UmbraBackGestureDetector({super.key, required this.route, required this.child});

  final PageRoute<T> route;
  final Widget child;

  @override
  State<UmbraBackGestureDetector<T>> createState() => _UmbraBackGestureDetectorState<T>();
}

class _UmbraBackGestureDetectorState<T> extends State<UmbraBackGestureDetector<T>> {
  static const double _edgeWidth = 20.0;

  _UmbraBackGestureController<T>? _controller;
  late HorizontalDragGestureRecognizer _recognizer;

  @override
  void initState() {
    super.initState();
    _recognizer = HorizontalDragGestureRecognizer(debugOwner: this)
      ..onStart = _onDragStart
      ..onUpdate = _onDragUpdate
      ..onEnd = _onDragEnd
      ..onCancel = _onDragCancel;
  }

  @override
  void dispose() {
    _recognizer.dispose();
    if (_controller != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_controller?.navigator.mounted ?? false) {
          _controller?.navigator.didStopUserGesture();
        }
        _controller = null;
      });
    }
    super.dispose();
  }

  double _toLogical(double value) {
    return Directionality.of(context) == TextDirection.rtl ? -value : value;
  }

  void _onDragStart(DragStartDetails details) {
    _controller = _UmbraBackGestureController<T>(route: widget.route);
  }

  void _onDragUpdate(DragUpdateDetails details) {
    _controller?.dragUpdate(_toLogical(details.primaryDelta! / context.size!.width));
  }

  void _onDragEnd(DragEndDetails details) {
    _controller?.dragEnd(_toLogical(details.velocity.pixelsPerSecond.dx / context.size!.width));
    _controller = null;
  }

  void _onDragCancel() {
    _controller?.dragEnd(0.0);
    _controller = null;
  }

  void _onPointerDown(PointerDownEvent event) {
    if (widget.route.popGestureEnabled) _recognizer.addPointer(event);
  }

  @override
  Widget build(BuildContext context) {
    // Wider on the notch side, same accommodation Cupertino's detector makes.
    final dragAreaWidth = Directionality.of(context) == TextDirection.rtl
        ? MediaQuery.paddingOf(context).right
        : MediaQuery.paddingOf(context).left;

    return Stack(
      fit: StackFit.passthrough,
      children: [
        widget.child,
        PositionedDirectional(
          start: 0,
          width: max(dragAreaWidth, _edgeWidth),
          top: 0,
          bottom: 0,
          child: Listener(onPointerDown: _onPointerDown, behavior: HitTestBehavior.translucent),
        ),
      ],
    );
  }
}

/// Drives [PageRoute.controller] directly from drag input, then resolves to
/// either finishing the pop or snapping back open on release. Numbers
/// (fling velocity threshold, settle duration, settle curve) match
/// Cupertino's own gesture controller.
class _UmbraBackGestureController<T> {
  _UmbraBackGestureController({required this.route}) : navigator = route.navigator! {
    navigator.didStartUserGesture();
  }

  final PageRoute<T> route;
  final NavigatorState navigator;

  static const double _minFlingVelocity = 1.0; // screen widths per second
  static const Duration _settleDuration = Duration(milliseconds: 350);
  static const Curve _settleCurve = Curves.fastEaseInToSlowEaseOut;

  void dragUpdate(double delta) {
    // Flutter's own CupertinoRouteTransitionMixin reaches into this exact
    // protected member the exact same way (see its "// protected access"
    // comment in cupertino/route.dart): driving the route's own transition
    // controller from raw drag input is the only way to get the animation
    // to track the finger 1:1.
    // ignore: invalid_use_of_protected_member
    route.controller!.value -= delta;
  }

  void dragEnd(double velocity) {
    // ignore: invalid_use_of_protected_member
    final controller = route.controller!;
    final bool animateForward;

    if (!route.isCurrent) {
      // Already navigated away from (e.g. a programmatic pop raced the
      // drag): finish based on stack membership, not the drag itself.
      animateForward = route.isActive;
    } else if (velocity.abs() >= _minFlingVelocity) {
      animateForward = velocity <= 0;
    } else {
      animateForward = controller.value > 0.5;
    }

    if (animateForward) {
      controller.animateTo(1.0, duration: _settleDuration, curve: _settleCurve);
    } else {
      if (route.isCurrent) navigator.pop();
      if (controller.isAnimating) {
        controller.animateBack(0.0, duration: _settleDuration, curve: _settleCurve);
      }
    }

    if (controller.isAnimating) {
      late final AnimationStatusListener listener;
      listener = (status) {
        navigator.didStopUserGesture();
        controller.removeStatusListener(listener);
      };
      controller.addStatusListener(listener);
    } else {
      navigator.didStopUserGesture();
    }
  }
}
