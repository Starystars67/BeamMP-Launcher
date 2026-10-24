import 'package:meta/meta.dart' show immutable;

typedef Json = Map<String, Object?>;

/// Engine stages, see the protocol's "Engine states".
enum EngineState {
  starting('starting'),
  updating('updating'),
  preparingGame('preparing_game'),
  ready('ready'),
  launchingGame('launching_game'),
  gameRunning('game_running'),
  error('error'),
  fatal('fatal'),

  /// A state added by a newer engine.
  unknown('');

  const EngineState(this.wire);

  final String wire;

  static EngineState parse(Object? value) => values.firstWhere(
        (state) => state.wire == value && state != unknown,
        orElse: () => unknown,
      );

  /// A game is being started or runs.
  bool get gameActive => this == launchingGame || this == gameRunning;
}

enum GameState {
  /// Listed, but there is no multiplayer support for it yet.
  unavailable('unavailable'),

  /// Another game is starting or running; the engine runs one game at a time.
  busy('busy'),
  notFound('not_found'),
  preparing('preparing'),
  ready('ready'),
  launching('launching'),
  running('running'),
  exited('exited'),
  error('error'),
  unknown('');

  const GameState(this.wire);

  final String wire;

  static GameState parse(Object? value) => values.firstWhere(
        (state) => state.wire == value && state != unknown,
        orElse: () => unknown,
      );
}

/// Progress of a sign-in through the browser.
enum SignInState {
  idle('idle'),
  waitingForBrowser('waiting_for_browser'),
  exchanging('exchanging'),
  failed('failed'),

  /// A state added by a newer engine.
  unknown('');

  const SignInState(this.wire);

  final String wire;

  static SignInState parse(Object? value) => values.firstWhere(
        (state) => state.wire == value && state != unknown,
        orElse: () => unknown,
      );
}

enum LogLevel {
  debug('debug'),
  info('info'),
  warn('warn'),
  error('error'),
  fatal('fatal'),
  except('except');

  const LogLevel(this.wire);

  final String wire;

  static LogLevel parse(Object? value) => values.firstWhere(
        (level) => level.wire == value,
        orElse: () => info,
      );
}

String? _string(Json json, String key) {
  final value = json[key];
  return value is String ? value : null;
}

int? _int(Json json, String key) {
  final value = json[key];
  return value is int ? value : null;
}

@immutable
final class AccountInfo {
  const AccountInfo({
    required this.signedIn,
    this.username,
    this.role,
    this.id,
  });

  static const signedOut = AccountInfo(signedIn: false);

  factory AccountInfo.fromJson(Json json) => AccountInfo(
        signedIn: json['signed_in'] == true,
        username: _string(json, 'username'),
        role: _string(json, 'role'),
        id: _int(json, 'id'),
      );

  final bool signedIn;
  final String? username;
  final String? role;
  final int? id;

  @override
  bool operator ==(Object other) =>
      other is AccountInfo &&
      other.signedIn == signedIn &&
      other.username == username &&
      other.role == role &&
      other.id == id;

  @override
  int get hashCode => Object.hash(signedIn, username, role, id);

  @override
  String toString() => 'AccountInfo(signedIn: $signedIn, username: $username)';
}

/// What the engine knows about the running session, mostly for diagnostics.
@immutable
final class EngineStatus {
  const EngineStatus({
    this.account = AccountInfo.signedOut,
    this.ping = -1,
    this.uploadStatus = '',
    this.modStatus = '',
    this.gamePid = 0,
  });

  factory EngineStatus.fromJson(Json json) {
    final account = json['account'];
    return EngineStatus(
      account: account is Map<String, Object?>
          ? AccountInfo.fromJson(account)
          : AccountInfo.signedOut,
      ping: _int(json, 'ping') ?? -1,
      uploadStatus: _string(json, 'upload_status') ?? '',
      modStatus: _string(json, 'mod_status') ?? '',
      gamePid: _int(json, 'game_pid') ?? 0,
    );
  }

  final AccountInfo account;
  final int ping;
  final String uploadStatus;
  final String modStatus;
  final int gamePid;

  @override
  bool operator ==(Object other) =>
      other is EngineStatus &&
      other.account == account &&
      other.ping == ping &&
      other.uploadStatus == uploadStatus &&
      other.modStatus == modStatus &&
      other.gamePid == gamePid;

  @override
  int get hashCode =>
      Object.hash(account, ping, uploadStatus, modStatus, gamePid);
}

@immutable
final class EngineSnapshot {
  const EngineSnapshot({
    required this.state,
    this.detail = '',
    this.status = const EngineStatus(),
  });

  factory EngineSnapshot.fromJson(Json json) {
    final status = json['status'];
    return EngineSnapshot(
      state: EngineState.parse(json['state']),
      detail: _string(json, 'detail') ?? '',
      status: status is Map<String, Object?>
          ? EngineStatus.fromJson(status)
          : const EngineStatus(),
    );
  }

  final EngineState state;
  final String detail;
  final EngineStatus status;

  @override
  bool operator ==(Object other) =>
      other is EngineSnapshot &&
      other.state == state &&
      other.detail == detail &&
      other.status == status;

  @override
  int get hashCode => Object.hash(state, detail, status);
}

/// Where a game's multiplayer support stands (`install.state` of `games.list`).
enum InstallStatus {
  installed,
  needsInstall,
  outdated,
  unsupportedBuild,
  gameMissing,
  unknown;

  static InstallStatus parse(Object? value) => switch (value) {
        'installed' => installed,
        'needs_install' => needsInstall,
        'outdated' => outdated,
        'unsupported_build' => unsupportedBuild,
        'game_missing' => gameMissing,
        _ => unknown,
      };
}

/// The install of a game that has one (`games.list` carries it as `install`).
@immutable
final class GameInstall {
  const GameInstall({
    required this.status,
    this.busy = false,
    this.version,
    this.gameBuild,
    this.filesAvailable = true,
  });

  factory GameInstall.fromJson(Json json) => GameInstall(
        status: InstallStatus.parse(json['state']),
        busy: json['busy'] == true,
        version: _string(json, 'version'),
        gameBuild: _string(json, 'game_build'),
        filesAvailable: json['files_available'] != false,
      );

  final InstallStatus status;

  /// An install or uninstall of this game is running.
  final bool busy;

  /// The version of the launcher's files for this game.
  final String? version;

  /// The build of the game on this machine, when the engine could read it.
  final String? gameBuild;

  /// The launcher has the files it would install.
  final bool filesAvailable;

  @override
  bool operator ==(Object other) =>
      other is GameInstall &&
      other.status == status &&
      other.busy == busy &&
      other.version == version &&
      other.gameBuild == gameBuild &&
      other.filesAvailable == filesAvailable;

  @override
  int get hashCode =>
      Object.hash(status, busy, version, gameBuild, filesAvailable);
}

@immutable
final class GameInfo {
  const GameInfo({
    required this.id,
    required this.name,
    required this.state,
    this.pid,
    this.detail,
    this.steamAppId,
    this.sessions = false,
    this.install,
  });

  factory GameInfo.fromJson(Json json) => GameInfo(
        id: _string(json, 'id') ?? '',
        name: _string(json, 'name') ?? _string(json, 'id') ?? '',
        state: GameState.parse(json['state']),
        pid: _int(json, 'pid'),
        detail: _string(json, 'detail'),
        steamAppId: _int(json, 'steam_app_id'),
        sessions: json['sessions'] == true,
        install: json['install'] is Json
            ? GameInstall.fromJson(json['install'] as Json)
            : null,
      );

  final String id;
  final String name;
  final GameState state;
  final int? pid;

  /// The game's Steam app id, when it has one (its cover comes from there).
  final int? steamAppId;

  /// Players of this game can meet in relay rooms (the engine has a relay).
  final bool sessions;

  /// Why the game is not available (not found, failed), when the engine said.
  final String? detail;

  /// The game's multiplayer support, when it has something to install.
  final GameInstall? install;

  @override
  bool operator ==(Object other) =>
      other is GameInfo &&
      other.id == id &&
      other.name == name &&
      other.state == state &&
      other.pid == pid &&
      other.detail == detail &&
      other.steamAppId == steamAppId &&
      other.sessions == sessions &&
      other.install == install;

  @override
  int get hashCode =>
      Object.hash(id, name, state, pid, detail, steamAppId, sessions, install);
}

@immutable
final class LogEntry {
  const LogEntry({
    required this.time,
    required this.level,
    required this.message,
  });

  factory LogEntry.fromJson(Json json) => LogEntry(
        time: DateTime.tryParse(_string(json, 'ts') ?? '') ??
            DateTime.fromMillisecondsSinceEpoch(0, isUtc: true),
        level: LogLevel.parse(json['level']),
        message: _string(json, 'message') ?? '',
      );

  final DateTime time;
  final LogLevel level;
  final String message;

  @override
  bool operator ==(Object other) =>
      other is LogEntry &&
      other.time == time &&
      other.level == level &&
      other.message == message;

  @override
  int get hashCode => Object.hash(time, level, message);
}

/// What the engine said in answer to `hello`.
@immutable
final class HelloInfo {
  const HelloInfo({
    required this.version,
    required this.engine,
    required this.capabilities,
    required this.games,
  });

  factory HelloInfo.fromJson(Json json) {
    List<String> strings(Object? value) =>
        value is List<Object?> ? value.whereType<String>().toList() : const [];
    return HelloInfo(
      version: _int(json, 'version') ?? 0,
      engine: _string(json, 'engine') ?? '',
      capabilities: strings(json['capabilities']),
      games: strings(json['games']),
    );
  }

  final int version;
  final String engine;
  final List<String> capabilities;
  final List<String> games;
}

enum SessionState {
  idle('idle'),
  connecting('connecting'),
  inRoom('in_room'),

  /// A state added by a newer engine.
  unknown('');

  const SessionState(this.wire);

  final String wire;

  static SessionState parse(Object? value) => values.firstWhere(
        (state) => state.wire == value && state != unknown,
        orElse: () => unknown,
      );
}

/// A player in a room.
@immutable
final class SessionPeer {
  const SessionPeer({required this.id, required this.name});

  final int id;
  final String name;

  @override
  bool operator ==(Object other) =>
      other is SessionPeer && other.id == id && other.name == name;

  @override
  int get hashCode => Object.hash(id, name);
}

/// Where the player stands in a relay room (see `session.state` in the protocol).
@immutable
final class SessionInfo {
  const SessionInfo({
    this.game = '',
    this.state = SessionState.idle,
    this.role,
    this.code,
    this.selfId,
    this.hostId,
    this.peers = const [],
    this.errorCode,
    this.errorMessage,
  });

  factory SessionInfo.fromJson(Json json) {
    final error = json['error'];
    final peers = json['peers'];
    return SessionInfo(
      game: _string(json, 'game') ?? '',
      state: SessionState.parse(json['state']),
      role: _string(json, 'role'),
      code: _string(json, 'code'),
      selfId: _int(json, 'self_id'),
      hostId: _int(json, 'host_id'),
      peers: peers is List<Object?>
          ? [
              for (final peer in peers.whereType<Json>())
                if (_int(peer, 'id') != null)
                  SessionPeer(
                    id: _int(peer, 'id')!,
                    name: _string(peer, 'name') ?? '',
                  ),
            ]
          : const [],
      errorCode: error is Json ? _string(error, 'code') : null,
      errorMessage: error is Json ? _string(error, 'message') : null,
    );
  }

  static const SessionInfo none = SessionInfo();

  final String game;
  final SessionState state;

  /// `host` or `guest` while in a room.
  final String? role;
  final String? code;
  final int? selfId;
  final int? hostId;

  /// Everybody in the room, the host first, yourself included.
  final List<SessionPeer> peers;

  /// Why a session ended or could not start, together with [SessionState.idle].
  final String? errorCode;
  final String? errorMessage;

  bool get isHost => role == 'host';
  bool get active => state != SessionState.idle;

  @override
  bool operator ==(Object other) =>
      other is SessionInfo &&
      other.game == game &&
      other.state == state &&
      other.role == role &&
      other.code == code &&
      other.selfId == selfId &&
      other.hostId == hostId &&
      _listEquals(other.peers, peers) &&
      other.errorCode == errorCode &&
      other.errorMessage == errorMessage;

  @override
  int get hashCode => Object.hash(
        game,
        state,
        role,
        code,
        selfId,
        hostId,
        Object.hashAll(peers),
        errorCode,
        errorMessage,
      );
}

bool _listEquals<T>(List<T> a, List<T> b) {
  if (a.length != b.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

/// Messages the engine sends on its own.
sealed class EngineEvent {
  const EngineEvent();

  /// Never throws: anything unrecognised or malformed becomes [UnknownEvent].
  factory EngineEvent.decode(String name, Json data) {
    switch (name) {
      case 'engine.state':
        final state = EngineState.parse(data['state']);
        if (data['state'] is String) {
          return EngineStateChanged(state, _string(data, 'detail') ?? '');
        }
      case 'game.state':
        final game = _string(data, 'game');
        if (game != null && data['state'] is String) {
          return GameStateChanged(
            game: game,
            state: GameState.parse(data['state']),
            pid: _int(data, 'pid'),
            detail: _string(data, 'detail'),
          );
        }
      case 'account.state':
        if (data['signed_in'] is bool) {
          return AccountChanged(AccountInfo.fromJson(data));
        }
      case 'log':
        if (data['message'] is String) {
          return LogReceived(LogEntry.fromJson(data));
        }
      case 'account.signin':
        if (data['state'] is String) {
          return SignInProgress(
            state: SignInState.parse(data['state']),
            url: _string(data, 'url'),
            reason: _string(data, 'reason'),
          );
        }
      case 'session.state':
        if (data['state'] is String) {
          return SessionChanged(SessionInfo.fromJson(data));
        }
      case 'game.install':
        final game = _string(data, 'game');
        if (game != null) {
          return InstallProgressChanged(
            game: game,
            phase: InstallPhase.parse(data['state']),
            step: _string(data, 'step') ?? '',
            message: _string(data, 'message') ?? '',
            fraction: data['fraction'] is num
                ? (data['fraction'] as num).toDouble()
                : null,
            uninstall: data['uninstall'] == true,
            errorCode: data['error'] is Json
                ? _string(data['error'] as Json, 'code')
                : null,
          );
        }
      case 'fatal':
        return FatalReported(
          _string(data, 'code') ?? 'fatal',
          _string(data, 'message') ?? '',
        );
    }
    return UnknownEvent(name, data);
  }
}

final class EngineStateChanged extends EngineEvent {
  const EngineStateChanged(this.state, this.detail);

  final EngineState state;
  final String detail;
}

final class GameStateChanged extends EngineEvent {
  const GameStateChanged({
    required this.game,
    required this.state,
    this.pid,
    this.detail,
  });

  final String game;
  final GameState state;
  final int? pid;
  final String? detail;
}

final class AccountChanged extends EngineEvent {
  const AccountChanged(this.account);

  final AccountInfo account;
}

final class LogReceived extends EngineEvent {
  const LogReceived(this.entry);

  final LogEntry entry;
}

final class SignInProgress extends EngineEvent {
  const SignInProgress({required this.state, this.url, this.reason});

  final SignInState state;

  /// While waiting: the page the user has to open.
  final String? url;

  /// On failure: `cancelled`, `timeout`, `denied`, `network`, `server`,
  /// `rejected`, `expired` or `internal`.
  final String? reason;
}

final class FatalReported extends EngineEvent {
  const FatalReported(this.code, this.message);

  final String code;
  final String message;
}

/// How an install or uninstall stands (`game.install`).
enum InstallPhase {
  running,
  done,
  failed,
  cancelled;

  static InstallPhase parse(Object? value) => switch (value) {
        'done' => done,
        'failed' => failed,
        'cancelled' => cancelled,
        _ => running,
      };

  bool get ended => this != running;
}

final class InstallProgressChanged extends EngineEvent {
  const InstallProgressChanged({
    required this.game,
    required this.phase,
    required this.step,
    required this.message,
    this.fraction,
    this.uninstall = false,
    this.errorCode,
  });

  final String game;
  final InstallPhase phase;

  /// A stable name of what is being done (`check`, `loader`, `plugin`, `mod`...).
  final String step;

  /// The engine's own words, in English.
  final String message;

  /// 0 to 1, when the engine knows.
  final double? fraction;

  /// The job is an uninstall.
  final bool uninstall;

  /// Why it failed (`download_failed`, `loader_conflict`, ...).
  final String? errorCode;
}

final class SessionChanged extends EngineEvent {
  const SessionChanged(this.session);

  final SessionInfo session;
}

final class UnknownEvent extends EngineEvent {
  const UnknownEvent(this.name, this.data);

  final String name;
  final Json data;
}
