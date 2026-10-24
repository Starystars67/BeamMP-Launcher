import 'package:launcher_control/launcher_control.dart';

import '../../l10n/app_localizations.dart';
import '../../l10n/state_text.dart';
import '../engine/engine_controller.dart';
import 'window_host.dart';

abstract final class TrayAction {
  static const toggleWindow = 'toggle';
  static const play = 'play';
  static const openLogs = 'logs';
  static const quit = 'quit';
}

/// The tray menu for the current engine and window state.
List<TrayMenuItem> buildTrayMenu({
  required AppLocalizations l10n,
  required EngineConnection connection,
  required bool windowHidden,
}) {
  final online = connection is EngineOnline ? connection : null;
  final game = online?.activeGame ?? online?.beamng;
  final stateText = game == null
      ? (online == null ? l10n.gameStatePreparing : l10n.gameStateError)
      : gameStateText(l10n, game.state);
  return [
    TrayMenuItem(
      id: 'status',
      label: l10n.trayStatus(game?.name ?? l10n.gameBeamNG, stateText),
      enabled: false,
    ),
    const TrayMenuItem.separator(),
    TrayMenuItem(
      id: TrayAction.toggleWindow,
      label: windowHidden ? l10n.trayShow : l10n.trayHide,
    ),
    TrayMenuItem(
      id: TrayAction.play,
      label: l10n.trayPlay,
      enabled: online != null && online.state == EngineState.ready,
    ),
    TrayMenuItem(id: TrayAction.openLogs, label: l10n.trayOpenLogs),
    const TrayMenuItem.separator(),
    TrayMenuItem(id: TrayAction.quit, label: l10n.trayQuit),
  ];
}
