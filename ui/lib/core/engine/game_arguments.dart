import '../settings/launcher_settings.dart';

/// The engine's command line flags for the saved game settings, passed when
/// the engine starts (`--game-dir <id>=<path>` and so on).
List<String> engineGameArguments(LauncherSettings settings) {
  final arguments = <String>[];
  for (final entry in settings.games.entries) {
    final game = entry.value;
    void add(String flag, String value) {
      if (value.isNotEmpty) {
        arguments
          ..add(flag)
          ..add('${entry.key}=$value');
      }
    }

    add('--game-dir', game.directory);
    add('--game-command', game.command);
    add('--game-user-path', game.userPath);
  }
  return arguments;
}
