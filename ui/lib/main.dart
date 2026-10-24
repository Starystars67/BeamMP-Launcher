import 'dart:io';

import 'package:flutter/material.dart';
import 'package:path/path.dart' as p;

import 'app/app_services.dart';
import 'app/launcher_app.dart';
import 'core/engine/engine_controller.dart';
import 'core/engine/engine_locator.dart';
import 'core/engine/game_arguments.dart';
import 'core/engine/process_engine_gateway.dart';
import 'core/platform/file_revealer.dart';
import 'core/platform/game_covers.dart';
import 'core/platform/url_opener.dart';
import 'core/settings/settings_controller.dart';
import 'core/settings/settings_store.dart';
import 'core/window/native_shell.dart';
import 'core/window/window_host.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final environment = Platform.environment;
  final platform = Platform.operatingSystem;

  final settings = SettingsController(
    FileSettingsStore(
      settingsFilePath(environment: environment, platform: platform),
    ),
  );
  await settings.load();

  ({EngineCommand? command, List<String> searched}) locate() => locateEngine(
    environment: environment,
    uiExecutable: Platform.resolvedExecutable,
    platform: platform,
    exists: (path) => File(path).existsSync(),
  );

  final engine = EngineController(
    ProcessEngineGateway(
      locate: locate,
      extraArguments: () => engineGameArguments(settings.settings),
    ),
    launchArguments: (game) => settings.settings.game(game).launchArguments,
  );
  final enginePath = locate().command?.executable;

  // Only macOS has a native shell so far; elsewhere the window simply stays open.
  final WindowHost window;
  final TrayHost tray;
  final NotificationHost notifications;
  if (platform == 'macos') {
    final native = NativeShell();
    window = native;
    tray = native;
    notifications = native;
  } else {
    window = const UnsupportedShell();
    tray = const UnsupportedShell();
    notifications = const UnsupportedShell();
  }

  runApp(
    LauncherApp(
      services: AppServices(
        engine: engine,
        settings: settings,
        window: window,
        tray: tray,
        notifications: notifications,
        engineLogPath: enginePath == null
            ? null
            : p.join(p.dirname(enginePath), 'Launcher.log'),
        revealer: const SystemFileRevealer(),
        urlOpener: const SystemUrlOpener(),
        covers: SteamGameCovers.system(
          environment: environment,
          platform: platform,
        ),
      ),
    ),
  );
}
