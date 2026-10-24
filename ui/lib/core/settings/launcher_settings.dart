import 'package:flutter/foundation.dart';

/// What the window does when the game starts.
enum LaunchWindowBehavior {
  /// Hide to the tray when there is one, otherwise stay open.
  auto,
  hideToTray,
  stayOpen,
  minimise,
}

enum CloseBehavior { quit, closeToTray }

enum ThemeSetting { system, light, dark }

/// What the user set for one game. Empty means "work it out yourself".
@immutable
final class GameSettings {
  const GameSettings({
    this.directory = '',
    this.command = '',
    this.userPath = '',
    this.launchArguments = '',
  });

  factory GameSettings.fromJson(Object? json) {
    if (json is! Map<String, Object?>) {
      return const GameSettings();
    }
    String text(String key) => json[key] is String ? json[key]! as String : '';
    return GameSettings(
      directory: text('directory'),
      command: text('command'),
      userPath: text('userPath'),
      launchArguments: text('launchArguments'),
    );
  }

  /// The game's folder, instead of looking for it in Steam.
  final String directory;

  /// A program or script that starts the game instead of its own executable
  /// (for example one that runs it through Wine).
  final String command;

  /// The game's folder for user data (BeamNG: the one that contains `current`).
  final String userPath;

  /// Extra game arguments as typed, parsed by [parseLaunchArguments].
  final String launchArguments;

  /// Nothing set: not written to the file.
  bool get isEmpty =>
      directory.isEmpty &&
      command.isEmpty &&
      userPath.isEmpty &&
      launchArguments.isEmpty;

  /// What the engine needs to know (the arguments are given at every launch).
  bool sameEngineSettingsAs(GameSettings other) =>
      directory == other.directory &&
      command == other.command &&
      userPath == other.userPath;

  Map<String, Object?> toJson() => {
    'directory': directory,
    'command': command,
    'userPath': userPath,
    'launchArguments': launchArguments,
  };

  GameSettings copyWith({
    String? directory,
    String? command,
    String? userPath,
    String? launchArguments,
  }) => GameSettings(
    directory: directory ?? this.directory,
    command: command ?? this.command,
    userPath: userPath ?? this.userPath,
    launchArguments: launchArguments ?? this.launchArguments,
  );

  @override
  bool operator ==(Object other) =>
      other is GameSettings &&
      other.directory == directory &&
      other.command == command &&
      other.userPath == userPath &&
      other.launchArguments == launchArguments;

  @override
  int get hashCode =>
      Object.hash(directory, command, userPath, launchArguments);
}

/// Settings owned by the UI (the engine has its own).
@immutable
final class LauncherSettings {
  const LauncherSettings({
    this.trayEnabled = true,
    this.launchWindowBehavior = LaunchWindowBehavior.auto,
    this.closeBehavior = CloseBehavior.quit,
    this.restoreWindowOnGameExit = true,
    this.notificationsEnabled = true,
    this.theme = ThemeSetting.system,
    this.games = const {},
  });

  /// Reads settings written by this or an older version; anything missing or
  /// invalid falls back to the default.
  factory LauncherSettings.fromJson(Object? json) {
    if (json is! Map<String, Object?>) {
      return const LauncherSettings();
    }
    T parse<T extends Enum>(List<T> values, Object? name, T fallback) {
      for (final value in values) {
        if (value.name == name) {
          return value;
        }
      }
      return fallback;
    }

    const defaults = LauncherSettings();
    return LauncherSettings(
      trayEnabled: json['tray.enabled'] is bool
          ? json['tray.enabled']! as bool
          : defaults.trayEnabled,
      launchWindowBehavior: parse(
        LaunchWindowBehavior.values,
        json['launch.windowBehavior'],
        defaults.launchWindowBehavior,
      ),
      closeBehavior: parse(
        CloseBehavior.values,
        json['close.behavior'],
        defaults.closeBehavior,
      ),
      restoreWindowOnGameExit: json['gameExit.restoreWindow'] is bool
          ? json['gameExit.restoreWindow']! as bool
          : defaults.restoreWindowOnGameExit,
      notificationsEnabled: json['notifications.enabled'] is bool
          ? json['notifications.enabled']! as bool
          : defaults.notificationsEnabled,
      theme: parse(
        ThemeSetting.values,
        json['appearance.theme'],
        defaults.theme,
      ),
      games: _parseGames(json),
    );
  }

  static Map<String, GameSettings> _parseGames(Map<String, Object?> json) {
    final games = <String, GameSettings>{};
    final raw = json['games'];
    if (raw is Map<String, Object?>) {
      for (final entry in raw.entries) {
        final game = GameSettings.fromJson(entry.value);
        if (!game.isEmpty) {
          games[entry.key] = game;
        }
      }
    }
    // Before there were several games the launch arguments were BeamNG's alone.
    final legacy = json['game.launchArguments'];
    if (legacy is String &&
        legacy.isNotEmpty &&
        (games['beamng']?.launchArguments ?? '').isEmpty) {
      games['beamng'] = (games['beamng'] ?? const GameSettings()).copyWith(
        launchArguments: legacy,
      );
    }
    return games;
  }

  final bool trayEnabled;
  final LaunchWindowBehavior launchWindowBehavior;
  final CloseBehavior closeBehavior;
  final bool restoreWindowOnGameExit;
  final bool notificationsEnabled;
  final ThemeSetting theme;

  /// Per game id; a game without an entry has nothing set.
  final Map<String, GameSettings> games;

  GameSettings game(String id) => games[id] ?? const GameSettings();

  LauncherSettings copyWithGame(
    String id,
    GameSettings Function(GameSettings current) change,
  ) {
    final next = change(game(id));
    final updated = {...games};
    if (next.isEmpty) {
      updated.remove(id);
    } else {
      updated[id] = next;
    }
    return copyWith(games: updated);
  }

  Map<String, Object?> toJson() => {
    'tray.enabled': trayEnabled,
    'launch.windowBehavior': launchWindowBehavior.name,
    'close.behavior': closeBehavior.name,
    'gameExit.restoreWindow': restoreWindowOnGameExit,
    'notifications.enabled': notificationsEnabled,
    'appearance.theme': theme.name,
    'games': {
      for (final entry in games.entries) entry.key: entry.value.toJson(),
    },
  };

  LauncherSettings copyWith({
    bool? trayEnabled,
    LaunchWindowBehavior? launchWindowBehavior,
    CloseBehavior? closeBehavior,
    bool? restoreWindowOnGameExit,
    bool? notificationsEnabled,
    ThemeSetting? theme,
    Map<String, GameSettings>? games,
  }) => LauncherSettings(
    trayEnabled: trayEnabled ?? this.trayEnabled,
    launchWindowBehavior: launchWindowBehavior ?? this.launchWindowBehavior,
    closeBehavior: closeBehavior ?? this.closeBehavior,
    restoreWindowOnGameExit:
        restoreWindowOnGameExit ?? this.restoreWindowOnGameExit,
    notificationsEnabled: notificationsEnabled ?? this.notificationsEnabled,
    theme: theme ?? this.theme,
    games: games ?? this.games,
  );

  @override
  bool operator ==(Object other) =>
      other is LauncherSettings &&
      other.trayEnabled == trayEnabled &&
      other.launchWindowBehavior == launchWindowBehavior &&
      other.closeBehavior == closeBehavior &&
      other.restoreWindowOnGameExit == restoreWindowOnGameExit &&
      other.notificationsEnabled == notificationsEnabled &&
      other.theme == theme &&
      mapEquals(other.games, games);

  @override
  int get hashCode => Object.hash(
    trayEnabled,
    launchWindowBehavior,
    closeBehavior,
    restoreWindowOnGameExit,
    notificationsEnabled,
    theme,
    Object.hashAll(
      (games.entries.toList()..sort((a, b) => a.key.compareTo(b.key))).map(
        (entry) => Object.hash(entry.key, entry.value),
      ),
    ),
  );
}
