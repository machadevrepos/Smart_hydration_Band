/// Shared across Home, session tracking, and history, rather than living on
/// any one screen, because more than one screen needs to reason about which
/// mode a session was run in.
enum HydrationMode { daily, night }
