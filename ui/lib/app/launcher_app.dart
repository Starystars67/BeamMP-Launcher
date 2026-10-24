import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';

import '../core/settings/launcher_settings.dart';
import '../core/window/window_behaviour.dart';
import '../l10n/app_localizations.dart';
import 'app_services.dart';
import 'app_shell.dart';
import 'theme.dart';

class LauncherApp extends StatefulWidget {
  const LauncherApp({required this.services, super.key});

  final AppServices services;

  @override
  State<LauncherApp> createState() => _LauncherAppState();
}

class _LauncherAppState extends State<LauncherApp> {
  final GlobalKey<NavigatorState> _navigator = GlobalKey<NavigatorState>();
  StreamSubscription<void>? _quits;
  bool _quitting = false;

  AppServices get _services => widget.services;

  @override
  void initState() {
    super.initState();
    unawaited(_startup());
  }

  Future<void> _startup() async {
    final behaviour = WindowBehaviour(
      engine: _services.engine,
      settings: _services.settings,
      window: _services.window,
      tray: _services.tray,
      notifications: _services.notifications,
      strings: () => AppLocalizations.of(_navigator.currentContext!),
      onOpenLogs: () => _services.page.value = AppPage.logs,
    );
    _services.behaviour = behaviour;
    _quits = behaviour.quitRequests.listen((_) => unawaited(_quit()));
    await _services.engine.start();
    await behaviour.start();
  }

  Future<void> _quit() async {
    if (_quitting) {
      return;
    }
    _quitting = true;
    final context = _navigator.currentContext;
    final active = _services.engine.gameActive;
    if (active && context != null && context.mounted) {
      await _services.behaviour?.showWindow();
      if (!context.mounted) {
        _quitting = false;
        return;
      }
      final l10n = AppLocalizations.of(context);
      final confirmed = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          title: Text(l10n.quitTitle),
          content: Text(l10n.quitGameRunning),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context, false),
              child: Text(l10n.quitCancel),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(context, true),
              child: Text(l10n.quitConfirm),
            ),
          ],
        ),
      );
      if (confirmed != true) {
        _quitting = false;
        return;
      }
    }
    await _services.engine.shutdown(force: active);
    await _services.window.quit();
  }

  @override
  void dispose() {
    unawaited(_quits?.cancel());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AppScope(
    services: _services,
    child: ListenableBuilder(
      listenable: _services.settings,
      builder: (context, _) => MaterialApp(
        themeMode: switch (_services.settings.settings.theme) {
          ThemeSetting.system => ThemeMode.system,
          ThemeSetting.light => ThemeMode.light,
          ThemeSetting.dark => ThemeMode.dark,
        },
        debugShowCheckedModeBanner: false,
        navigatorKey: _navigator,
        onGenerateTitle: (context) => AppLocalizations.of(context).appTitle,
        theme: buildTheme(Brightness.light),
        darkTheme: buildTheme(Brightness.dark),
        localizationsDelegates: const [
          AppLocalizations.delegate,
          GlobalMaterialLocalizations.delegate,
          GlobalWidgetsLocalizations.delegate,
          GlobalCupertinoLocalizations.delegate,
        ],
        supportedLocales: AppLocalizations.supportedLocales,
        home: const AppShell(),
      ),
    ),
  );
}
