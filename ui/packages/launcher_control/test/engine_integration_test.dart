@Tags(['integration'])
library;

// Runs against the real engine binary:
//   BEAMMP_ENGINE=/path/to/BeamMP-Launcher-Console dart test -t integration
// BEAMMP_ENGINE_SANDBOX=1 wraps it in sandbox-exec (macOS) so it cannot reach the network.
import 'dart:io';

import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

const _sandbox =
    '(version 1)(allow default)(deny network-outbound (remote ip "*:*"))'
    '(allow network-outbound (remote ip "localhost:*"))';

void main() {
  final engine = Platform.environment['BEAMMP_ENGINE'];
  final skip = engine == null ? 'Set BEAMMP_ENGINE to run' : null;

  test('drives the real engine through a launcher session', () async {
    final sandboxed = Platform.environment['BEAMMP_ENGINE_SANDBOX'] == '1';
    final process = await EngineProcess.start(
      executable: sandboxed ? 'sandbox-exec' : engine!,
      arguments: sandboxed ? ['-p', _sandbox, engine!] : const [],
      engineArguments: [
        '--no-update',
        '--no-download',
        '--no-launch',
        '-p',
        Platform.environment['BEAMMP_ENGINE_GAME_PORT'] ?? '4466',
      ],
    );
    addTearDown(process.kill);

    final client = await process.connect(clientName: 'integration');
    expect(client.hello.version, 1);
    expect(client.hello.games, ['beamng']);

    final events = <EngineEvent>[];
    final listening = client.events.listen(events.add);
    addTearDown(listening.cancel);

    var snapshot = await client.engineState();
    for (var i = 0; snapshot.state != EngineState.ready && i < 300; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 100));
      snapshot = await client.engineState();
    }
    expect(snapshot.state, EngineState.ready);
    expect(snapshot.status.account.signedIn, isFalse);

    final sent = await client.subscribeLogs(backlog: 20);
    expect(sent, greaterThan(0));
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(events.whereType<LogReceived>(), hasLength(sent));
    expect(
        events.whereType<LogReceived>().every((e) => e.entry.time.year >= 2024),
        isTrue);

    final games = await client.listGames();
    expect(games.single.id, 'beamng');
    expect(games.single.state, GameState.ready);

    await expectLater(
      client.launchGame('beamng'),
      throwsA(isA<ControlException>()
          .having((e) => e.code, 'code', 'launch_failed')),
    );
    await expectLater(
      client.stopGame('beamng'),
      throwsA(isA<ControlException>()
          .having((e) => e.code, 'code', 'game_not_running')),
    );
    await expectLater(
      client.launchGame('other'),
      throwsA(isA<ControlException>()
          .having((e) => e.code, 'code', 'unknown_game')),
    );

    await client.shutdown();
    expect(await process.exitCode.timeout(const Duration(seconds: 10)), 0);
    await client.closed;
  }, skip: skip);
}
