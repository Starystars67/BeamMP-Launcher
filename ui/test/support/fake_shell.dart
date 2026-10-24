import 'dart:async';

import 'package:beammp_launcher/core/window/window_host.dart';

/// A window and a tray the test controls.
class FakeShell implements WindowHost, TrayHost, NotificationHost {
  FakeShell({this.trayAvailable = true});

  bool trayAvailable;
  final List<String> calls = [];
  List<TrayMenuItem> menu = const [];
  bool trayVisible = false;
  final List<({String title, String body})> notifications = [];

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

  void userClosesWindow() => _close.add(null);
  void userQuits() => _quit.add(null);
  void userRestoresWindow() => _restored.add(null);
  void userPicksTrayItem(String id) => _actions.add(id);

  @override
  Future<void> show({bool focus = true}) async =>
      calls.add(focus ? 'show' : 'show-inactive');

  @override
  Future<void> hide() async => calls.add('hide');

  @override
  Future<void> minimise() async => calls.add('minimise');

  @override
  Future<void> quit() async => calls.add('quit');

  @override
  Future<void> notify({required String title, required String body}) async =>
      notifications.add((title: title, body: body));

  @override
  Future<bool> probe() async => trayAvailable;

  @override
  Future<void> setVisible({required bool visible}) async {
    trayVisible = visible;
    calls.add(visible ? 'tray-on' : 'tray-off');
  }

  @override
  Future<void> setMenu(List<TrayMenuItem> items) async => menu = items;

  TrayMenuItem item(String id) => menu.firstWhere((item) => item.id == id);
}
