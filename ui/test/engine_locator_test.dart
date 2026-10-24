import 'package:beammp_launcher/core/engine/engine_locator.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('macOS looks in the bundle Helpers folder', () {
    expect(
      engineCandidates(
        environment: const {},
        uiExecutable:
            '/Applications/BeamMP-Launcher.app/Contents/MacOS/BeamMP-Launcher',
        platform: 'macos',
      ),
      [
        '/Applications/BeamMP-Launcher.app/Contents/Helpers/BeamMP-Launcher-Console',
      ],
    );
  });

  test('Windows and Linux look next to the UI', () {
    expect(
      engineCandidates(
        environment: const {},
        uiExecutable: '/opt/beammp/BeamMP-Launcher',
        platform: 'linux',
      ),
      ['/opt/beammp/BeamMP-Launcher-Console'],
    );
    expect(
      engineCandidates(
        environment: const {},
        uiExecutable: 'C:/BeamMP/BeamMP-Launcher.exe',
        platform: 'windows',
      ),
      ['C:/BeamMP/BeamMP-Launcher-Console.exe'],
    );
  });

  test('BEAMMP_ENGINE overrides the search', () {
    expect(
      engineCandidates(
        environment: const {'BEAMMP_ENGINE': '/dev/engine'},
        uiExecutable: '/x/ui',
        platform: 'linux',
      ),
      ['/dev/engine'],
    );
    expect(
      engineCandidates(
        environment: const {'BEAMMP_ENGINE': ''},
        uiExecutable: '/x/ui',
        platform: 'linux',
      ),
      ['/x/BeamMP-Launcher-Console'],
    );
  });

  test('the first candidate that exists is used, with the extra arguments', () {
    final found = locateEngine(
      environment: const {
        'BEAMMP_ENGINE': '/dev/engine',
        'BEAMMP_ENGINE_ARGS': '  --no-update   --no-launch ',
      },
      uiExecutable: '/x/ui',
      platform: 'macos',
      exists: (path) => path == '/dev/engine',
    );
    expect(found.command?.executable, '/dev/engine');
    expect(found.command?.engineArguments, ['--no-update', '--no-launch']);
  });

  test('quotes keep an engine argument with spaces together', () {
    final found = locateEngine(
      environment: const {
        'BEAMMP_ENGINE': '/dev/engine',
        'BEAMMP_ENGINE_ARGS':
            '--game-dir "beamng=/Users/x/Library/Application Support/Steam" -p 4444',
      },
      uiExecutable: '/x/ui',
      platform: 'macos',
      exists: (path) => path == '/dev/engine',
    );
    expect(found.command?.engineArguments, [
      '--game-dir',
      'beamng=/Users/x/Library/Application Support/Steam',
      '-p',
      '4444',
    ]);
  });

  test('a missing engine reports where it looked', () {
    final found = locateEngine(
      environment: const {},
      uiExecutable: '/x/ui',
      platform: 'linux',
      exists: (_) => false,
    );
    expect(found.command, isNull);
    expect(found.searched, ['/x/BeamMP-Launcher-Console']);
  });

  test('no extra arguments by default', () {
    final found = locateEngine(
      environment: const {},
      uiExecutable: '/x/ui',
      platform: 'linux',
      exists: (_) => true,
    );
    expect(found.command?.engineArguments, isEmpty);
  });
}
