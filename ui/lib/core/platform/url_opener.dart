import 'dart:io';

/// Opens a web page in the user's browser.
abstract interface class UrlOpener {
  /// False when the address is not one we open, or the browser could not be started.
  Future<bool> open(String url);
}

/// `open` (macOS), `rundll32` (Windows) or `xdg-open` (Linux).
///
/// Only https pages and the loopback address are opened: the address comes
/// from the engine, and nothing else should ever be launched from it.
final class SystemUrlOpener implements UrlOpener {
  const SystemUrlOpener();

  static bool isOpenable(String url) {
    final uri = Uri.tryParse(url);
    if (uri == null || uri.host.isEmpty || uri.userInfo.isNotEmpty) {
      return false;
    }
    return uri.scheme == 'https' ||
        (uri.scheme == 'http' && uri.host == '127.0.0.1');
  }

  @override
  Future<bool> open(String url) async {
    if (!isOpenable(url)) {
      return false;
    }
    try {
      final result = switch (Platform.operatingSystem) {
        'macos' => await Process.run('open', [url]),
        'windows' => await Process.run('rundll32', [
          'url.dll,FileProtocolHandler',
          url,
        ]),
        _ => await Process.run('xdg-open', [url]),
      };
      return result.exitCode == 0;
    } on ProcessException {
      return false;
    }
  }
}
