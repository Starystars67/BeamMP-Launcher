// A stand-in for the launcher engine: announces a port and speaks just enough
// of the protocol for the process tests.
import 'dart:convert';
import 'dart:io';

Future<void> main(List<String> args) async {
  final mode = args
      .firstWhere((a) => a.startsWith('--mode='), orElse: () => '--mode=normal')
      .substring(7);
  final token = Platform.environment['BEAMMP_CONTROL_TOKEN'];

  if (mode == 'exit-early') {
    stderr.writeln('engine failed: no game found');
    exit(3);
  }
  if (mode == 'silent') {
    await Future<void>.delayed(const Duration(minutes: 5));
    return;
  }
  if (mode == 'echo-env') {
    stdout.writeln(jsonEncode({'token_length': token?.length, 'args': args}));
  }

  final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
  if (mode == 'noisy') {
    stdout.writeln('some banner line');
    stderr.writeln('a warning');
  }
  stdout.writeln(jsonEncode({'control_port': server.port}));

  await for (final socket in server) {
    final lines = utf8.decoder.bind(socket).transform(const LineSplitter());
    var authenticated = false;
    await for (final line in lines) {
      final request = jsonDecode(line) as Map<String, Object?>;
      final data = request['data'] as Map<String, Object?>? ?? {};
      if (!authenticated) {
        if (request['name'] != 'hello' || data['token'] != token) {
          socket.destroy();
          break;
        }
        authenticated = true;
      }
      Map<String, Object?> reply;
      switch (request['name']) {
        case 'hello':
          reply = {
            'version': 1,
            'engine': 'fake',
            'capabilities': <String>[],
            'games': ['beamng']
          };
        case 'engine.state':
          reply = {
            'state': 'ready',
            'detail': '',
            'status': <String, Object?>{}
          };
        case 'engine.shutdown':
          socket.write('${jsonEncode({
                'v': 1,
                'id': request['id'],
                'type': 'response',
                'ok': true,
                'data': <String, Object?>{}
              })}\n');
          await socket.flush();
          exit(0);
        default:
          socket.write('${jsonEncode({
                'v': 1,
                'id': request['id'],
                'type': 'response',
                'ok': false,
                'error': {'code': 'unknown_request', 'message': 'no'},
              })}\n');
          continue;
      }
      socket.write('${jsonEncode({
            'v': 1,
            'id': request['id'],
            'type': 'response',
            'ok': true,
            'data': reply
          })}\n');
    }
  }
}
