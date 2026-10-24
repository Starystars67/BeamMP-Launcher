import 'package:beammp_launcher/app/app_services.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:launcher_control/launcher_control.dart';

import 'support/harness.dart';

void main() {
  late Harness harness;

  setUpAll(loadFonts);
  tearDown(() => harness.dispose());

  GameInfo gearBlocks({
    InstallStatus status = InstallStatus.needsInstall,
    bool busy = false,
    bool filesAvailable = true,
    String? build,
    GameState state = GameState.notFound,
  }) => GameInfo(
    id: 'gearblocks',
    name: 'GearBlocks',
    state: state,
    detail: 'BepInEx is not installed',
    install: GameInstall(
      status: status,
      busy: busy,
      version: '0.1.0',
      gameBuild: build,
      filesAvailable: filesAvailable,
    ),
  );

  Future<void> show(
    WidgetTester tester,
    GameInfo game, {
    EngineState engineState = EngineState.ready,
  }) async {
    harness = Harness();
    harness.link.games = [game];
    harness.link.snapshot = EngineSnapshot(state: engineState);
    await harness.pump(tester);
    harness.services.page.value = AppPage.home;
    await harness.settleUi(tester);
  }

  testWidgets('a game that needs its install offers it, and starts it', (
    tester,
  ) async {
    await show(tester, gearBlocks());
    expect(find.text('Multiplayer support'), findsOneWidget);
    expect(find.textContaining('needs a mod loader'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Install'));
    await harness.settleUi(tester);
    expect(harness.link.installCalls, ['install:gearblocks']);
    // progress shows at once, with a way to stop
    expect(find.text('Installing…'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    expect(find.text('Cancel'), findsOneWidget);
  });

  testWidgets('progress from the engine shows its words and its fraction', (
    tester,
  ) async {
    await show(tester, gearBlocks());
    await tester.tap(find.widgetWithText(FilledButton, 'Install'));
    await harness.settleUi(tester);
    harness.link.emit(
      const InstallProgressChanged(
        game: 'gearblocks',
        phase: InstallPhase.running,
        step: 'loader',
        message: 'Downloading BepInEx 6.0.0',
        fraction: 0.4,
      ),
    );
    await harness.settleUi(tester);
    expect(find.text('Downloading BepInEx 6.0.0'), findsOneWidget);
    final bar = tester.widget<LinearProgressIndicator>(
      find.byType(LinearProgressIndicator),
    );
    expect(bar.value, 0.4);
    await tester.tap(find.text('Cancel'));
    await harness.settleUi(tester);
    expect(harness.link.installCalls.last, 'cancel:gearblocks');
  });

  testWidgets('a failed download says what to do, and can be tried again', (
    tester,
  ) async {
    await show(tester, gearBlocks());
    await tester.tap(find.widgetWithText(FilledButton, 'Install'));
    await harness.settleUi(tester);
    harness.link.emit(
      const InstallProgressChanged(
        game: 'gearblocks',
        phase: InstallPhase.failed,
        step: 'done',
        message: 'The download failed: no network',
        errorCode: 'download_failed',
      ),
    );
    await harness.settleUi(tester);
    expect(find.text('Installing did not work.'), findsOneWidget);
    expect(find.textContaining('Check your connection'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Install'));
    await harness.settleUi(tester);
    expect(harness.link.installCalls, [
      'install:gearblocks',
      'install:gearblocks',
    ]);
  });

  testWidgets('each kind of failure has its own words, and can be dismissed', (
    tester,
  ) async {
    await show(tester, gearBlocks());
    const messages = {
      'bad_checksum': 'not the file BeamMP\'s list names',
      'bad_signature': 'could not be verified',
      'manifest_rollback': 'could not be verified',
      'launcher_too_old': 'needs a newer launcher',
      'manifest_unconfigured': 'not set up to get',
      'loader_conflict': 'another mod loader',
      'not_writable': 'cannot be changed',
      'unsupported_build': 'not one the launcher',
      'something_new': 'The engine said this',
    };
    for (final entry in messages.entries) {
      harness.link.emit(
        InstallProgressChanged(
          game: 'gearblocks',
          phase: InstallPhase.failed,
          step: 'done',
          message: 'The engine said this',
          errorCode: entry.key,
        ),
      );
      await harness.settleUi(tester);
      expect(
        find.textContaining(entry.value),
        findsOneWidget,
        reason: entry.key,
      );
    }
    await tester.tap(find.text('Dismiss'));
    await harness.settleUi(tester);
    expect(find.text('Installing did not work.'), findsNothing);
    expect(find.widgetWithText(FilledButton, 'Install'), findsOneWidget);
  });

  testWidgets('a cancelled install is not shown as a failure', (tester) async {
    await show(tester, gearBlocks());
    harness.link.emit(
      const InstallProgressChanged(
        game: 'gearblocks',
        phase: InstallPhase.cancelled,
        step: 'done',
        message: 'Cancelled',
      ),
    );
    await harness.settleUi(tester);
    expect(find.text('Installing was cancelled.'), findsOneWidget);
    expect(find.text('Installing did not work.'), findsNothing);
  });

  testWidgets(
    'without the launcher\'s own files it explains and cannot start',
    (tester) async {
      await show(tester, gearBlocks(filesAvailable: false));
      expect(find.textContaining('not set up to get'), findsOneWidget);
      final button = tester.widget<FilledButton>(
        find.widgetWithText(FilledButton, 'Install'),
      );
      expect(button.onPressed, isNull);
    },
  );

  testWidgets('while a game runs the install waits', (tester) async {
    await show(tester, gearBlocks(), engineState: EngineState.gameRunning);
    final button = tester.widget<FilledButton>(
      find.widgetWithText(FilledButton, 'Install'),
    );
    expect(button.onPressed, isNull);
    expect(find.textContaining('Wait until'), findsOneWidget);
  });

  testWidgets('an outdated install offers an update', (tester) async {
    await show(
      tester,
      gearBlocks(status: InstallStatus.outdated, state: GameState.ready),
    );
    expect(find.textContaining('not the current one'), findsOneWidget);
    await tester.tap(find.widgetWithText(FilledButton, 'Update'));
    await harness.settleUi(tester);
    expect(harness.link.installCalls, ['install:gearblocks']);
  });

  testWidgets('a build the plugin was not made for is explained, not blocked', (
    tester,
  ) async {
    await show(
      tester,
      gearBlocks(
        status: InstallStatus.unsupportedBuild,
        build: '0.9.1000',
        state: GameState.ready,
      ),
    );
    expect(find.textContaining('0.9.1000'), findsOneWidget);
    expect(find.widgetWithText(FilledButton, 'Install'), findsNothing);
  });

  testWidgets('a game whose folder is not set asks for it', (tester) async {
    await show(tester, gearBlocks(status: InstallStatus.gameMissing));
    expect(find.textContaining('folder first'), findsOneWidget);
  });

  testWidgets('an installed game shows nothing on the home page', (
    tester,
  ) async {
    await show(
      tester,
      gearBlocks(status: InstallStatus.installed, state: GameState.ready),
    );
    expect(find.text('Multiplayer support'), findsNothing);
  });

  testWidgets('a game without an install shows nothing', (tester) async {
    harness = Harness();
    await harness.pump(tester);
    harness.services.page.value = AppPage.home;
    await harness.settleUi(tester);
    expect(find.text('Multiplayer support'), findsNothing);
  });

  testWidgets('the settings of an installed game can remove it', (
    tester,
  ) async {
    await show(
      tester,
      gearBlocks(status: InstallStatus.installed, state: GameState.ready),
    );
    await tester.tap(find.byTooltip('Game settings'));
    await tester.pumpAndSettle();
    expect(find.textContaining('version 0.1.0'), findsOneWidget);
    await tester.tap(
      find.widgetWithText(OutlinedButton, 'Remove multiplayer support'),
    );
    await harness.settleUi(tester);
    expect(harness.link.installCalls, ['uninstall:gearblocks:false']);
  });

  testWidgets(
    'the settings of a game that has nothing installed show no removal',
    (tester) async {
      await show(tester, gearBlocks());
      await tester.tap(find.byTooltip('Game settings'));
      await tester.pumpAndSettle();
      expect(find.text('Remove multiplayer support'), findsNothing);
    },
  );
}
