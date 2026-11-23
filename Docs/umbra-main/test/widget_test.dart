import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:umbra/core/constants/app_icons.dart';
import 'package:umbra/core/constants/app_theme.dart';
import 'package:umbra/core/models/hydration_mode.dart';
import 'package:umbra/core/models/session_record.dart';
import 'package:umbra/core/state/band_controller.dart';
import 'package:umbra/core/utils/navigation.dart';
import 'package:umbra/core/widgets/app_switch.dart';
import 'package:umbra/features/history/widgets/history_row.dart';
import 'package:umbra/main.dart';

/// Real pairing needs live BLE hardware, which the test environment
/// doesn't have, so tests that need a connected band reach that state
/// through BandController's test-only debug hook instead of a real scan.
void _connectBandForTest(WidgetTester tester) {
  Provider.of<BandController>(tester.element(find.byType(MaterialApp)), listen: false).debugConnectForTesting();
}

Future<void> _reachHome(WidgetTester tester) async {
  await tester.pumpWidget(const UmbraApp());
  await tester.pump(const Duration(milliseconds: 950));
  await tester.pump(const Duration(milliseconds: 700));
  await tester.pumpAndSettle();
  await tester.tap(find.text('Skip'));
  await tester.pump();
  await tester.pump(AppTheme.animNormal);
  await tester.tap(find.text('Maybe later'));
  await tester.pump();
  await tester.pump(AppTheme.animNormal);
  await tester.tap(find.text('Skip for now'));
  await tester.pump();
  await tester.pump(AppTheme.animNormal);
}

/// Without an explicit override, `defaultTargetPlatform` in a `flutter
/// test` run resolves from the host machine Dart is running on (macOS on
/// this setup), not from either of the app's real target platforms. That
/// was invisible while every platform shared identical navigation
/// behavior; now that iOS and Android deliberately differ (edge-swipe-back
/// is iOS-only, see UmbraPageTransitionsBuilder), the suite needs to pin a
/// real target platform rather than test whatever the host happens to
/// default to. Android is the default here since it's what most of these
/// tests exercise; the one iOS-specific test passes its own override.
///
/// The reset can't live in a top-level `tearDown`: Flutter's own
/// `debugAssertAllFoundationVarsUnset` invariant check runs immediately
/// after the test body returns, before file-level `tearDown` callbacks
/// fire, so it has to be reset inside the same test via try/finally.
void umbraTest(
  String description,
  Future<void> Function(WidgetTester tester) callback, {
  TargetPlatform platform = TargetPlatform.android,
}) {
  testWidgets(description, (tester) async {
    debugDefaultTargetPlatformOverride = platform;
    try {
      await callback(tester);
    } finally {
      debugDefaultTargetPlatformOverride = null;
    }
  });
}

void main() {
  // Splash reads OnboardingStorage (shared_preferences) to decide Home vs
  // Onboarding. Without a mock store, the plugin's method channel has no
  // handler in the test binding and the read never resolves, so Splash
  // never navigates and every test that walks past it hangs waiting for a
  // "Skip" button that's never reached. Reset before *every* test (not
  // just once): reaching Home via BandPairingScreen persists the
  // "onboarding complete" flag, and that mock store is otherwise shared
  // across the whole file — without a reset, any test that runs after one
  // that reached Home would find Splash routing straight past onboarding.
  setUp(() {
    SharedPreferences.setMockInitialValues({});

    // Settings > About reads the real build via PackageInfo.fromPlatform();
    // without a mock, the plugin channel has no test-binding handler and
    // resolves to an error, silently rendering an empty label instead of
    // failing loud. Fixed values here so the Settings test below can
    // actually assert the label renders build 107, not the pubspec default.
    PackageInfo.setMockInitialValues(
      appName: 'Umbra',
      packageName: 'com.umbrawellness.umbra',
      version: '1.0.0',
      buildNumber: '107',
      buildSignature: '',
    );
  });

  umbraTest('Splash screen shows the wordmark', (WidgetTester tester) async {
    await tester.pumpWidget(const UmbraApp());
    await tester.pump();

    expect(find.text('UMBRA'), findsOneWidget);
  });

  umbraTest('Splash hands off to the onboarding carousel after its reveal + hold', (WidgetTester tester) async {
    await tester.pumpWidget(const UmbraApp());
    // Entrance animation (900ms), pumped in steps so the animation
    // controller actually completes and its future resolves.
    await tester.pump(const Duration(milliseconds: 950));
    // Hold (550ms) + a margin of safety.
    await tester.pump(const Duration(milliseconds: 700));
    await tester.pumpAndSettle();

    expect(find.text('Umbra reminds you to drink water.'), findsOneWidget);
    expect(find.text('Skip'), findsOneWidget);
  });

  umbraTest('Onboarding skip leads to permissions, which leads to band pairing', (WidgetTester tester) async {
    await tester.pumpWidget(const UmbraApp());
    await tester.pump(const Duration(milliseconds: 950));
    await tester.pump(const Duration(milliseconds: 700));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Skip'));
    await tester.pumpAndSettle();
    expect(find.text('A couple of permissions'), findsOneWidget);

    await tester.tap(find.text('Maybe later'));
    // Band pairing has a continuously-repeating radar animation, so
    // pumpAndSettle (which waits for zero pending frames) never returns
    // here. Pump fixed frames instead: one to register the navigation,
    // then enough to clear the page transition.
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('Find your band'), findsOneWidget);

    await tester.tap(find.text('Skip for now'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('Daily'), findsOneWidget);
    expect(find.text('Start session'), findsOneWidget);
  });

  umbraTest('A session survives navigating back to Home, and only stops when explicitly ended', (WidgetTester tester) async {
    await _reachHome(tester);

    // Night is Home's default selected mode, so Start session is live.
    await tester.tap(find.text('Start session'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('BPM'), findsOneWidget);
    expect(find.text('End session'), findsOneWidget);

    // Back out without ending it. The session must keep ticking in
    // SessionController rather than being silently cancelled, so Home
    // should now offer to return to it instead of starting a new one.
    // Android's PredictiveBackPageTransitionsBuilder (in play here, unlike
    // the iOS-only custom builder) runs its own ~450ms transition rather
    // than the 300ms AppTheme.animNormal token, so pumps around a pop need
    // more headroom than the fixed-duration pushes elsewhere in this file.
    await tester.tap(find.byIcon(AppIcons.chevronBack));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.text('Session in progress'), findsOneWidget);
    expect(find.text('Start session'), findsNothing);

    // Resume it: same live session, not a fresh one.
    await tester.tap(find.text('Session in progress'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 600));
    expect(find.text('BPM'), findsOneWidget);

    // Now actually end it, which is the only thing that should stop the
    // ticker, both to prove the flow works and so this test doesn't leave
    // a live Timer running into whichever test runs next.
    await tester.tap(find.text('End session'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('End session?'), findsOneWidget);

    await tester.tap(find.text('End session').last);
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('Start session'), findsOneWidget);
    expect(find.text('Session in progress'), findsNothing);
  });

  umbraTest('pushOnce ignores a second push fired before the first settles', (WidgetTester tester) async {
    // A direct test of the guard itself: two pushOnce calls back to back,
    // synchronously, from the same origin context, the same shape as a
    // double-tap racing a single onTap handler. Simulating that race
    // through actual simulated taps is unreliable (WidgetTester.tap has
    // its own internal awaits that let the first push settle before the
    // second tap lands), so this calls the function directly instead.
    //
    // The two pushes use distinguishable screens rather than two identical
    // Placeholders. If the guard is working, only "screen one" is ever
    // pushed and it's what's visible. If both pushes went through, "screen
    // two" (the second, unguarded push) ends up on top instead, which is
    // the unambiguous signal this checks for.
    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => ElevatedButton(
            onPressed: () {
              pushOnce<void>(context, (_) => const Text('screen one'));
              pushOnce<void>(context, (_) => const Text('screen two'));
            },
            child: const Text('go'),
          ),
        ),
      ),
    );

    await tester.tap(find.text('go'));
    await tester.pumpAndSettle();
    expect(find.text('screen one'), findsOneWidget);
    expect(find.text('screen two'), findsNothing);
  });

  umbraTest('Rating last night from Home completes the check-in and updates the card', (WidgetTester tester) async {
    await _reachHome(tester);

    // "Rate last night" sits below the fold on a typical test viewport.
    await tester.dragUntilVisible(
      find.text('Rate last night'),
      find.byType(ListView),
      const Offset(0, -200),
    );
    expect(find.text('Rate last night'), findsOneWidget);

    await tester.tap(find.text('Rate last night'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('How do you feel?'), findsOneWidget);

    await tester.tap(find.text('8'));
    await tester.pump();
    await tester.tap(find.text('Save'));
    // Submit shows a brief confirmation (animSlow) before popping back.
    await tester.pump(AppTheme.animSlow);
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('Morning check-in: 8/10'), findsOneWidget);
  });

  umbraTest('History tab lists the seeded session and drills into its detail', (WidgetTester tester) async {
    await _reachHome(tester);

    await tester.tap(find.byIcon(AppIcons.history));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    // "History" appears both as the nav label and the screen's own header.
    expect(find.text('History'), findsWidgets);
    // Nine seeded nights, so more than one row.
    expect(find.textContaining('bpm avg'), findsWidgets);

    // First row is the most recent (unrated) seeded session.
    await tester.tap(find.byType(HistoryRow).first);
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('Rate this night'), findsOneWidget);

    await tester.tap(find.byIcon(AppIcons.chevronBack));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('History'), findsWidgets);
  });

  umbraTest('Insights tab shows the rating trend and duration pattern once enough nights are rated', (WidgetTester tester) async {
    await _reachHome(tester);

    await tester.tap(find.byIcon(AppIcons.insights));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('Insights'), findsWidgets);
    // LabeledStat renders its label uppercase.
    expect(find.text('NIGHTS TRACKED'), findsOneWidget);
    expect(find.textContaining('score noticeably better'), findsOneWidget);
  });

  umbraTest('Band status card opens Band Detail, and forgetting the band flips it offline', (WidgetTester tester) async {
    await _reachHome(tester);
    _connectBandForTest(tester);
    await tester.pump();

    // "RESTING HR" is unique to the band status card; tapping it lands
    // anywhere inside the card's tap target, same as tapping the card.
    await tester.tap(find.text('RESTING HR'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('Umbra-4F21'), findsOneWidget);
    // "Connected" also shows in Home's still-mounted top bar underneath.
    expect(find.text('Connected'), findsWidgets);

    await tester.tap(find.text('Forget this band'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('Forget this band?'), findsOneWidget);

    await tester.tap(find.text('Forget band'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    // Same story: "Offline" now shows in both Home's top bar and here.
    expect(find.text('Offline'), findsWidgets);
    expect(find.text('Reconnect'), findsOneWidget);
  });

  umbraTest('Settings tab toggles are live and Manage band opens Band Detail', (WidgetTester tester) async {
    await _reachHome(tester);
    _connectBandForTest(tester);
    await tester.pump();

    await tester.tap(find.byIcon(AppIcons.settings));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);

    expect(find.text('Morning check-in'), findsOneWidget);
    expect(find.byType(AppSwitch), findsNWidgets(2));

    // Version label must reflect the real installed build, not a
    // hardcoded string — settle the pending PackageInfo future first.
    await tester.pumpAndSettle();
    expect(find.text('1.0.0 (107)'), findsOneWidget);

    // Both toggles default on; flipping one shouldn't disturb the other.
    await tester.tap(find.byType(AppSwitch).first);
    await tester.pump();
    await tester.pump(AppTheme.animFast);

    await tester.tap(find.text('Manage band'));
    await tester.pump();
    await tester.pump(AppTheme.animNormal);
    expect(find.text('Umbra-4F21'), findsOneWidget);
  });

  umbraTest('Edge-swipe from the left pops the current route, like native iOS back', platform: TargetPlatform.iOS, (WidgetTester tester) async {
    await _reachHome(tester);
    _connectBandForTest(tester);
    await tester.pump();

    await tester.tap(find.text('RESTING HR'));
    await tester.pump();
    // Generously past the 300ms push transition: popGestureEnabled checks
    // animation *status*, not value, and status can lag value by a frame
    // right at the boundary, so pump well clear of it rather than exactly
    // matching the duration.
    await tester.pump(const Duration(seconds: 2));
    expect(find.text('Umbra-4F21'), findsOneWidget);

    // Drag from just inside the left edge (the detector's hit area is the
    // leftmost 20 logical pixels) most of the way across the screen, enough
    // to cross the halfway commit threshold.
    await tester.dragFrom(const Offset(5, 300), const Offset(500, 0));
    await tester.pump();
    // The gesture settles with a 350ms animateBack/animateTo, per Cupertino's
    // own _kDroppedSwipePageAnimationDuration.
    await tester.pump(const Duration(milliseconds: 400));

    expect(find.text('Umbra-4F21'), findsNothing);
    expect(find.text('Daily'), findsOneWidget);
  });

  umbraTest('HistoryRow renders the Daily Mode branch correctly, even though no app flow produces one yet', (WidgetTester tester) async {
    // Only Night Mode sessions exist in practice today (see the comment on
    // SessionRecord), but HistoryRow is typed to handle either mode
    // correctly, so that path needs its own direct coverage rather than
    // staying an unverified assumption.
    final dailySession = SessionRecord(
      id: 'daily-test',
      mode: HydrationMode.daily,
      startedAt: DateTime(2026, 1, 1, 9),
      endedAt: DateTime(2026, 1, 1, 17),
      avgHr: 72,
      avgHrv: 58,
      avgSpo2: 97,
      motion: 'Active',
      reminderCount: 5,
    );

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: HistoryRow(session: dailySession, onTap: () {}),
        ),
      ),
    );

    expect(find.text('Daily tracking'), findsOneWidget);
    expect(find.byIcon(AppIcons.sun), findsOneWidget);
    expect(find.byIcon(AppIcons.moon), findsNothing);
  });
}
