import 'dart:async';
import 'dart:convert';

import 'exceptions.dart';
import 'models.dart';
import 'transport.dart';

/// Protocol versions this client speaks.
const List<int> supportedVersions = [1];

/// A connection to the engine, after a successful handshake.
///
/// Requests are matched to responses by id; everything else the engine sends
/// is decoded into [events]. When the connection ends, pending requests fail
/// with [ControlClosedException] and [events] completes.
final class ControlClient {
  ControlClient._(this._transport, this._requestTimeout) {
    _subscription = _transport.lines.listen(
      _onLine,
      onError: (Object _) => _shutdown(),
      onDone: _shutdown,
      cancelOnError: true,
    );
  }

  /// Performs the handshake. The transport is closed again if it fails.
  ///
  /// The events the engine sends right after the handshake (current engine,
  /// account and game state) are kept in [events] until somebody listens.
  static Future<ControlClient> connect(
    LineTransport transport, {
    required String token,
    String clientName = 'launcher_control',
    Duration handshakeTimeout = const Duration(seconds: 5),
    Duration requestTimeout = const Duration(seconds: 10),
  }) async {
    final client = ControlClient._(transport, requestTimeout);
    try {
      final data = await client.request(
        'hello',
        data: {
          'token': token,
          'client': clientName,
          'versions': supportedVersions,
        },
        timeout: handshakeTimeout,
      );
      final hello = HelloInfo.fromJson(data);
      if (!supportedVersions.contains(hello.version)) {
        throw HandshakeException(
          'The engine answered with protocol version ${hello.version}',
          incompatibleProtocol: true,
        );
      }
      client._hello = hello;
      return client;
    } on ControlException catch (e) {
      await client.close();
      throw HandshakeException(
        e.message,
        incompatibleProtocol: e.code == 'incompatible_protocol',
      );
    } on ControlClosedException {
      await client.close();
      throw const HandshakeException(
        'The engine closed the connection (wrong token?)',
      );
    } on ControlTimeoutException {
      await client.close();
      throw const HandshakeException('The engine did not answer the handshake');
    } on Object {
      await client.close();
      rethrow;
    }
  }

  final LineTransport _transport;
  final Duration _requestTimeout;
  late final StreamSubscription<String> _subscription;
  late final HelloInfo _hello;
  int _nextId = 1;
  final Map<int, Completer<Json>> _pending = {};
  final StreamController<EngineEvent> _events = StreamController<EngineEvent>();
  final Completer<void> _closed = Completer<void>();

  HelloInfo get hello => _hello;

  /// Engine events in the order they arrived. Single subscription: events are
  /// buffered until it is listened to.
  Stream<EngineEvent> get events => _events.stream;

  /// Completes when the connection has ended, for whatever reason.
  Future<void> get closed => _closed.future;

  bool get isClosed => _closed.isCompleted;

  /// Sends a request and returns the response's `data`.
  ///
  /// Throws [ControlException] when the engine refuses,
  /// [ControlClosedException] when the connection is gone and
  /// [ControlTimeoutException] when no answer arrives in time.
  Future<Json> request(
    String name, {
    Json data = const {},
    Duration? timeout,
  }) {
    if (isClosed) {
      return Future<Json>.error(const ControlClosedException());
    }
    final id = _nextId++;
    final completer = Completer<Json>();
    _pending[id] = completer;
    try {
      _transport.send(
        jsonEncode({
          'v': 1,
          'id': id,
          'type': 'request',
          'name': name,
          'data': data,
        }),
      );
    } on Object {
      _pending.remove(id);
      return Future<Json>.error(const ControlClosedException());
    }
    return completer.future.timeout(
      timeout ?? _requestTimeout,
      onTimeout: () {
        _pending.remove(id);
        throw ControlTimeoutException(name);
      },
    );
  }

  Future<EngineSnapshot> engineState() async =>
      EngineSnapshot.fromJson(await request('engine.state'));

  /// Asks the engine to exit. Refused with `game_running` while a game runs,
  /// unless [force].
  Future<void> shutdown({bool force = false}) async {
    await request('engine.shutdown', data: {'force': force});
  }

  /// Starts the log stream, preceded by up to [backlog] recent records.
  /// Returns how many backlog records were sent.
  Future<int> subscribeLogs({
    LogLevel level = LogLevel.info,
    int backlog = 200,
  }) async {
    final data = await request(
      'log.subscribe',
      data: {'level': level.wire, 'backlog': backlog},
    );
    final sent = data['sent'];
    return sent is int ? sent : 0;
  }

  Future<void> unsubscribeLogs() async {
    await request('log.unsubscribe');
  }

  Future<List<GameInfo>> listGames() async {
    final games = (await request('games.list'))['games'];
    return games is List<Object?>
        ? games.whereType<Json>().map(GameInfo.fromJson).toList()
        : const [];
  }

  Future<void> launchGame(String game, {List<String> args = const []}) async {
    await request('game.launch', data: {'game': game, 'args': args});
  }

  Future<void> stopGame(String game) async {
    await request('game.stop', data: {'game': game});
  }

  /// Tells the engine where a game is and how it starts (see `game.configure`
  /// in the protocol); the engine prepares the game again. An empty string
  /// means "work it out yourself", so always send the whole picture. Throws
  /// [ControlException] with `game_not_ready` while a game starts or runs.
  Future<void> configureGame(
    String game, {
    String directory = '',
    String command = '',
    String userPath = '',
  }) async {
    await request(
      'game.configure',
      data: {
        'game': game,
        'directory': directory,
        'command': command,
        'user_path': userPath,
      },
    );
  }

  /// Sets up the game's multiplayer support; progress and the end arrive as
  /// [InstallProgressChanged] events. Throws [ControlException] with
  /// `game_not_ready`, `install_busy`, `install_unsupported`, `unknown_game`.
  Future<void> installGame(String game) async {
    await request('game.install', data: {'game': game});
  }

  /// Takes it out again; the loader too when [removeLoader] (only if the
  /// launcher installed it).
  Future<void> uninstallGame(String game, {bool removeLoader = false}) async {
    await request(
      'game.uninstall',
      data: {'game': game, if (removeLoader) 'remove_loader': true},
    );
  }

  /// Stops a running install at the next chance (it ends as cancelled).
  Future<void> cancelInstall(String game) async {
    await request('game.install_cancel', data: {'game': game});
  }

  /// Creates a relay room for [game] and becomes its host; progress arrives as
  /// [SessionChanged] events. Throws [ControlException] with `session_busy`,
  /// `session_unavailable`, `session_unsupported`, `game_not_found`.
  Future<void> createSession(
    String game, {
    int? maxPlayers,
    String password = '',
  }) async {
    await request(
      'session.create',
      data: {
        'game': game,
        if (maxPlayers != null) 'max_players': maxPlayers,
        if (password.isNotEmpty) 'password': password,
      },
    );
  }

  /// Joins the room with this code (6 letters and digits).
  Future<void> joinSession(
    String game,
    String code, {
    String password = '',
  }) async {
    await request(
      'session.join',
      data: {
        'game': game,
        'code': code,
        if (password.isNotEmpty) 'password': password,
      },
    );
  }

  /// Leaves the room and the relay (nothing to do when not in one).
  Future<void> leaveSession() async {
    await request('session.leave');
  }

  Future<SessionInfo> sessionState() async =>
      SessionInfo.fromJson(await request('session.state'));

  /// Starts signing in through the system browser. Progress arrives as
  /// [SignInProgress] events, the result as an account event. Throws
  /// [ControlException] with `sign_in_busy`, `already_signed_in` or
  /// `sign_in_unavailable`.
  Future<void> signIn() async {
    await request('account.sign_in');
  }

  Future<void> cancelSignIn() async {
    await request('account.cancel_sign_in');
  }

  Future<AccountInfo> accountState() async =>
      AccountInfo.fromJson(await request('account.state'));

  Future<AccountInfo> signOut() async =>
      AccountInfo.fromJson(await request('account.sign_out'));

  /// Closes the connection. The engine treats this as the controller leaving.
  Future<void> close() async {
    _shutdown();
    await _transport.close();
  }

  void _onLine(String line) {
    final Object? message;
    try {
      message = jsonDecode(line);
    } on FormatException {
      return;
    }
    if (message is! Json) {
      return;
    }
    switch (message['type']) {
      case 'response':
        final id = message['id'];
        final completer = id is int ? _pending.remove(id) : null;
        if (completer == null) {
          return;
        }
        if (message['ok'] == true) {
          final data = message['data'];
          completer.complete(data is Json ? data : const {});
        } else {
          final error = message['error'];
          final fields = error is Json ? error : const <String, Object?>{};
          completer.completeError(
            ControlException(
              fields['code'] is String ? fields['code']! as String : 'unknown',
              fields['message'] is String ? fields['message']! as String : '',
            ),
          );
        }
      case 'event':
        final name = message['name'];
        final data = message['data'];
        if (name is String && !_events.isClosed) {
          _events.add(EngineEvent.decode(name, data is Json ? data : const {}));
        }
    }
  }

  void _shutdown() {
    if (isClosed) {
      return;
    }
    _closed.complete();
    for (final completer in _pending.values) {
      completer.completeError(const ControlClosedException());
    }
    _pending.clear();
    unawaited(_events.close());
    unawaited(_subscription.cancel());
  }
}
