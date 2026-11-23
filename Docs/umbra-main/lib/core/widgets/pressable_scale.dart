import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import '../constants/app_theme.dart';
import '../state/settings_controller.dart';

/// Tactile-feedback wrapper for every tappable element: rows, pills, icon
/// buttons, cards. Scales to ~0.97 on press-down and springs back on
/// release. See CLAUDE.md "Every tappable thing gives tactile feedback":
/// a bare `GestureDetector`/`InkWell` with no visual response is the
/// fastest way to make the app feel unfinished.
///
/// [hapticOnTap] is opt-in, not default-on. Haptics are for *meaningful*
/// taps (selecting a mode, confirming an action), not every row press.
class PressableScale extends StatefulWidget {
  const PressableScale({
    super.key,
    required this.child,
    this.onTap,
    this.hapticOnTap = false,
    this.scaleTo = 0.97,
    this.semanticLabel,
  });

  final Widget child;
  final VoidCallback? onTap;
  final bool hapticOnTap;
  final double scaleTo;
  final String? semanticLabel;

  @override
  State<PressableScale> createState() => _PressableScaleState();
}

class _PressableScaleState extends State<PressableScale> {
  bool _pressed = false;

  void _setPressed(bool value) {
    if (widget.onTap == null) return;
    setState(() => _pressed = value);
  }

  @override
  Widget build(BuildContext context) {
    final reduceMotion = MediaQuery.maybeOf(context)?.disableAnimations ?? false;
    final duration = reduceMotion ? Duration.zero : AppTheme.animXs;

    return Semantics(
      button: widget.onTap != null,
      label: widget.semanticLabel,
      child: GestureDetector(
        onTap: widget.onTap == null
            ? null
            : () {
                if (widget.hapticOnTap && context.read<SettingsController>().hapticsEnabled) {
                  HapticFeedback.selectionClick();
                }
                widget.onTap!();
              },
        onTapDown: (_) => _setPressed(true),
        onTapUp: (_) => _setPressed(false),
        onTapCancel: () => _setPressed(false),
        child: AnimatedScale(
          scale: _pressed ? widget.scaleTo : 1.0,
          duration: duration,
          curve: AppTheme.motionCurve,
          child: widget.child,
        ),
      ),
    );
  }
}
