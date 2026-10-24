import 'dart:io';

import 'package:flutter/painting.dart';
import 'package:path/path.dart' as p;

/// Finds a game's portrait cover. Nothing is bundled or downloaded: the art is
/// the user's own, from the library cache of their Steam client.
abstract interface class GameCovers {
  /// The cover image of a Steam game, or nothing when there is none (no Steam,
  /// or Steam has not fetched it yet).
  Future<ImageProvider?> coverFor(int steamAppId);
}

/// Where Steam keeps its library art, most likely first.
List<String> steamLibraryCacheDirectories({
  required Map<String, String> environment,
  required String platform,
}) {
  final home = environment['HOME'] ?? environment['USERPROFILE'] ?? '';
  return switch (platform) {
    'macos' => [
      p.join(
        home,
        'Library',
        'Application Support',
        'Steam',
        'appcache',
        'librarycache',
      ),
    ],
    'windows' => [
      for (final key in ['ProgramFiles(x86)', 'ProgramFiles'])
        if (environment[key] != null)
          p.join(environment[key]!, 'Steam', 'appcache', 'librarycache'),
    ],
    _ => [
      p.join(home, '.local', 'share', 'Steam', 'appcache', 'librarycache'),
      p.join(home, '.steam', 'steam', 'appcache', 'librarycache'),
      p.join(
        home,
        '.var',
        'app',
        'com.valvesoftware.Steam',
        '.local',
        'share',
        'Steam',
        'appcache',
        'librarycache',
      ),
    ],
  };
}

/// The portrait art names Steam uses, best first: `library_600x900.jpg` (what
/// older clients and some games have next to the app folder) and
/// `library_capsule.jpg` (what newer clients keep in a folder named by a hash).
const List<String> steamCoverNames = [
  'library_600x900.jpg',
  'library_600x900.png',
  'library_capsule.jpg',
  'library_capsule.png',
];

/// Looks the cover up in Steam's library cache, once per game.
final class SteamGameCovers implements GameCovers {
  SteamGameCovers(this.libraryCaches);

  factory SteamGameCovers.system({
    required Map<String, String> environment,
    required String platform,
  }) => SteamGameCovers(
    steamLibraryCacheDirectories(environment: environment, platform: platform),
  );

  final List<String> libraryCaches;
  final Map<int, Future<File?>> _found = {};

  @override
  Future<ImageProvider?> coverFor(int steamAppId) async {
    final file = await coverFileFor(steamAppId);
    return file == null ? null : FileImage(file);
  }

  /// The cover's file, looked up once per game.
  Future<File?> coverFileFor(int steamAppId) =>
      _found.putIfAbsent(steamAppId, () => _find(steamAppId));

  Future<File?> _find(int steamAppId) async {
    for (final cache in libraryCaches) {
      final folder = Directory(p.join(cache, '$steamAppId'));
      try {
        if (!await folder.exists()) {
          continue;
        }
        // next to the folder's own files first, then one level down
        for (final name in steamCoverNames) {
          final file = File(p.join(folder.path, name));
          if (await _usable(file)) {
            return file;
          }
        }
        final subfolders = await folder
            .list()
            .where((entry) => entry is Directory)
            .toList();
        for (final name in steamCoverNames) {
          for (final subfolder in subfolders) {
            final file = File(p.join(subfolder.path, name));
            if (await _usable(file)) {
              return file;
            }
          }
        }
      } on FileSystemException {
        // not readable: try the next place
      }
    }
    return null;
  }

  static Future<bool> _usable(File file) async =>
      await file.exists() && await file.length() > 0;
}
