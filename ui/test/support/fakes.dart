import 'dart:async';

import 'package:beammp_launcher/core/engine/engine_link.dart';
import 'package:launcher_control/launcher_control.dart';

/// An engine the test controls.
class FakeEngineLink implements EngineLink {
  final StreamController<EngineEvent> _events = StreamController<EngineEvent>();
  final Completer<void> _closed = Completer<void>();

  EngineSnapshot snapshot = const EngineSnapshot(state: EngineState.ready);
  List<GameInfo> games = const [
    GameInfo(id: 'beamng', name: 'BeamNG.drive', state: GameState.ready),
  ];
  Object? launchError;
  Object? stopError;
  Object? listGamesError;
  Object? signInError;
  bool shutdownHangs = false;

  final List<String> calls = [];
  final List<List<String>> launchArgs = [];
  final List<({LogLevel level, int backlog})> logSubscriptions = [];
  bool killed = false;

  @override
  HelloInfo hello = const HelloInfo(
    version: 1,
    engine: '9.9.9',
    capabilities: ['logs', 'games', 'account'],
    games: ['beamng'],
  );

  @override
  List<String> output = const [];

  @override
  Stream<EngineEvent> get events => _events.stream;

  @override
  Future<void> get closed => _closed.future;

  void emit(EngineEvent event) => _events.add(event);

  void close() {
    if (!_closed.isCompleted) {
      _closed.complete();
    }
    unawaited(_events.close());
  }

  @override
  Future<EngineSnapshot> engineState() async {
    calls.add('engineState');
    return snapshot;
  }

  @override
  Future<List<GameInfo>> listGames() async {
    calls.add('listGames');
    if (listGamesError != null) {
      throw listGamesError!;
    }
    return games;
  }

  @override
  Future<int> subscribeLogs({
    LogLevel level = LogLevel.info,
    int backlog = 200,
  }) async {
    calls.add('subscribeLogs');
    logSubscriptions.add((level: level, backlog: backlog));
    return 0;
  }

  @override
  Future<void> launchGame(String game, {List<String> args = const []}) async {
    calls.add('launchGame:$game');
    launchArgs.add(args);
    if (launchError != null) {
      throw launchError!;
    }
  }

  final List<({String game, String directory, String command, String userPath})>
  configured = [];
  Object? configureError;

  @override
  Future<void> configureGame(
    String game, {
    String directory = '',
    String command = '',
    String userPath = '',
  }) async {
    calls.add('configureGame:$game');
    configured.add((
      game: game,
      directory: directory,
      command: command,
      userPath: userPath,
    ));
    if (configureError != null) {
      throw configureError!;
    }
  }

  Object? installError;
  final List<String> installCalls = [];

  @override
  Future<void> installGame(String game) async {
    installCalls.add('install:$game');
    if (installError != null) {
      throw installError!;
    }
  }

  @override
  Future<void> uninstallGame(String game, {bool removeLoader = false}) async {
    installCalls.add('uninstall:$game:$removeLoader');
    if (installError != null) {
      throw installError!;
    }
  }

  @override
  Future<void> cancelInstall(String game) async {
    installCalls.add('cancel:$game');
  }

  Object? sessionError;
  final List<String> sessionCalls = [];

  @override
  Future<void> createSession(
    String game, {
    int? maxPlayers,
    String password = '',
  }) async {
    sessionCalls.add('create:$game:${maxPlayers ?? ''}:$password');
    if (sessionError != null) {
      throw sessionError!;
    }
  }

  @override
  Future<void> joinSession(
    String game,
    String code, {
    String password = '',
  }) async {
    sessionCalls.add('join:$game:$code:$password');
    if (sessionError != null) {
      throw sessionError!;
    }
  }

  @override
  Future<void> leaveSession() async {
    sessionCalls.add('leave');
  }

  @override
  Future<void> stopGame(String game) async {
    calls.add('stopGame:$game');
    if (stopError != null) {
      throw stopError!;
    }
  }

  @override
  Future<void> signIn() async {
    calls.add('signIn');
    if (signInError != null) {
      throw signInError!;
    }
  }

  @override
  Future<void> cancelSignIn() async {
    calls.add('cancelSignIn');
  }

  @override
  Future<AccountInfo> signOut() async {
    calls.add('signOut');
    return AccountInfo.signedOut;
  }

  @override
  Future<void> shutdown({bool force = false}) async {
    calls.add('shutdown:$force');
    if (!shutdownHangs) {
      close();
    }
  }

  @override
  void kill() {
    killed = true;
    close();
  }
}

class FakeGateway implements EngineGateway {
  FakeGateway([FakeEngineLink? link]) : link = link ?? FakeEngineLink();

  FakeEngineLink link;
  Object? failure;
  int starts = 0;
  Completer<void>? gate;

  @override
  Future<EngineLink> start() async {
    starts++;
    await gate?.future;
    if (failure != null) {
      throw failure!;
    }
    return link;
  }
}

LogEntry log(String message, {LogLevel level = LogLevel.info}) =>
    LogEntry(time: DateTime.utc(2024, 1, 1), level: level, message: message);

/// Lets queued microtasks and stream deliveries run.
Future<void> settle() => Future<void>.delayed(const Duration(milliseconds: 5));
