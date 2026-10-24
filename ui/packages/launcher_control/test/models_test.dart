import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

void main() {
  group('EngineState', () {
    test('parses every wire name', () {
      expect(EngineState.parse('starting'), EngineState.starting);
      expect(EngineState.parse('preparing_game'), EngineState.preparingGame);
      expect(EngineState.parse('launching_game'), EngineState.launchingGame);
      expect(EngineState.parse('game_running'), EngineState.gameRunning);
      expect(EngineState.parse('fatal'), EngineState.fatal);
    });

    test('keeps working with states a newer engine adds', () {
      expect(EngineState.parse('hibernating'), EngineState.unknown);
      expect(EngineState.parse(null), EngineState.unknown);
      expect(EngineState.parse(5), EngineState.unknown);
      expect(EngineState.parse(''), EngineState.unknown);
    });

    test('knows when a game is active', () {
      expect(EngineState.launchingGame.gameActive, isTrue);
      expect(EngineState.gameRunning.gameActive, isTrue);
      expect(EngineState.ready.gameActive, isFalse);
    });
  });

  test('a game knows its Steam app id, and its settings reason', () {
    final game = GameInfo.fromJson({
      'id': 'beamng',
      'name': 'BeamNG.drive',
      'state': 'not_found',
      'detail': 'BeamNG.drive was not found',
      'steam_app_id': 284160,
    });
    expect(game.steamAppId, 284160);
    expect(game.detail, 'BeamNG.drive was not found');
    expect(game.state, GameState.notFound);
    expect(
      GameInfo.fromJson({'id': 'x', 'name': 'X', 'state': 'ready'}).steamAppId,
      isNull,
    );
    expect(
      GameInfo.fromJson({'id': 'x', 'name': 'X', 'steam_app_id': 'no'})
          .steamAppId,
      isNull,
    );
    expect(
        game,
        isNot(GameInfo.fromJson({
          'id': 'beamng',
          'name': 'BeamNG.drive',
          'state': 'not_found',
          'detail': 'BeamNG.drive was not found',
        })));
  });

  group('sessions', () {
    test('an idle session', () {
      final session = SessionInfo.fromJson({'game': '', 'state': 'idle'});
      expect(session.state, SessionState.idle);
      expect(session.active, isFalse);
      expect(session.peers, isEmpty);
      expect(session.errorCode, isNull);
      expect(session, SessionInfo.none);
    });

    test('a room, with who is in it', () {
      final session = SessionInfo.fromJson({
        'game': 'gearblocks',
        'state': 'in_room',
        'role': 'guest',
        'code': 'ABC234',
        'self_id': 3,
        'host_id': 1,
        'peers': [
          {'id': 1, 'name': 'Host'},
          {'id': 3, 'name': 'Ann'},
        ],
      });
      expect(session.state, SessionState.inRoom);
      expect(session.active, isTrue);
      expect(session.isHost, isFalse);
      expect(session.code, 'ABC234');
      expect(session.selfId, 3);
      expect(session.hostId, 1);
      expect(session.peers, const [
        SessionPeer(id: 1, name: 'Host'),
        SessionPeer(id: 3, name: 'Ann'),
      ]);
      expect(
        SessionInfo.fromJson({'state': 'in_room', 'role': 'host'}).isHost,
        isTrue,
      );
    });

    test('why a session ended', () {
      final session = SessionInfo.fromJson({
        'game': 'gearblocks',
        'state': 'idle',
        'error': {'code': 'room_full', 'message': 'The room is full'},
      });
      expect(session.errorCode, 'room_full');
      expect(session.errorMessage, 'The room is full');
      expect(session.active, isFalse);
    });

    test('odd content is dropped, not thrown at the caller', () {
      final session = SessionInfo.fromJson({
        'state': 'in_room',
        'peers': [
          {'name': 'no id'},
          'text',
          {'id': 2},
          {'id': 'x', 'name': 'bad id'},
        ],
        'error': 'not an object',
      });
      expect(session.peers, const [SessionPeer(id: 2, name: '')]);
      expect(session.errorCode, isNull);
      expect(SessionInfo.fromJson({'state': 'from the future'}).state,
          SessionState.unknown);
      expect(SessionInfo.fromJson({'peers': 5}).peers, isEmpty);
    });

    test('sessions compare by what they hold', () {
      const a = SessionInfo(
        state: SessionState.inRoom,
        peers: [SessionPeer(id: 1, name: 'A')],
      );
      const b = SessionInfo(
        state: SessionState.inRoom,
        peers: [SessionPeer(id: 1, name: 'A')],
      );
      expect(a, b);
      expect(a.hashCode, b.hashCode);
      expect(
          a,
          isNot(const SessionInfo(
            state: SessionState.inRoom,
            peers: [SessionPeer(id: 1, name: 'B')],
          )));
      expect(a, isNot(const SessionInfo(state: SessionState.inRoom)));
    });

    test('session.state is an event, and a game knows if it has sessions', () {
      final event = EngineEvent.decode('session.state', {
        'game': 'gearblocks',
        'state': 'connecting',
      });
      expect(event, isA<SessionChanged>());
      expect((event as SessionChanged).session.state, SessionState.connecting);
      expect(EngineEvent.decode('session.state', {'game': 'x'}),
          isA<UnknownEvent>());

      expect(
        GameInfo.fromJson(
                {'id': 'g', 'name': 'G', 'state': 'ready', 'sessions': true})
            .sessions,
        isTrue,
      );
      expect(
          GameInfo.fromJson({'id': 'g', 'name': 'G', 'state': 'ready'})
              .sessions,
          isFalse);
      expect(
        GameInfo.fromJson(
                {'id': 'g', 'name': 'G', 'state': 'ready', 'sessions': 'yes'})
            .sessions,
        isFalse,
      );
    });
  });

  test('an unavailable game is a game state of its own', () {
    expect(GameState.parse('unavailable'), GameState.unavailable);
    expect(
      GameInfo.fromJson({
        'id': 'gearblocks',
        'name': 'GearBlocks',
        'state': 'unavailable',
      }),
      const GameInfo(
        id: 'gearblocks',
        name: 'GearBlocks',
        state: GameState.unavailable,
      ),
    );
  });

  test('GameState and LogLevel fall back instead of throwing', () {
    expect(GameState.parse('exited'), GameState.exited);
    expect(GameState.parse('teleporting'), GameState.unknown);
    expect(LogLevel.parse('warn'), LogLevel.warn);
    expect(LogLevel.parse('shout'), LogLevel.info);
  });

  group('AccountInfo', () {
    test('parses a signed-in account', () {
      final account = AccountInfo.fromJson({
        'signed_in': true,
        'username': 'user',
        'role': 'EA',
        'id': 9,
      });
      expect(
          account,
          const AccountInfo(
              signedIn: true, username: 'user', role: 'EA', id: 9));
    });

    test('is signed out unless the engine says otherwise', () {
      expect(AccountInfo.fromJson({}), AccountInfo.signedOut);
      expect(AccountInfo.fromJson({'signed_in': 'yes'}).signedIn, isFalse);
    });

    test('ignores fields of the wrong type', () {
      final account =
          AccountInfo.fromJson({'signed_in': true, 'username': 5, 'id': 'x'});
      expect(account.username, isNull);
      expect(account.id, isNull);
    });
  });

  test('EngineSnapshot parses the engine.state response', () {
    final snapshot = EngineSnapshot.fromJson({
      'state': 'ready',
      'detail': 'ok',
      'status': {
        'account': {'signed_in': false},
        'ping': 42,
        'upload_status': 'UlLoading',
        'mod_status': 'mods',
        'game_pid': 7,
      },
    });
    expect(snapshot.state, EngineState.ready);
    expect(snapshot.detail, 'ok');
    expect(snapshot.status.ping, 42);
    expect(snapshot.status.uploadStatus, 'UlLoading');
    expect(snapshot.status.modStatus, 'mods');
    expect(snapshot.status.gamePid, 7);
  });

  test('EngineSnapshot survives a missing status', () {
    final snapshot = EngineSnapshot.fromJson({'state': 'starting'});
    expect(snapshot.status, const EngineStatus());
    expect(snapshot.status.ping, -1);
  });

  test('GameInfo parses and falls back to the id for the name', () {
    expect(
      GameInfo.fromJson({
        'id': 'beamng',
        'name': 'BeamNG.drive',
        'state': 'running',
        'pid': 4
      }),
      const GameInfo(
          id: 'beamng', name: 'BeamNG.drive', state: GameState.running, pid: 4),
    );
    expect(
        GameInfo.fromJson({'id': 'beamng', 'state': 'ready'}).name, 'beamng');
  });

  test('LogEntry parses the timestamp as UTC', () {
    final entry = LogEntry.fromJson({
      'ts': '2023-11-14T22:13:20.123Z',
      'level': 'warn',
      'message': 'careful',
    });
    expect(entry.time, DateTime.utc(2023, 11, 14, 22, 13, 20, 123));
    expect(entry.time.isUtc, isTrue);
    expect(entry.level, LogLevel.warn);
    expect(entry.message, 'careful');
  });

  test('LogEntry tolerates a bad timestamp', () {
    final entry =
        LogEntry.fromJson({'ts': 'yesterday', 'level': 'info', 'message': 'x'});
    expect(entry.time.millisecondsSinceEpoch, 0);
  });

  group('EngineEvent.decode', () {
    test('engine.state', () {
      final event = EngineEvent.decode(
          'engine.state', {'state': 'game_running', 'detail': 'd'});
      expect(event, isA<EngineStateChanged>());
      expect((event as EngineStateChanged).state, EngineState.gameRunning);
      expect(event.detail, 'd');
    });

    test('game.state with pid and detail', () {
      final event = EngineEvent.decode('game.state', {
        'game': 'beamng',
        'state': 'exited',
        'pid': 3,
        'detail': 'The game exited with code 0',
      }) as GameStateChanged;
      expect(event.game, 'beamng');
      expect(event.state, GameState.exited);
      expect(event.pid, 3);
      expect(event.detail, 'The game exited with code 0');
    });

    test('account.state', () {
      final event = EngineEvent.decode(
              'account.state', {'signed_in': true, 'username': 'u'})
          as AccountChanged;
      expect(event.account.username, 'u');
    });

    test('log', () {
      final event = EngineEvent.decode('log', {
        'ts': '2024-01-01T00:00:00.000Z',
        'level': 'error',
        'message': 'boom',
      }) as LogReceived;
      expect(event.entry.level, LogLevel.error);
    });

    test('fatal', () {
      final event =
          EngineEvent.decode('fatal', {'code': 'fatal', 'message': 'no game'})
              as FatalReported;
      expect(event.message, 'no game');
    });

    test('account.signin waiting', () {
      final event = EngineEvent.decode('account.signin', {
        'state': 'waiting_for_browser',
        'url': 'https://accounts.example/auth/external?x=1',
      }) as SignInProgress;
      expect(event.state, SignInState.waitingForBrowser);
      expect(event.url, 'https://accounts.example/auth/external?x=1');
      expect(event.reason, isNull);
    });

    test('account.signin failed', () {
      final event = EngineEvent.decode('account.signin', {
        'state': 'failed',
        'reason': 'denied',
      }) as SignInProgress;
      expect(event.state, SignInState.failed);
      expect(event.reason, 'denied');
    });

    test('a sign-in state from a newer engine is not an error', () {
      final event =
          EngineEvent.decode('account.signin', {'state': 'on_the_moon'})
              as SignInProgress;
      expect(event.state, SignInState.unknown);
    });

    test('account.signin without a state is unknown', () {
      expect(EngineEvent.decode('account.signin', {}), isA<UnknownEvent>());
    });

    test('unknown names are kept, not dropped', () {
      final event = EngineEvent.decode('progress', {'task': 'mods'});
      expect(event, isA<UnknownEvent>());
      expect((event as UnknownEvent).name, 'progress');
      expect(event.data['task'], 'mods');
    });

    test('malformed known events become unknown instead of throwing', () {
      expect(EngineEvent.decode('engine.state', {}), isA<UnknownEvent>());
      expect(EngineEvent.decode('engine.state', {'state': 5}),
          isA<UnknownEvent>());
      expect(EngineEvent.decode('game.state', {'state': 'ready'}),
          isA<UnknownEvent>());
      expect(EngineEvent.decode('account.state', {'signed_in': 'yes'}),
          isA<UnknownEvent>());
      expect(EngineEvent.decode('log', {'level': 'info'}), isA<UnknownEvent>());
    });
  });

  group('install', () {
    test('InstallStatus parses every state, and anything else is unknown', () {
      expect(InstallStatus.parse('installed'), InstallStatus.installed);
      expect(InstallStatus.parse('needs_install'), InstallStatus.needsInstall);
      expect(InstallStatus.parse('outdated'), InstallStatus.outdated);
      expect(
        InstallStatus.parse('unsupported_build'),
        InstallStatus.unsupportedBuild,
      );
      expect(InstallStatus.parse('game_missing'), InstallStatus.gameMissing);
      expect(InstallStatus.parse('unknown'), InstallStatus.unknown);
      expect(InstallStatus.parse('nonsense'), InstallStatus.unknown);
      expect(InstallStatus.parse(null), InstallStatus.unknown);
      expect(InstallStatus.parse(3), InstallStatus.unknown);
    });

    test('a game carries its install', () {
      final game = GameInfo.fromJson({
        'id': 'gearblocks',
        'name': 'GearBlocks',
        'state': 'not_found',
        'install': {
          'state': 'needs_install',
          'busy': true,
          'version': '0.1.0',
          'game_build': '0.8.9740',
          'files_available': false,
        },
      });
      expect(game.install?.status, InstallStatus.needsInstall);
      expect(game.install?.busy, isTrue);
      expect(game.install?.version, '0.1.0');
      expect(game.install?.gameBuild, '0.8.9740');
      expect(game.install?.filesAvailable, isFalse);
    });

    test('defaults: not busy, files available, nothing known', () {
      final game = GameInfo.fromJson({
        'id': 'g',
        'name': 'G',
        'state': 'ready',
        'install': {'state': 'installed'},
      });
      expect(game.install?.busy, isFalse);
      expect(game.install?.filesAvailable, isTrue);
      expect(game.install?.version, isNull);
      expect(game.install?.gameBuild, isNull);
      expect(
        GameInfo.fromJson({'id': 'g', 'name': 'G', 'state': 'ready'}).install,
        isNull,
      );
      // not an object: ignored
      expect(
        GameInfo.fromJson({
          'id': 'g',
          'name': 'G',
          'state': 'ready',
          'install': 'yes',
        }).install,
        isNull,
      );
    });

    test('the install is part of what makes two games equal', () {
      GameInfo game(Object? install) => GameInfo.fromJson({
            'id': 'g',
            'name': 'G',
            'state': 'ready',
            'install': install,
          });
      expect(game({'state': 'installed'}), game({'state': 'installed'}));
      expect(
        game({'state': 'installed'}).hashCode,
        game({'state': 'installed'}).hashCode,
      );
      expect(
        game({'state': 'installed'}),
        isNot(game({'state': 'needs_install'})),
      );
      expect(
        game({'state': 'installed'}),
        isNot(game({'state': 'installed', 'busy': true})),
      );
      expect(
        game({'state': 'installed'}),
        isNot(game({'state': 'installed', 'version': '1'})),
      );
      expect(
        game({'state': 'installed'}),
        isNot(game({'state': 'installed', 'files_available': false})),
      );
    });

    test('game.install events: running, done, failed, cancelled', () {
      final running = EngineEvent.decode('game.install', {
        'game': 'gearblocks',
        'state': 'running',
        'step': 'loader',
        'message': 'Downloading',
        'fraction': 0.25,
      }) as InstallProgressChanged;
      expect(running.game, 'gearblocks');
      expect(running.phase, InstallPhase.running);
      expect(running.phase.ended, isFalse);
      expect(running.step, 'loader');
      expect(running.message, 'Downloading');
      expect(running.fraction, 0.25);
      expect(running.uninstall, isFalse);
      expect(running.errorCode, isNull);

      final done = EngineEvent.decode('game.install', {
        'game': 'gearblocks',
        'state': 'done',
        'step': 'done',
        'fraction': 1,
        'uninstall': true,
      }) as InstallProgressChanged;
      expect(done.phase, InstallPhase.done);
      expect(done.phase.ended, isTrue);
      expect(done.fraction, 1.0);
      expect(done.uninstall, isTrue);

      final failed = EngineEvent.decode('game.install', {
        'game': 'gearblocks',
        'state': 'failed',
        'step': 'done',
        'message': 'no network',
        'error': {'code': 'download_failed', 'message': 'no network'},
      }) as InstallProgressChanged;
      expect(failed.phase, InstallPhase.failed);
      expect(failed.errorCode, 'download_failed');
      expect(failed.fraction, isNull);

      final cancelled = EngineEvent.decode('game.install', {
        'game': 'gearblocks',
        'state': 'cancelled',
      }) as InstallProgressChanged;
      expect(cancelled.phase, InstallPhase.cancelled);
      expect(cancelled.phase.ended, isTrue);
      expect(cancelled.step, '');
      expect(cancelled.message, '');
    });

    test('an unknown state is running, an event without a game is unknown', () {
      expect(
        (EngineEvent.decode('game.install', {'game': 'g', 'state': 'zzz'})
                as InstallProgressChanged)
            .phase,
        InstallPhase.running,
      );
      expect(
        EngineEvent.decode('game.install', {'state': 'done'}),
        isA<UnknownEvent>(),
      );
    });
  });
}
