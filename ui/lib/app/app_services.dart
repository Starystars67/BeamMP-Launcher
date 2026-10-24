import 'package:flutter/widgets.dart';

import '../core/engine/engine_controller.dart';
import '../core/platform/file_revealer.dart';
import '../core/platform/game_covers.dart';
import '../core/platform/url_opener.dart';
import '../core/settings/settings_controller.dart';
import '../core/window/window_behaviour.dart';
import '../core/window/window_host.dart';

enum AppPage { home, logs, account, settings }

/// Everything the screens share. Built once in `main`, replaced by fakes in tests.
class AppServices {
  AppServices({
    required this.engine,
    required this.settings,
    required this.window,
    required this.tray,
    this.notifications,
    this.engineLogPath,
    this.revealer,
    this.urlOpener,
    this.covers,
    this.behaviour,
  });

  final EngineController engine;
  final SettingsController settings;
  final WindowHost window;
  final TrayHost tray;
  final NotificationHost? notifications;

  /// The engine's log file, when known (for "show log file").
  final String? engineLogPath;

  /// Reveals a file in the system file manager.
  final FileRevealer? revealer;

  /// Opens web pages (the sign-in page) in the browser.
  final UrlOpener? urlOpener;

  /// Finds the cover of a game (from the user's Steam library).
  final GameCovers? covers;

  /// Set once the window behaviour exists (it needs the localised strings).
  WindowBehaviour? behaviour;

  /// The page being shown; the tray can switch it.
  final ValueNotifier<AppPage> page = ValueNotifier<AppPage>(AppPage.home);
}

class AppScope extends InheritedWidget {
  const AppScope({required this.services, required super.child, super.key});

  final AppServices services;

  static AppServices of(BuildContext context) {
    final scope = context.dependOnInheritedWidgetOfExactType<AppScope>();
    assert(scope != null, 'No AppScope above this widget');
    return scope!.services;
  }

  @override
  bool updateShouldNotify(AppScope oldWidget) => services != oldWidget.services;
}
