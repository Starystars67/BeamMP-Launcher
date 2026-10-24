import 'dart:convert';
import 'dart:io';

import 'package:beammp_launcher/l10n/app_localizations.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

Map<String, Object?> arb(File file) =>
    jsonDecode(file.readAsStringSync()) as Map<String, Object?>;

Iterable<String> messageKeys(Map<String, Object?> arb) =>
    arb.keys.where((key) => !key.startsWith('@'));

Set<String> placeholders(String message) =>
    RegExp(r'\{(\w+)\}').allMatches(message).map((m) => m.group(1)!).toSet();

void main() {
  final files = Directory('lib/l10n')
      .listSync()
      .whereType<File>()
      .where((f) => RegExp(r'app_\w+\.arb$').hasMatch(f.path))
      .toList();
  final template = arb(File('lib/l10n/app_en.arb'));

  test('there is a template to compare against', () {
    expect(files, isNotEmpty);
    expect(messageKeys(template), isNotEmpty);
  });

  group('every locale matches the English template', () {
    for (final file in files) {
      final name = file.uri.pathSegments.last;
      test(name, () {
        final locale = arb(file);
        expect(
          messageKeys(locale).toSet(),
          messageKeys(template).toSet(),
          reason: 'missing or extra messages',
        );
        for (final key in messageKeys(template)) {
          expect(
            placeholders(locale[key]! as String),
            placeholders(template[key]! as String),
            reason: 'placeholders of $key differ',
          );
        }
      });
    }
  });

  test('every placeholder in the template is declared', () {
    for (final key in messageKeys(template)) {
      final used = placeholders(template[key]! as String);
      if (used.isEmpty) {
        continue;
      }
      final meta = template['@$key'] as Map<String, Object?>?;
      final declared =
          (meta?['placeholders'] as Map<String, Object?>?)?.keys.toSet() ??
          <String>{};
      expect(declared, used, reason: 'placeholders of $key');
    }
  });

  test('every message is used somewhere in the app', () {
    final sources = Directory('lib')
        .listSync(recursive: true)
        .whereType<File>()
        .where(
          (f) =>
              f.path.endsWith('.dart') &&
              !f.path.contains('/l10n/app_localizations'),
        )
        .map((f) => f.readAsStringSync())
        .join('\n');
    final unused = [
      for (final key in messageKeys(template))
        if (!sources.contains('.$key')) key,
    ];
    expect(unused, isEmpty, reason: 'unused messages: $unused');
  });

  test('no user-visible text is hard-coded in the screens', () {
    final offenders = <String>[];
    for (final file in Directory(
      'lib',
    ).listSync(recursive: true).whereType<File>()) {
      if (!file.path.endsWith('.dart') || file.path.contains('/l10n/')) {
        continue;
      }
      final lines = file.readAsLinesSync();
      for (var i = 0; i < lines.length; i++) {
        if (RegExp(
              r'''\b(Text|SelectableText)\(\s*['"]''',
            ).hasMatch(lines[i]) ||
            RegExp(
              r'''\b(labelText|hintText|tooltip|helperText):\s*['"]''',
            ).hasMatch(lines[i])) {
          offenders.add('${file.path}:${i + 1}: ${lines[i].trim()}');
        }
      }
    }
    expect(offenders, isEmpty);
  });

  test('English messages are available for the supported locale', () {
    expect(AppLocalizations.supportedLocales, contains(const Locale('en')));
    final l10n = lookupAppLocalizations(const Locale('en'));
    expect(l10n.appTitle, 'BeamMP Launcher');
    expect(l10n.trayStatus('Game', 'Running'), 'Game - Running');
    expect(l10n.logsCount(3), '3 entries');
  });
}
