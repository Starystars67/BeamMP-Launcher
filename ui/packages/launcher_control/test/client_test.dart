import 'dart:async';

import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

import 'support/fake_transport.dart';

Future<(ControlClient, FakeTransport)> connected() async {
  final transport = FakeTransport();
  final client =
      ControlClient.connect(transport, token: 'secret', clientName: 'tests');
  await transport.acceptHello();
  return (await client, transport);
}

void main() {
  group('handshake', () {
    test('sends the token, the client name and the supported versions',
        () async {
      final transport = FakeTransport();
      final client = ControlClient.connect(transport,
          token: 'secret', clientName: 'tests');
      await transport.acceptHello();
      await client;

      final hello = transport.sent.single;
      expect(hello['v'], 1);
      expect(hello['type'], 'request');
      expect(hello['name'], 'hello');
      expect(hello['data'], {
        'token': 'secret',
        'client': 'tests',
        'versions': [1],
      });
    });

    test('exposes what the engine said', () async {
      final (client, _) = await connected();
      expect(client.hello.version, 1);
      expect(client.hello.engine, '9.9.9');
      expect(client.hello.capabilities, ['logs', 'games', 'account']);
      expect(client.hello.games, ['beamng']);
    });

    test('a silently dropped connection means the token was refused', () async {
      final transport = FakeTransport();
      final client = ControlClient.connect(transport, token: 'wrong');
      await transport.waitForSent(1);
      await transport.hangUp();
      await expectLater(
          client,
          throwsA(isA<HandshakeException>()
              .having((e) => e.incompatibleProtocol, 'incompatible', isFalse)));
      expect(transport.closed, isTrue);
    });

    test('an incompatible protocol is reported as such', () async {
      final transport = FakeTransport();
      final client = ControlClient.connect(transport, token: 'secret');
      await transport.waitForSent(1);
      transport.refuse(1, 'incompatible_protocol', 'update the launcher');
      await expectLater(
          client,
          throwsA(isA<HandshakeException>()
              .having((e) => e.incompatibleProtocol, 'incompatible', isTrue)));
    });

    test('a version this client does not speak is incompatible', () async {
      final transport = FakeTransport();
      final client = ControlClient.connect(transport, token: 'secret');
      await transport.acceptHello(version: 2);
      await expectLater(
          client,
          throwsA(isA<HandshakeException>()
              .having((e) => e.incompatibleProtocol, 'incompatible', isTrue)));
      expect(transport.closed, isTrue);
    });

    test('no answer in time fails and closes the transport', () async {
      final transport = FakeTransport();
      final client = ControlClient.connect(
        transport,
        token: 'secret',
        handshakeTimeout: const Duration(milliseconds: 50),
      );
      await expectLater(client, throwsA(isA<HandshakeException>()));
      expect(transport.closed, isTrue);
    });
  });

  group('requests', () {
    test('responses are matched by id, whatever the order', () async {
      final (client, transport) = await connected();
      final first = client.request('a');
      final second = client.request('b');
      await transport.waitForSent(3);
      final firstId = transport.sent[1]['id']! as int;
      final secondId = transport.sent[2]['id']! as int;
      expect(firstId, isNot(secondId));

      transport.respond(secondId, {'answer': 'b'});
      transport.respond(firstId, {'answer': 'a'});
      expect(await first, {'answer': 'a'});
      expect(await second, {'answer': 'b'});
    });

    test('an error response becomes a ControlException with the code',
        () async {
      final (client, transport) = await connected();
      final pending = client.request('game.launch');
      await transport.waitForSent(2);
      transport.refuse(
          transport.sent[1]['id']! as int, 'game_not_ready', 'later');
      await expectLater(
        pending,
        throwsA(isA<ControlException>()
            .having((e) => e.code, 'code', 'game_not_ready')
            .having((e) => e.message, 'message', 'later')),
      );
    });

    test('a request nobody answers times out and does not leak', () async {
      final (client, _) = await connected();
      await expectLater(
        client.request('slow', timeout: const Duration(milliseconds: 30)),
        throwsA(isA<ControlTimeoutException>()),
      );
    });

    test('a late answer after a timeout is ignored', () async {
      final (client, transport) = await connected();
      final pending =
          client.request('slow', timeout: const Duration(milliseconds: 30));
      await expectLater(pending, throwsA(isA<ControlTimeoutException>()));
      transport.respond(transport.sent[1]['id']! as int);
      expect(
          await client
              .request('ping', timeout: const Duration(milliseconds: 100))
              .then((_) => 'answered', onError: (Object _) => 'timeout'),
          'timeout');
    });

    test('pending requests fail when the connection drops', () async {
      final (client, transport) = await connected();
      final pending = client.request('engine.state');
      await transport.waitForSent(2);
      await transport.hangUp();
      await expectLater(pending, throwsA(isA<ControlClosedException>()));
      await client.closed;
      expect(client.isClosed, isTrue);
    });

    test('requests after the connection ended fail immediately', () async {
      final (client, transport) = await connected();
      await transport.hangUp();
      await client.closed;
      await expectLater(client.request('engine.state'),
          throwsA(isA<ControlClosedException>()));
    });

    test('a failing send is reported as a closed connection', () async {
      final (client, transport) = await connected();
      transport.closed = true;
      await expectLater(client.request('engine.state'),
          throwsA(isA<ControlClosedException>()));
    });

    test('close ends the client and the event stream', () async {
      final (client, transport) = await connected();
      final done = client.events.toList();
      await client.close();
      expect(client.isClosed, isTrue);
      expect(transport.closed, isTrue);
      expect(await done, isEmpty);
    });
  });

  group('incoming lines', () {
    test('garbage and unknown responses are ignored', () async {
      final (client, transport) = await connected();
      transport.sayRaw('not json');
      transport.sayRaw('[1,2]');
      transport.sayRaw('{"type":"mystery"}');
      transport.respond(9999);
      final answer = client.request('x');
      await transport.waitForSent(2);
      transport.respond(transport.sent[1]['id']! as int, {'ok': 1});
      expect(await answer, {'ok': 1});
      expect(client.isClosed, isFalse);
    });

    test('a response without data gives an empty map', () async {
      final (client, transport) = await connected();
      final answer = client.request('x');
      await transport.waitForSent(2);
      transport.say({
        'v': 1,
        'id': transport.sent[1]['id'],
        'type': 'response',
        'ok': true
      });
      expect(await answer, isEmpty);
    });

    test('an error without a body still fails the request', () async {
      final (client, transport) = await connected();
      final answer = client.request('x');
      await transport.waitForSent(2);
      transport.say({
        'v': 1,
        'id': transport.sent[1]['id'],
        'type': 'response',
        'ok': false
      });
      await expectLater(
          answer,
          throwsA(isA<ControlException>()
              .having((e) => e.code, 'code', 'unknown')));
    });
  });

  group('events', () {
    test('events sent right after the handshake are not lost', () async {
      final transport = FakeTransport();
      final pending = ControlClient.connect(transport, token: 'secret');
      await transport.acceptHello();
      transport.event('engine.state', {'state': 'ready', 'detail': ''});
      transport.event('account.state', {'signed_in': false});
      transport.event('game.state', {'game': 'beamng', 'state': 'ready'});
      final client = await pending;
      await Future<void>.delayed(const Duration(milliseconds: 10));

      final events = await client.events.take(3).toList();
      expect(events[0], isA<EngineStateChanged>());
      expect(events[1], isA<AccountChanged>());
      expect(events[2], isA<GameStateChanged>());
    });

    test('keep their order and decode into types', () async {
      final (client, transport) = await connected();
      final seen = client.events.take(3).toList();
      transport
          .event('engine.state', {'state': 'launching_game', 'detail': ''});
      transport.event(
          'game.state', {'game': 'beamng', 'state': 'running', 'pid': 12});
      transport.event('log',
          {'ts': '2024-01-01T00:00:00.000Z', 'level': 'info', 'message': 'hi'});
      final events = await seen;
      expect(
          (events[0] as EngineStateChanged).state, EngineState.launchingGame);
      expect((events[1] as GameStateChanged).pid, 12);
      expect((events[2] as LogReceived).entry.message, 'hi');
    });

    test('an unknown event is passed on as UnknownEvent', () async {
      final (client, transport) = await connected();
      final seen = client.events.first;
      transport.event('progress', {'task': 'mods', 'fraction': 0.5});
      expect(await seen, isA<UnknownEvent>());
    });

    test('the stream completes when the engine goes away', () async {
      final (client, transport) = await connected();
      final done = client.events.toList();
      transport.event('fatal', {'code': 'fatal', 'message': 'gone'});
      await transport.hangUp();
      final events = await done;
      expect(events.single, isA<FatalReported>());
    });
  });

  group('typed requests', () {
    Future<T> answer<T>(
      FakeTransport transport,
      Future<T> Function() ask,
      Map<String, Object?> data,
      void Function(Map<String, Object?> sent) check,
    ) async {
      final before = transport.sent.length;
      final result = ask();
      await transport.waitForSent(before + 1);
      check(transport.sent.last);
      transport.respond(transport.sent.last['id']! as int, data);
      return result;
    }

    test('engineState', () async {
      final (client, transport) = await connected();
      final snapshot = await answer(
        transport,
        client.engineState,
        {
          'state': 'ready',
          'detail': '',
          'status': {'ping': 5},
        },
        (sent) => expect(sent['name'], 'engine.state'),
      );
      expect(snapshot.state, EngineState.ready);
      expect(snapshot.status.ping, 5);
    });

    test('shutdown passes force', () async {
      final (client, transport) = await connected();
      await answer(transport, () => client.shutdown(force: true), {}, (sent) {
        expect(sent['name'], 'engine.shutdown');
        expect(sent['data'], {'force': true});
      });
    });

    test('subscribeLogs sends level and backlog and returns the count',
        () async {
      final (client, transport) = await connected();
      final sent = await answer(
        transport,
        () => client.subscribeLogs(level: LogLevel.warn, backlog: 50),
        {'level': 'warn', 'sent': 12},
        (sent) {
          expect(sent['name'], 'log.subscribe');
          expect(sent['data'], {'level': 'warn', 'backlog': 50});
        },
      );
      expect(sent, 12);
    });

    test('listGames', () async {
      final (client, transport) = await connected();
      final games = await answer(
        transport,
        client.listGames,
        {
          'games': [
            {'id': 'beamng', 'name': 'BeamNG.drive', 'state': 'ready'},
            'junk',
          ],
        },
        (sent) => expect(sent['name'], 'games.list'),
      );
      expect(games, [
        const GameInfo(
            id: 'beamng', name: 'BeamNG.drive', state: GameState.ready)
      ]);
    });

    test('launchGame sends the game and its arguments', () async {
      final (client, transport) = await connected();
      await answer(
          transport, () => client.launchGame('beamng', args: ['-console']), {},
          (sent) {
        expect(sent['name'], 'game.launch');
        expect(sent['data'], {
          'game': 'beamng',
          'args': ['-console'],
        });
      });
    });

    test('configureGame sends the whole picture, with empty strings to clear',
        () async {
      final (client, transport) = await connected();
      await answer(
          transport,
          () => client.configureGame('beamng',
              directory: '/games/BeamNG', command: '/opt/run.sh'),
          {}, (sent) {
        expect(sent['name'], 'game.configure');
        expect(sent['data'], {
          'game': 'beamng',
          'directory': '/games/BeamNG',
          'command': '/opt/run.sh',
          'user_path': '',
        });
      });
    });

    test(
        'installGame, uninstallGame and cancelInstall send what the engine expects',
        () async {
      final (client, transport) = await connected();
      await answer(transport, () => client.installGame('gearblocks'), {},
          (sent) {
        expect(sent['name'], 'game.install');
        expect(sent['data'], {'game': 'gearblocks'});
      });
      await answer(transport, () => client.uninstallGame('gearblocks'), {},
          (sent) {
        expect(sent['name'], 'game.uninstall');
        expect(sent['data'], {'game': 'gearblocks'});
      });
      await answer(
          transport,
          () => client.uninstallGame('gearblocks', removeLoader: true),
          {}, (sent) {
        expect(sent['data'], {'game': 'gearblocks', 'remove_loader': true});
      });
      await answer(transport, () => client.cancelInstall('gearblocks'), {},
          (sent) {
        expect(sent['name'], 'game.install_cancel');
        expect(sent['data'], {'game': 'gearblocks'});
      });
    });

    test('createSession and joinSession send what the engine expects',
        () async {
      final (client, transport) = await connected();
      await answer(
          transport,
          () =>
              client.createSession('gearblocks', maxPlayers: 4, password: 'pw'),
          {}, (sent) {
        expect(sent['name'], 'session.create');
        expect(sent['data'], {
          'game': 'gearblocks',
          'max_players': 4,
          'password': 'pw',
        });
      });
      await answer(transport, () => client.createSession('gearblocks'), {},
          (sent) {
        expect(sent['data'], {'game': 'gearblocks'});
      });
      await answer(
          transport, () => client.joinSession('gearblocks', 'ABC234'), {},
          (sent) {
        expect(sent['name'], 'session.join');
        expect(sent['data'], {'game': 'gearblocks', 'code': 'ABC234'});
      });
      await answer(
          transport,
          () => client.joinSession('gearblocks', 'ABC234', password: 'pw'),
          {}, (sent) {
        expect(sent['data'],
            {'game': 'gearblocks', 'code': 'ABC234', 'password': 'pw'});
      });
    });

    test('leaveSession and sessionState', () async {
      final (client, transport) = await connected();
      await answer(transport, client.leaveSession, {}, (sent) {
        expect(sent['name'], 'session.leave');
      });
      final session = await answer(
        transport,
        client.sessionState,
        {
          'game': 'gearblocks',
          'state': 'in_room',
          'role': 'host',
          'code': 'ABC234'
        },
        (sent) => expect(sent['name'], 'session.state'),
      );
      expect(session.state, SessionState.inRoom);
      expect(session.code, 'ABC234');
    });

    test('stopGame', () async {
      final (client, transport) = await connected();
      await answer(transport, () => client.stopGame('beamng'), {}, (sent) {
        expect(sent['name'], 'game.stop');
        expect(sent['data'], {'game': 'beamng'});
      });
    });

    test('signIn and cancelSignIn', () async {
      final (client, transport) = await connected();
      await answer(transport, client.signIn, {}, (sent) {
        expect(sent['name'], 'account.sign_in');
      });
      await answer(transport, client.cancelSignIn, {}, (sent) {
        expect(sent['name'], 'account.cancel_sign_in');
      });
    });

    test('signIn reports why it cannot start', () async {
      final (client, transport) = await connected();
      final pending = client.signIn();
      await transport.waitForSent(2);
      transport.refuse(transport.sent[1]['id']! as int, 'sign_in_busy');
      await expectLater(
        pending,
        throwsA(
          isA<ControlException>().having((e) => e.code, 'code', 'sign_in_busy'),
        ),
      );
    });

    test('accountState and signOut', () async {
      final (client, transport) = await connected();
      final account = await answer(
        transport,
        client.accountState,
        {'signed_in': true, 'username': 'user'},
        (sent) => expect(sent['name'], 'account.state'),
      );
      expect(account.username, 'user');
      final after = await answer(
        transport,
        client.signOut,
        {'signed_in': false},
        (sent) => expect(sent['name'], 'account.sign_out'),
      );
      expect(after.signedIn, isFalse);
    });
  });
}
