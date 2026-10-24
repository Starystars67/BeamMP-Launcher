import 'dart:collection';

import 'package:flutter/foundation.dart';
import 'package:launcher_control/launcher_control.dart';

/// The most recent engine log records, kept apart from the engine state so
/// a busy log does not rebuild every screen.
class LogBuffer extends ChangeNotifier {
  LogBuffer({this.capacity = 5000});

  final int capacity;
  final List<LogEntry> _entries = [];

  UnmodifiableListView<LogEntry> get entries => UnmodifiableListView(_entries);

  void add(LogEntry entry) {
    _entries.add(entry);
    if (_entries.length > capacity) {
      _entries.removeRange(0, _entries.length - capacity);
    }
    notifyListeners();
  }

  void clear() {
    _entries.clear();
    notifyListeners();
  }
}
