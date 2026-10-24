import 'package:launcher_control/launcher_control.dart';

import 'engine_link.dart';
import 'engine_locator.dart';

/// Starts the real engine process and talks to it over the control channel.
final class ProcessEngineGateway implements EngineGateway {
  ProcessEngineGateway({
    required this.locate,
    this.extraArguments = _none,
    this.clientName = 'beammp-ui/0.1.0',
  });

  static List<String> _none() => const [];

  final ({EngineCommand? command, List<String> searched}) Function() locate;

  /// More engine arguments, asked for at every start (the saved game settings).
  final List<String> Function() extraArguments;
  final String clientName;

  @override
  Future<EngineLink> start() async {
    final located = locate();
    final command = located.command;
    if (command == null) {
      throw EngineNotFoundException(located.searched);
    }
    final process = await EngineProcess.start(
      executable: command.executable,
      engineArguments: [...command.engineArguments, ...extraArguments()],
    );
    try {
      return _ProcessEngineLink(
        process,
        await process.connect(clientName: clientName),
      );
    } on Object {
      process.kill();
      rethrow;
    }
  }
}

final class _ProcessEngineLink implements EngineLink {
  _ProcessEngineLink(this._process, this._client);

  final EngineProcess _process;
  final ControlClient _client;

  @override
  Stream<EngineEvent> get events => _client.events;

  @override
  Future<void> get closed => _client.closed;

  @override
  HelloInfo get hello => _client.hello;

  @override
  List<String> get output => _process.output;

  @override
  Future<EngineSnapshot> engineState() => _client.engineState();

  @override
  Future<List<GameInfo>> listGames() => _client.listGames();

  @override
  Future<int> subscribeLogs({
    LogLevel level = LogLevel.info,
    int backlog = 200,
  }) => _client.subscribeLogs(level: level, backlog: backlog);

  @override
  Future<void> launchGame(String game, {List<String> args = const []}) =>
      _client.launchGame(game, args: args);

  @override
  Future<void> stopGame(String game) => _client.stopGame(game);

  @override
  Future<void> createSession(
    String game, {
    int? maxPlayers,
    String password = '',
  }) => _client.createSession(game, maxPlayers: maxPlayers, password: password);

  @override
  Future<void> joinSession(String game, String code, {String password = ''}) =>
      _client.joinSession(game, code, password: password);

  @override
  Future<void> leaveSession() => _client.leaveSession();

  @override
  Future<void> configureGame(
    String game, {
    String directory = '',
    String command = '',
    String userPath = '',
  }) => _client.configureGame(
    game,
    directory: directory,
    command: command,
    userPath: userPath,
  );

  @override
  Future<void> installGame(String game) => _client.installGame(game);

  @override
  Future<void> uninstallGame(String game, {bool removeLoader = false}) =>
      _client.uninstallGame(game, removeLoader: removeLoader);

  @override
  Future<void> cancelInstall(String game) => _client.cancelInstall(game);

  @override
  Future<void> signIn() => _client.signIn();

  @override
  Future<void> cancelSignIn() => _client.cancelSignIn();

  @override
  Future<AccountInfo> signOut() => _client.signOut();

  @override
  Future<void> shutdown({bool force = false}) => _client.shutdown(force: force);

  @override
  void kill() {
    _process.kill();
    _client.close().ignore();
  }
}
