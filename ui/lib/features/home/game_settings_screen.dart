import 'package:flutter/material.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../core/engine/engine_controller.dart';
import '../../core/settings/launch_arguments.dart';
import '../../core/settings/launcher_settings.dart';
import '../../l10n/app_localizations.dart';
import '../../l10n/state_text.dart';
import 'install_panel.dart';

/// Where one game is, how it starts and what it is started with. Opened from
/// the game's card; the app's own settings stay in the Settings page.
class GameSettingsScreen extends StatelessWidget {
  const GameSettingsScreen({required this.gameId, super.key});

  final String gameId;

  @override
  Widget build(BuildContext context) {
    final services = AppScope.of(context);
    final l10n = AppLocalizations.of(context);
    return ListenableBuilder(
      listenable: Listenable.merge([services.engine, services.settings]),
      builder: (context, _) {
        final connection = services.engine.connection;
        GameInfo? game;
        if (connection is EngineOnline) {
          for (final candidate in connection.games) {
            if (candidate.id == gameId) {
              game = candidate;
            }
          }
        }
        return Scaffold(
          appBar: AppBar(
            title: Text(l10n.gameSettingsTitle(game?.name ?? gameId)),
          ),
          body: game == null
              ? const SizedBox.shrink()
              : _GameSettingsForm(game: game),
        );
      },
    );
  }
}

class _GameSettingsForm extends StatelessWidget {
  const _GameSettingsForm({required this.game});

  final GameInfo game;

  @override
  Widget build(BuildContext context) {
    final services = AppScope.of(context);
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final saved = services.settings.settings.game(game.id);

    Future<void> save(GameSettings Function(GameSettings) change) async {
      final messenger = ScaffoldMessenger.of(context);
      final before = services.settings.settings.game(game.id);
      final after = change(before);
      if (after == before) {
        return;
      }
      await services.settings.update((s) => s.copyWithGame(game.id, change));
      if (after.sameEngineSettingsAs(before)) {
        return; // only the launch arguments changed, read at the next launch
      }
      final problem = await services.engine.configureGame(
        game.id,
        directory: after.directory,
        command: after.command,
        userPath: after.userPath,
      );
      final text = switch (problem) {
        null || ConfigureNoEngine() => null,
        ConfigureBusy() => l10n.settingsGameBusy,
        ConfigureRefused(:final message) => l10n.settingsGameNotApplied(
          message,
        ),
      };
      if (text != null) {
        messenger.showSnackBar(SnackBar(content: Text(text)));
      }
    }

    final status = [
      gameStateText(l10n, game.state),
      if (game.detail != null && game.detail!.isNotEmpty) game.detail!,
    ].join(' – ');
    return ListView(
      padding: const EdgeInsets.symmetric(vertical: 8),
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          child: Semantics(
            liveRegion: true,
            child: Text(
              status,
              style: theme.textTheme.bodyMedium?.copyWith(
                color: theme.colorScheme.onSurfaceVariant,
              ),
            ),
          ),
        ),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: InstalledTile(game: game),
        ),
        _SavedTextField(
          key: ValueKey('${game.id}.directory'),
          label: l10n.settingsGameFolder,
          help: l10n.settingsGameFolderHelp,
          initial: saved.directory,
          onSaved: (text) => save((g) => g.copyWith(directory: text.trim())),
        ),
        _SavedTextField(
          key: ValueKey('${game.id}.command'),
          label: l10n.settingsStartCommand,
          help: l10n.settingsStartCommandHelp,
          initial: saved.command,
          onSaved: (text) => save((g) => g.copyWith(command: text.trim())),
        ),
        if (game.id == beamngGameId)
          _SavedTextField(
            key: ValueKey('${game.id}.userPath'),
            label: l10n.settingsUserFolder,
            help: l10n.settingsUserFolderHelp,
            initial: saved.userPath,
            onSaved: (text) => save((g) => g.copyWith(userPath: text.trim())),
          ),
        _SavedTextField(
          key: ValueKey('${game.id}.arguments'),
          label: l10n.settingsLaunchArguments,
          hint: l10n.settingsLaunchArgumentsHint,
          help: l10n.settingsLaunchArgumentsHelp,
          initial: saved.launchArguments,
          validate: (text) => switch (parseLaunchArguments(text).problem) {
            null => null,
            ArgumentsProblem.unterminatedQuote =>
              l10n.launchArgumentsUnterminated,
            ArgumentsProblem.tooMany => l10n.launchArgumentsTooMany,
            ArgumentsProblem.tooLong => l10n.launchArgumentsTooLong,
          },
          onSaved: (text) => save((g) => g.copyWith(launchArguments: text)),
        ),
      ],
    );
  }
}

/// Saved when the field is submitted or loses the focus, not on every key.
class _SavedTextField extends StatefulWidget {
  const _SavedTextField({
    required super.key,
    required this.label,
    required this.initial,
    required this.onSaved,
    this.hint,
    this.help,
    this.validate,
  });

  final String label;
  final String? hint;
  final String? help;
  final String initial;
  final ValueChanged<String> onSaved;
  final String? Function(String text)? validate;

  @override
  State<_SavedTextField> createState() => _SavedTextFieldState();
}

class _SavedTextFieldState extends State<_SavedTextField> {
  late final TextEditingController _controller = TextEditingController(
    text: widget.initial,
  );
  late final FocusNode _focus = FocusNode()..addListener(_onFocus);

  void _onFocus() {
    if (!_focus.hasFocus) {
      widget.onSaved(_controller.text);
    }
  }

  @override
  void dispose() {
    _focus.removeListener(_onFocus);
    _focus.dispose();
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
    child: TextField(
      controller: _controller,
      focusNode: _focus,
      onChanged: (_) => setState(() {}),
      onSubmitted: widget.onSaved,
      autocorrect: false,
      enableSuggestions: false,
      decoration: InputDecoration(
        labelText: widget.label,
        hintText: widget.hint,
        helperText: widget.help,
        helperMaxLines: 4,
        errorText: widget.validate?.call(_controller.text),
        errorMaxLines: 2,
        border: const OutlineInputBorder(),
      ),
      style: const TextStyle(fontFamily: 'Menlo'),
    ),
  );
}
