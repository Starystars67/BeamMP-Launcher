import 'package:beammp_launcher/core/engine/engine_controller.dart';
import 'package:beammp_launcher/core/engine/engine_link.dart';
import 'package:beammp_launcher/core/settings/launch_arguments.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/fakes.dart';

void main() {
  late FakeGateway gateway;
  late EngineController controller;

  setUp(() {
    gateway = FakeGateway();
    controller = EngineController(
      gateway,
      shutdownTimeout: const Duration(milliseconds: 50),
    );
  });

  tearDown(() => controller.dispose());

  EngineOnline online() => controller.connection as EngineOnline;

  group('start', () {
    test('is connecting until the engine answers', () async {
      expect(controller.connection, isA<EngineConnecting>());
      final started = controller.start();
      expect(controller.connection, isA<EngineConnecting>());
      await started;
      expect(controller.connection, isA<EngineOnline>());
    });

    test('reads the state and the games and subscribes to the log', () async {
      gateway.link.snapshot = const EngineSnapshot(
        state: EngineState.preparingGame,
        detail: 'checking',
        status: EngineStatus(
          account: AccountInfo(signedIn: true, username: 'user'),
        ),
      );
      await controller.start();

      expect(online().state, EngineState.preparingGame);
      expect(online().detail, 'checking');
      expect(online().account.username, 'user');
      expect(online().beamng?.name, 'BeamNG.drive');
      expect(gateway.link.logSubscriptions.single.level, LogLevel.debug);
      expect(gateway.link.logSubscriptions.single.backlog, 500);
    });

    test('starting twice starts one engine', () async {
      await Future.wait([controller.start(), controller.start()]);
      await controller.start();
      expect(gateway.starts, 1);
    });

    test('notifies listeners as it goes', () async {
      var notifications = 0;
      controller.addListener(() => notifications++);
      await controller.start();
      expect(notifications, greaterThanOrEqualTo(2));
    });
  });

  group('failures', () {
    test('no engine executable', () async {
      gateway.failure = const EngineNotFoundException(['/x']);
      await controller.start();
      expect(
        (controller.connection as EngineFailed).kind,
        FailureKind.notFound,
      );
    });

    test('the engine exits while starting', () async {
      gateway.failure = const EngineStartException(
        'exited',
        exitCode: 3,
        output: ['no game'],
      );
      await controller.start();
      final failed = controller.connection as EngineFailed;
      expect(failed.kind, FailureKind.startFailed);
      expect(failed.output, ['no game']);
    });

    test('a protocol mismatch', () async {
      gateway.failure = const HandshakeException(
        'nope',
        incompatibleProtocol: true,
      );
      await controller.start();
      expect(
        (controller.connection as EngineFailed).kind,
        FailureKind.incompatible,
      );
    });

    test('a refused handshake is a start failure', () async {
      gateway.failure = const HandshakeException('wrong token');
      await controller.start();
      expect(
        (controller.connection as EngineFailed).kind,
        FailureKind.startFailed,
      );
    });

    test('anything unexpected is reported, not thrown', () async {
      gateway.failure = StateError('boom');
      await controller.start();
      final failed = controller.connection as EngineFailed;
      expect(failed.kind, FailureKind.startFailed);
      expect(failed.message, contains('boom'));
    });

    test('the engine going away is a failure with its last output', () async {
      gateway.link.output = ['last words'];
      await controller.start();
      gateway.link.close();
      await settle();
      final failed = controller.connection as EngineFailed;
      expect(failed.kind, FailureKind.lost);
      expect(failed.output, ['last words']);
    });

    test('a fatal event explains the loss', () async {
      await controller.start();
      gateway.link.emit(
        const FatalReported('fatal', 'Failed to find the game'),
      );
      await settle();
      gateway.link.close();
      await settle();
      final failed = controller.connection as EngineFailed;
      expect(failed.kind, FailureKind.fatal);
      expect(failed.message, 'Failed to find the game');
    });

    test('trying again starts a fresh engine', () async {
      gateway.failure = StateError('first');
      await controller.start();
      expect(controller.connection, isA<EngineFailed>());

      gateway.failure = null;
      gateway.link = FakeEngineLink();
      await controller.start();
      expect(controller.connection, isA<EngineOnline>());
      expect(gateway.starts, 2);
    });

    test('a failure after the engine came up stops it', () async {
      final link = gateway.link
        ..listGamesError = const ControlClosedException();
      await controller.start();
      expect(controller.connection, isA<EngineFailed>());
      expect(link.killed, isTrue);
    });
  });

  group('events', () {
    setUp(() async => controller.start());

    test('engine state changes', () async {
      gateway.link.emit(
        const EngineStateChanged(EngineState.launchingGame, 'go'),
      );
      await settle();
      expect(online().state, EngineState.launchingGame);
      expect(online().detail, 'go');
      expect(controller.gameActive, isTrue);
      expect(controller.canLaunch, isFalse);
    });

    test('game state changes update the game card', () async {
      gateway.link.emit(
        const GameStateChanged(
          game: 'beamng',
          state: GameState.running,
          pid: 42,
        ),
      );
      await settle();
      expect(online().beamng?.state, GameState.running);
      expect(online().beamng?.pid, 42);
      expect(online().beamng?.name, 'BeamNG.drive');
    });

    test('events for other games are ignored', () async {
      gateway.link.emit(
        const GameStateChanged(game: 'other', state: GameState.running),
      );
      await settle();
      expect(online().beamng?.state, GameState.ready);
    });

    test('account changes', () async {
      gateway.link.emit(
        const AccountChanged(AccountInfo(signedIn: true, username: 'u')),
      );
      await settle();
      expect(online().account.username, 'u');
    });

    test('log records go to the log buffer, not the state', () async {
      var stateChanges = 0;
      controller.addListener(() => stateChanges++);
      gateway.link.emit(LogReceived(log('hello')));
      gateway.link.emit(LogReceived(log('world', level: LogLevel.warn)));
      await settle();
      expect(controller.logs.entries.map((e) => e.message), ['hello', 'world']);
      expect(stateChanges, 0);
    });

    test('the game ending leaves a note until the next launch', () async {
      gateway.link.emit(
        const GameStateChanged(
          game: 'beamng',
          state: GameState.exited,
          detail: 'The game exited with code 0',
        ),
      );
      await settle();
      expect(controller.gameNote, 'The game exited with code 0');

      gateway.link.emit(
        const GameStateChanged(game: 'beamng', state: GameState.launching),
      );
      await settle();
      expect(controller.gameNote, isNull);
    });

    test('unknown events are ignored', () async {
      gateway.link.emit(const UnknownEvent('progress', {}));
      await settle();
      expect(online().state, EngineState.ready);
    });
  });

  group('session', () {
    setUp(() async => controller.start());

    Future<void> settle() async => Future<void>.delayed(Duration.zero);

    test('starts out of any room', () {
      expect(controller.session, SessionInfo.none);
      expect(controller.sessionProblem, isNull);
    });

    test('follows what the engine says', () async {
      gateway.link.emit(
        const SessionChanged(
          SessionInfo(game: 'gearblocks', state: SessionState.connecting),
        ),
      );
      await settle();
      expect(controller.session.state, SessionState.connecting);

      gateway.link.emit(
        const SessionChanged(
          SessionInfo(
            game: 'gearblocks',
            state: SessionState.inRoom,
            role: 'host',
            code: 'ABC234',
            peers: [SessionPeer(id: 1, name: 'Ann')],
          ),
        ),
      );
      await settle();
      expect(controller.session.code, 'ABC234');
      expect(controller.session.isHost, isTrue);
    });

    test('asks the engine to create, join and leave', () async {
      await controller.createRoom('gearblocks', maxPlayers: 4, password: 'pw');
      await controller.joinRoom('gearblocks', 'ABC234');
      await controller.joinRoom('gearblocks', 'ABC234', password: 'pw');
      await controller.leaveRoom();
      expect(gateway.link.sessionCalls, [
        'create:gearblocks:4:pw',
        'join:gearblocks:ABC234:',
        'join:gearblocks:ABC234:pw',
        'leave',
      ]);
      expect(controller.sessionProblem, isNull);
    });

    test('a refusal is kept for the screen until the next try', () async {
      gateway.link.sessionError = const ControlException(
        'session_busy',
        'Already in a session; leave it first',
      );
      await controller.createRoom('gearblocks');
      final problem = controller.sessionProblem;
      expect(problem, isA<SessionRefused>());
      expect(
        (problem as SessionRefused).message,
        'Already in a session; leave it first',
      );

      gateway.link.sessionError = null;
      await controller.createRoom('gearblocks');
      expect(controller.sessionProblem, isNull);
    });

    test('a refusal without words shows its code', () async {
      gateway.link.sessionError = const ControlException(
        'session_unavailable',
        '',
      );
      await controller.joinRoom('gearblocks', 'ABC234');
      expect(
        (controller.sessionProblem as SessionRefused).message,
        'session_unavailable',
      );
    });

    test('an engine that does not answer is its own problem', () async {
      gateway.link.sessionError = const ControlTimeoutException(
        'session.create',
      );
      await controller.createRoom('gearblocks');
      expect(controller.sessionProblem, isA<SessionNoAnswer>());
    });

    test('a new state clears an old refusal', () async {
      gateway.link.sessionError = const ControlException(
        'session_busy',
        'busy',
      );
      await controller.createRoom('gearblocks');
      expect(controller.sessionProblem, isNotNull);
      gateway.link.emit(
        const SessionChanged(
          SessionInfo(game: 'gearblocks', state: SessionState.connecting),
        ),
      );
      await settle();
      expect(controller.sessionProblem, isNull);
    });

    test('changes notify the listeners', () async {
      var notified = 0;
      controller.addListener(() => notified++);
      gateway.link.emit(
        const SessionChanged(
          SessionInfo(game: 'gearblocks', state: SessionState.connecting),
        ),
      );
      await settle();
      expect(notified, greaterThan(0));
    });

    test('a game says whether it has rooms', () async {
      controller.dispose();
      gateway = FakeGateway();
      gateway.link.games = const [
        GameInfo(
          id: 'gearblocks',
          name: 'GearBlocks',
          state: GameState.ready,
          sessions: true,
        ),
      ];
      controller = EngineController(
        gateway,
        shutdownTimeout: const Duration(milliseconds: 50),
      );
      await controller.start();
      expect(online().games.single.sessions, isTrue);
    });
  });

  group('actions', () {
    setUp(() async => controller.start());

    test('launch asks the engine for the game', () async {
      expect(controller.canLaunch, isTrue);
      await controller.launchGame();
      expect(gateway.link.calls, contains('launchGame:beamng'));
      expect(controller.launchProblem, isNull);
    });

    test('launch does nothing unless the engine is ready', () async {
      gateway.link.emit(
        const EngineStateChanged(EngineState.launchingGame, ''),
      );
      await settle();
      await controller.launchGame();
      expect(gateway.link.calls, isNot(contains('launchGame:beamng')));
    });

    test('a refused launch is kept for the screen', () async {
      gateway.link.launchError = const ControlException(
        'launch_failed',
        'no such file',
      );
      await controller.launchGame();
      expect(
        (controller.launchProblem! as LaunchRefused).message,
        'no such file',
      );

      gateway.link.emit(
        const GameStateChanged(game: 'beamng', state: GameState.launching),
      );
      await settle();
      expect(controller.launchProblem, isNull);
    });

    test('a refused launch without a message shows the code', () async {
      gateway.link.launchError = const ControlException('game_not_ready', '');
      await controller.launchGame();
      expect(
        (controller.launchProblem! as LaunchRefused).message,
        'game_not_ready',
      );
    });

    test('the engine not answering is a problem of its own', () async {
      gateway.link.launchError = const ControlTimeoutException('game.launch');
      await controller.launchGame();
      expect(controller.launchProblem, isA<LaunchNoAnswer>());
    });

    test('stop only while a game is active', () async {
      await controller.stopGame();
      expect(gateway.link.calls, isNot(contains('stopGame:beamng')));

      gateway.link.emit(const EngineStateChanged(EngineState.gameRunning, ''));
      await settle();
      await controller.stopGame();
      expect(gateway.link.calls, contains('stopGame:beamng'));
    });

    test('stop tolerates the game already being gone', () async {
      gateway.link.emit(const EngineStateChanged(EngineState.gameRunning, ''));
      await settle();
      gateway.link.stopError = const ControlException('game_not_running', '');
      await controller.stopGame();
      expect(controller.connection, isA<EngineOnline>());
    });

    test('sign out updates the account', () async {
      gateway.link.emit(
        const AccountChanged(AccountInfo(signedIn: true, username: 'u')),
      );
      await settle();
      await controller.signOut();
      expect(online().account.signedIn, isFalse);
    });
  });

  group('launch arguments', () {
    late String typed;
    late String other;

    setUp(() async {
      typed = '';
      other = '';
      controller = EngineController(
        gateway,
        shutdownTimeout: const Duration(milliseconds: 50),
        launchArguments: (game) => game == 'gearblocks' ? other : typed,
      );
      await controller.start();
    });

    test('the typed arguments are parsed and passed on', () async {
      typed = '-console -userpath "C:\\My Games" \'quoted arg\'';
      await controller.launchGame();
      expect(gateway.link.launchArgs.single, [
        '-console',
        '-userpath',
        'C:\\My Games',
        'quoted arg',
      ]);
    });

    test(
      'every game gets the arguments typed for it, a bad one stops only that game',
      () async {
        typed = '-console';
        other = '-x "open'; // unterminated
        await controller.launchGame();
        expect(gateway.link.launchArgs.single, ['-console']);

        await controller.launchGame('gearblocks');
        expect(controller.launchProblem, isA<LaunchBadArguments>());
        expect(gateway.link.calls, isNot(contains('launchGame:gearblocks')));

        other = '-windowed';
        await controller.launchGame('gearblocks');
        expect(gateway.link.calls, contains('launchGame:gearblocks'));
        expect(gateway.link.launchArgs.last, ['-windowed']);
        expect(controller.launchProblem, isNull);
        expect(controller.lastLaunchedGame, 'gearblocks');
      },
    );

    test('no arguments are sent as an empty list', () async {
      await controller.launchGame();
      expect(gateway.link.launchArgs.single, isEmpty);
    });

    test('they are read at every launch, not once', () async {
      typed = 'one';
      await controller.launchGame();
      gateway.link.emit(
        const EngineStateChanged(EngineState.launchingGame, ''),
      );
      gateway.link.emit(const EngineStateChanged(EngineState.ready, ''));
      await settle();
      typed = 'two';
      await controller.launchGame();
      expect(gateway.link.launchArgs, [
        ['one'],
        ['two'],
      ]);
    });

    test(
      'unusable arguments stop the launch before the engine hears of it',
      () async {
        typed = '-flag "never closed';
        await controller.launchGame();
        expect(gateway.link.launchArgs, isEmpty);
        final problem = controller.launchProblem! as LaunchBadArguments;
        expect(problem.problem, ArgumentsProblem.unterminatedQuote);
      },
    );
  });

  test('the engine info from the handshake is available', () async {
    expect(controller.engineInfo, isNull);
    await controller.start();
    expect(controller.engineInfo?.engine, '9.9.9');
    expect(controller.engineInfo?.version, 1);
  });

  group('signing in', () {
    setUp(() async => controller.start());

    test('starts at idle', () {
      expect(controller.signIn, isA<SignInIdle>());
    });

    test('starting asks the engine', () async {
      await controller.startSignIn();
      expect(gateway.link.calls, contains('signIn'));
    });

    test('the engine\'s progress events become the status', () async {
      gateway.link.emit(
        const SignInProgress(
          state: SignInState.waitingForBrowser,
          url: 'https://accounts.example/x',
        ),
      );
      await settle();
      expect(
        (controller.signIn as SignInWaiting).url,
        'https://accounts.example/x',
      );

      gateway.link.emit(const SignInProgress(state: SignInState.exchanging));
      await settle();
      expect(controller.signIn, isA<SignInExchanging>());

      gateway.link.emit(
        const SignInProgress(state: SignInState.failed, reason: 'denied'),
      );
      await settle();
      expect((controller.signIn as SignInFailed).reason, 'denied');

      gateway.link.emit(const SignInProgress(state: SignInState.idle));
      await settle();
      expect(controller.signIn, isA<SignInIdle>());
    });

    test('a failure without a reason is an internal one', () async {
      gateway.link.emit(const SignInProgress(state: SignInState.failed));
      await settle();
      expect((controller.signIn as SignInFailed).reason, 'internal');
    });

    test('a successful sign-in clears the sign-in status', () async {
      gateway.link.emit(const SignInProgress(state: SignInState.exchanging));
      await settle();
      gateway.link.emit(
        const AccountChanged(AccountInfo(signedIn: true, username: 'u')),
      );
      await settle();
      expect(controller.signIn, isA<SignInIdle>());
    });

    test('busy and already-signed-in answers change nothing', () async {
      gateway.link.signInError = const ControlException('sign_in_busy', '');
      await controller.startSignIn();
      gateway.link.signInError = const ControlException(
        'already_signed_in',
        '',
      );
      await controller.startSignIn();
      expect(controller.signIn, isA<SignInIdle>());
    });

    test('an engine that cannot sign in shows an error', () async {
      gateway.link.signInError = const ControlException(
        'sign_in_unavailable',
        '',
      );
      await controller.startSignIn();
      expect((controller.signIn as SignInFailed).reason, 'internal');
    });

    test('an engine that does not answer shows an error', () async {
      gateway.link.signInError = const ControlTimeoutException(
        'account.sign_in',
      );
      await controller.startSignIn();
      expect(controller.signIn, isA<SignInFailed>());
    });

    test('cancelling asks the engine', () async {
      await controller.cancelSignIn();
      expect(gateway.link.calls, contains('cancelSignIn'));
    });

    test('a failure message can be dismissed, other states cannot', () async {
      gateway.link.emit(const SignInProgress(state: SignInState.exchanging));
      await settle();
      controller.dismissSignIn();
      expect(controller.signIn, isA<SignInExchanging>());

      gateway.link.emit(
        const SignInProgress(state: SignInState.failed, reason: 'timeout'),
      );
      await settle();
      controller.dismissSignIn();
      expect(controller.signIn, isA<SignInIdle>());
    });
  });

  group('shutdown', () {
    setUp(() async => controller.start());

    test(
      'asks the engine to exit and does not report that as a failure',
      () async {
        await controller.shutdown();
        await settle();
        expect(gateway.link.calls, contains('shutdown:false'));
        expect(controller.connection, isA<EngineOnline>());
      },
    );

    test('can force it while a game runs', () async {
      await controller.shutdown(force: true);
      expect(gateway.link.calls, contains('shutdown:true'));
    });

    test('an engine that does not leave is stopped', () async {
      gateway.link.shutdownHangs = true;
      final link = gateway.link;
      await controller.shutdown().timeout(const Duration(seconds: 10));
      expect(link.killed, isTrue);
    });
  });

  test('dispose stops the engine', () async {
    final other = FakeGateway();
    final standalone = EngineController(other);
    await standalone.start();
    standalone.dispose();
    expect(other.link.killed, isTrue);
  });

  group('install', () {
    GameInfo gearBlocks({
      InstallStatus status = InstallStatus.needsInstall,
      bool busy = false,
    }) => GameInfo(
      id: 'gearblocks',
      name: 'GearBlocks',
      state: GameState.notFound,
      install: GameInstall(status: status, busy: busy),
    );

    setUp(() {
      gateway.link.games = [gearBlocks()];
    });

    test('installing asks the engine and shows progress at once', () async {
      await controller.start();
      expect(controller.installProgress('gearblocks'), isNull);
      final problem = await controller.installGame('gearblocks');
      expect(problem, isNull);
      expect(gateway.link.installCalls, ['install:gearblocks']);
      final shown = controller.installProgress('gearblocks')!;
      expect(shown.phase, InstallPhase.running);
    });

    test('the engine\'s progress events replace it', () async {
      await controller.start();
      await controller.installGame('gearblocks');
      gateway.link.emit(
        const InstallProgressChanged(
          game: 'gearblocks',
          phase: InstallPhase.running,
          step: 'loader',
          message: 'Downloading',
          fraction: 0.5,
        ),
      );
      await pumpEventQueue();
      expect(controller.installProgress('gearblocks')?.fraction, 0.5);
      expect(controller.installProgress('gearblocks')?.message, 'Downloading');
    });

    test('done clears the progress and brings the games up to date', () async {
      await controller.start();
      await controller.installGame('gearblocks');
      gateway.link.games = [gearBlocks(status: InstallStatus.installed)];
      gateway.link.emit(
        const InstallProgressChanged(
          game: 'gearblocks',
          phase: InstallPhase.done,
          step: 'done',
          message: '',
          fraction: 1,
        ),
      );
      await pumpEventQueue();
      expect(controller.installProgress('gearblocks'), isNull);
      expect(
        (controller.connection as EngineOnline).games.single.install?.status,
        InstallStatus.installed,
      );
      expect(gateway.link.calls.where((c) => c == 'listGames').length, 2);
    });

    test('a failure stays until it is dismissed or the next try', () async {
      await controller.start();
      await controller.installGame('gearblocks');
      gateway.link.emit(
        const InstallProgressChanged(
          game: 'gearblocks',
          phase: InstallPhase.failed,
          step: 'done',
          message: 'no network',
          errorCode: 'download_failed',
        ),
      );
      await pumpEventQueue();
      expect(
        controller.installProgress('gearblocks')?.errorCode,
        'download_failed',
      );
      var notified = 0;
      controller.addListener(() => notified++);
      controller.dismissInstall('gearblocks');
      expect(controller.installProgress('gearblocks'), isNull);
      expect(notified, 1);
      controller.dismissInstall('gearblocks'); // nothing to forget
      expect(notified, 1);
    });

    test('a cancelled install is shown as such', () async {
      await controller.start();
      await controller.installGame('gearblocks');
      await controller.cancelInstall('gearblocks');
      expect(gateway.link.installCalls.last, 'cancel:gearblocks');
      gateway.link.emit(
        const InstallProgressChanged(
          game: 'gearblocks',
          phase: InstallPhase.cancelled,
          step: 'done',
          message: 'Cancelled',
        ),
      );
      await pumpEventQueue();
      expect(
        controller.installProgress('gearblocks')?.phase,
        InstallPhase.cancelled,
      );
    });

    test('uninstalling passes on whether the loader goes too', () async {
      await controller.start();
      await controller.uninstallGame('gearblocks');
      await controller.uninstallGame('gearblocks', removeLoader: true);
      expect(gateway.link.installCalls, [
        'uninstall:gearblocks:false',
        'uninstall:gearblocks:true',
      ]);
    });

    test('refusals say why', () async {
      await controller.start();
      for (final code in ['game_not_ready', 'install_busy', 'game_busy']) {
        gateway.link.installError = ControlException(code, 'busy');
        expect(await controller.installGame('gearblocks'), isA<InstallBusy>());
      }
      gateway.link.installError = const ControlException(
        'install_unsupported',
        'nothing to install',
      );
      final refused = await controller.installGame('gearblocks');
      expect(refused, isA<InstallRefused>());
      expect((refused! as InstallRefused).message, 'nothing to install');
      // no progress is invented for a request that was refused
      expect(controller.installProgress('gearblocks'), isNull);
      gateway.link.installError = const ControlClosedException();
      expect(
        await controller.installGame('gearblocks'),
        isA<InstallNoEngine>(),
      );
    });

    test('without an engine there is nothing to ask', () async {
      expect(
        await controller.installGame('gearblocks'),
        isA<InstallNoEngine>(),
      );
      expect(
        await controller.uninstallGame('gearblocks'),
        isA<InstallNoEngine>(),
      );
      await controller.cancelInstall('gearblocks'); // does nothing, no error
    });

    test('a cancel the engine refuses is not an error', () async {
      await controller.start();
      gateway.link.installError = const ControlException('x', 'y');
      await controller.cancelInstall('gearblocks');
    });
  });
}
