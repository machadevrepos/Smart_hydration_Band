import 'package:flutter/material.dart';
import '../constants/app_colors.dart';

/// Concentric rings pulsing outward from a center point, used wherever the
/// app is actively searching for something (BLE scanning today). One
/// `AnimationController` drives every ring via a phase offset, so the
/// rings stay staggered without needing N separate controllers.
///
/// Reduced motion gets a single static ring, not a frozen mid-pulse frame.
/// A paused animation reads as broken; a deliberately static ring reads as
/// a state.
class PulseRadar extends StatefulWidget {
  const PulseRadar({
    super.key,
    required this.child,
    this.color = const Color(0xFFC9A227),
    this.size = 220,
    this.ringCount = 3,
  });

  final Widget child;
  final Color color;
  final double size;
  final int ringCount;

  @override
  State<PulseRadar> createState() => _PulseRadarState();
}

class _PulseRadarState extends State<PulseRadar> with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  late final bool _reduceMotion;

  @override
  void initState() {
    super.initState();
    _reduceMotion = WidgetsBinding.instance.platformDispatcher.accessibilityFeatures.disableAnimations;
    _controller = AnimationController(vsync: this, duration: const Duration(milliseconds: 2400));
    if (!_reduceMotion) _controller.repeat();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: widget.size,
      height: widget.size,
      child: Stack(
        alignment: Alignment.center,
        children: [
          if (_reduceMotion)
            Container(
              width: widget.size * 0.6,
              height: widget.size * 0.6,
              decoration: BoxDecoration(shape: BoxShape.circle, border: Border.all(color: widget.color.withValues(alpha: 0.3))),
            )
          else
            AnimatedBuilder(
              animation: _controller,
              builder: (context, _) {
                return Stack(
                  alignment: Alignment.center,
                  children: List.generate(widget.ringCount, (i) {
                    final phase = (_controller.value + (i / widget.ringCount)) % 1.0;
                    final scale = 0.3 + (phase * 0.7);
                    final opacity = (1.0 - phase).clamp(0.0, 1.0);
                    return Opacity(
                      opacity: opacity * 0.6,
                      child: Transform.scale(
                        scale: scale,
                        child: Container(
                          width: widget.size,
                          height: widget.size,
                          decoration: BoxDecoration(shape: BoxShape.circle, border: Border.all(color: widget.color, width: 1.5)),
                        ),
                      ),
                    );
                  }),
                );
              },
            ),
          Container(
            width: widget.size * 0.34,
            height: widget.size * 0.34,
            decoration: BoxDecoration(
              color: AppColors.bgSurface,
              shape: BoxShape.circle,
              border: Border.all(color: widget.color.withValues(alpha: 0.5)),
            ),
            alignment: Alignment.center,
            child: widget.child,
          ),
        ],
      ),
    );
  }
}
