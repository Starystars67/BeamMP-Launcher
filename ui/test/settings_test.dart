import 'dart:convert';
import 'dart:io';

import 'package:beammp_launcher/core/settings/launcher_settings.dart';
import 'package:beammp_launcher/core/settings/settings_controller.dart';
import 'package:beammp_launcher/core/settings/settings_store.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('LauncherSettings', () {
    test('the defaults match the design', () {
      const settings = LauncherSettings();
      expect(settings.trayEnabled, isTrue);
      expect(settings.launchWindowBehavior, LaunchWindowBehavior.auto);
      expect(settings.closeBehavior, CloseBehavior.quit);
      expect(settings.restoreWindowOnGameExit, isTrue);
      expect(settings.notificationsEnabled, isTrue);
      expect(settings.theme, ThemeSetting.system);
      expect(settings.games, isEmpty);
    });

    test('survives a JSON round trip', () {
      const settings = LauncherSettings(
        trayEnabled: false,
        launchWindowBehavior: LaunchWindowBehavior.minimise,
        closeBehavior: CloseBehavior.closeToTray,
        restoreWindowOnGameExit: false,
        notificationsEnabled: false,
        theme: ThemeSetting.dark,
        games: {
          'beamng': GameSettings(
            directory: '/games/BeamNG',
            command: '/opt/run-beamng.sh',
            userPath: '/users/x/BeamNG.drive',
            launchArguments: '-console "a b"',
          ),
          'gearblocks': GameSettings(command: '/opt/start-gearblocks.sh'),
        },
      );
      expect(
        LauncherSettings.fromJson(jsonDecode(jsonEncode(settings.toJson()))),
        settings,
      );
    });

    test('uses the documented key names', () {
      expect(
        const LauncherSettings().toJson().keys,
        containsAll([
          'tray.enabled',
          'launch.windowBehavior',
          'close.behavior',
          'gameExit.restoreWindow',
          'notifications.enabled',
          'appearance.theme',
          'games',
        ]),
      );
    });

    test('anything unreadable falls back to the default, field by field', () {
      expect(LauncherSettings.fromJson(null), const LauncherSettings());
      expect(LauncherSettings.fromJson('nope'), const LauncherSettings());
      expect(LauncherSettings.fromJson([1, 2]), const LauncherSettings());

      final partly = LauncherSettings.fromJson({
        'tray.enabled': 'yes',
        'launch.windowBehavior': 'teleport',
        'close.behavior': 'closeToTray',
        'appearance.theme': 5,
        'games': 'nope',
        'notifications.enabled': false,
      });
      expect(partly.trayEnabled, isTrue);
      expect(partly.launchWindowBehavior, LaunchWindowBehavior.auto);
      expect(partly.closeBehavior, CloseBehavior.closeToTray);
      expect(partly.theme, ThemeSetting.system);
      expect(partly.games, isEmpty);
      expect(partly.notificationsEnabled, isFalse);
    });

    test('the launch arguments of before several games become BeamNG\'s', () {
      final old = LauncherSettings.fromJson({
        'game.launchArguments': '-console',
      });
      expect(old.game('beamng').launchArguments, '-console');
      expect(old.game('gearblocks').isEmpty, isTrue);

      // the new place wins over the old one
      final both = LauncherSettings.fromJson({
        'game.launchArguments': '-old',
        'games': {
          'beamng': {'launchArguments': '-new'},
        },
      });
      expect(both.game('beamng').launchArguments, '-new');
    });

    test('a game with nothing set is not kept', () {
      final loaded = LauncherSettings.fromJson({
        'games': {
          'beamng': {'directory': '', 'command': 5},
          'gearblocks': {'command': '/start'},
          'broken': 'x',
        },
      });
      expect(loaded.games.keys, ['gearblocks']);

      const set = LauncherSettings(
        games: {'beamng': GameSettings(directory: '/x')},
      );
      final cleared = set.copyWithGame(
        'beamng',
        (g) => g.copyWith(directory: ''),
      );
      expect(cleared.games, isEmpty);
    });

    test('game settings compare by value, whatever the order', () {
      const a = LauncherSettings(
        games: {
          'beamng': GameSettings(directory: '/x'),
          'gearblocks': GameSettings(command: '/y'),
        },
      );
      const b = LauncherSettings(
        games: {
          'gearblocks': GameSettings(command: '/y'),
          'beamng': GameSettings(directory: '/x'),
        },
      );
      expect(a, b);
      expect(a.hashCode, b.hashCode);
      expect(
        a,
        isNot(a.copyWithGame('beamng', (g) => g.copyWith(command: 'z'))),
      );
    });

    test('only directory, command and user folder matter to the engine', () {
      const base = GameSettings(directory: '/x', launchArguments: '-a');
      expect(
        base.sameEngineSettingsAs(base.copyWith(launchArguments: '-b')),
        isTrue,
      );
      expect(
        base.sameEngineSettingsAs(base.copyWith(directory: '/y')),
        isFalse,
      );
      expect(base.sameEngineSettingsAs(base.copyWith(command: '/c')), isFalse);
      expect(base.sameEngineSettingsAs(base.copyWith(userPath: '/u')), isFalse);
    });

    test('settings written by an older version still load', () {
      final old = LauncherSettings.fromJson({'tray.enabled': false});
      expect(old.trayEnabled, isFalse);
      expect(old.notificationsEnabled, isTrue);
    });

    test('copyWith changes only what it is given', () {
      const base = LauncherSettings(
        games: {'beamng': GameSettings(launchArguments: 'x')},
      );
      final changed = base.copyWith(theme: ThemeSetting.light);
      expect(changed.theme, ThemeSetting.light);
      expect(changed.game('beamng').launchArguments, 'x');
      expect(changed, isNot(base));
      expect(base.copyWith(), base);
      expect(base.copyWith().hashCode, base.hashCode);
    });
  });

  group('settingsFilePath', () {
    test('macOS', () {
      expect(
        settingsFilePath(environment: {'HOME': '/Users/me'}, platform: 'macos'),
        '/Users/me/Library/Application Support/BeamMP/launcher-ui.json',
      );
    });

    test('Linux honours XDG_CONFIG_HOME', () {
      expect(
        settingsFilePath(
          environment: {'HOME': '/home/me', 'XDG_CONFIG_HOME': '/cfg'},
          platform: 'linux',
        ),
        '/cfg/beammp/launcher-ui.json',
      );
      expect(
        settingsFilePath(environment: {'HOME': '/home/me'}, platform: 'linux'),
        '/home/me/.config/beammp/launcher-ui.json',
      );
    });
  });

  group('FileSettingsStore', () {
    late Directory directory;
    late String path;

    setUp(() {
      directory = Directory.systemTemp.createTempSync('beammp-settings-test');
      path = '${directory.path}/nested/launcher-ui.json';
    });

    tearDown(() => directory.deleteSync(recursive: true));

    test('a missing file gives the defaults', () async {
      expect(await FileSettingsStore(path).load(), const LauncherSettings());
    });

    test('saves (creating folders) and loads', () async {
      final store = FileSettingsStore(path);
      await store.save(
        const LauncherSettings(
          theme: ThemeSetting.dark,
          games: {'beamng': GameSettings(launchArguments: '-x')},
        ),
      );
      expect(File(path).existsSync(), isTrue);
      expect(File('$path.tmp').existsSync(), isFalse);
      final loaded = await FileSettingsStore(path).load();
      expect(loaded.theme, ThemeSetting.dark);
      expect(loaded.game('beamng').launchArguments, '-x');
    });

    test('a corrupt file gives the defaults instead of an error', () async {
      File(path)
        ..createSync(recursive: true)
        ..writeAsStringSync('{ not json');
      expect(await FileSettingsStore(path).load(), const LauncherSettings());
    });

    test('a later save replaces an earlier one', () async {
      final store = FileSettingsStore(path);
      await store.save(_arguments('first'));
      await store.save(_arguments('second'));
      expect((await store.load()).game('beamng').launchArguments, 'second');
    });
  });

  group('SettingsController', () {
    test('changes apply at once and are saved in order', () async {
      final saved = <String>[];
      final controller = SettingsController(_RecordingStore(saved));
      var notifications = 0;
      controller.addListener(() => notifications++);

      final first = controller.update(
        (s) =>
            s.copyWithGame('beamng', (g) => g.copyWith(launchArguments: 'a')),
      );
      final second = controller.update(
        (s) =>
            s.copyWithGame('beamng', (g) => g.copyWith(launchArguments: 'b')),
      );
      expect(controller.settings.game('beamng').launchArguments, 'b');
      await Future.wait([first, second]);
      expect(saved, ['a', 'b']);
      expect(notifications, 2);
    });

    test('an unchanged value is neither notified nor saved', () async {
      final saved = <String>[];
      final controller = SettingsController(_RecordingStore(saved));
      var notifications = 0;
      controller.addListener(() => notifications++);
      await controller.update((s) => s);
      expect(notifications, 0);
      expect(saved, isEmpty);
    });

    test('a store that cannot save does not break the app', () async {
      final controller = SettingsController(_FailingStore());
      await controller.update((s) => s.copyWith(theme: ThemeSetting.dark));
      expect(controller.settings.theme, ThemeSetting.dark);
      await controller.update((s) => s.copyWith(theme: ThemeSetting.light));
      expect(controller.settings.theme, ThemeSetting.light);
    });

    test('load replaces the settings with the stored ones', () async {
      final controller = SettingsController(
        _FixedStore(const LauncherSettings(theme: ThemeSetting.light)),
      );
      await controller.load();
      expect(controller.settings.theme, ThemeSetting.light);
    });
  });
}

LauncherSettings _arguments(String text) =>
    LauncherSettings(games: {'beamng': GameSettings(launchArguments: text)});

class _RecordingStore implements SettingsStore {
  _RecordingStore(this.saved);

  final List<String> saved;

  @override
  Future<LauncherSettings> load() async => const LauncherSettings();

  @override
  Future<void> save(LauncherSettings settings) async {
    await Future<void>.delayed(const Duration(milliseconds: 5));
    saved.add(settings.game('beamng').launchArguments);
  }
}

class _FailingStore implements SettingsStore {
  @override
  Future<LauncherSettings> load() async => const LauncherSettings();

  @override
  Future<void> save(LauncherSettings settings) async =>
      throw const FileSystemException('disk full');
}

class _FixedStore implements SettingsStore {
  _FixedStore(this.settings);

  final LauncherSettings settings;

  @override
  Future<LauncherSettings> load() async => settings;

  @override
  Future<void> save(LauncherSettings settings) async {}
}
