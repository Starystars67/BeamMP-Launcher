import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:launcher_control/launcher_control.dart';

import '../core/engine/engine_controller.dart';
import '../features/account/account_screen.dart';
import '../features/home/home_screen.dart';
import '../features/logs/logs_screen.dart';
import '../features/settings/settings_screen.dart';
import '../features/startup/startup_screen.dart';
import '../l10n/app_localizations.dart';
import 'app_services.dart';
import 'brand.dart';

/// The startup screen until the engine is ready, then the main navigation.
class AppShell extends StatelessWidget {
  const AppShell({super.key});

  static bool _ready(EngineConnection connection) =>
      connection is EngineOnline &&
      (connection.state == EngineState.ready || connection.state.gameActive);

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    return ListenableBuilder(
      listenable: engine,
      builder: (context, _) => _ready(engine.connection)
          ? const _MainNavigation()
          : const StartupScreen(),
    );
  }
}

class _MainNavigation extends StatelessWidget {
  const _MainNavigation();

  @override
  Widget build(BuildContext context) {
    final page = AppScope.of(context).page;
    final l10n = AppLocalizations.of(context);
    return ValueListenableBuilder<AppPage>(
      valueListenable: page,
      builder: (context, current, _) => CallbackShortcuts(
        bindings: _pageShortcuts(page),
        child: Focus(
          autofocus: true,
          child: Scaffold(
            body: Row(
              children: [
                NavigationRail(
                  selectedIndex: current.index,
                  onDestinationSelected: (index) =>
                      page.value = AppPage.values[index],
                  labelType: NavigationRailLabelType.all,
                  leading: const Padding(
                    padding: EdgeInsets.symmetric(vertical: 12),
                    child: BeamMpLogo(width: 60),
                  ),
                  destinations: [
                    NavigationRailDestination(
                      icon: const Icon(Icons.play_circle_outline),
                      selectedIcon: const Icon(Icons.play_circle),
                      label: Text(l10n.navHome),
                    ),
                    NavigationRailDestination(
                      icon: const Icon(Icons.notes_outlined),
                      selectedIcon: const Icon(Icons.notes),
                      label: Text(l10n.navLogs),
                    ),
                    NavigationRailDestination(
                      icon: const Icon(Icons.person_outline),
                      selectedIcon: const Icon(Icons.person),
                      label: Text(l10n.navAccount),
                    ),
                    NavigationRailDestination(
                      icon: const Icon(Icons.settings_outlined),
                      selectedIcon: const Icon(Icons.settings),
                      label: Text(l10n.navSettings),
                    ),
                  ],
                ),
                const VerticalDivider(width: 1),
                Expanded(
                  child: switch (current) {
                    AppPage.home => const HomeScreen(),
                    AppPage.logs => const LogsScreen(),
                    AppPage.account => const AccountScreen(),
                    AppPage.settings => const SettingsScreen(),
                  },
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Cmd+1..4 (Ctrl+1..4 elsewhere) jump to a page.
Map<ShortcutActivator, VoidCallback> _pageShortcuts(
  ValueNotifier<AppPage> page,
) {
  const keys = [
    LogicalKeyboardKey.digit1,
    LogicalKeyboardKey.digit2,
    LogicalKeyboardKey.digit3,
    LogicalKeyboardKey.digit4,
  ];
  return {
    for (final (index, key) in keys.indexed) ...{
      SingleActivator(key, meta: true): () =>
          page.value = AppPage.values[index],
      SingleActivator(key, control: true): () =>
          page.value = AppPage.values[index],
    },
  };
}
