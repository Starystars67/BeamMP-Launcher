import 'dart:async';
import 'dart:io';

import 'package:beammp_launcher/app/app_services.dart';
import 'package:beammp_launcher/core/engine/engine_link.dart';
import 'package:beammp_launcher/core/settings/launcher_settings.dart';
import 'package:beammp_launcher/core/window/tray_menu.dart';
import 'package:beammp_launcher/features/home/game_cover.dart';
import 'package:beammp_launcher/features/home/game_settings_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/fakes.dart';
import 'support/harness.dart';

void main() {
  late Harness harness;

  setUpAll(loadFonts);

  Future<void> start(WidgetTester tester, {Harness? with_}) async {
    harness = with_ ?? Harness();
    await harness.pump(tester);
  }

  tearDown(() => harness.dispose());

  group('startup', () {
    testWidgets('shows progress until the engine answers', (tester) async {
      harness = Harness();
      final gate = harness.gateway.gate = Completer<void>();
      await harness.pump(tester);
      expect(find.text('Starting the launcher engine…'), findsOneWidget);
      expect(find.byType(CircularProgressIndicator), findsOneWidget);
      expect(find.text('Play'), findsNothing);

      gate.complete();
      await harness.settleUi(tester);
      expect(find.byType(CircularProgressIndicator), findsNothing);
      expect(find.text('Ready to play'), findsOneWidget);
    });

    testWidgets('names the stage the engine is in', (tester) async {
      harness = Harness();
      harness.link.snapshot = const EngineSnapshot(
        state: EngineState.preparingGame,
        detail: 'Checking the mod',
      );
      await harness.pump(tester);
      expect(
        find.text('Preparing BeamNG.drive for multiplayer…'),
        findsOneWidget,
      );
      expect(find.text('Checking the mod'), findsOneWidget);

      harness.link.emit(const EngineStateChanged(EngineState.ready, ''));
      await harness.settleUi(tester);
      expect(find.text('Ready to play'), findsOneWidget);
    });

    testWidgets('a missing engine offers another try', (tester) async {
      harness = Harness();
      harness.gateway.failure = const EngineNotFoundException(['/x']);
      await harness.pump(tester);
      expect(find.text('The launcher engine stopped'), findsOneWidget);
      expect(find.textContaining('could not be found'), findsOneWidget);

      harness.gateway.failure = null;
      await tester.tap(find.text('Try again'));
      await harness.settleUi(tester);
      expect(find.text('Ready to play'), findsOneWidget);
      expect(harness.gateway.starts, 2);
    });

    testWidgets('failure details can be shown and hidden', (tester) async {
      harness = Harness();
      harness.gateway.failure = const EngineStartException(
        'exited',
        exitCode: 3,
        output: ['engine failed: no game found'],
      );
      await harness.pump(tester);
      expect(find.textContaining('engine failed: no game found'), findsNothing);

      await tester.tap(find.text('Show details'));
      await tester.pump();
      expect(
        find.textContaining('engine failed: no game found'),
        findsOneWidget,
      );

      await tester.tap(find.text('Hide details'));
      await tester.pump();
      expect(find.textContaining('engine failed: no game found'), findsNothing);
    });

    testWidgets('losing the engine later returns to the failure screen', (
      tester,
    ) async {
      await start(tester);
      expect(find.text('Ready to play'), findsOneWidget);
      harness.link.output = ['it crashed'];
      harness.link.close();
      await harness.settleUi(tester);
      expect(find.text('The launcher engine stopped'), findsOneWidget);
      expect(find.text('Ready to play'), findsNothing);
    });

    testWidgets('a fatal engine state is shown as a failure with its reason', (
      tester,
    ) async {
      await start(tester);
      harness.link.emit(
        const EngineStateChanged(EngineState.fatal, 'Failed to find the game'),
      );
      await harness.settleUi(tester);
      expect(find.text('Failed to find the game'), findsOneWidget);
    });
  });

  group('home', () {
    testWidgets('Play starts the game', (tester) async {
      await start(tester);
      await tester.tap(find.widgetWithText(FilledButton, 'Play'));
      await tester.pump();
      expect(harness.link.calls, contains('launchGame:beamng'));
    });

    testWidgets(
      'Play is disabled while the game is starting, Stop appears while it runs',
      (tester) async {
        await start(tester);
        harness.link.emit(
          const EngineStateChanged(EngineState.launchingGame, ''),
        );
        harness.link.emit(
          const GameStateChanged(game: 'beamng', state: GameState.launching),
        );
        await harness.settleUi(tester);
        expect(find.text('Starting…'), findsOneWidget);
        expect(find.text('Stop game'), findsOneWidget);
        expect(find.widgetWithText(FilledButton, 'Play'), findsNothing);

        harness.link.emit(
          const EngineStateChanged(EngineState.gameRunning, ''),
        );
        harness.link.emit(
          const GameStateChanged(
            game: 'beamng',
            state: GameState.running,
            pid: 9,
          ),
        );
        await harness.settleUi(tester);
        expect(find.text('Running'), findsOneWidget);

        await tester.tap(find.text('Stop game'));
        await tester.pump();
        expect(harness.link.calls, contains('stopGame:beamng'));
      },
    );

    testWidgets('a refused launch tells the user why', (tester) async {
      await start(tester);
      harness.link.launchError = const ControlException(
        'launch_failed',
        'no such file',
      );
      await tester.tap(find.widgetWithText(FilledButton, 'Play'));
      await harness.settleUi(tester);
      expect(
        find.text('Could not start the game: no such file'),
        findsOneWidget,
      );
    });

    testWidgets('says how the last game ended', (tester) async {
      await start(tester);
      harness.link.emit(
        const EngineStateChanged(
          EngineState.ready,
          'The game exited with code 0',
        ),
      );
      harness.link.emit(
        const GameStateChanged(
          game: 'beamng',
          state: GameState.exited,
          detail: 'The game exited with code 0',
        ),
      );
      await harness.settleUi(tester);
      expect(find.text('The game exited with code 0'), findsOneWidget);
    });
  });

  group('play together', () {
    const gearblocks = GameInfo(
      id: 'gearblocks',
      name: 'GearBlocks',
      state: GameState.ready,
      sessions: true,
    );
    const beamng = GameInfo(
      id: 'beamng',
      name: 'BeamNG.drive',
      state: GameState.ready,
    );

    Future<void> startWith(WidgetTester tester, List<GameInfo> games) async {
      harness = Harness();
      harness.link.games = games;
      await harness.pump(tester);
    }

    Future<void> emit(WidgetTester tester, SessionInfo session) async {
      harness.link.emit(SessionChanged(session));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 30));
    }

    const inRoom = SessionInfo(
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
    );

    Finder dialogField(String label) => find.descendant(
      of: find.byType(AlertDialog),
      matching: find.byWidgetPredicate(
        (widget) =>
            widget is TextField && widget.decoration?.labelText == label,
      ),
    );

    testWidgets('only a game with rooms has the panel', (tester) async {
      await startWith(tester, [beamng, gearblocks]);
      expect(find.text('Play together'), findsOneWidget);
      expect(find.text('Host a room'), findsOneWidget);
      expect(find.text('Join a room'), findsOneWidget);
    });

    for (final state in [
      GameState.notFound,
      GameState.error,
      GameState.unavailable,
    ]) {
      testWidgets('a game that is ${state.name} has no panel', (tester) async {
        await startWith(tester, [
          GameInfo(
            id: 'gearblocks',
            name: 'GearBlocks',
            state: state,
            sessions: true,
          ),
        ]);
        expect(find.text('Play together'), findsNothing);
      });
    }

    testWidgets('hosting asks how many players and an optional password', (
      tester,
    ) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      expect(find.text('Host a room'), findsWidgets);
      expect(
        tester
            .widget<TextField>(dialogField('Players allowed'))
            .controller
            ?.text,
        '8',
      );
      await tester.enterText(dialogField('Players allowed'), '4');
      await tester.enterText(dialogField('Password (optional)'), 'secret');
      await tester.tap(find.text('Create room'));
      await tester.pumpAndSettle();
      expect(harness.link.sessionCalls, ['create:gearblocks:4:secret']);
    });

    testWidgets('hosting with the defaults sends 8 and no password', (
      tester,
    ) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Create room'));
      await tester.pumpAndSettle();
      expect(harness.link.sessionCalls, ['create:gearblocks:8:']);
    });

    testWidgets('a room needs 2 to 32 players', (tester) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      FilledButton create() => tester.widget<FilledButton>(
        find.widgetWithText(FilledButton, 'Create room'),
      );
      for (final bad in ['', '1', '0', '33', '100']) {
        await tester.enterText(dialogField('Players allowed'), bad);
        await tester.pump();
        expect(create().onPressed, isNull, reason: 'players "$bad"');
      }
      for (final good in ['2', '32', '8']) {
        await tester.enterText(dialogField('Players allowed'), good);
        await tester.pump();
        expect(create().onPressed, isNotNull, reason: 'players "$good"');
      }
    });

    testWidgets('cancelling a dialog asks for nothing', (tester) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Join a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Cancel'));
      await tester.pumpAndSettle();
      expect(harness.link.sessionCalls, isEmpty);
    });

    testWidgets(
      'a code is typed in capitals, without characters it cannot have',
      (tester) async {
        await startWith(tester, [gearblocks]);
        await tester.tap(find.text('Join a room'));
        await tester.pumpAndSettle();
        await tester.enterText(dialogField('Room code'), 'ab-c 0O1Il 2x3y4z99');
        await tester.pump();
        expect(
          tester.widget<TextField>(dialogField('Room code')).controller?.text,
          'ABC2X3',
        );
      },
    );

    testWidgets('joining needs the whole code, then asks the engine', (
      tester,
    ) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Join a room'));
      await tester.pumpAndSettle();
      FilledButton join() => tester.widget<FilledButton>(
        find.widgetWithText(FilledButton, 'Join'),
      );
      expect(join().onPressed, isNull);
      await tester.enterText(dialogField('Room code'), 'abc23');
      await tester.pump();
      expect(join().onPressed, isNull);
      await tester.enterText(dialogField('Room code'), 'abc234');
      await tester.pump();
      expect(join().onPressed, isNotNull);
      await tester.enterText(dialogField('Password (optional)'), 'pw');
      await tester.tap(find.widgetWithText(FilledButton, 'Join'));
      await tester.pumpAndSettle();
      expect(harness.link.sessionCalls, ['join:gearblocks:ABC234:pw']);
    });

    testWidgets('enter in the code field joins', (tester) async {
      await startWith(tester, [gearblocks]);
      await tester.tap(find.text('Join a room'));
      await tester.pumpAndSettle();
      await tester.enterText(dialogField('Room code'), 'abc234');
      await tester.testTextInput.receiveAction(TextInputAction.done);
      await tester.pumpAndSettle();
      expect(harness.link.sessionCalls, ['join:gearblocks:ABC234:']);
    });

    testWidgets('while connecting there is a way out', (tester) async {
      await startWith(tester, [gearblocks]);
      await emit(
        tester,
        const SessionInfo(game: 'gearblocks', state: SessionState.connecting),
      );
      expect(find.text('Connecting to the relay…'), findsOneWidget);
      expect(find.text('Host a room'), findsNothing);
      await tester.tap(find.widgetWithText(OutlinedButton, 'Cancel'));
      await tester.pump();
      expect(harness.link.sessionCalls, ['leave']);
    });

    testWidgets('in a room: the code, the role, and who is there', (
      tester,
    ) async {
      await startWith(tester, [gearblocks]);
      await emit(tester, inRoom);
      expect(find.text('ABC234'), findsOneWidget);
      expect(find.text('You joined this room.'), findsOneWidget);
      expect(find.text('Players: 3'), findsOneWidget);
      expect(find.text('Host'), findsOneWidget);
      expect(find.text('Bob'), findsOneWidget);
      expect(find.text(' (host)'), findsOneWidget);
      expect(find.text(' (you)'), findsOneWidget);
      expect(find.text('Host a room'), findsNothing);
    });

    testWidgets('the host is told friends join with the code', (tester) async {
      await startWith(tester, [gearblocks]);
      await emit(
        tester,
        const SessionInfo(
          game: 'gearblocks',
          state: SessionState.inRoom,
          role: 'host',
          code: 'ABC234',
          selfId: 1,
          hostId: 1,
          peers: [SessionPeer(id: 1, name: 'Ann')],
        ),
      );
      expect(
        find.text('You are the host. Friends join with this code.'),
        findsOneWidget,
      );
      expect(find.text('Players: 1'), findsOneWidget);
    });

    testWidgets('the list follows players coming and going', (tester) async {
      await startWith(tester, [gearblocks]);
      await emit(tester, inRoom);
      await emit(
        tester,
        const SessionInfo(
          game: 'gearblocks',
          state: SessionState.inRoom,
          role: 'guest',
          code: 'ABC234',
          selfId: 3,
          hostId: 1,
          peers: [
            SessionPeer(id: 1, name: 'Host'),
            SessionPeer(id: 3, name: 'Ann'),
          ],
        ),
      );
      expect(find.text('Players: 2'), findsOneWidget);
      expect(find.text('Bob'), findsNothing);
    });

    testWidgets('the code can be copied', (tester) async {
      String? copied;
      tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        (call) async {
          if (call.method == 'Clipboard.setData') {
            copied = (call.arguments as Map)['text'] as String?;
          }
          return null;
        },
      );
      addTearDown(
        () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
          SystemChannels.platform,
          null,
        ),
      );
      await startWith(tester, [gearblocks]);
      await emit(tester, inRoom);
      await tester.tap(find.byTooltip('Copy the code'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 30));
      expect(copied, 'ABC234');
      expect(find.text('Code copied'), findsOneWidget);
    });

    testWidgets('leaving asks the engine', (tester) async {
      await startWith(tester, [gearblocks]);
      await emit(tester, inRoom);
      await tester.tap(find.text('Leave room'));
      await tester.pump();
      expect(harness.link.sessionCalls, ['leave']);
    });

    testWidgets('why a session ended is shown', (tester) async {
      await startWith(tester, [gearblocks]);
      await emit(
        tester,
        const SessionInfo(
          game: 'gearblocks',
          errorCode: 'room_closed',
          errorMessage: 'The host left the room',
        ),
      );
      expect(find.text('The host left the room'), findsOneWidget);
      expect(find.text('Host a room'), findsOneWidget);
    });

    testWidgets('a refused request is shown, and goes with the next try', (
      tester,
    ) async {
      await startWith(tester, [gearblocks]);
      harness.link.sessionError = const ControlException(
        'session_unavailable',
        'no relay is set up',
      );
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Create room'));
      await tester.pumpAndSettle();
      expect(find.text('no relay is set up'), findsOneWidget);

      harness.link.sessionError = null;
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Create room'));
      await tester.pumpAndSettle();
      expect(find.text('no relay is set up'), findsNothing);
    });

    testWidgets('an engine that does not answer says so', (tester) async {
      await startWith(tester, [gearblocks]);
      harness.link.sessionError = const ControlTimeoutException(
        'session.create',
      );
      await tester.tap(find.text('Host a room'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Create room'));
      await tester.pumpAndSettle();
      expect(find.text('The launcher engine did not answer.'), findsOneWidget);
    });

    testWidgets('a room for another game is not offered a second one', (
      tester,
    ) async {
      await startWith(tester, [
        gearblocks,
        const GameInfo(
          id: 'other',
          name: 'Other Game',
          state: GameState.ready,
          sessions: true,
        ),
      ]);
      await emit(tester, inRoom);
      expect(find.text('ABC234'), findsOneWidget);
      expect(
        find.text('You are in a room for gearblocks already.'),
        findsOneWidget,
      );
    });

    testWidgets('the panel passes the contrast and tap target checks', (
      tester,
    ) async {
      final semantics = tester.ensureSemantics();
      await startWith(tester, [gearblocks]);
      await emit(tester, inRoom);
      await expectLater(tester, meetsGuideline(labeledTapTargetGuideline));
      await expectLater(tester, meetsGuideline(textContrastGuideline));
      semantics.dispose();
    });
  });

  group('game covers', () {
    const beamng = GameInfo(
      id: 'beamng',
      name: 'BeamNG.drive',
      state: GameState.ready,
      steamAppId: 284160,
    );

    Finder coverImages() => find.descendant(
      of: find.byType(GameCover),
      matching: find.byType(Image),
    );

    Future<void> startWith(WidgetTester tester, List<GameInfo> games) async {
      harness = Harness();
      harness.link.games = games;
      final cover = MemoryImage(
        File('test/fixtures/test_cover.png').readAsBytesSync(),
      );
      harness.covers.covers[284160] = cover;
      await harness.pump(tester);
      // decoding happens off the test clock
      await tester.runAsync(
        () => precacheImage(cover, tester.element(find.byType(MaterialApp))),
      );
      await tester.pumpAndSettle();
    }

    testWidgets('a game with a cover in the Steam library shows it', (
      tester,
    ) async {
      await startWith(tester, [beamng]);
      expect(harness.covers.asked, contains(284160));
      expect(coverImages(), findsOneWidget);
      expect(find.byIcon(Icons.directions_car_filled), findsNothing);
    });

    testWidgets('the cover is portrait, 2:3, like Steam\'s', (tester) async {
      await startWith(tester, [beamng]);
      final size = tester.getSize(coverImages());
      expect(size.height / size.width, closeTo(1.5, 0.01));
    });

    testWidgets('without a cover the game shows its icon, same size', (
      tester,
    ) async {
      harness = Harness();
      harness.link.games = [beamng];
      await harness.pump(tester);
      await tester.pumpAndSettle();
      expect(coverImages(), findsNothing);
      expect(find.byIcon(Icons.directions_car_filled), findsOneWidget);
    });

    testWidgets('a game with no Steam id never asks for a cover', (
      tester,
    ) async {
      await startWith(tester, [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.ready,
        ),
      ]);
      expect(harness.covers.asked, isEmpty);
      expect(find.byIcon(Icons.directions_car_filled), findsOneWidget);
    });

    testWidgets('a cover that cannot be read falls back to the icon', (
      tester,
    ) async {
      final broken = MemoryImage(Uint8List.fromList([1, 2, 3]));
      harness = Harness();
      harness.link.games = [beamng];
      harness.covers.covers[284160] = broken;
      await harness.pump(tester);
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 200)),
      );
      await tester.pumpAndSettle();
      expect(find.byIcon(Icons.directions_car_filled), findsOneWidget);
    });

    testWidgets('the cover stays while the game changes state', (tester) async {
      await startWith(tester, [beamng]);
      harness.link.emit(
        const GameStateChanged(game: 'beamng', state: GameState.launching),
      );
      await tester.pumpAndSettle();
      expect(coverImages(), findsOneWidget);
    });

    testWidgets('a game that is not found still has its cover', (tester) async {
      await startWith(tester, [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.notFound,
          steamAppId: 284160,
        ),
      ]);
      expect(coverImages(), findsOneWidget);
    });

    testWidgets('the cover is decoration: the name beside it is what is read', (
      tester,
    ) async {
      final semantics = tester.ensureSemantics();
      await startWith(tester, [beamng]);
      expect(
        find.descendant(
          of: find.byType(GameCover),
          matching: find.byType(ExcludeSemantics),
        ),
        findsWidgets,
      );
      expect(find.bySemanticsLabel(RegExp('BeamNG.drive')), findsWidgets);
      semantics.dispose();
    });
  });

  group('game settings', () {
    Finder field(String label) => find.byWidgetPredicate(
      (widget) => widget is TextField && widget.decoration?.labelText == label,
    );

    // The cards of the home page stay in the tree under the pushed page.
    Finder onPage(Finder finder) =>
        find.descendant(of: find.byType(GameSettingsScreen), matching: finder);

    Future<void> openGameSettings(
      WidgetTester tester, {
      Harness? with_,
      int index = 0,
    }) async {
      harness = with_ ?? Harness();
      harness.link.games = [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.ready,
        ),
        const GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.ready,
        ),
      ];
      await harness.pump(tester);
      await tester.tap(find.byTooltip('Game settings').at(index));
      await tester.pumpAndSettle();
    }

    Future<void> submit(WidgetTester tester, String label, String text) async {
      await tester.enterText(field(label), text);
      await tester.testTextInput.receiveAction(TextInputAction.done);
      await harness.settleUi(tester);
    }

    testWidgets('every game with something to configure has a settings button', (
      tester,
    ) async {
      harness = Harness();
      harness.link.games = [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.notFound,
        ),
        const GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.unavailable,
        ),
      ];
      await harness.pump(tester);
      // a game that is not found needs its settings most, one that is not there yet has none
      expect(find.byTooltip('Game settings'), findsOneWidget);
    });

    testWidgets('the page is the game\'s own, with its fields', (tester) async {
      await openGameSettings(tester);
      expect(find.text('BeamNG.drive settings'), findsOneWidget);
      expect(field('Game folder'), findsOneWidget);
      expect(field('Start command'), findsOneWidget);
      expect(field('User folder'), findsOneWidget);
      expect(field('Extra game arguments'), findsOneWidget);
      expect(onPage(find.text('Ready to play')), findsOneWidget);
    });

    testWidgets('another game has no user folder, and its own page', (
      tester,
    ) async {
      await openGameSettings(tester, index: 1);
      expect(find.text('GearBlocks settings'), findsOneWidget);
      expect(field('Game folder'), findsOneWidget);
      expect(field('Start command'), findsOneWidget);
      expect(field('User folder'), findsNothing);
      expect(field('Extra game arguments'), findsOneWidget);
    });

    testWidgets(
      'the game folder and start command are saved and sent to the engine',
      (tester) async {
        await openGameSettings(tester);
        await submit(tester, 'Game folder', '/games/BeamNG.drive');
        await submit(tester, 'Start command', '/opt/run-beamng.sh');
        await submit(tester, 'User folder', '/users/x/BeamNG.drive');

        final saved = harness.store.saved.game('beamng');
        expect(saved.directory, '/games/BeamNG.drive');
        expect(saved.command, '/opt/run-beamng.sh');
        expect(saved.userPath, '/users/x/BeamNG.drive');
        expect(harness.link.configured.last, (
          game: 'beamng',
          directory: '/games/BeamNG.drive',
          command: '/opt/run-beamng.sh',
          userPath: '/users/x/BeamNG.drive',
        ));
        expect(harness.link.configured, hasLength(3));
      },
    );

    testWidgets(
      'clearing a setting sends it empty, so the engine finds it again',
      (tester) async {
        await openGameSettings(
          tester,
          with_: Harness(
            settings: const LauncherSettings(
              games: {'beamng': GameSettings(directory: '/games/BeamNG.drive')},
            ),
          ),
        );
        await submit(tester, 'Game folder', '');
        expect(harness.store.saved.game('beamng').isEmpty, isTrue);
        expect(harness.link.configured.single.directory, isEmpty);
      },
    );

    testWidgets('an unchanged field tells the engine nothing', (tester) async {
      await openGameSettings(tester);
      await submit(tester, 'Game folder', '');
      expect(harness.link.configured, isEmpty);
    });

    testWidgets(
      'launch arguments are saved on submit, not sent to the engine, and passed to the game',
      (tester) async {
        await openGameSettings(tester);
        await submit(tester, 'Extra game arguments', '-console "my folder"');
        expect(
          harness.store.saved.game('beamng').launchArguments,
          '-console "my folder"',
        );
        expect(harness.link.configured, isEmpty);

        await tester.pageBack();
        await tester.pumpAndSettle();
        await tester.tap(find.widgetWithText(FilledButton, 'Play').first);
        await harness.settleUi(tester);
        expect(harness.link.launchArgs.single, ['-console', 'my folder']);
      },
    );

    testWidgets('settings are also saved when a field loses the focus', (
      tester,
    ) async {
      await openGameSettings(tester);
      await tester.enterText(field('Extra game arguments'), '-nosound');
      FocusManager.instance.primaryFocus?.unfocus();
      await harness.settleUi(tester);
      expect(harness.store.saved.game('beamng').launchArguments, '-nosound');
    });

    testWidgets('each game keeps its own settings', (tester) async {
      await openGameSettings(tester);
      await submit(tester, 'Extra game arguments', '-one');
      await tester.pageBack();
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('Game settings').at(1));
      await tester.pumpAndSettle();
      expect(onPage(find.text('-one')), findsNothing);
      await submit(tester, 'Extra game arguments', '-two');
      expect(harness.store.saved.game('beamng').launchArguments, '-one');
      expect(harness.store.saved.game('gearblocks').launchArguments, '-two');
    });

    testWidgets('a broken quote is flagged while typing', (tester) async {
      await openGameSettings(tester);
      await tester.enterText(field('Extra game arguments'), '-x "open');
      await tester.pump();
      expect(find.textContaining('opening quote'), findsOneWidget);

      await tester.enterText(field('Extra game arguments'), '-x "closed"');
      await tester.pump();
      expect(find.textContaining('opening quote'), findsNothing);
    });

    testWidgets('saved settings are shown', (tester) async {
      await openGameSettings(
        tester,
        with_: Harness(
          settings: const LauncherSettings(
            games: {
              'beamng': GameSettings(
                directory: '/games/BeamNG.drive',
                command: '/opt/run-beamng.sh',
                userPath: '/users/x',
                launchArguments: '-console',
              ),
            },
          ),
        ),
      );
      expect(onPage(find.text('/games/BeamNG.drive')), findsOneWidget);
      expect(onPage(find.text('/opt/run-beamng.sh')), findsOneWidget);
      expect(onPage(find.text('/users/x')), findsOneWidget);
      expect(onPage(find.text('-console')), findsOneWidget);
    });

    testWidgets(
      'while a game runs the settings are kept and the user is told when they apply',
      (tester) async {
        await openGameSettings(tester);
        harness.link.configureError = const ControlException(
          'game_not_ready',
          'busy',
        );
        await submit(tester, 'Game folder', '/games/BeamNG.drive');
        expect(
          harness.store.saved.game('beamng').directory,
          '/games/BeamNG.drive',
        );
        expect(
          find.textContaining('apply the next time the launcher starts'),
          findsWidgets,
        );
      },
    );

    testWidgets('settings the engine refuses are explained', (tester) async {
      await openGameSettings(tester);
      harness.link.configureError = const ControlException(
        'invalid_argument',
        'Invalid argument: directory',
      );
      await submit(tester, 'Game folder', '/x');
      expect(find.textContaining('could not use these settings'), findsWidgets);
      expect(find.textContaining('Invalid argument: directory'), findsWidgets);
    });

    testWidgets('the page shows how the engine finds the game, live', (
      tester,
    ) async {
      await openGameSettings(tester);
      expect(onPage(find.text('Ready to play')), findsOneWidget);
      harness.link.emit(
        const GameStateChanged(
          game: 'beamng',
          state: GameState.notFound,
          detail: 'BeamNG.drive was not found in /x',
        ),
      );
      await harness.settleUi(tester);
      expect(
        onPage(find.text('Game not found – BeamNG.drive was not found in /x')),
        findsOneWidget,
      );
    });
  });

  group('several games', () {
    const gearblocks = GameInfo(
      id: 'gearblocks',
      name: 'GearBlocks',
      state: GameState.unavailable,
    );

    Future<void> startWithBoth(WidgetTester tester) async {
      harness = Harness();
      harness.link.games = [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.ready,
        ),
        gearblocks,
      ];
      await harness.pump(tester);
    }

    testWidgets('lists every game the engine reports', (tester) async {
      await startWithBoth(tester);
      expect(find.text('BeamNG.drive'), findsOneWidget);
      expect(find.text('GearBlocks'), findsOneWidget);
      expect(find.text('Ready to play'), findsOneWidget);
      expect(find.text('Coming soon'), findsOneWidget);
    });

    testWidgets('GearBlocks cannot be played yet, BeamNG still can', (
      tester,
    ) async {
      await startWithBoth(tester);
      final buttons = tester
          .widgetList<FilledButton>(find.widgetWithText(FilledButton, 'Play'))
          .toList();
      expect(buttons, hasLength(2));
      expect(buttons[0].onPressed, isNotNull);
      expect(buttons[1].onPressed, isNull);

      await tester.tap(find.widgetWithText(FilledButton, 'Play').first);
      await tester.pump();
      expect(harness.link.calls, contains('launchGame:beamng'));
      expect(
        harness.link.calls.where((c) => c.contains('gearblocks')),
        isEmpty,
      );
    });

    testWidgets('a ready GearBlocks can be played, with no BeamNG arguments', (
      tester,
    ) async {
      harness = Harness(
        settings: const LauncherSettings(
          games: {'beamng': GameSettings(launchArguments: '-console')},
        ),
      );
      harness.link.games = [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.ready,
        ),
        const GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.ready,
        ),
      ];
      await harness.pump(tester);
      await tester.tap(find.widgetWithText(FilledButton, 'Play').last);
      await tester.pump();
      expect(harness.link.calls, contains('launchGame:gearblocks'));
      expect(harness.link.calls, isNot(contains('launchGame:beamng')));
      expect(harness.link.launchArgs.single, isEmpty);
    });

    testWidgets('a game that is not installed says why and cannot be played', (
      tester,
    ) async {
      harness = Harness();
      harness.link.games = [
        const GameInfo(
          id: 'beamng',
          name: 'BeamNG.drive',
          state: GameState.notFound,
          detail: 'BeamNG.drive was not found',
        ),
        const GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.ready,
        ),
      ];
      await harness.pump(tester);
      expect(find.text('Game not found'), findsOneWidget);
      expect(find.text('BeamNG.drive was not found'), findsOneWidget);
      final buttons = tester
          .widgetList<FilledButton>(find.widgetWithText(FilledButton, 'Play'))
          .toList();
      expect(buttons[0].onPressed, isNull);
      expect(buttons[1].onPressed, isNotNull);
    });

    testWidgets(
      'while one game runs the other is busy and Stop stops that one',
      (tester) async {
        harness = Harness();
        harness.link.games = [
          const GameInfo(
            id: 'beamng',
            name: 'BeamNG.drive',
            state: GameState.busy,
          ),
          const GameInfo(
            id: 'gearblocks',
            name: 'GearBlocks',
            state: GameState.running,
            pid: 9,
          ),
        ];
        await harness.pump(tester);
        harness.link.emit(
          const EngineStateChanged(EngineState.gameRunning, ''),
        );
        await harness.settleUi(tester);
        expect(find.text('Another game is running'), findsOneWidget);
        expect(find.text('Running'), findsOneWidget);
        final play = tester.widget<FilledButton>(
          find.widgetWithText(FilledButton, 'Play'),
        );
        expect(play.onPressed, isNull);
        await tester.tap(find.text('Stop game'));
        await tester.pump();
        expect(harness.link.calls, contains('stopGame:gearblocks'));
      },
    );

    testWidgets('a game state does not touch the GearBlocks card', (
      tester,
    ) async {
      await startWithBoth(tester);
      harness.link.emit(
        const GameStateChanged(
          game: 'beamng',
          state: GameState.running,
          pid: 3,
        ),
      );
      await harness.settleUi(tester);
      expect(find.text('Running'), findsOneWidget);
      expect(find.text('Coming soon'), findsOneWidget);
    });
  });

  group('navigation', () {
    testWidgets('moves between the pages', (tester) async {
      await start(tester);
      await tester.tap(find.text('Logs'));
      await tester.pump();
      expect(find.text('Nothing logged yet.'), findsOneWidget);

      await tester.tap(find.text('Account'));
      await tester.pump();
      expect(find.text('Not signed in'), findsOneWidget);

      await tester.tap(find.text('Settings'));
      await tester.pump();
      expect(find.text('When the game starts'), findsOneWidget);
    });

    testWidgets('the tray can open the log page', (tester) async {
      await start(tester);
      harness.shell.userPicksTrayItem(TrayAction.openLogs);
      await harness.settleUi(tester);
      expect(harness.services.page.value, AppPage.logs);
    });
  });

  group('logs', () {
    LogReceived entry(String message, LogLevel level) =>
        LogReceived(log(message, level: level));

    testWidgets('shows the records and filters them by level', (tester) async {
      await start(tester);
      harness.link.emit(entry('a debug line', LogLevel.debug));
      harness.link.emit(entry('an info line', LogLevel.info));
      harness.link.emit(entry('a warning line', LogLevel.warn));
      harness.link.emit(entry('an error line', LogLevel.error));
      await tester.tap(find.text('Logs'));
      await harness.settleUi(tester);

      expect(find.textContaining('a debug line'), findsNothing);
      expect(find.textContaining('an info line'), findsOneWidget);
      expect(find.text('3 entries'), findsOneWidget);

      await tester.tap(find.text('Level: Info'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Level: Warnings').last);
      await tester.pumpAndSettle();
      expect(find.textContaining('an info line'), findsNothing);
      expect(find.textContaining('a warning line'), findsOneWidget);
      expect(find.text('2 entries'), findsOneWidget);

      await tester.tap(find.text('Level: Warnings'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Level: Debug').last);
      await tester.pumpAndSettle();
      expect(find.textContaining('a debug line'), findsOneWidget);
    });

    testWidgets('Clear empties the view', (tester) async {
      await start(tester);
      harness.link.emit(entry('something', LogLevel.info));
      await tester.tap(find.text('Logs'));
      await harness.settleUi(tester);
      expect(find.textContaining('something'), findsOneWidget);

      await tester.tap(find.byTooltip('Clear'));
      await tester.pump();
      expect(find.textContaining('something'), findsNothing);
      expect(find.text('Nothing logged yet.'), findsOneWidget);
    });

    testWidgets('new records appear while the page is open', (tester) async {
      await start(tester);
      await tester.tap(find.text('Logs'));
      await tester.pump();
      harness.link.emit(entry('live line', LogLevel.info));
      await harness.settleUi(tester);
      expect(find.textContaining('live line'), findsOneWidget);
    });
  });

  group('account', () {
    testWidgets('shows who is signed in and can sign out', (tester) async {
      await start(tester);
      harness.link.emit(
        const AccountChanged(
          AccountInfo(signedIn: true, username: 'user', role: 'EA'),
        ),
      );
      await tester.tap(find.text('Account'));
      await harness.settleUi(tester);
      expect(find.text('Signed in as user'), findsOneWidget);
      expect(find.text('Role: EA'), findsOneWidget);

      await tester.tap(find.text('Sign out'));
      await harness.settleUi(tester);
      expect(harness.link.calls, contains('signOut'));
      expect(find.text('Not signed in'), findsOneWidget);
    });
  });

  group('settings', () {
    testWidgets('the tray switch changes the setting', (tester) async {
      await start(tester);
      await tester.tap(find.text('Settings'));
      await tester.pump();
      await tester.tap(find.byType(SwitchListTile).first);
      await harness.settleUi(tester);
      expect(harness.settings.settings.trayEnabled, isFalse);
      expect(harness.store.saved.trayEnabled, isFalse);
    });

    testWidgets(
      'without a tray the switch is off and disabled, with the reason',
      (tester) async {
        await start(tester, with_: Harness(trayAvailable: false));
        await tester.tap(find.text('Settings'));
        await tester.pump();
        final tile = tester.widget<SwitchListTile>(
          find.byType(SwitchListTile).first,
        );
        expect(tile.value, isFalse);
        expect(tile.onChanged, isNull);
        expect(find.text('Not available on this system'), findsOneWidget);
      },
    );

    testWidgets('a saved choice shows up in the drop-down', (tester) async {
      await start(
        tester,
        with_: Harness(
          settings: const LauncherSettings(
            launchWindowBehavior: LaunchWindowBehavior.minimise,
          ),
        ),
      );
      await tester.tap(find.text('Settings'));
      await tester.pump();
      expect(find.text('Minimise'), findsOneWidget);
    });

    testWidgets(
      'close-to-tray saved while the tray is gone does not break the page',
      (tester) async {
        await start(
          tester,
          with_: Harness(
            trayAvailable: false,
            settings: const LauncherSettings(
              closeBehavior: CloseBehavior.closeToTray,
            ),
          ),
        );
        await tester.tap(find.text('Settings'));
        await tester.pump();
        expect(tester.takeException(), isNull);
      },
    );
  });

  group('signing in', () {
    Future<void> openAccount(WidgetTester tester) async {
      await start(tester);
      await tester.tap(find.text('Account'));
      await tester.pump();
    }

    Future<void> progress(WidgetTester tester, SignInProgress event) async {
      harness.link.emit(event);
      await harness.settleUi(tester);
    }

    testWidgets('offers to sign in and asks the engine to start', (
      tester,
    ) async {
      await openAccount(tester);
      expect(find.text('Not signed in'), findsOneWidget);
      await tester.tap(find.text('Sign in with BeamMP'));
      await tester.pump();
      expect(harness.link.calls, contains('signIn'));
    });

    testWidgets(
      'while waiting it can reopen the page, copy the link and cancel',
      (tester) async {
        await openAccount(tester);
        await progress(
          tester,
          const SignInProgress(
            state: SignInState.waitingForBrowser,
            url: 'https://accounts.example/auth/external?x=1',
          ),
        );
        expect(find.text('Finish signing in in your browser'), findsOneWidget);
        expect(find.text('Sign in with BeamMP'), findsNothing);

        await tester.tap(find.text('Open the page again'));
        await tester.pump();
        expect(harness.urlOpener.opened, [
          'https://accounts.example/auth/external?x=1',
        ]);

        String? copied;
        tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
          SystemChannels.platform,
          (call) async {
            if (call.method == 'Clipboard.setData') {
              copied =
                  (call.arguments as Map<Object?, Object?>)['text'] as String?;
            }
            return null;
          },
        );
        addTearDown(
          () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
            SystemChannels.platform,
            null,
          ),
        );
        await tester.tap(find.text('Copy link'));
        await harness.settleUi(tester);
        expect(copied, 'https://accounts.example/auth/external?x=1');
        expect(find.text('Link copied'), findsOneWidget);

        await tester.tap(find.text('Cancel'));
        await tester.pump();
        expect(harness.link.calls, contains('cancelSignIn'));
      },
    );

    testWidgets('without an address there is nothing to reopen or copy', (
      tester,
    ) async {
      await openAccount(tester);
      await progress(
        tester,
        const SignInProgress(state: SignInState.waitingForBrowser),
      );
      expect(find.text('Open the page again'), findsNothing);
      expect(find.text('Copy link'), findsNothing);
      expect(find.text('Cancel'), findsOneWidget);
    });

    testWidgets('shows that it is exchanging the result', (tester) async {
      await openAccount(tester);
      await progress(
        tester,
        const SignInProgress(state: SignInState.exchanging),
      );
      expect(find.text('Signing you in…'), findsOneWidget);
    });

    for (final (reason, text) in [
      ('cancelled', 'The sign-in was cancelled.'),
      ('timeout', 'The sign-in took too long.'),
      ('denied', 'The sign-in was declined.'),
      ('network', 'BeamMP could not be reached. Check your connection.'),
      ('server', 'BeamMP had a problem. Try again in a moment.'),
      ('rejected', 'The sign-in was refused.'),
      ('expired', 'Your saved sign-in has ended. Please sign in again.'),
      ('internal', 'Something went wrong while signing in.'),
      ('from-the-future', 'Something went wrong while signing in.'),
    ]) {
      testWidgets('explains a $reason failure', (tester) async {
        await openAccount(tester);
        await progress(
          tester,
          SignInProgress(state: SignInState.failed, reason: reason),
        );
        expect(find.text(text), findsOneWidget);
      });
    }

    testWidgets('a failed sign-in can be tried again or dismissed', (
      tester,
    ) async {
      await openAccount(tester);
      await progress(
        tester,
        const SignInProgress(state: SignInState.failed, reason: 'timeout'),
      );
      await tester.tap(find.text('Try again'));
      await tester.pump();
      expect(harness.link.calls, contains('signIn'));

      await progress(
        tester,
        const SignInProgress(state: SignInState.failed, reason: 'timeout'),
      );
      await tester.tap(find.text('Cancel'));
      await tester.pump();
      expect(find.text('Sign in with BeamMP'), findsOneWidget);
    });

    testWidgets('a finished sign-in shows the account', (tester) async {
      await openAccount(tester);
      await progress(
        tester,
        const SignInProgress(state: SignInState.exchanging),
      );
      harness.link.emit(
        const AccountChanged(
          AccountInfo(signedIn: true, username: 'Racer', role: 'EA'),
        ),
      );
      await harness.settleUi(tester);
      expect(find.text('Signed in as Racer'), findsOneWidget);
      expect(find.text('Signing you in…'), findsNothing);
    });
  });

  group('settings screen', () {
    Future<void> openSettings(WidgetTester tester, {Harness? with_}) async {
      await start(tester, with_: with_);
      await tester.tap(find.text('Settings'));
      await tester
          .pumpAndSettle(); // the page fades in, nothing can be tapped before
    }

    testWidgets('the theme setting switches the app theme', (tester) async {
      await openSettings(tester);
      MaterialApp app() => tester.widget<MaterialApp>(find.byType(MaterialApp));
      expect(app().themeMode, ThemeMode.system);

      await tester.tap(find.text('Follow the system'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Dark').last);
      await tester.pumpAndSettle();
      expect(app().themeMode, ThemeMode.dark);
      expect(harness.store.saved.theme, ThemeSetting.dark);
    });

    testWidgets('the app settings do not hold any game settings', (
      tester,
    ) async {
      await openSettings(tester);
      expect(find.text('Game folder'), findsNothing);
      expect(find.text('Start command'), findsNothing);
      expect(find.text('Extra game arguments'), findsNothing);
    });

    testWidgets('bad saved arguments stop the launch with an explanation', (
      tester,
    ) async {
      await start(
        tester,
        with_: Harness(
          settings: const LauncherSettings(
            games: {'beamng': GameSettings(launchArguments: '-x "open')},
          ),
        ),
      );
      await tester.tap(find.widgetWithText(FilledButton, 'Play'));
      await harness.settleUi(tester);
      expect(harness.link.launchArgs, isEmpty);
      expect(find.textContaining('opening quote'), findsOneWidget);
    });

    testWidgets('notifications can be switched off', (tester) async {
      await openSettings(tester);
      await tester.dragUntilVisible(
        find.textContaining('Notify me'),
        find.byType(ListView),
        const Offset(0, -100),
      );
      await tester.tap(
        find.widgetWithText(
          SwitchListTile,
          'Notify me when the game closes or the engine stops while the window is hidden',
        ),
      );
      await harness.settleUi(tester);
      expect(harness.store.saved.notificationsEnabled, isFalse);
    });

    testWidgets('the log file can be revealed', (tester) async {
      await openSettings(tester);
      await tester.ensureVisible(find.text('Show the launcher log file'));
      await tester.pump();
      await tester.tap(find.text('Show the launcher log file'));
      await harness.settleUi(tester);
      expect(harness.revealer.revealed, ['/engine/Launcher.log']);
      expect(find.text('The log file could not be found'), findsNothing);
    });

    testWidgets('a log file that is gone is reported', (tester) async {
      await openSettings(tester);
      harness.revealer.exists = false;
      await tester.ensureVisible(find.text('Show the launcher log file'));
      await tester.pump();
      await tester.tap(find.text('Show the launcher log file'));
      await harness.settleUi(tester);
      expect(find.text('The log file could not be found'), findsOneWidget);
    });

    testWidgets('About shows the versions of the launcher and the engine', (
      tester,
    ) async {
      await openSettings(tester);
      await tester.dragUntilVisible(
        find.text('Launcher 0.1.0'),
        find.byType(ListView),
        const Offset(0, -100),
      );
      expect(find.text('Launcher 0.1.0'), findsOneWidget);
      expect(find.text('Engine 9.9.9'), findsOneWidget);
      expect(find.text('Control protocol 1'), findsOneWidget);
    });
  });

  group('quitting', () {
    testWidgets('quits straight away when no game runs', (tester) async {
      await start(tester);
      harness.shell.userQuits();
      await harness.settleUi(tester);
      expect(find.text('Quit the launcher?'), findsNothing);
      expect(harness.link.calls, contains('shutdown:false'));
      expect(harness.shell.calls, contains('quit'));
    });

    testWidgets('closing the window quits by default', (tester) async {
      await start(tester);
      harness.shell.userClosesWindow();
      await harness.settleUi(tester);
      expect(harness.shell.calls, contains('quit'));
    });

    testWidgets('asks first while a game runs; staying cancels', (
      tester,
    ) async {
      await start(tester);
      harness.link.emit(
        const EngineStateChanged(EngineState.launchingGame, ''),
      );
      harness.link.emit(const EngineStateChanged(EngineState.gameRunning, ''));
      await harness.settleUi(tester);

      harness.shell.userQuits();
      await harness.settleUi(tester);
      expect(find.text('Quit the launcher?'), findsOneWidget);
      await tester.tap(find.text('Stay'));
      await tester.pumpAndSettle();
      expect(find.text('Quit the launcher?'), findsNothing);
      expect(harness.shell.calls, isNot(contains('quit')));
      expect(
        harness.link.calls.where((c) => c.startsWith('shutdown')),
        isEmpty,
      );
    });

    testWidgets('confirming stops the engine, not the game, and quits', (
      tester,
    ) async {
      await start(tester);
      harness.link.emit(
        const EngineStateChanged(EngineState.launchingGame, ''),
      );
      harness.link.emit(const EngineStateChanged(EngineState.gameRunning, ''));
      await harness.settleUi(tester);

      harness.shell.userQuits();
      await harness.settleUi(tester);
      await tester.tap(find.text('Quit'));
      await harness.settleUi(tester);
      expect(harness.link.calls, contains('shutdown:true'));
      expect(harness.link.calls, isNot(contains('stopGame:beamng')));
      expect(harness.shell.calls, contains('quit'));
    });

    testWidgets('a second quit request while quitting is ignored', (
      tester,
    ) async {
      await start(tester);
      harness.shell.userQuits();
      harness.shell.userQuits();
      await harness.settleUi(tester);
      expect(
        harness.link.calls.where((c) => c.startsWith('shutdown')),
        hasLength(1),
      );
    });
  });

  testWidgets('the window title comes from the localisation', (tester) async {
    await start(tester);
    final app = tester.widget<MaterialApp>(find.byType(MaterialApp));
    expect(app.onGenerateTitle, isNotNull);
  });
}
