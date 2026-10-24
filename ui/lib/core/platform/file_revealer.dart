import 'dart:io';

/// Shows a file in the system file manager.
abstract interface class FileRevealer {
  /// Returns false when the file does not exist or could not be revealed.
  Future<bool> reveal(String path);
}

/// Uses `open -R` (macOS), `explorer /select,` (Windows) or opens the folder
/// with `xdg-open` (Linux).
final class SystemFileRevealer implements FileRevealer {
  const SystemFileRevealer();

  @override
  Future<bool> reveal(String path) async {
    if (!File(path).existsSync()) {
      return false;
    }
    try {
      final result = switch (Platform.operatingSystem) {
        'macos' => await Process.run('open', ['-R', path]),
        'windows' => await Process.run('explorer', ['/select,', path]),
        _ => await Process.run('xdg-open', [File(path).parent.path]),
      };
      // explorer reports 1 even on success.
      return result.exitCode == 0 || Platform.isWindows;
    } on ProcessException {
      return false;
    }
  }
}
