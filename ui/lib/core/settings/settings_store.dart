import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;

import 'launcher_settings.dart';

abstract interface class SettingsStore {
  /// Never throws: unreadable settings are the defaults.
  Future<LauncherSettings> load();

  Future<void> save(LauncherSettings settings);
}

/// Where the UI keeps its settings on [platform].
String settingsFilePath({
  required Map<String, String> environment,
  required String platform,
}) {
  final home = environment['HOME'] ?? environment['USERPROFILE'] ?? '.';
  switch (platform) {
    case 'macos':
      return p.join(
        home,
        'Library',
        'Application Support',
        'BeamMP',
        'launcher-ui.json',
      );
    case 'windows':
      return p.join(
        environment['APPDATA'] ?? p.join(home, 'AppData', 'Roaming'),
        'BeamMP',
        'launcher-ui.json',
      );
    default:
      return p.join(
        environment['XDG_CONFIG_HOME'] ?? p.join(home, '.config'),
        'beammp',
        'launcher-ui.json',
      );
  }
}

/// Settings in a small JSON file, replaced atomically on every save.
final class FileSettingsStore implements SettingsStore {
  FileSettingsStore(this.path);

  final String path;

  @override
  Future<LauncherSettings> load() async {
    try {
      return LauncherSettings.fromJson(
        jsonDecode(await File(path).readAsString()),
      );
    } on Object {
      return const LauncherSettings();
    }
  }

  @override
  Future<void> save(LauncherSettings settings) async {
    final file = File(path);
    await file.parent.create(recursive: true);
    final temporary = File('$path.tmp');
    await temporary.writeAsString(
      const JsonEncoder.withIndent('  ').convert(settings.toJson()),
      flush: true,
    );
    await temporary.rename(path);
  }
}
