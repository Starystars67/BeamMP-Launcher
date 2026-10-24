import 'package:flutter/material.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../core/engine/engine_controller.dart';
import '../../core/settings/launch_arguments.dart';
import '../../l10n/app_localizations.dart';
import '../../l10n/state_text.dart';
import 'game_cover.dart';
import 'game_settings_screen.dart';
import 'install_panel.dart';
import 'session_panel.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l10n.homeTitle)),
      body: ListenableBuilder(
        listenable: engine,
        builder: (context, _) {
          final connection = engine.connection;
          if (connection is! EngineOnline) {
            return const SizedBox.shrink();
          }
          // like the sites: content in a column that stops growing on wide windows
          return Align(
            alignment: Alignment.topCenter,
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 880),
              child: ListView(
                padding: const EdgeInsets.all(24),
                children: [
                  for (final game in connection.games)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 16),
                      child: game.state == GameState.unavailable
                          ? _OtherGameCard(game: game)
                          : Column(
                              children: [
                                _GameCard(game: game, engine: engine),
                                if (installNeedsAttention(game, engine)) ...[
                                  const SizedBox(height: 8),
                                  InstallPanel(game: game),
                                ],
                                if (_canPlayTogether(game)) ...[
                                  const SizedBox(height: 8),
                                  SessionPanel(game: game),
                                ],
                              ],
                            ),
                    ),
                ],
              ),
            ),
          );
        },
      ),
    );
  }
}

String _launchProblemText(AppLocalizations l10n, LaunchProblem problem) =>
    switch (problem) {
      LaunchRefused(:final message) => l10n.launchFailed(message),
      LaunchNoAnswer() => l10n.launchNoAnswer,
      LaunchBadArguments(:final problem) => switch (problem) {
        ArgumentsProblem.unterminatedQuote => l10n.launchArgumentsUnterminated,
        ArgumentsProblem.tooMany => l10n.launchArgumentsTooMany,
        ArgumentsProblem.tooLong => l10n.launchArgumentsTooLong,
      },
    };

/// A game that is listed but has nothing to play yet.
class _OtherGameCard extends StatelessWidget {
  const _OtherGameCard({required this.game});

  final GameInfo game;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Row(
          children: [
            Opacity(opacity: 0.6, child: GameCover(game: game)),
            const SizedBox(width: 24),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(game.name, style: theme.textTheme.titleLarge),
                  const SizedBox(height: 4),
                  Text(
                    gameStateText(l10n, game.state),
                    style: theme.textTheme.bodyMedium,
                  ),
                ],
              ),
            ),
            const SizedBox(width: 16),
            FilledButton(onPressed: null, child: Text(l10n.actionPlay)),
          ],
        ),
      ),
    );
  }
}

class _GameCard extends StatelessWidget {
  const _GameCard({required this.game, required this.engine});

  final GameInfo game;
  final EngineController engine;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final active =
        game.state == GameState.launching || game.state == GameState.running;
    // The last launch attempt and the last exit are shown on the card of the game they were about.
    final lastLaunched = engine.lastLaunchedGame == game.id;
    final problem = lastLaunched ? engine.launchProblem : null;
    final error = problem == null ? null : _launchProblemText(l10n, problem);
    final unusable =
        game.state == GameState.notFound || game.state == GameState.error;
    final note =
        error ??
        (lastLaunched ? engine.gameNote : null) ??
        (unusable ? game.detail : null);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Row(
          children: [
            GameCover(game: game),
            const SizedBox(width: 24),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(game.name, style: theme.textTheme.titleLarge),
                  const SizedBox(height: 4),
                  Semantics(
                    liveRegion: true,
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        _StateDot(state: game.state),
                        const SizedBox(width: 8),
                        Flexible(
                          child: Text(
                            gameStateText(l10n, game.state),
                            style: theme.textTheme.bodyMedium,
                          ),
                        ),
                      ],
                    ),
                  ),
                  if (note != null && note.isNotEmpty) ...[
                    const SizedBox(height: 8),
                    Text(
                      note,
                      style: theme.textTheme.bodySmall?.copyWith(
                        color: error != null
                            ? theme.colorScheme.error
                            : theme.colorScheme.onSurfaceVariant,
                      ),
                    ),
                  ],
                ],
              ),
            ),
            const SizedBox(width: 8),
            IconButton(
              tooltip: l10n.gameSettingsTooltip,
              icon: const Icon(Icons.tune),
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (_) => GameSettingsScreen(gameId: game.id),
                ),
              ),
            ),
            const SizedBox(width: 8),
            if (active)
              OutlinedButton(
                onPressed: () => engine.stopGame(game.id),
                child: Text(l10n.actionStop),
              )
            else
              FilledButton(
                onPressed: engine.canLaunch && game.state == GameState.ready
                    ? () => engine.launchGame(game.id)
                    : null,
                child: Text(l10n.actionPlay),
              ),
          ],
        ),
      ),
    );
  }
}

/// A small coloured dot that says at a glance how a game stands (the words
/// next to it say the same, the colour is not the only signal).
class _StateDot extends StatelessWidget {
  const _StateDot({required this.state});

  final GameState state;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final color = switch (state) {
      GameState.ready || GameState.running => scheme.tertiary,
      GameState.launching || GameState.preparing => scheme.primary,
      GameState.notFound || GameState.error => scheme.error,
      _ => scheme.outline,
    };
    return ExcludeSemantics(
      child: Container(
        width: 10,
        height: 10,
        decoration: BoxDecoration(color: color, shape: BoxShape.circle),
      ),
    );
  }
}

/// Rooms need the engine to have a relay, and a game that can actually be started.
bool _canPlayTogether(GameInfo game) =>
    game.sessions &&
    game.state != GameState.notFound &&
    game.state != GameState.error &&
    game.state != GameState.unavailable;
