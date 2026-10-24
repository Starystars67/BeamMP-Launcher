import 'dart:async';

import 'package:flutter/services.dart';

import 'window_host.dart';

/// The window and tray of the macOS runner (`ShellChannel.swift`), reached
/// over a method channel. Other platforms get their own implementation of
/// the same two interfaces.
final class NativeShell implements WindowHost, TrayHost, NotificationHost {
  NativeShell({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel(channelName) {
    _channel.setMethodCallHandler(_onCall);
  }

  static const String channelName = 'com.beammp.launcher/shell';

  final MethodChannel _channel;
  final StreamController<void> _close = StreamController<void>.broadcast();
  final StreamController<void> _quit = StreamController<void>.broadcast();
  final StreamController<void> _restored = StreamController<void>.broadcast();
  final StreamController<String> _actions =
      StreamController<String>.broadcast();

  @override
  Stream<void> get closeRequests => _close.stream;

  @override
  Stream<void> get quitRequests => _quit.stream;

  @override
  Stream<void> get restored => _restored.stream;

  @override
  Stream<String> get actions => _actions.stream;

  Future<Object?> _onCall(MethodCall call) async {
    switch (call.method) {
      case 'closeRequested':
        _close.add(null);
      case 'quitRequested':
        _quit.add(null);
      case 'restored':
        _restored.add(null);
      case 'trayAction':
        final id = call.arguments;
        if (id is String) {
          _actions.add(id);
        }
    }
    return null;
  }

  @override
  Future<void> show({bool focus = true}) =>
      _channel.invokeMethod<void>('show', {'focus': focus});

  @override
  Future<void> hide() => _channel.invokeMethod<void>('hide');

  @override
  Future<void> minimise() => _channel.invokeMethod<void>('minimise');

  @override
  Future<void> quit() => _channel.invokeMethod<void>('quit');

  @override
  Future<bool> probe() async {
    try {
      return await _channel.invokeMethod<bool>('tray.probe') ?? false;
    } on PlatformException {
      return false;
    } on MissingPluginException {
      return false;
    }
  }

  @override
  Future<void> notify({required String title, required String body}) async {
    try {
      await _channel.invokeMethod<void>('notify', {
        'title': title,
        'body': body,
      });
    } on PlatformException {
      // A refused permission is not an error.
    } on MissingPluginException {
      // No native side.
    }
  }

  @override
  Future<void> setVisible({required bool visible}) =>
      _channel.invokeMethod<void>('tray.setVisible', {'visible': visible});

  @override
  Future<void> setMenu(List<TrayMenuItem> items) =>
      _channel.invokeMethod<void>('tray.setMenu', {
        'items': [
          for (final item in items)
            {
              'id': item.id,
              'label': item.label,
              'enabled': item.enabled,
              'separator': item.isSeparator,
            },
        ],
      });
}
