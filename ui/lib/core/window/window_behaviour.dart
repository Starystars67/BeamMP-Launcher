import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../l10n/app_localizations.dart';
import '../engine/engine_controller.dart';
import '../settings/launcher_settings.dart';
import '../settings/settings_controller.dart';
import 'tray_menu.dart';
import 'window_host.dart';

/// Decides what happens to the window and the tray icon: hiding when the game
/// starts, coming back when it ends, what closing the window means.
///
/// The tray is probed first. Without one, settings that need it are treated
/// as off and the window is never hidden where it could not be brought back.
class WindowBehaviour {
  WindowBehaviour({
    required this.engine,
    required this.settings,
    required this.window,
    required this.tray,
    required this.strings,
    this.notifications,
    this.onOpenLogs,
  });

  final EngineController engine;
  final SettingsController settings;
  final WindowHost window;
  final TrayHost tray;

  /// Tells the user about things that happen while the window is away.
  final NotificationHost? notifications;

  /// Current strings, for the tray menu.
  final AppLocalizations Function() strings;

  /// Brings the log screen to the front.
  final VoidCallback? onOpenLogs;

  final StreamController<void> _quitRequests =
      StreamController<void>.broadcast();
  final List<StreamSubscription<void>> _subscriptions = [];
  bool _trayAvailable = false;
  bool _windowAway = false;
  bool _gameWasActive = false;
  bool _started = false;
  bool _trayShown = false;
  bool _engineFailed = false;

  /// The user wants to quit (window closed with "quit", Cmd+Q, tray menu).
  /// The app confirms if needed and then calls [WindowHost.quit].
  Stream<void> get quitRequests => _quitRequests.stream;

  bool get trayAvailable => _trayAvailable;

  /// A tray icon is wanted and possible.
  bool get trayActive => settings.settings.trayEnabled && _trayAvailable;

  /// The window is hidden or minimised by this logic.
  bool get windowAway => _windowAway;

  Future<void> start() async {
    if (_started) {
      return;
    }
    _started = true;
    _trayAvailable = await tray.probe();
    _subscriptions
      ..add(window.closeRequests.listen((_) => _onCloseRequested()))
      ..add(window.quitRequests.listen((_) => _quitRequests.add(null)))
      ..add(window.restored.listen((_) => _onRestoredByUser()))
      ..add(tray.actions.listen((id) => unawaited(_onTrayAction(id))));
    engine.addListener(_onEngineChanged);
    settings.addListener(_onSettingsChanged);
    await _syncTray();
  }

  Future<void> showWindow({bool focus = true}) async {
    await window.show(focus: focus);
    _windowAway = false;
    await _syncMenu();
  }

  Future<void> dispose() async {
    engine.removeListener(_onEngineChanged);
    settings.removeListener(_onSettingsChanged);
    for (final subscription in _subscriptions) {
      await subscription.cancel();
    }
    await _quitRequests.close();
  }

  LaunchWindowBehavior get _launchBehavior {
    final chosen = settings.settings.launchWindowBehavior;
    return switch (chosen) {
      LaunchWindowBehavior.auto =>
        trayActive
            ? LaunchWindowBehavior.hideToTray
            : LaunchWindowBehavior.stayOpen,
      LaunchWindowBehavior.hideToTray when !trayActive =>
        LaunchWindowBehavior.minimise,
      _ => chosen,
    };
  }

  void _onEngineChanged() {
    final connection = engine.connection;
    if (connection is EngineFailed) {
      // The engine going away is not "the game closed": handled on its own.
      _gameWasActive = false;
      if (!_engineFailed) {
        _engineFailed = true;
        unawaited(_onEngineFailed(connection));
      }
      return;
    }
    _engineFailed = false;
    final active = connection is EngineOnline && connection.activeGame != null;
    final started = active && !_gameWasActive;
    final ended = !active && _gameWasActive;
    _gameWasActive = active;
    if (started) {
      unawaited(_onGameStarted());
    } else if (ended) {
      unawaited(_onGameEnded());
    } else {
      unawaited(_syncMenu());
    }
  }

  Future<void> _onGameStarted() async {
    switch (_launchBehavior) {
      case LaunchWindowBehavior.hideToTray:
        _windowAway = true;
        await window.hide();
      case LaunchWindowBehavior.minimise:
        _windowAway = true;
        await window.minimise();
      case LaunchWindowBehavior.stayOpen || LaunchWindowBehavior.auto:
        break;
    }
    await _syncMenu();
  }

  Future<void> _onGameEnded() async {
    if (_windowAway && settings.settings.restoreWindowOnGameExit) {
      await showWindow(focus: false);
    } else {
      await _syncMenu();
    }
    if (_windowAway) {
      final l10n = strings();
      await _notify(
        l10n.notifyGameClosedTitle,
        engine.gameNote ?? l10n.gameStateExited,
      );
    }
  }

  /// Without a working engine nothing in the window is usable: bring it back
  /// and say so, whatever the user had chosen for the game.
  Future<void> _onEngineFailed(EngineFailed failed) async {
    final l10n = strings();
    final wasAway = _windowAway;
    if (wasAway) {
      await showWindow(focus: false);
    }
    await _syncMenu();
    if (wasAway) {
      await _notify(
        l10n.notifyEngineStoppedTitle,
        failed.kind == FailureKind.fatal && failed.message.isNotEmpty
            ? failed.message
            : l10n.startupFailedLost,
      );
    }
  }

  Future<void> _notify(String title, String body) async {
    if (settings.settings.notificationsEnabled) {
      await notifications?.notify(title: title, body: body);
    }
  }

  void _onRestoredByUser() {
    _windowAway = false;
    unawaited(_syncMenu());
  }

  void _onCloseRequested() {
    if (settings.settings.closeBehavior == CloseBehavior.closeToTray &&
        trayActive) {
      _windowAway = true;
      unawaited(window.hide().then((_) => _syncMenu()));
    } else {
      _quitRequests.add(null);
    }
  }

  void _onSettingsChanged() {
    unawaited(_syncTray());
  }

  Future<void> _syncTray() async {
    final wanted = trayActive;
    if (wanted != _trayShown) {
      _trayShown = wanted;
      await tray.setVisible(visible: wanted);
    }
    if (!wanted && _windowAway && !_gameWasActive) {
      // Never leave a hidden window nobody can bring back.
      await showWindow();
    }
    await _syncMenu();
  }

  Future<void> _syncMenu() async {
    if (!_trayShown) {
      return;
    }
    await tray.setMenu(
      buildTrayMenu(
        l10n: strings(),
        connection: engine.connection,
        windowHidden: _windowAway,
      ),
    );
  }

  Future<void> _onTrayAction(String id) async {
    switch (id) {
      case TrayAction.toggleWindow:
        if (_windowAway) {
          await showWindow();
        } else {
          _windowAway = true;
          await window.hide();
          await _syncMenu();
        }
      case TrayAction.play:
        await engine.launchGame();
      case TrayAction.openLogs:
        await showWindow();
        onOpenLogs?.call();
      case TrayAction.quit:
        _quitRequests.add(null);
    }
  }
}
