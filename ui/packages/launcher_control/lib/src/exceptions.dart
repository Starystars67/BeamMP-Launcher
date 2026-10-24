/// The engine answered a request with an error.
final class ControlException implements Exception {
  const ControlException(this.code, this.message);

  final String code;
  final String message;

  @override
  String toString() => 'ControlException($code): $message';
}

/// The connection ended before an answer arrived, or while sending.
final class ControlClosedException implements Exception {
  const ControlClosedException([this.message = 'The engine connection closed']);

  final String message;

  @override
  String toString() => 'ControlClosedException: $message';
}

final class ControlTimeoutException implements Exception {
  const ControlTimeoutException(this.request);

  final String request;

  @override
  String toString() => 'ControlTimeoutException: no answer to $request';
}

/// The handshake did not succeed: wrong token, incompatible version, or no answer.
final class HandshakeException implements Exception {
  const HandshakeException(this.message, {this.incompatibleProtocol = false});

  final String message;

  /// The engine speaks no protocol version this client knows: the launcher
  /// has to be updated.
  final bool incompatibleProtocol;

  @override
  String toString() => 'HandshakeException: $message';
}

/// The peer sent something that cannot be a protocol line.
final class FramingException implements Exception {
  const FramingException(this.message);

  final String message;

  @override
  String toString() => 'FramingException: $message';
}

/// The engine process could not be started or did not announce its port.
final class EngineStartException implements Exception {
  const EngineStartException(this.message,
      {this.exitCode, this.output = const []});

  final String message;
  final int? exitCode;

  /// The last lines the process wrote.
  final List<String> output;

  @override
  String toString() => 'EngineStartException: $message';
}
