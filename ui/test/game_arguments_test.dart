import 'package:beammp_launcher/core/engine/game_arguments.dart';
import 'package:beammp_launcher/core/settings/launcher_settings.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('nothing set gives no engine flags', () {
    expect(engineGameArguments(const LauncherSettings()), isEmpty);
  });

  test(
    'each setting becomes a flag for its game, paths with spaces intact',
    () {
      const settings = LauncherSettings(
        games: {
          'beamng': GameSettings(
            directory:
                '/Users/x/Library/Application Support/Steam/BeamNG.drive',
            command: '/opt/run-beamng.sh',
            userPath: '/Users/x/BeamNG.drive',
            launchArguments: '-console',
          ),
          'gearblocks': GameSettings(directory: '/games/GearBlocks'),
        },
      );
      expect(engineGameArguments(settings), [
        '--game-dir',
        'beamng=/Users/x/Library/Application Support/Steam/BeamNG.drive',
        '--game-command',
        'beamng=/opt/run-beamng.sh',
        '--game-user-path',
        'beamng=/Users/x/BeamNG.drive',
        '--game-dir',
        'gearblocks=/games/GearBlocks',
      ]);
    },
  );

  test(
    'the launch arguments are not engine flags, they are given at launch',
    () {
      const settings = LauncherSettings(
        games: {'beamng': GameSettings(launchArguments: '-console')},
      );
      expect(engineGameArguments(settings), isEmpty);
    },
  );
}
