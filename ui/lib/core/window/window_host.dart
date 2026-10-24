/// The application window as the UI logic sees it. The native side implements
/// it per platform; tests use a fake.
abstract interface class WindowHost {
  /// The user closed the window (close button, Cmd+W). The window stays until
  /// the UI decides what to do.
  Stream<void> get closeRequests;

  /// The user asked to quit the whole application (Cmd+Q, Dock menu).
  Stream<void> get quitRequests;

  /// The user brought the window back themselves (Dock icon, un-minimising).
  Stream<void> get restored;

  /// Shows the window; without [focus] it does not steal the focus.
  Future<void> show({bool focus = true});

  /// Hides the window completely. Only safe while a tray icon exists.
  Future<void> hide();

  Future<void> minimise();

  /// Ends the application now.
  Future<void> quit();
}

final class TrayMenuItem {
  const TrayMenuItem({
    required this.id,
    required this.label,
    this.enabled = true,
  });

  const TrayMenuItem.separator() : id = '', label = '', enabled = false;

  final String id;
  final String label;
  final bool enabled;

  bool get isSeparator => id.isEmpty;

  @override
  bool operator ==(Object other) =>
      other is TrayMenuItem &&
      other.id == id &&
      other.label == label &&
      other.enabled == enabled;

  @override
  int get hashCode => Object.hash(id, label, enabled);

  @override
  String toString() => isSeparator
      ? 'TrayMenuItem.separator'
      : 'TrayMenuItem($id, $label, enabled: $enabled)';
}

/// The menu bar / notification area icon.
abstract interface class TrayHost {
  /// Whether an icon can be shown at all here (a Linux desktop without a
  /// status notifier host, for instance, has none).
  Future<bool> probe();

  Future<void> setVisible({required bool visible});

  Future<void> setMenu(List<TrayMenuItem> items);

  /// Ids of the chosen menu items.
  Stream<String> get actions;
}

/// System notifications.
abstract interface class NotificationHost {
  /// Shows a notification. Never throws: a refused permission is not an error.
  Future<void> notify({required String title, required String body});
}

/// A platform without any native window or tray support: nothing can be
/// hidden, so the window behaviour degrades to staying open.
final class UnsupportedShell implements WindowHost, TrayHost, NotificationHost {
  const UnsupportedShell();

  @override
  Stream<void> get closeRequests => const Stream.empty();

  @override
  Stream<void> get quitRequests => const Stream.empty();

  @override
  Stream<void> get restored => const Stream.empty();

  @override
  Stream<String> get actions => const Stream.empty();

  @override
  Future<void> show({bool focus = true}) async {}

  @override
  Future<void> hide() async {}

  @override
  Future<void> minimise() async {}

  @override
  Future<void> quit() async {}

  @override
  Future<bool> probe() async => false;

  @override
  Future<void> setVisible({required bool visible}) async {}

  @override
  Future<void> setMenu(List<TrayMenuItem> items) async {}

  @override
  Future<void> notify({required String title, required String body}) async {}
}
