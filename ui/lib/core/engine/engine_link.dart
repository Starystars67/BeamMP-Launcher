import 'package:launcher_control/launcher_control.dart';

/// What the UI needs from a running engine. [ControlClient] plus the process
/// around it implement this; tests use a fake.
abstract interface class EngineLink {
  /// Events in arrival order, buffered until listened to (single subscription).
  Stream<EngineEvent> get events;

  /// Completes when the connection to the engine ended.
  Future<void> get closed;

  /// What the engine said in the handshake (version, capabilities).
  HelloInfo get hello;

  /// The last lines the engine wrote to stdout and stderr.
  List<String> get output;

  Future<EngineSnapshot> engineState();
  Future<List<GameInfo>> listGames();
  Future<int> subscribeLogs({LogLevel level, int backlog});
  Future<void> launchGame(String game, {List<String> args});
  Future<void> stopGame(String game);
  Future<void> createSession(String game, {int? maxPlayers, String password});
  Future<void> joinSession(String game, String code, {String password});
  Future<void> leaveSession();
  Future<void> configureGame(
    String game, {
    String directory,
    String command,
    String userPath,
  });
  Future<void> installGame(String game);
  Future<void> uninstallGame(String game, {bool removeLoader});
  Future<void> cancelInstall(String game);
  Future<void> signIn();
  Future<void> cancelSignIn();
  Future<AccountInfo> signOut();
  Future<void> shutdown({bool force});

  /// Stops the engine without asking.
  void kill();
}

/// Starts (and owns) an engine.
abstract interface class EngineGateway {
  Future<EngineLink> start();
}

/// There is no engine executable to start.
final class EngineNotFoundException implements Exception {
  const EngineNotFoundException([this.searched = const []]);

  final List<String> searched;

  @override
  String toString() => 'EngineNotFoundException: ${searched.join(', ')}';
}
