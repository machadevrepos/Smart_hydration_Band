/// Manual relative-day formatting so screens that only need "Yesterday" /
/// "Jul 15" don't have to pull in `intl` for it. Swap for `DateFormat`
/// once intl is wired up for real localized dates app-wide (see CLAUDE.md
/// localization section). This helper's callers won't need to change.
String relativeDayLabel(DateTime dt) {
  final now = DateTime.now();
  final today = DateTime(now.year, now.month, now.day);
  final that = DateTime(dt.year, dt.month, dt.day);
  final diff = today.difference(that).inDays;
  if (diff == 0) return 'Today';
  if (diff == 1) return 'Yesterday';
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  return '${months[dt.month - 1]} ${dt.day}';
}

/// "Just now" / "5m ago" / "3h ago" / falls back to [relativeDayLabel] past
/// a day, for a cached reading's staleness (e.g. the band's last known
/// battery level after the link drops).
String relativeTimeAgoLabel(DateTime dt) {
  final diff = DateTime.now().difference(dt);
  if (diff.inMinutes < 1) return 'Just now';
  if (diff.inMinutes < 60) return '${diff.inMinutes}m ago';
  if (diff.inHours < 24) return '${diff.inHours}h ago';
  return relativeDayLabel(dt);
}

/// 12-hour "h:mm a" formatting, same rationale as [relativeDayLabel].
String shortTimeLabel(DateTime dt) {
  final hour24 = dt.hour;
  final period = hour24 >= 12 ? 'PM' : 'AM';
  final hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;
  final minute = dt.minute.toString().padLeft(2, '0');
  return '$hour12:$minute $period';
}
