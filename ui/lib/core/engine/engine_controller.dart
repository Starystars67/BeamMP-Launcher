import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:launcher_control/launcher_control.dart';

import '../settings/launch_arguments.dart';
import 'engine_link.dart';
import 'log_buffer.dart';

/// Id of the only game there is for now.
const String beamngGameId = 'beamng';

enum FailureKind { notFound, startFailed, lost, incompatible, fatal }

/// Why the last launch attempt did not start the game.
sealed class LaunchProblem {
  const LaunchProblem();
}

/// The engine refused or could not start the game; [message] is its reason.
final class LaunchRefused extends LaunchProblem {
  const LaunchRefused(this.message);

  final String message;
}

final class LaunchNoAnswer extends LaunchProblem {
  const LaunchNoAnswer();
}

/// The configured launch arguments cannot be used.
final class LaunchBadArguments extends LaunchProblem {
  const LaunchBadArguments(this.problem);

  final ArgumentsProblem problem;
}

/// Why the last request about the relay room was not accepted.
sealed class SessionProblem {
  const SessionProblem();
}

/// The engine refused; [message] is its reason.
final class SessionRefused extends SessionProblem {
  const SessionRefused(this.message);

  final String message;
}

final class SessionNoAnswer extends SessionProblem {
  const SessionNoAnswer();
}

/// Why new settings for a game could not be handed to the engine.
sealed class ConfigureProblem {
  const ConfigureProblem();
}

/// A game is starting or running (or being prepared): try again after it.
final class ConfigureBusy extends ConfigureProblem {
  const ConfigureBusy();
}

final class ConfigureRefused extends ConfigureProblem {
  const ConfigureRefused(this.message);

  final String message;
}

/// There is no engine to tell; the settings apply at the next start.
final class ConfigureNoEngine extends ConfigureProblem {
  const ConfigureNoEngine();
}

/// Why an install or uninstall could not be handed to the engine.
sealed class InstallProblem {
  const InstallProblem();
}

/// A game starts or runs, or something is being installed already.
final class InstallBusy extends InstallProblem {
  const InstallBusy();
}

final class InstallRefused extends InstallProblem {
  const InstallRefused(this.message);

  final String message;
}

final class InstallNoEngine extends InstallProblem {
  const InstallNoEngine();
}

/// Where a sign-in through the browser stands.
sealed class SignInStatus {
  const SignInStatus();
}

final class SignInIdle extends SignInStatus {
  const SignInIdle();
}

/// The page to sign in on is open (or has to be opened from [url]).
final class SignInWaiting extends SignInStatus {
  const SignInWaiting(this.url);

  final String url;
}

final class SignInExchanging extends SignInStatus {
  const SignInExchanging();
}

/// [reason] is the engine's: `cancelled`, `timeout`, `denied`, `network`,
/// `server`, `rejected`, `expired` or `internal`.
final class SignInFailed extends SignInStatus {
  const SignInFailed(this.reason);

  final String reason;
}

sealed class EngineConnection {
  const EngineConnection();
}

/// The engine is being started or has not answered yet.
final class EngineConnecting extends EngineConnection {
  const EngineConnecting();
}

final class EngineOnline extends EngineConnection {
  const EngineOnline({
    required this.state,
    this.detail = '',
    this.games = const [],
    this.account = AccountInfo.signedOut,
  });

  final EngineState state;
  final String detail;
  final List<GameInfo> games;
  final AccountInfo account;

  GameInfo? get beamng {
    for (final game in games) {
      if (game.id == beamngGameId) {
        return game;
      }
    }
    return null;
  }

  /// The game that is starting or running (the engine runs one at a time).
  GameInfo? get activeGame {
    for (final game in games) {
      if (game.state == GameState.launching ||
          game.state == GameState.running) {
        return game;
      }
    }
    return null;
  }

  EngineOnline copyWith({
    EngineState? state,
    String? detail,
    List<GameInfo>? games,
    AccountInfo? account,
  }) => EngineOnline(
    state: state ?? this.state,
    detail: detail ?? this.detail,
    games: games ?? this.games,
    account: account ?? this.account,
  );
}

final class EngineFailed extends EngineConnection {
  const EngineFailed(this.kind, this.message, {this.output = const []});

  final FailureKind kind;
  final String message;

  /// What the engine last wrote, for the details panel.
  final List<String> output;
}

/// Owns the engine for the UI: starts it, follows its events and turns them
/// into state the screens can show.
class EngineController extends ChangeNotifier {
  EngineController(
    this._gateway, {
    LogBuffer? logs,
    this.shutdownTimeout = const Duration(seconds: 5),
    this.launchArguments = _noArguments,
  }) : logs = logs ?? LogBuffer();

  static String _noArguments(String game) => '';

  final EngineGateway _gateway;
  final Duration shutdownTimeout;

  /// The user's extra arguments for a game, as typed, read at every launch.
  final String Function(String game) launchArguments;
  final LogBuffer logs;

  EngineConnection _connection = const EngineConnecting();
  EngineLink? _link;
  StreamSubscription<EngineEvent>? _events;
  bool _starting = false;
  bool _disposed = false;
  bool _shutdownRequested = false;
  String? _fatalMessage;
  LaunchProblem? _launchProblem;
  String? _gameNote;
  String? _lastLaunchedGame;
  SignInStatus _signIn = const SignInIdle();
  SessionInfo _session = SessionInfo.none;
  SessionProblem? _sessionProblem;
  final Map<String, InstallProgressChanged> _installs = {};

  EngineConnection get connection => _connection;

  /// What an install or uninstall of [game] is doing, or how the last one
  /// failed or was cancelled (until the next one starts or it is dismissed).
  InstallProgressChanged? installProgress(String game) => _installs[game];

  /// Forgets how the last install of [game] ended.
  void dismissInstall(String game) {
    if (_installs.remove(game) != null) {
      notifyListeners();
    }
  }

  /// Why the last launch attempt failed, until the next attempt.
  LaunchProblem? get launchProblem => _launchProblem;

  SignInStatus get signIn => _signIn;

  /// Where the player stands in a relay room, as the engine last said.
  SessionInfo get session => _session;

  /// Why the last request about the session was refused, until the next one.
  SessionProblem? get sessionProblem => _sessionProblem;

  /// What the engine said in the handshake, once connected.
  HelloInfo? get engineInfo => _link?.hello;

  /// What the engine said when the game last ended, until the next launch.
  String? get gameNote => _gameNote;

  /// The game the launch problem and the note are about.
  String? get lastLaunchedGame => _lastLaunchedGame;

  bool get canLaunch {
    final connection = _connection;
    return connection is EngineOnline && connection.state == EngineState.ready;
  }

  bool get gameActive {
    final connection = _connection;
    return connection is EngineOnline && connection.state.gameActive;
  }

  /// Starts the engine, or starts it again after a failure.
  Future<void> start() async {
    if (_starting || _connection is EngineOnline) {
      return;
    }
    _starting = true;
    _shutdownRequested = false;
    _fatalMessage = null;
    _signIn = const SignInIdle();
    _session = SessionInfo.none;
    _sessionProblem = null;
    _installs.clear();
    _setConnection(const EngineConnecting());
    EngineLink? link;
    try {
      final started = await _gateway.start();
      link = started;
      _link = started;
      _events = started.events.listen(_onEvent);
      unawaited(started.closed.then((_) => _onClosed(started)));

      final snapshot = await started.engineState();
      _setConnection(
        EngineOnline(
          state: snapshot.state,
          detail: snapshot.detail,
          account: snapshot.status.account,
        ),
      );
      final games = await started.listGames();
      _update((online) => online.copyWith(games: games));
      await started.subscribeLogs(level: LogLevel.debug, backlog: 500);
    } on EngineNotFoundException catch (e) {
      _fail(FailureKind.notFound, output: e.searched);
    } on EngineStartException catch (e) {
      _fail(FailureKind.startFailed, message: e.message, output: e.output);
    } on HandshakeException catch (e) {
      _fail(
        e.incompatibleProtocol
            ? FailureKind.incompatible
            : FailureKind.startFailed,
        message: e.message,
      );
      link?.kill();
    } on Object catch (e) {
      if (_connection is! EngineFailed) {
        _fail(FailureKind.startFailed, message: '$e');
      }
      link?.kill();
    } finally {
      _starting = false;
    }
  }

  /// Starts a game with the arguments the user set for it.
  Future<void> launchGame([String game = beamngGameId]) async {
    final link = _link;
    if (link == null || !canLaunch) {
      return;
    }
    _lastLaunchedGame = game;
    final parsed = parseLaunchArguments(launchArguments(game));
    if (parsed.problem != null) {
      _launchProblem = LaunchBadArguments(parsed.problem!);
      notifyListeners();
      return;
    }
    _launchProblem = null;
    notifyListeners();
    try {
      await link.launchGame(game, args: parsed.args);
    } on ControlException catch (e) {
      _launchProblem = LaunchRefused(e.message.isEmpty ? e.code : e.message);
      notifyListeners();
    } on ControlClosedException {
      // The closed handler reports it.
    } on ControlTimeoutException {
      _launchProblem = const LaunchNoAnswer();
      notifyListeners();
    }
  }

  /// Tells the engine where [game] is and how it starts; it prepares the game
  /// again. Returns why that was not possible, nothing when it was accepted.
  Future<ConfigureProblem?> configureGame(
    String game, {
    String directory = '',
    String command = '',
    String userPath = '',
  }) async {
    final link = _link;
    if (link == null) {
      return const ConfigureNoEngine();
    }
    try {
      await link.configureGame(
        game,
        directory: directory,
        command: command,
        userPath: userPath,
      );
      return null;
    } on ControlException catch (e) {
      return e.code == 'game_not_ready'
          ? const ConfigureBusy()
          : ConfigureRefused(e.message.isEmpty ? e.code : e.message);
    } on ControlClosedException {
      return const ConfigureNoEngine();
    } on ControlTimeoutException {
      return const ConfigureNoEngine();
    }
  }

  /// Sets up the multiplayer support of [game] (the engine downloads and
  /// installs what it needs). Progress arrives through [installProgress];
  /// returns why the engine could not start it, nothing when it did.
  Future<InstallProblem?> installGame(String game) =>
      _installRequest(game, (link) => link.installGame(game));

  /// Takes the multiplayer support of [game] out again.
  Future<InstallProblem?> uninstallGame(
    String game, {
    bool removeLoader = false,
  }) => _installRequest(
    game,
    (link) => link.uninstallGame(game, removeLoader: removeLoader),
  );

  /// Stops a running install of [game] at the next chance.
  Future<void> cancelInstall(String game) async {
    try {
      await _link?.cancelInstall(game);
    } on ControlException {
      // nothing is running
    } on ControlClosedException {
      // the closed handler reports it
    } on ControlTimeoutException {
      // the engine is slow; the install ends on its own
    }
  }

  Future<InstallProblem?> _installRequest(
    String game,
    Future<void> Function(EngineLink link) ask,
  ) async {
    final link = _link;
    if (link == null) {
      return const InstallNoEngine();
    }
    try {
      await ask(link);
      // shown at once; the engine's own events take over
      _installs[game] = InstallProgressChanged(
        game: game,
        phase: InstallPhase.running,
        step: 'check',
        message: '',
      );
      notifyListeners();
      return null;
    } on ControlException catch (e) {
      return switch (e.code) {
        'game_not_ready' ||
        'install_busy' ||
        'game_busy' => const InstallBusy(),
        _ => InstallRefused(e.message.isEmpty ? e.code : e.message),
      };
    } on ControlClosedException {
      return const InstallNoEngine();
    } on ControlTimeoutException {
      return const InstallNoEngine();
    }
  }

  Future<void> _refreshGames() async {
    final link = _link;
    if (link == null) {
      return;
    }
    try {
      final games = await link.listGames();
      if (link == _link) {
        _update((online) => online.copyWith(games: games));
      }
    } on Object {
      // the next state event or restart brings the list up to date
    }
  }

  /// Creates a relay room for [game] (the others join with its code). Progress
  /// and the result arrive as [session] changes; a refusal is [sessionProblem].
  Future<void> createRoom(
    String game, {
    int? maxPlayers,
    String password = '',
  }) => _sessionRequest(
    (link) =>
        link.createSession(game, maxPlayers: maxPlayers, password: password),
  );

  Future<void> joinRoom(String game, String code, {String password = ''}) =>
      _sessionRequest(
        (link) => link.joinSession(game, code, password: password),
      );

  Future<void> leaveRoom() => _sessionRequest((link) => link.leaveSession());

  Future<void> _sessionRequest(
    Future<void> Function(EngineLink link) ask,
  ) async {
    final link = _link;
    if (link == null) {
      return;
    }
    _sessionProblem = null;
    notifyListeners();
    try {
      await ask(link);
    } on ControlException catch (e) {
      _sessionProblem = SessionRefused(e.message.isEmpty ? e.code : e.message);
      notifyListeners();
    } on ControlClosedException {
      // The closed handler reports it.
    } on ControlTimeoutException {
      _sessionProblem = const SessionNoAnswer();
      notifyListeners();
    }
  }

  /// Stops [game], by default the one that is running.
  Future<void> stopGame([String? game]) async {
    final link = _link;
    if (link == null || !gameActive) {
      return;
    }
    final connection = _connection;
    final target =
        game ?? (connection is EngineOnline ? connection.activeGame?.id : null);
    try {
      await link.stopGame(target ?? beamngGameId);
    } on ControlException {
      // Already gone: the state events say so.
    } on ControlClosedException {
      // The closed handler reports it.
    }
  }

  /// Starts signing in through the browser; progress arrives as events.
  Future<void> startSignIn() async {
    final link = _link;
    if (link == null) {
      return;
    }
    try {
      await link.signIn();
    } on ControlException catch (e) {
      // Busy or already signed in: the events and the account say where it stands.
      if (e.code == 'sign_in_unavailable') {
        _setSignIn(const SignInFailed('internal'));
      }
    } on ControlClosedException {
      // The closed handler reports it.
    } on ControlTimeoutException {
      _setSignIn(const SignInFailed('internal'));
    }
  }

  Future<void> cancelSignIn() async {
    final link = _link;
    if (link == null) {
      return;
    }
    try {
      await link.cancelSignIn();
    } on ControlException {
      // Nothing to cancel.
    } on ControlClosedException {
      // The closed handler reports it.
    }
  }

  /// Hides a failed sign-in's message.
  void dismissSignIn() {
    if (_signIn is SignInFailed) {
      _setSignIn(const SignInIdle());
    }
  }

  Future<void> signOut() async {
    final link = _link;
    if (link == null) {
      return;
    }
    try {
      final account = await link.signOut();
      _update((online) => online.copyWith(account: account));
    } on ControlException {
      // Nothing to sign out of.
    } on ControlClosedException {
      // The closed handler reports it.
    }
  }

  /// Asks the engine to exit and waits for it. With [force] a running game is
  /// left running without the launcher.
  Future<void> shutdown({bool force = false}) async {
    final link = _link;
    if (link == null) {
      return;
    }
    _shutdownRequested = true;
    try {
      await link.shutdown(force: force);
      await link.closed.timeout(shutdownTimeout);
    } on Object {
      link.kill();
    }
  }

  void _onEvent(EngineEvent event) {
    switch (event) {
      case LogReceived(:final entry):
        logs.add(entry);
      case EngineStateChanged(:final state, :final detail):
        _update((online) => online.copyWith(state: state, detail: detail));
      case GameStateChanged():
        _onGameState(event);
      case AccountChanged(:final account):
        _update((online) => online.copyWith(account: account));
        if (account.signedIn && _signIn is! SignInIdle) {
          _setSignIn(const SignInIdle());
        }
      case SignInProgress(:final state, :final url, :final reason):
        _setSignIn(switch (state) {
          SignInState.waitingForBrowser => SignInWaiting(url ?? ''),
          SignInState.exchanging => const SignInExchanging(),
          SignInState.failed => SignInFailed(reason ?? 'internal'),
          SignInState.idle || SignInState.unknown => const SignInIdle(),
        });
      case InstallProgressChanged():
        if (event.phase == InstallPhase.done) {
          _installs.remove(event.game);
        } else {
          _installs[event.game] = event;
        }
        notifyListeners();
        if (event.phase.ended) {
          unawaited(_refreshGames());
        }
      case SessionChanged(:final session):
        _session = session;
        if (session.active || session.errorCode != null) {
          _sessionProblem = null;
        }
        notifyListeners();
      case FatalReported(:final message):
        _fatalMessage = message;
      case UnknownEvent():
        break;
    }
  }

  void _onGameState(GameStateChanged event) {
    if (event.state == GameState.launching) {
      _launchProblem = null;
      _gameNote = null;
      _lastLaunchedGame = event.game;
    } else if (event.state == GameState.exited) {
      _gameNote = event.detail;
      _lastLaunchedGame = event.game;
    }
    _update((online) {
      final games = [
        for (final game in online.games)
          if (game.id == event.game)
            GameInfo(
              id: game.id,
              name: game.name,
              state: event.state,
              pid: event.pid,
              detail: event.detail,
              steamAppId: game.steamAppId,
            )
          else
            game,
      ];
      return online.copyWith(games: games);
    });
  }

  void _onClosed(EngineLink link) {
    if (link != _link) {
      return;
    }
    _link = null;
    if (_shutdownRequested) {
      return;
    }
    final fatal = _fatalMessage;
    _fail(
      fatal == null ? FailureKind.lost : FailureKind.fatal,
      message: fatal ?? '',
      output: link.output,
    );
  }

  void _setSignIn(SignInStatus status) {
    if (_disposed) {
      return;
    }
    _signIn = status;
    notifyListeners();
  }

  void _fail(
    FailureKind kind, {
    String message = '',
    List<String> output = const [],
  }) {
    _setConnection(EngineFailed(kind, message, output: output));
  }

  void _update(EngineOnline Function(EngineOnline) change) {
    final connection = _connection;
    if (connection is EngineOnline) {
      _setConnection(change(connection));
    }
  }

  void _setConnection(EngineConnection connection) {
    if (_disposed) {
      return;
    }
    _connection = connection;
    notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    unawaited(_events?.cancel());
    _link?.kill();
    super.dispose();
  }
}
