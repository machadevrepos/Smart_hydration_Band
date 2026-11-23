import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../../core/constants/app_colors.dart';
import '../../core/constants/app_icons.dart';
import '../../core/constants/app_theme.dart';
import '../../core/constants/app_typography.dart';
import '../../core/state/session_store.dart';
import '../../core/utils/navigation.dart';
import 'session_detail_screen.dart';
import 'widgets/history_row.dart';

class HistoryScreen extends StatelessWidget {
  const HistoryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final history = context.watch<SessionStore>().history;

    return SafeArea(
      bottom: false,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: AppTheme.screenPaddingH),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const SizedBox(height: AppTheme.spaceM),
            Text('History', style: AppTypography.titleL()),
            const SizedBox(height: AppTheme.spaceXl),
            Expanded(
              child: history.isEmpty
                  ? const _EmptyHistory()
                  : ListView.separated(
                      padding: const EdgeInsets.only(bottom: AppTheme.spaceXxxl),
                      itemCount: history.length,
                      separatorBuilder: (context, i) => const SizedBox(height: AppTheme.spaceM),
                      itemBuilder: (context, i) {
                        final session = history[i];
                        return HistoryRow(
                          session: session,
                          onTap: () => pushOnce(context, (_) => SessionDetailScreen(sessionId: session.id)),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}

class _EmptyHistory extends StatelessWidget {
  const _EmptyHistory();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.only(bottom: AppTheme.spaceXxxl),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(AppIcons.moon, size: AppTheme.iconXl, color: AppColors.textMuted),
            const SizedBox(height: AppTheme.spaceL),
            Text('Your nights start here.', style: AppTypography.bodyL(color: AppColors.textSecondary)),
            const SizedBox(height: AppTheme.spaceXs),
            Text('Sessions you track will show up in this list.', style: AppTypography.bodyS()),
          ],
        ),
      ),
    );
  }
}
