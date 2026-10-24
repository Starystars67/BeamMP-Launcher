import 'package:beammp_launcher/core/engine/engine_controller.dart';
import 'package:beammp_launcher/core/settings/launcher_settings.dart';
import 'package:beammp_launcher/core/settings/settings_controller.dart';
import 'package:beammp_launcher/core/settings/settings_store.dart';
import 'package:beammp_launcher/core/window/tray_menu.dart';
import 'package:beammp_launcher/core/window/window_behaviour.dart';
import 'package:beammp_launcher/l10n/app_localizations.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/fake_shell.dart';
import 'support/fakes.dart';

class _MemoryStore implements SettingsStore {
  LauncherSettings saved = const LauncherSettings();

  @override
  Future<LauncherSettings> load() async => saved;

  @override
  Future<void> save(LauncherSettings settings) async => saved = settings;
}

void main() {
  late FakeGateway gateway;
  late EngineController engine;
  late SettingsController settings;
  late FakeShell shell;
  late WindowBehaviour behaviour;
  late int logsOpened;
  late int quitRequests;

  Future<void> setUpWith({
    bool trayAvailable = true,
    LauncherSettings initial = const LauncherSettings(),
  }) async {
    gateway = FakeGateway();
    engine = EngineController(gateway);
    settings = SettingsController(_MemoryStore(), initial: initial);
    shell = FakeShell(trayAvailable: trayAvailable);
    logsOpened = 0;
    quitRequests = 0;
    behaviour = WindowBehaviour(
      engine: engine,
      settings: settings,
      window: shell,
      tray: shell,
      notifications: shell,
      strings: () => lookupAppLocalizations(const Locale('en')),
      onOpenLogs: () => logsOpened++,
    );
    behaviour.quitRequests.listen((_) => quitRequests++);
    await engine.start();
    await behaviour.start();
    shell.calls.clear();
  }

  tearDown(() async {
    await behaviour.dispose();
    engine.dispose();
  });

  Future<void> gameState(GameState state) async {
    gateway.link.emit(GameStateChanged(game: 'beamng', state: state, pid: 1));
    await settle();
  }

  group('tray', () {
    test('is shown when enabled and available', () async {
      shell = FakeShell();
      await setUpWith();
      expect(behaviour.trayActive, isTrue);
      expect(shell.trayVisible, isTrue);
    });

    test(
      'is not shown when the system has none, whatever the setting',
      () async {
        await setUpWith(trayAvailable: false);
        expect(behaviour.trayAvailable, isFalse);
        expect(behaviour.trayActive, isFalse);
        expect(shell.trayVisible, isFalse);
      },
    );

    test('follows the setting', () async {
      await setUpWith();
      await settings.update((s) => s.copyWith(trayEnabled: false));
      await settle();
      expect(shell.trayVisible, isFalse);
      await settings.update((s) => s.copyWith(trayEnabled: true));
      await settle();
      expect(shell.trayVisible, isTrue);
    });

    test(
      'the menu shows the game status and enables Play only when ready',
      () async {
        await setUpWith();
        await settle();
        expect(shell.item('status').label, 'BeamNG.drive - Ready to play');
        expect(shell.item(TrayAction.play).enabled, isTrue);
        expect(
          shell.item(TrayAction.toggleWindow).label,
          'Hide BeamMP Launcher',
        );

        gateway.link.emit(
          const EngineStateChanged(EngineState.launchingGame, ''),
        );
        await gameState(GameState.launching);
        expect(shell.item('status').label, 'BeamNG.drive - Starting…');
        expect(shell.item(TrayAction.play).enabled, isFalse);
      },
    );

    test('menu actions: play, logs, quit', () async {
      await setUpWith();
      shell.userPicksTrayItem(TrayAction.play);
      await settle();
      expect(gateway.link.calls, contains('launchGame:beamng'));

      shell.userPicksTrayItem(TrayAction.openLogs);
      await settle();
      expect(shell.calls, contains('show'));
      expect(logsOpened, 1);

      shell.userPicksTrayItem(TrayAction.quit);
      await settle();
      expect(quitRequests, 1);
    });

    test('the toggle item hides and shows the window', () async {
      await setUpWith();
      shell.userPicksTrayItem(TrayAction.toggleWindow);
      await settle();
      expect(shell.calls, ['hide']);
      expect(shell.item(TrayAction.toggleWindow).label, 'Show BeamMP Launcher');

      shell.userPicksTrayItem(TrayAction.toggleWindow);
      await settle();
      expect(shell.calls, ['hide', 'show']);
    });
  });

  group('when the game starts', () {
    test('hides to the tray by default when there is a tray', () async {
      await setUpWith();
      await gameState(GameState.launching);
      expect(shell.calls, ['hide']);
      expect(behaviour.windowAway, isTrue);
    });

    test('stays open by default when there is no tray', () async {
      await setUpWith(trayAvailable: false);
      await gameState(GameState.launching);
      expect(shell.calls, isEmpty);
      expect(behaviour.windowAway, isFalse);
    });

    test('stays open by default when the tray is switched off', () async {
      await setUpWith(initial: const LauncherSettings(trayEnabled: false));
      await gameState(GameState.launching);
      expect(shell.calls, isEmpty);
    });

    test('minimises when asked to', () async {
      await setUpWith(
        initial: const LauncherSettings(
          launchWindowBehavior: LaunchWindowBehavior.minimise,
        ),
      );
      await gameState(GameState.launching);
      expect(shell.calls, ['minimise']);
    });

    test('stays open when asked to, even with a tray', () async {
      await setUpWith(
        initial: const LauncherSettings(
          launchWindowBehavior: LaunchWindowBehavior.stayOpen,
        ),
      );
      await gameState(GameState.launching);
      expect(shell.calls, isEmpty);
    });

    test(
      '"hide to tray" without a tray falls back to minimising, never to a lost window',
      () async {
        await setUpWith(
          trayAvailable: false,
          initial: const LauncherSettings(
            launchWindowBehavior: LaunchWindowBehavior.hideToTray,
          ),
        );
        await gameState(GameState.launching);
        expect(shell.calls, ['minimise']);
      },
    );

    test('acts once per game, not for every state change', () async {
      await setUpWith();
      await gameState(GameState.launching);
      await gameState(GameState.running);
      await gameState(GameState.running);
      expect(shell.calls.where((c) => c == 'hide'), hasLength(1));
    });
  });

  group('when the game ends', () {
    test('the window comes back without taking the focus', () async {
      await setUpWith();
      await gameState(GameState.launching);
      await gameState(GameState.running);
      shell.calls.clear();
      await gameState(GameState.exited);
      expect(shell.calls, ['show-inactive']);
      expect(behaviour.windowAway, isFalse);
    });

    test('a minimised window comes back too', () async {
      await setUpWith(
        initial: const LauncherSettings(
          launchWindowBehavior: LaunchWindowBehavior.minimise,
        ),
      );
      await gameState(GameState.launching);
      shell.calls.clear();
      await gameState(GameState.exited);
      expect(shell.calls, ['show-inactive']);
    });

    test('nothing happens when restoring is off', () async {
      await setUpWith(
        initial: const LauncherSettings(restoreWindowOnGameExit: false),
      );
      await gameState(GameState.launching);
      shell.calls.clear();
      await gameState(GameState.exited);
      expect(shell.calls, isNot(contains('show-inactive')));
      expect(behaviour.windowAway, isTrue);
    });

    test('an untouched window is left alone', () async {
      await setUpWith(trayAvailable: false);
      await gameState(GameState.launching);
      await gameState(GameState.exited);
      expect(shell.calls, isEmpty);
    });

    test('the user bringing the window back first is respected', () async {
      await setUpWith();
      await gameState(GameState.launching);
      shell.userRestoresWindow();
      await settle();
      shell.calls.clear();
      await gameState(GameState.exited);
      expect(shell.calls, isEmpty);
    });
  });

  group('closing the window', () {
    test('quits by default', () async {
      await setUpWith();
      shell.userClosesWindow();
      await settle();
      expect(quitRequests, 1);
      expect(shell.calls, isEmpty);
    });

    test('hides to the tray when that is chosen and there is a tray', () async {
      await setUpWith(
        initial: const LauncherSettings(
          closeBehavior: CloseBehavior.closeToTray,
        ),
      );
      shell.userClosesWindow();
      await settle();
      expect(shell.calls, ['hide']);
      expect(quitRequests, 0);
    });

    test('quits instead when there is no tray to come back from', () async {
      await setUpWith(
        trayAvailable: false,
        initial: const LauncherSettings(
          closeBehavior: CloseBehavior.closeToTray,
        ),
      );
      shell.userClosesWindow();
      await settle();
      expect(quitRequests, 1);
      expect(shell.calls, isEmpty);
    });

    test('Cmd+Q is always a quit request', () async {
      await setUpWith(
        initial: const LauncherSettings(
          closeBehavior: CloseBehavior.closeToTray,
        ),
      );
      shell.userQuits();
      await settle();
      expect(quitRequests, 1);
    });
  });

  group('notifications', () {
    Future<void> gameEnds({String? detail}) async {
      gateway.link.emit(
        GameStateChanged(
          game: 'beamng',
          state: GameState.exited,
          detail: detail,
        ),
      );
      await settle();
    }

    test('say so when the game closed and the window stays away', () async {
      await setUpWith(
        initial: const LauncherSettings(restoreWindowOnGameExit: false),
      );
      await gameState(GameState.launching);
      await gameEnds(detail: 'The game exited with code 0');
      expect(shell.notifications, [
        (title: 'BeamNG.drive closed', body: 'The game exited with code 0'),
      ]);
    });

    test('fall back to a plain text without a detail', () async {
      await setUpWith(
        initial: const LauncherSettings(restoreWindowOnGameExit: false),
      );
      await gameState(GameState.launching);
      await gameEnds();
      expect(shell.notifications.single.body, 'Closed');
    });

    test('are not needed when the window comes back by itself', () async {
      await setUpWith();
      await gameState(GameState.launching);
      await gameEnds();
      expect(shell.calls, contains('show-inactive'));
      expect(shell.notifications, isEmpty);
    });

    test('are not sent when the window never left', () async {
      await setUpWith(trayAvailable: false);
      await gameState(GameState.launching);
      await gameEnds();
      expect(shell.notifications, isEmpty);
    });

    test('can be switched off', () async {
      await setUpWith(
        initial: const LauncherSettings(
          restoreWindowOnGameExit: false,
          notificationsEnabled: false,
        ),
      );
      await gameState(GameState.launching);
      await gameEnds();
      expect(shell.notifications, isEmpty);
    });

    test(
      'the engine stopping while the window is away brings it back and says why',
      () async {
        await setUpWith();
        await gameState(GameState.launching);
        shell.calls.clear();
        gateway.link.emit(
          const FatalReported('fatal', 'Failed to find the game'),
        );
        await settle();
        gateway.link.close();
        await settle();
        expect(shell.calls, contains('show-inactive'));
        expect(shell.notifications, [
          (
            title: 'The launcher engine stopped',
            body: 'Failed to find the game',
          ),
        ]);
      },
    );

    test('an engine that vanishes gets the generic text', () async {
      await setUpWith();
      await gameState(GameState.launching);
      gateway.link.close();
      await settle();
      expect(
        shell.notifications.single.body,
        'The launcher engine stopped unexpectedly.',
      );
    });

    test(
      'the window still comes back for a failed engine when notifications are off',
      () async {
        await setUpWith(
          initial: const LauncherSettings(notificationsEnabled: false),
        );
        await gameState(GameState.launching);
        shell.calls.clear();
        gateway.link.close();
        await settle();
        expect(shell.calls, contains('show-inactive'));
        expect(shell.notifications, isEmpty);
      },
    );

    test(
      'a failed engine is one notification, even when games do not restore the window',
      () async {
        await setUpWith(
          initial: const LauncherSettings(restoreWindowOnGameExit: false),
        );
        await gameState(GameState.launching);
        shell.calls.clear();
        gateway.link.close();
        await settle();
        expect(shell.calls, contains('show-inactive'));
        expect(shell.notifications.map((n) => n.title), [
          'The launcher engine stopped',
        ]);
      },
    );

    test(
      'a visible window needs no notification for a failed engine',
      () async {
        await setUpWith();
        gateway.link.close();
        await settle();
        expect(shell.notifications, isEmpty);
      },
    );

    test('one failure is announced once', () async {
      await setUpWith();
      await gameState(GameState.launching);
      gateway.link.close();
      await settle();
      await settle();
      expect(shell.notifications, hasLength(1));
    });
  });

  test(
    'turning the tray off while the window is hidden brings the window back',
    () async {
      await setUpWith(
        initial: const LauncherSettings(
          closeBehavior: CloseBehavior.closeToTray,
        ),
      );
      shell.userClosesWindow();
      await settle();
      shell.calls.clear();

      await settings.update((s) => s.copyWith(trayEnabled: false));
      await settle();
      expect(shell.calls, containsAllInOrder(['tray-off', 'show']));
      expect(behaviour.windowAway, isFalse);
    },
  );

  test('the menu follows the engine going away', () async {
    await setUpWith();
    gateway.link.close();
    await settle();
    expect(engine.connection, isA<EngineFailed>());
    expect(shell.item(TrayAction.play).enabled, isFalse);
  });
}
