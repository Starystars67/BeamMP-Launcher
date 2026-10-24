import 'dart:async';

import 'package:flutter/foundation.dart';

import 'launcher_settings.dart';
import 'settings_store.dart';

class SettingsController extends ChangeNotifier {
  SettingsController(
    this._store, {
    LauncherSettings initial = const LauncherSettings(),
  }) : _settings = initial;

  final SettingsStore _store;
  LauncherSettings _settings;
  Future<void> _saving = Future<void>.value();

  LauncherSettings get settings => _settings;

  Future<void> load() async {
    _settings = await _store.load();
    notifyListeners();
  }

  /// Applies the change right away and saves it in the background, in order.
  Future<void> update(
    LauncherSettings Function(LauncherSettings current) change,
  ) {
    final next = change(_settings);
    if (next == _settings) {
      return _saving;
    }
    _settings = next;
    notifyListeners();
    return _saving = _saving.then((_) => _store.save(next)).catchError((
      Object _,
    ) {
      // Not being able to save must not break the running app.
    });
  }
}
