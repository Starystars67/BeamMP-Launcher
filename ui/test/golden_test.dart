import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/harness.dart';

// Rendered locally (macOS, Roboto from the SDK): `flutter test --update-goldens test/golden_test.dart`.
void main() {
  late Harness harness;

  setUpAll(loadFonts);
  tearDown(() => harness.dispose());

  Future<void> expectGolden(String name) => expectLater(
    find.byType(MaterialApp),
    matchesGoldenFile('goldens/$name.png'),
  );

  testWidgets('startup', (tester) async {
    harness = Harness();
    harness.gateway.gate = Completer<void>();
    await harness.pump(tester);
    await expectGolden('startup');
  });

  testWidgets('startup preparing', (tester) async {
    harness = Harness();
    harness.link.snapshot = const EngineSnapshot(
      state: EngineState.preparingGame,
      detail: 'Checking the multiplayer mod',
    );
    await harness.pump(tester);
    await expectGolden('startup_preparing');
  });

  testWidgets('failure', (tester) async {
    harness = Harness();
    harness.gateway.failure = const EngineStartException(
      'exited',
      exitCode: 1,
      output: [
        '[1/10/2026 17:19:26] [ERROR] Failed to find the game please launch it. code 11',
        'engine exited with code 1',
      ],
    );
    await harness.pump(tester);
    await tester.tap(find.text('Show details'));
    await tester.pump();
    await expectGolden('failure');
  });

  for (final brightness in Brightness.values) {
    testWidgets('home in a room ${brightness.name}', (tester) async {
      harness = Harness();
      harness.link.games = const [
        GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.ready,
          sessions: true,
        ),
      ];
      await harness.pump(
        tester,
        brightness: brightness,
        size: const Size(980, 760),
      );
      harness.link.emit(
        const SessionChanged(
          SessionInfo(
            game: 'gearblocks',
            state: SessionState.inRoom,
            role: 'guest',
            code: 'ABC234',
            selfId: 3,
            hostId: 1,
            peers: [
              SessionPeer(id: 1, name: 'Host'),
              SessionPeer(id: 2, name: 'Bob'),
              SessionPeer(id: 3, name: 'Ann'),
            ],
          ),
        ),
      );
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 50));
      await expectGolden('home_room_${brightness.name}');
    });
  }

  testWidgets('home before a room', (tester) async {
    harness = Harness();
    harness.link.games = const [
      GameInfo(
        id: 'gearblocks',
        name: 'GearBlocks',
        state: GameState.ready,
        sessions: true,
      ),
    ];
    await harness.pump(tester, size: const Size(980, 560));
    await expectGolden('home_room_idle');
  });

  for (final brightness in Brightness.values) {
    testWidgets('home covers ${brightness.name}', (tester) async {
      harness = Harness();
      harness.link.games = const [
        GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.ready,
          steamAppId: 284160,
        ),
        GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.notFound,
          detail: 'GearBlocks.exe was not found',
          steamAppId: 1305080,
        ),
      ];
      final cover = MemoryImage(
        File('test/fixtures/test_cover.png').readAsBytesSync(),
      );
      harness.covers.covers[284160] = cover;
      await harness.pump(tester, brightness: brightness);
      // decode the art off the test clock so it is there for the screenshot
      await tester.runAsync(
        () => precacheImage(cover, tester.element(find.byType(MaterialApp))),
      );
      await tester.pump(const Duration(milliseconds: 50));
      await tester.pump(const Duration(milliseconds: 50));
      await expectGolden('home_covers_${brightness.name}');
    });
  }

  for (final brightness in Brightness.values) {
    testWidgets('home ready ${brightness.name}', (tester) async {
      harness = Harness();
      await harness.pump(tester, brightness: brightness);
      await expectGolden('home_ready_${brightness.name}');
    });
  }

  testWidgets('home two games', (tester) async {
    harness = Harness();
    harness.link.games = [
      const GameInfo(
        id: 'beamng',
        name: 'BeamNG.drive',
        state: GameState.ready,
      ),
      const GameInfo(
        id: 'gearblocks',
        name: 'GearBlocks',
        state: GameState.unavailable,
      ),
    ];
    await harness.pump(tester);
    await expectGolden('home_two_games');
  });

  testWidgets('home running', (tester) async {
    harness = Harness();
    await harness.pump(tester);
    harness.link.emit(const EngineStateChanged(EngineState.launchingGame, ''));
    harness.link.emit(const EngineStateChanged(EngineState.gameRunning, ''));
    harness.link.emit(
      const GameStateChanged(game: 'beamng', state: GameState.running, pid: 42),
    );
    await harness.settleUi(tester);
    await expectGolden('home_running');
  });

  testWidgets('home launch error', (tester) async {
    harness = Harness();
    await harness.pump(tester);
    harness.link.launchError = const ControlException(
      'launch_failed',
      'Exec format error',
    );
    await tester.tap(find.widgetWithText(FilledButton, 'Play'));
    await harness.settleUi(tester);
    await expectGolden('home_launch_error');
  });

  testWidgets('logs', (tester) async {
    harness = Harness();
    await harness.pump(tester);
    final base = DateTime.utc(2026, 10, 1, 17, 19, 26);
    for (final (i, (level, message)) in [
      (LogLevel.info, 'BeamMP Launcher v2.8.1'),
      (LogLevel.info, 'Game Version : 0.39.4.0'),
      (
        LogLevel.warn,
        'Found custom user folder path in startup.ini but it does not exist, skipping',
      ),
      (
        LogLevel.error,
        'GET to https://backend.beammp.com/sha/mod failed: Could not resolve host',
      ),
      (LogLevel.info, 'Control channel listening on 127.0.0.1:51881'),
    ].indexed) {
      harness.link.emit(
        LogReceived(
          LogEntry(
            time: base.add(Duration(seconds: i)),
            level: level,
            message: message,
          ),
        ),
      );
    }
    await tester.tap(find.text('Logs'));
    await harness.settleUi(tester);
    await expectGolden('logs');
  });

  testWidgets('account signed in', (tester) async {
    harness = Harness();
    await harness.pump(tester);
    harness.link.emit(
      const AccountChanged(
        AccountInfo(
          signedIn: true,
          username: 'Starystars67',
          role: 'ADMIN',
          id: 1,
        ),
      ),
    );
    await tester.tap(find.text('Account'));
    await harness.settleUi(tester);
    await expectGolden('account');
  });

  for (final (name, event) in [
    ('account_signed_out', null),
    (
      'account_waiting',
      const SignInProgress(
        state: SignInState.waitingForBrowser,
        url:
            'https://portal.beammp.com/auth/external?client_id=beammp-launcher',
      ),
    ),
    (
      'account_failed',
      const SignInProgress(state: SignInState.failed, reason: 'network'),
    ),
  ]) {
    testWidgets(name, (tester) async {
      harness = Harness();
      await harness.pump(tester);
      if (event != null) {
        harness.link.emit(event);
      }
      await tester.tap(find.text('Account'));
      await harness.settleUi(tester);
      await expectGolden(name);
    });
  }

  testWidgets('settings', (tester) async {
    harness = Harness();
    await harness.pump(tester, size: const Size(980, 1100));
    await tester.tap(find.text('Settings'));
    await harness.settleUi(tester);
    await expectGolden('settings');
  });
}
