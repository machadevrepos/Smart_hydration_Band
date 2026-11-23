import 'package:flutter/material.dart';
import '../../core/constants/app_colors.dart';
import '../history/history_screen.dart';
import '../home/home_screen.dart';
import '../insights/insights_screen.dart';
import '../settings/settings_screen.dart';
import 'widgets/umbra_bottom_nav.dart';

/// Owns the bottom nav and swaps tab bodies via `IndexedStack` so each tab
/// keeps its scroll position and state when you switch away and back,
/// rather than every tab screen carrying its own `Scaffold` and rebuilding
/// from scratch. Home, History, Insights, Settings are content only; this
/// is the one place the tab chrome lives.
class MainShell extends StatefulWidget {
  const MainShell({super.key});

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  int _index = 0;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.bgVoid,
      body: IndexedStack(
        index: _index,
        children: const [
          HomeScreen(),
          HistoryScreen(),
          InsightsScreen(),
          SettingsScreen(),
        ],
      ),
      bottomNavigationBar: UmbraBottomNav(activeIndex: _index, onTap: (i) => setState(() => _index = i)),
    );
  }
}
