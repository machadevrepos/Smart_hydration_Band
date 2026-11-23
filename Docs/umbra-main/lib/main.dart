import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';
import 'core/state/band_controller.dart';
import 'core/state/session_controller.dart';
import 'core/state/session_store.dart';
import 'core/state/settings_controller.dart';
import 'core/theme/app_theme_data.dart';
import 'features/splash/splash_screen.dart';

void main() {
  SystemChrome.setSystemUIOverlayStyle(umbraSystemUiOverlayStyle);
  runApp(const UmbraApp());
}

class UmbraApp extends StatelessWidget {
  const UmbraApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => SessionStore()),
        ChangeNotifierProvider(create: (_) => SessionController()),
        ChangeNotifierProvider(create: (_) => BandController()),
        ChangeNotifierProvider(create: (_) => SettingsController()),
      ],
      child: MaterialApp(
        title: 'Umbra',
        debugShowCheckedModeBanner: false,
        theme: buildUmbraTheme(),
        home: const SplashScreen(),
      ),
    );
  }
}
