import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'client.dart';
import 'exceptions.dart';
import 'transport.dart';

/// A launcher engine started (and owned) by this process.
///
/// The engine is started in controlled mode (`--control-port 0`), told a
/// random token through its environment and announces the port it picked on
/// stdout.
final class EngineProcess {
  EngineProcess._(this._process, this.port, this.token, this._output);

  /// Starts [executable] and waits for it to announce its control port.
  ///
  /// [arguments] go before the control flags (a wrapper command's own
  /// arguments, for instance), [engineArguments] after them.
  static Future<EngineProcess> start({
    required String executable,
    List<String> arguments = const [],
    List<String> engineArguments = const [],
    Map<String, String> environment = const {},
    Duration startupTimeout = const Duration(seconds: 30),
    int outputLines = 200,
  }) async {
    final token = generateToken();
    final Process process;
    try {
      process = await Process.start(
        executable,
        [...arguments, '--control-port', '0', ...engineArguments],
        environment: {...environment, tokenVariable: token},
      );
    } on ProcessException catch (e) {
      throw EngineStartException('Cannot start the engine: ${e.message}');
    }

    final output = _Output(outputLines);
    final announced = Completer<int>();

    final stdoutDone = process.stdout
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen((line) {
      output.add(line);
      final port = _parseAnnouncement(line);
      if (port != null && !announced.isCompleted) {
        announced.complete(port);
      }
    }).asFuture<void>();
    final stderrDone = process.stderr
        .transform(utf8.decoder)
        .transform(const LineSplitter())
        .listen(output.add)
        .asFuture<void>();

    unawaited(
      process.exitCode.then((code) async {
        await Future.wait([stdoutDone, stderrDone]);
        if (!announced.isCompleted) {
          announced.completeError(
            EngineStartException(
              'The engine exited with code $code before it was ready',
              exitCode: code,
              output: output.lines,
            ),
          );
        }
      }),
    );

    try {
      final port = await announced.future.timeout(startupTimeout);
      return EngineProcess._(process, port, token, output);
    } on TimeoutException {
      process.kill();
      throw EngineStartException(
        'The engine did not announce its control port in time',
        output: output.lines,
      );
    }
  }

  /// Environment variable the engine reads its token from.
  static const String tokenVariable = 'BEAMMP_CONTROL_TOKEN';

  /// 32 random bytes, hex encoded.
  static String generateToken([Random? random]) {
    final source = random ?? Random.secure();
    return List<String>.generate(
      32,
      (_) => source.nextInt(256).toRadixString(16).padLeft(2, '0'),
    ).join();
  }

  static int? _parseAnnouncement(String line) {
    try {
      final message = jsonDecode(line);
      if (message is Map<String, Object?>) {
        final port = message['control_port'];
        if (port is int && port > 0 && port <= 65535) {
          return port;
        }
      }
    } on FormatException {
      // Not the announcement.
    }
    return null;
  }

  final Process _process;
  final _Output _output;
  final int port;
  final String token;

  int get pid => _process.pid;

  /// Completes with the exit code when the engine ends.
  Future<int> get exitCode => _process.exitCode;

  /// The last lines the engine wrote to stdout and stderr.
  List<String> get output => _output.lines;

  /// Connects to the engine and performs the handshake.
  Future<ControlClient> connect({
    String clientName = 'launcher_control',
    Duration handshakeTimeout = const Duration(seconds: 5),
    Duration requestTimeout = const Duration(seconds: 10),
  }) async {
    final transport = await TcpLineTransport.connect(port);
    return ControlClient.connect(
      transport,
      token: token,
      clientName: clientName,
      handshakeTimeout: handshakeTimeout,
      requestTimeout: requestTimeout,
    );
  }

  /// Stops the engine process. A running game is a separate process and is left alone.
  bool kill() => _process.kill();
}

final class _Output {
  _Output(this._capacity);

  final int _capacity;
  final Queue<String> _lines = Queue<String>();

  void add(String line) {
    _lines.add(line);
    while (_lines.length > _capacity) {
      _lines.removeFirst();
    }
  }

  List<String> get lines => List.unmodifiable(_lines);
}
