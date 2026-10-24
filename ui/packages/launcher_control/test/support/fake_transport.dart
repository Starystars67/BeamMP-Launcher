import 'dart:async';
import 'dart:convert';

import 'package:launcher_control/launcher_control.dart';

/// In-memory [LineTransport]: the test plays the engine.
final class FakeTransport implements LineTransport {
  final StreamController<String> _incoming = StreamController<String>();
  final List<Map<String, Object?>> sent = [];
  final StreamController<void> _sentSignal = StreamController<void>.broadcast();
  bool closed = false;

  @override
  Stream<String> get lines => _incoming.stream;

  @override
  void send(String line) {
    if (closed) {
      throw StateError('closed');
    }
    sent.add(jsonDecode(line) as Map<String, Object?>);
    _sentSignal.add(null);
  }

  @override
  Future<void> close() async {
    closed = true;
    if (!_incoming.isClosed) {
      await _incoming.close();
    }
  }

  /// Waits until the client has sent [count] messages in total.
  Future<void> waitForSent(int count) async {
    while (sent.length < count) {
      await _sentSignal.stream.first;
    }
  }

  /// Engine -> client.
  void say(Map<String, Object?> message) => _incoming.add(jsonEncode(message));
  void sayRaw(String line) => _incoming.add(line);
  Future<void> hangUp() => _incoming.close();

  void respond(int id, [Map<String, Object?> data = const {}]) =>
      say({'v': 1, 'id': id, 'type': 'response', 'ok': true, 'data': data});

  void refuse(int id, String code, [String message = 'refused']) => say({
        'v': 1,
        'id': id,
        'type': 'response',
        'ok': false,
        'error': {'code': code, 'message': message},
      });

  void event(String name, [Map<String, Object?> data = const {}]) =>
      say({'v': 1, 'type': 'event', 'name': name, 'data': data});

  /// Answers the next `hello` like the real engine.
  Future<void> acceptHello({int version = 1}) async {
    await waitForSent(1);
    respond(sent.first['id']! as int, {
      'version': version,
      'engine': '9.9.9',
      'capabilities': ['logs', 'games', 'account'],
      'games': ['beamng'],
    });
  }
}
