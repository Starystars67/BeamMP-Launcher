import 'package:flutter/material.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../core/engine/engine_controller.dart';
import '../../l10n/app_localizations.dart';

/// Whether a game's install needs the player's attention (something to install,
/// update or explain, or an install that is running or has just failed).
bool installNeedsAttention(GameInfo game, EngineController engine) {
  final install = game.install;
  if (install == null) {
    return false;
  }
  if (engine.installProgress(game.id) != null || install.busy) {
    return true;
  }
  return switch (install.status) {
    InstallStatus.needsInstall ||
    InstallStatus.outdated ||
    InstallStatus.unsupportedBuild ||
    InstallStatus.gameMissing => true,
    InstallStatus.installed || InstallStatus.unknown => false,
  };
}

/// "Multiplayer support": installing what a game needs for playing together,
/// with progress while it runs and why it failed. Shown under the game's card
/// when there is something to do (see [installNeedsAttention]).
class InstallPanel extends StatelessWidget {
  const InstallPanel({required this.game, super.key});

  final GameInfo game;

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final install = game.install;
    if (install == null) {
      return const SizedBox.shrink();
    }
    final progress = engine.installProgress(game.id);
    final running =
        progress?.phase == InstallPhase.running ||
        (progress == null && install.busy);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Semantics(
              header: true,
              child: Text(
                l10n.installTitle,
                style: theme.textTheme.titleMedium,
              ),
            ),
            const SizedBox(height: 12),
            if (running)
              _Running(game: game, engine: engine, progress: progress)
            else if (progress != null)
              _Ended(game: game, engine: engine, progress: progress)
            else
              _Idle(game: game, engine: engine, install: install),
          ],
        ),
      ),
    );
  }
}

class _Running extends StatelessWidget {
  const _Running({
    required this.game,
    required this.engine,
    required this.progress,
  });

  final GameInfo game;
  final EngineController engine;
  final InstallProgressChanged? progress;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final message = progress?.message ?? '';
    final removing = progress?.uninstall ?? false;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Semantics(
          liveRegion: true,
          child: Text(
            message.isEmpty
                ? (removing ? l10n.installRemoving : l10n.installRunning)
                : message,
          ),
        ),
        const SizedBox(height: 8),
        LinearProgressIndicator(value: progress?.fraction),
        const SizedBox(height: 8),
        Align(
          alignment: Alignment.centerRight,
          child: TextButton(
            onPressed: () => engine.cancelInstall(game.id),
            child: Text(l10n.installCancel),
          ),
        ),
        Text(
          l10n.installFirstStart,
          style: theme.textTheme.bodySmall?.copyWith(
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
      ],
    );
  }
}

class _Ended extends StatelessWidget {
  const _Ended({
    required this.game,
    required this.engine,
    required this.progress,
  });

  final GameInfo game;
  final EngineController engine;
  final InstallProgressChanged progress;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final cancelled = progress.phase == InstallPhase.cancelled;
    final text = cancelled
        ? l10n.installCancelled
        : switch (progress.errorCode) {
            'download_failed' => l10n.installErrorDownload,
            'bad_checksum' => l10n.installErrorChecksum,
            'loader_conflict' => l10n.installErrorConflict,
            'not_writable' => l10n.installErrorNotWritable,
            'unsupported_build' => l10n.installErrorUnsupportedBuild,
            'bad_signature' ||
            'manifest_rollback' ||
            'bad_manifest' => l10n.installErrorSignature,
            'launcher_too_old' => l10n.installErrorLauncherOld,
            'manifest_unconfigured' => l10n.installErrorNotConfigured,
            _ => l10n.installErrorOther(progress.message),
          };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (!cancelled)
          Text(
            l10n.installFailedTitle,
            style: theme.textTheme.bodyMedium?.copyWith(
              color: theme.colorScheme.error,
            ),
          ),
        Semantics(
          liveRegion: true,
          child: Text(
            text,
            style: theme.textTheme.bodySmall?.copyWith(
              color: cancelled
                  ? theme.colorScheme.onSurfaceVariant
                  : theme.colorScheme.error,
            ),
          ),
        ),
        const SizedBox(height: 8),
        Wrap(
          spacing: 8,
          children: [
            FilledButton(
              onPressed: engine.canLaunch
                  ? () => engine.installGame(game.id)
                  : null,
              child: Text(l10n.installAction),
            ),
            TextButton(
              onPressed: () => engine.dismissInstall(game.id),
              child: Text(l10n.installDismiss),
            ),
          ],
        ),
      ],
    );
  }
}

class _Idle extends StatelessWidget {
  const _Idle({
    required this.game,
    required this.engine,
    required this.install,
  });

  final GameInfo game;
  final EngineController engine;
  final GameInstall install;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final canAct =
        engine.canLaunch &&
        install.filesAvailable &&
        game.state != GameState.preparing;
    final note = TextStyle(color: theme.colorScheme.onSurfaceVariant);
    switch (install.status) {
      case InstallStatus.gameMissing:
        return Text(l10n.installGameMissing, style: note);
      case InstallStatus.unsupportedBuild:
        return Text(
          l10n.installUnsupportedBuild(install.gameBuild ?? ''),
          style: note,
        );
      case InstallStatus.needsInstall:
      case InstallStatus.outdated:
        final update = install.status == InstallStatus.outdated;
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              update
                  ? l10n.installOutdated(game.name)
                  : l10n.installNeeds(game.name),
              style: note,
            ),
            if (!install.filesAvailable) ...[
              const SizedBox(height: 8),
              Text(
                l10n.installFilesMissing(game.name),
                style: TextStyle(color: theme.colorScheme.error),
              ),
            ],
            const SizedBox(height: 12),
            Align(
              alignment: Alignment.centerRight,
              child: FilledButton(
                onPressed: canAct ? () => engine.installGame(game.id) : null,
                child: Text(
                  update ? l10n.installUpdateAction : l10n.installAction,
                ),
              ),
            ),
            if (!engine.canLaunch)
              Text(l10n.installBusy, style: theme.textTheme.bodySmall),
          ],
        );
      case InstallStatus.installed:
      case InstallStatus.unknown:
        return const SizedBox.shrink();
    }
  }
}

/// In the game's settings: says the support is installed and removes it.
class InstalledTile extends StatelessWidget {
  const InstalledTile({required this.game, super.key});

  final GameInfo game;

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    final install = game.install;
    if (install == null ||
        (install.status != InstallStatus.installed &&
            install.status != InstallStatus.outdated &&
            install.status != InstallStatus.unsupportedBuild)) {
      return const SizedBox.shrink();
    }
    final busy = install.busy || engine.installProgress(game.id) != null;
    final version = install.version;
    return ListTile(
      contentPadding: EdgeInsets.zero,
      title: Text(l10n.installTitle),
      subtitle: Text(
        '${version == null ? l10n.installInstalled : l10n.installInstalledVersion(version)}\n${l10n.installRemoveHelp}',
      ),
      isThreeLine: true,
      trailing: OutlinedButton(
        onPressed: engine.canLaunch && !busy
            ? () => engine.uninstallGame(game.id)
            : null,
        child: Text(l10n.installRemove),
      ),
    );
  }
}
