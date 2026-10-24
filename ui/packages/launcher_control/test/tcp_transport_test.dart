import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:launcher_control/launcher_control.dart';
import 'package:test/test.dart';

void main() {
  late ServerSocket server;
  late Future<Socket> accepted;

  setUp(() async {
    server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    accepted = server.first;
  });

  tearDown(() => server.close());

  test('lines travel both ways', () async {
    final transport = await TcpLineTransport.connect(server.port);
    final peer = await accepted;
    final fromClient = utf8.decoder.bind(peer).transform(const LineSplitter());
    final received = fromClient.first;

    transport.send('{"hello":"engine"}');
    expect(await received, '{"hello":"engine"}');

    final incoming = transport.lines.first;
    peer.write('{"hello":"client"}\n');
    expect(await incoming, '{"hello":"client"}');

    await transport.close();
    await peer.close();
  });

  test('lines split over several packets are reassembled', () async {
    final transport = await TcpLineTransport.connect(server.port);
    final peer = await accepted;
    peer.setOption(SocketOption.tcpNoDelay, true);
    final lines = transport.lines.take(2).toList();

    peer.write('{"a":');
    await Future<void>.delayed(const Duration(milliseconds: 20));
    peer.write('1}\n{"b":2}\n');
    expect(await lines, ['{"a":1}', '{"b":2}']);

    await transport.close();
    await peer.close();
  });

  test('the stream ends when the peer closes', () async {
    final transport = await TcpLineTransport.connect(server.port);
    final peer = await accepted;
    final done = transport.lines.toList();
    await peer.close();
    expect(await done, isEmpty);
    await transport.close();
  });

  test('an oversized line is a framing error', () async {
    final transport = await TcpLineTransport.connect(server.port);
    final peer = await accepted;
    final done = transport.lines.toList();
    peer.write('x' * (maxLineLength + 10));
    await expectLater(done, throwsA(isA<FramingException>()));
    peer.destroy();
    await transport.close();
  });

  test('connecting to a port nobody listens on fails', () async {
    final port = server.port;
    await server.close();
    await accepted.then<void>((_) {}, onError: (Object _) {});
    await expectLater(
        TcpLineTransport.connect(port), throwsA(isA<SocketException>()));
  });

  test('a full handshake works over a real socket', () async {
    final transport = await TcpLineTransport.connect(server.port);
    final peer = await accepted;
    final requests =
        utf8.decoder.bind(peer).transform(const LineSplitter()).listen((line) {
      final request = jsonDecode(line) as Map<String, Object?>;
      peer.write('${jsonEncode({
            'v': 1,
            'id': request['id'],
            'type': 'response',
            'ok': true,
            'data': {
              'version': 1,
              'engine': 'fake',
              'capabilities': <String>[],
              'games': <String>[]
            },
          })}\n');
    });

    final client = await ControlClient.connect(transport, token: 'secret');
    expect(client.hello.engine, 'fake');

    await client.close();
    await requests.cancel();
    await peer.close();
  });
}
