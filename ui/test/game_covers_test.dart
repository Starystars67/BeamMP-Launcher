import 'dart:io';

import 'package:beammp_launcher/core/platform/game_covers.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('where Steam keeps its art', () {
    test('macOS', () {
      expect(
        steamLibraryCacheDirectories(
          environment: {'HOME': '/Users/x'},
          platform: 'macos',
        ),
        ['/Users/x/Library/Application Support/Steam/appcache/librarycache'],
      );
    });

    test('Linux, including the Flatpak', () {
      final dirs = steamLibraryCacheDirectories(
        environment: {'HOME': '/home/x'},
        platform: 'linux',
      );
      expect(dirs.first, '/home/x/.local/share/Steam/appcache/librarycache');
      expect(dirs, contains('/home/x/.steam/steam/appcache/librarycache'));
      expect(
        dirs.last,
        '/home/x/.var/app/com.valvesoftware.Steam/.local/share/Steam/appcache/librarycache',
      );
    });

    test('Windows, from the program files folders that exist', () {
      expect(
        steamLibraryCacheDirectories(
          environment: {'ProgramFiles(x86)': r'C:\Program Files (x86)'},
          platform: 'windows',
        ),
        hasLength(1),
      );
      expect(
        steamLibraryCacheDirectories(environment: {}, platform: 'windows'),
        isEmpty,
      );
    });
  });

  group('SteamGameCovers', () {
    late Directory cache;
    late Directory other;

    setUp(() {
      cache = Directory.systemTemp.createTempSync('beammp-covers-');
      other = Directory.systemTemp.createTempSync('beammp-covers2-');
    });

    tearDown(() {
      cache.deleteSync(recursive: true);
      other.deleteSync(recursive: true);
    });

    File put(Directory root, String relative, [List<int> bytes = const [1]]) =>
        File('${root.path}/$relative')
          ..createSync(recursive: true)
          ..writeAsBytesSync(bytes);

    test('finds library_600x900.jpg next to the game\'s files', () async {
      final file = put(cache, '1305080/library_600x900.jpg');
      final found = await SteamGameCovers([cache.path]).coverFileFor(1305080);
      expect(found?.path, file.path);
    });

    test('finds library_capsule.jpg in a folder named by a hash', () async {
      final file = put(
        cache,
        '284160/5b9d29c96707cb4b3de0e75428255f7d7c319b7a/library_capsule.jpg',
      );
      put(cache, '284160/872a6a68dcac/library_hero.jpg'); // not a cover
      final found = await SteamGameCovers([cache.path]).coverFileFor(284160);
      expect(found?.path, file.path);
    });

    test(
      'prefers the 600x900 art, and art next to the game over a deeper one',
      () async {
        put(cache, '7/abc/library_capsule.jpg');
        final deeper600 = put(cache, '7/abc/library_600x900.jpg');
        expect(
          (await SteamGameCovers([cache.path]).coverFileFor(7))?.path,
          deeper600.path,
        );

        final direct = put(cache, '7/library_capsule.jpg');
        expect(
          (await SteamGameCovers([cache.path]).coverFileFor(7))?.path,
          direct.path,
        );
      },
    );

    test(
      'an empty file, a missing cache and a missing game give nothing',
      () async {
        put(cache, '5/library_600x900.jpg', const []);
        expect(await SteamGameCovers([cache.path]).coverFileFor(5), isNull);
        expect(
          await SteamGameCovers(['/does/not/exist']).coverFileFor(5),
          isNull,
        );
        expect(await SteamGameCovers([]).coverFileFor(5), isNull);
      },
    );

    test('the places are tried in order', () async {
      final second = put(other, '3/library_600x900.jpg');
      final covers = SteamGameCovers([cache.path, other.path]);
      expect((await covers.coverFileFor(3))?.path, second.path);

      final first = put(cache, '3/library_600x900.jpg');
      // asked once per game: the answer is kept
      expect((await covers.coverFileFor(3))?.path, second.path);
      expect(
        (await SteamGameCovers([cache.path, other.path]).coverFileFor(3))?.path,
        first.path,
      );
    });
  });
}
