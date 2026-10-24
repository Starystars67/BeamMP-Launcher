import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

Future<EngineProcess> startFake(
  String mode, {
  Duration startupTimeout = const Duration(seconds: 20),
}) =>
    EngineProcess.start(
      executable: Platform.resolvedExecutable,
      arguments: ['run', 'test/fixtures/fake_engine.dart'],
      engineArguments: ['--mode=$mode'],
      startupTimeout: startupTimeout,
    );

void main() {
  group('generateToken', () {
    test('is 64 hex characters', () {
      expect(EngineProcess.generateToken(), matches(RegExp(r'^[0-9a-f]{64}$')));
    });

    test('differs every time', () {
      final tokens = {
        for (var i = 0; i < 50; i++) EngineProcess.generateToken()
      };
      expect(tokens, hasLength(50));
    });

    test('is deterministic for a seeded generator (so tests can rely on it)',
        () {
      expect(EngineProcess.generateToken(Random(1)),
          EngineProcess.generateToken(Random(1)));
    });
  });

  group('start', () {
    test('announces the port, passes the token and accepts a connection',
        () async {
      final engine = await startFake('normal');
      addTearDown(engine.kill);
      expect(engine.port, greaterThan(0));
      expect(engine.token, hasLength(64));

      final client = await engine.connect(clientName: 'tests');
      expect(client.hello.engine, 'fake');
      expect((await client.engineState()).state, EngineState.ready);

      await client.shutdown();
      expect(await engine.exitCode, 0);
      await client.closed;
    });

    test('hands the control flags to the engine after the wrapper arguments',
        () async {
      final engine = await startFake('echo-env');
      addTearDown(engine.kill);
      final announcement =
          engine.output.firstWhere((line) => line.contains('token_length'));
      final info = jsonDecode(announcement) as Map<String, Object?>;
      expect(info['token_length'], 64);
      expect(info['args'], ['--control-port', '0', '--mode=echo-env']);
    });

    test('a second connection with the wrong token is refused', () async {
      final engine = await startFake('normal');
      addTearDown(engine.kill);
      final transport = await TcpLineTransport.connect(engine.port);
      await expectLater(
          ControlClient.connect(transport, token: 'not-the-token'),
          throwsA(isA<HandshakeException>()));
    });

    test('keeps the output the engine wrote', () async {
      final engine = await startFake('noisy');
      addTearDown(engine.kill);
      final client = await engine.connect();
      await client.close();
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(engine.output, containsAll(['some banner line', 'a warning']));
    });

    test('an engine that exits early fails with its exit code and output',
        () async {
      await expectLater(
        startFake('exit-early'),
        throwsA(
          isA<EngineStartException>()
              .having((e) => e.exitCode, 'exitCode', 3)
              .having((e) => e.output, 'output',
                  contains('engine failed: no game found')),
        ),
      );
    });

    test('an engine that never announces a port is stopped after the timeout',
        () async {
      await expectLater(
        startFake('silent', startupTimeout: const Duration(milliseconds: 1500)),
        throwsA(isA<EngineStartException>()
            .having((e) => e.exitCode, 'exitCode', isNull)),
      );
    });

    test('a missing executable is reported, not thrown as a ProcessException',
        () async {
      await expectLater(
        EngineProcess.start(executable: '/definitely/not/here'),
        throwsA(isA<EngineStartException>()),
      );
    });
  });
}
