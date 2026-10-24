import 'package:beammp_launcher/app/app_services.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/fakes.dart';
import 'support/harness.dart';

void main() {
  late Harness harness;

  setUpAll(loadFonts);
  tearDown(() => harness.dispose());

  Future<void> show(
    WidgetTester tester,
    AppPage page, {
    Brightness brightness = Brightness.light,
    Size size = const Size(980, 660),
  }) async {
    harness = Harness();
    await harness.pump(tester, brightness: brightness, size: size);
    harness.link.emit(LogReceived(log('a log line')));
    harness.link.emit(LogReceived(log('a warning', level: LogLevel.warn)));
    harness.link.emit(
      const AccountChanged(
        AccountInfo(signedIn: true, username: 'user', role: 'EA'),
      ),
    );
    harness.services.page.value = page;
    await harness.settleUi(tester);
  }

  group('keyboard', () {
    testWidgets('Cmd+number and Ctrl+number switch pages', (tester) async {
      await show(tester, AppPage.home);
      await tester.sendKeyDownEvent(LogicalKeyboardKey.meta);
      await tester.sendKeyEvent(LogicalKeyboardKey.digit2);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.meta);
      await tester.pump();
      expect(harness.services.page.value, AppPage.logs);

      await tester.sendKeyDownEvent(LogicalKeyboardKey.control);
      await tester.sendKeyEvent(LogicalKeyboardKey.digit4);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.control);
      await tester.pump();
      expect(harness.services.page.value, AppPage.settings);

      await tester.sendKeyDownEvent(LogicalKeyboardKey.meta);
      await tester.sendKeyEvent(LogicalKeyboardKey.digit1);
      await tester.sendKeyUpEvent(LogicalKeyboardKey.meta);
      await tester.pump();
      expect(harness.services.page.value, AppPage.home);
    });

    testWidgets('Tab reaches the Play button and Enter presses it', (
      tester,
    ) async {
      await show(tester, AppPage.home);
      for (var i = 0; i < 12; i++) {
        await tester.sendKeyEvent(LogicalKeyboardKey.tab);
        await tester.pump();
        final focused = FocusManager.instance.primaryFocus?.context;
        if (focused != null &&
            find
                .descendant(
                  of: find.byType(FilledButton),
                  matching: find.byWidget(focused.widget),
                )
                .evaluate()
                .isNotEmpty) {
          break;
        }
        if (focused?.findAncestorWidgetOfExactType<FilledButton>() != null) {
          break;
        }
      }
      expect(
        FocusManager.instance.primaryFocus?.context
            ?.findAncestorWidgetOfExactType<FilledButton>(),
        isNotNull,
      );
      await tester.sendKeyEvent(LogicalKeyboardKey.enter);
      await harness.settleUi(tester);
      expect(harness.link.calls, contains('launchGame:beamng'));
    });
  });

  group('semantics', () {
    for (final page in AppPage.values) {
      for (final brightness in Brightness.values) {
        testWidgets(
          '${page.name} (${brightness.name}) labels its tap targets and has readable contrast',
          (tester) async {
            final handle = tester.ensureSemantics();
            await show(tester, page, brightness: brightness);
            await expectLater(
              tester,
              meetsGuideline(labeledTapTargetGuideline),
            );
            await expectLater(tester, meetsGuideline(textContrastGuideline));
            handle.dispose();
          },
        );
      }
    }

    testWidgets('the game state is announced as a live region', (tester) async {
      final handle = tester.ensureSemantics();
      await show(tester, AppPage.home);
      expect(
        tester
            .getSemantics(find.text('Ready to play'))
            .flagsCollection
            .isLiveRegion,
        isTrue,
      );
      handle.dispose();
    });

    testWidgets('settings sections are headers', (tester) async {
      final handle = tester.ensureSemantics();
      await show(tester, AppPage.settings, size: const Size(980, 1100));
      expect(
        tester.getSemantics(find.text('Appearance')).flagsCollection.isHeader,
        isTrue,
      );
      handle.dispose();
    });
  });

  group('text size and window size', () {
    const smallest = Size(760, 520);
    for (final scale in [1.0, 1.5, 2.0]) {
      for (final page in AppPage.values) {
        testWidgets(
          '${page.name} at ${scale}x text in the smallest window shows no overflow',
          (tester) async {
            tester.platformDispatcher.textScaleFactorTestValue = scale;
            addTearDown(
              tester.platformDispatcher.clearTextScaleFactorTestValue,
            );
            await show(tester, page, size: smallest);
            expect(tester.takeException(), isNull);
          },
        );
      }
    }

    testWidgets('the startup failure screen survives large text', (
      tester,
    ) async {
      tester.platformDispatcher.textScaleFactorTestValue = 2.0;
      addTearDown(tester.platformDispatcher.clearTextScaleFactorTestValue);
      harness = Harness();
      harness.gateway.failure = EngineStartException(
        'x',
        output: ['a long line of output ' * 8],
      );
      await harness.pump(tester, size: smallest);
      await tester.tap(find.text('Show details'));
      await tester.pump();
      expect(tester.takeException(), isNull);
    });
  });

  testWidgets('the app works with animations turned off', (tester) async {
    tester.platformDispatcher.accessibilityFeaturesTestValue =
        const FakeAccessibilityFeatures(disableAnimations: true);
    addTearDown(tester.platformDispatcher.clearAccessibilityFeaturesTestValue);
    await show(tester, AppPage.home);
    expect(find.text('Ready to play'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
