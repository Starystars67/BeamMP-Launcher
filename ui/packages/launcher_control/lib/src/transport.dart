import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'framing.dart';

/// One line in, one line out: what the client needs from a connection.
abstract interface class LineTransport {
  /// Complete lines without the terminator. Ends when the peer closes;
  /// errors on malformed input.
  Stream<String> get lines;

  /// Sends one message; the terminator is added.
  void send(String line);

  Future<void> close();
}

/// [LineTransport] over a loopback TCP socket.
final class TcpLineTransport implements LineTransport {
  TcpLineTransport._(this._socket) : lines = frameLines(_socket);

  /// Connects to the engine's control port on 127.0.0.1.
  static Future<TcpLineTransport> connect(
    int port, {
    Duration timeout = const Duration(seconds: 5),
  }) async {
    // ignore: close_sinks, closed by TcpLineTransport.close
    final socket = await Socket.connect(
      InternetAddress.loopbackIPv4,
      port,
      timeout: timeout,
    );
    socket.setOption(SocketOption.tcpNoDelay, true);
    return TcpLineTransport._(socket);
  }

  final Socket _socket;

  @override
  final Stream<String> lines;

  @override
  void send(String line) {
    // ignore: close_sinks, closed by close()
    _socket.add(utf8.encode('$line\n'));
  }

  @override
  Future<void> close() async {
    try {
      await _socket.flush();
    } on Object {
      // The peer may already be gone.
    }
    await _socket.close();
    _socket.destroy();
  }
}
