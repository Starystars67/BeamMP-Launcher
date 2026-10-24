import 'dart:async';
import 'dart:io';

import 'package:beammp_launcher/app/app_services.dart';
import 'package:beammp_launcher/app/brand.dart';
import 'package:beammp_launcher/app/launcher_app.dart';
import 'package:beammp_launcher/core/engine/engine_controller.dart';
import 'package:beammp_launcher/core/platform/file_revealer.dart';
import 'package:beammp_launcher/core/platform/game_covers.dart';
import 'package:beammp_launcher/core/platform/url_opener.dart';
import 'package:beammp_launcher/core/settings/launcher_settings.dart';
import 'package:beammp_launcher/core/settings/settings_controller.dart';
import 'package:beammp_launcher/core/settings/settings_store.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fake_shell.dart';
import 'fakes.dart';

class MemorySettingsStore implements SettingsStore {
  LauncherSettings saved = const LauncherSettings();

  @override
  Future<LauncherSettings> load() async => saved;

  @override
  Future<void> save(LauncherSettings settings) async => saved = settings;
}

/// Loads Roboto and the Material icons from the Flutter SDK so rendered
/// goldens show real text instead of test blocks.
Future<void> loadFonts() async {
  final root = Platform.environment['FLUTTER_ROOT'];
  if (root == null) {
    return;
  }
  final directory = '$root/bin/cache/artifacts/material_fonts';
  Future<void> load(String family, List<String> files) async {
    final loader = FontLoader(family);
    for (final file in files) {
      final path = '$directory/$file';
      if (File(path).existsSync()) {
        loader.addFont(
          Future.value(ByteData.sublistView(File(path).readAsBytesSync())),
        );
      }
    }
    await loader.load();
  }

  await load('Roboto', [
    'Roboto-Regular.ttf',
    'Roboto-Medium.ttf',
    'Roboto-Bold.ttf',
  ]);
  await load('MaterialIcons', ['MaterialIcons-Regular.otf']);
}

/// Covers by Steam app id; nothing unless a test adds one.
class FakeGameCovers implements GameCovers {
  final Map<int, ImageProvider> covers = {};
  final List<int> asked = [];

  @override
  Future<ImageProvider?> coverFor(int steamAppId) async {
    asked.add(steamAppId);
    return covers[steamAppId];
  }
}

class FakeRevealer implements FileRevealer {
  final List<String> revealed = [];
  bool exists = true;

  @override
  Future<bool> reveal(String path) async {
    revealed.add(path);
    return exists;
  }
}

class FakeUrlOpener implements UrlOpener {
  final List<String> opened = [];
  bool works = true;

  @override
  Future<bool> open(String url) async {
    opened.add(url);
    return works;
  }
}

class Harness {
  Harness({
    LauncherSettings settings = const LauncherSettings(),
    bool trayAvailable = true,
  }) : gateway = FakeGateway(),
       shell = FakeShell(trayAvailable: trayAvailable),
       store = MemorySettingsStore() {
    store.saved = settings;
    this.settings = SettingsController(store, initial: settings);
    engine = EngineController(
      gateway,
      shutdownTimeout: const Duration(milliseconds: 50),
      launchArguments: (game) =>
          this.settings.settings.game(game).launchArguments,
    );
    services = AppServices(
      engine: engine,
      settings: this.settings,
      window: shell,
      tray: shell,
      notifications: shell,
      engineLogPath: '/engine/Launcher.log',
      revealer: revealer,
      urlOpener: urlOpener,
      covers: covers,
    );
  }

  final FakeGateway gateway;
  final FakeShell shell;
  final MemorySettingsStore store;
  final FakeRevealer revealer = FakeRevealer();
  final FakeUrlOpener urlOpener = FakeUrlOpener();
  final FakeGameCovers covers = FakeGameCovers();
  late final EngineController engine;
  late final SettingsController settings;
  late final AppServices services;

  FakeEngineLink get link => gateway.link;

  /// Pumps the whole app at a fixed size.
  Future<void> pump(
    WidgetTester tester, {
    Brightness brightness = Brightness.light,
    Size size = const Size(980, 660),
  }) async {
    tester.view.physicalSize = size;
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    tester.platformDispatcher.platformBrightnessTestValue = brightness;
    addTearDown(tester.platformDispatcher.clearPlatformBrightnessTestValue);
    await tester.pumpWidget(LauncherApp(services: services));
    // the logo is decoded off the test clock: have it ready before any screenshot
    await tester.runAsync(() async {
      for (final path in beamMpLogoAssets) {
        await precacheImage(
          AssetImage(path),
          tester.element(find.byType(MaterialApp)),
        );
      }
    });
    await tester.pump(const Duration(milliseconds: 20));
    await tester.pump(const Duration(milliseconds: 20));
  }

  Future<void> settleUi(WidgetTester tester) async {
    await tester.pump(const Duration(milliseconds: 30));
    await tester.pump(const Duration(milliseconds: 30));
  }

  void dispose() => engine.dispose();
}

Future<void> waitFor(Future<void> Function() step) => step();

Completer<void> gate(FakeGateway gateway) => gateway.gate = Completer<void>();
