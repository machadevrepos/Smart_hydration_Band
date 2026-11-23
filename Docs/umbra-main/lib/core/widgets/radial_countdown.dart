import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../constants/app_colors.dart';
import '../constants/app_theme.dart';

/// A thin circular progress ring, gold arc over a hairline track, with a
/// center content slot. Used for the countdown to the next reminder, it's
/// the one place motion is allowed to be continuous, because it's tracking
/// something genuinely live (time passing), not decoration.
///
/// [progress] is 0 to 1. Changes to it animate smoothly rather than
/// jumping, so a once-a-second tick still reads as motion, not a snap.
class RadialCountdown extends StatelessWidget {
  const RadialCountdown({
    super.key,
    required this.progress,
    required this.child,
    this.size = 220,
    this.strokeWidth = 6,
  });

  final double progress;
  final Widget child;
  final double size;
  final double strokeWidth;

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.of(context).disableAnimations;

    return TweenAnimationBuilder<double>(
      tween: Tween(begin: progress, end: progress),
      duration: reduceMotion ? Duration.zero : AppTheme.animSlow,
      curve: AppTheme.motionCurve,
      builder: (context, value, _) {
        return CustomPaint(
          size: Size.square(size),
          painter: _RingPainter(progress: value.clamp(0.0, 1.0), strokeWidth: strokeWidth),
          child: SizedBox(width: size, height: size, child: Center(child: child)),
        );
      },
    );
  }
}

class _RingPainter extends CustomPainter {
  _RingPainter({required this.progress, required this.strokeWidth});

  final double progress;
  final double strokeWidth;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = (size.shortestSide - strokeWidth) / 2;

    final track = Paint()
      ..color = AppColors.bgHairline
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeWidth;
    canvas.drawCircle(center, radius, track);

    final arc = Paint()
      ..color = AppColors.gold
      ..style = PaintingStyle.stroke
      ..strokeWidth = strokeWidth
      ..strokeCap = StrokeCap.round;

    const start = -math.pi / 2;
    final sweep = progress * 2 * math.pi;
    canvas.drawArc(Rect.fromCircle(center: center, radius: radius), start, sweep, false, arc);
  }

  @override
  bool shouldRepaint(covariant _RingPainter oldDelegate) => oldDelegate.progress != progress;
}
