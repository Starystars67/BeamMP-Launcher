import 'package:launcher_control/launcher_control.dart';

import 'app_localizations.dart';

/// Short, user-facing text for a game state.
String gameStateText(AppLocalizations l10n, GameState state) => switch (state) {
  GameState.unavailable => l10n.gameStateUnavailable,
  GameState.busy => l10n.gameStateBusy,
  GameState.ready => l10n.gameStateReady,
  GameState.launching => l10n.gameStateLaunching,
  GameState.running => l10n.gameStateRunning,
  GameState.preparing => l10n.gameStatePreparing,
  GameState.notFound => l10n.gameStateNotFound,
  GameState.exited => l10n.gameStateExited,
  GameState.error || GameState.unknown => l10n.gameStateError,
};
