import 'dart:async';

import 'package:flutter/material.dart';

import '../../app/app_info.dart';
import '../../app/app_services.dart';
import '../../core/settings/launcher_settings.dart';
import '../../l10n/app_localizations.dart';

class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final services = AppScope.of(context);
    final l10n = AppLocalizations.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l10n.settingsTitle)),
      body: ListenableBuilder(
        listenable: services.settings,
        builder: (context, _) {
          final settings = services.settings.settings;
          final trayAvailable = services.behaviour?.trayAvailable ?? false;
          void change(LauncherSettings Function(LauncherSettings) update) =>
              unawaited(services.settings.update(update));
          return ListView(
            padding: const EdgeInsets.symmetric(vertical: 8),
            children: [
              _Section(l10n.settingsAppearance),
              _ChoiceTile(
                title: l10n.settingsTheme,
                child: DropdownButton<ThemeSetting>(
                  isExpanded: true,
                  value: settings.theme,
                  onChanged: (value) => change((s) => s.copyWith(theme: value)),
                  items: [
                    DropdownMenuItem(
                      value: ThemeSetting.system,
                      child: Text(l10n.settingsThemeSystem),
                    ),
                    DropdownMenuItem(
                      value: ThemeSetting.light,
                      child: Text(l10n.settingsThemeLight),
                    ),
                    DropdownMenuItem(
                      value: ThemeSetting.dark,
                      child: Text(l10n.settingsThemeDark),
                    ),
                  ],
                ),
              ),
              _Section(l10n.settingsWindow),
              SwitchListTile(
                title: Text(l10n.settingsTray),
                subtitle: trayAvailable
                    ? null
                    : Text(l10n.settingsTrayUnavailable),
                value: settings.trayEnabled && trayAvailable,
                onChanged: trayAvailable
                    ? (value) => change((s) => s.copyWith(trayEnabled: value))
                    : null,
              ),
              _ChoiceTile(
                title: l10n.settingsLaunchBehavior,
                child: DropdownButton<LaunchWindowBehavior>(
                  isExpanded: true,
                  value: settings.launchWindowBehavior,
                  onChanged: (value) =>
                      change((s) => s.copyWith(launchWindowBehavior: value)),
                  items: [
                    DropdownMenuItem(
                      value: LaunchWindowBehavior.auto,
                      child: Text(l10n.settingsLaunchAuto),
                    ),
                    DropdownMenuItem(
                      value: LaunchWindowBehavior.hideToTray,
                      child: Text(l10n.settingsLaunchHide),
                    ),
                    DropdownMenuItem(
                      value: LaunchWindowBehavior.stayOpen,
                      child: Text(l10n.settingsLaunchStay),
                    ),
                    DropdownMenuItem(
                      value: LaunchWindowBehavior.minimise,
                      child: Text(l10n.settingsLaunchMinimise),
                    ),
                  ],
                ),
              ),
              _ChoiceTile(
                title: l10n.settingsCloseBehavior,
                child: DropdownButton<CloseBehavior>(
                  isExpanded: true,
                  value: settings.closeBehavior,
                  onChanged: (value) =>
                      change((s) => s.copyWith(closeBehavior: value)),
                  items: [
                    DropdownMenuItem(
                      value: CloseBehavior.quit,
                      child: Text(l10n.settingsCloseQuit),
                    ),
                    DropdownMenuItem(
                      value: CloseBehavior.closeToTray,
                      enabled: trayAvailable && settings.trayEnabled,
                      child: Text(l10n.settingsCloseToTray),
                    ),
                  ],
                ),
              ),
              SwitchListTile(
                title: Text(l10n.settingsRestoreWindow),
                value: settings.restoreWindowOnGameExit,
                onChanged: (value) =>
                    change((s) => s.copyWith(restoreWindowOnGameExit: value)),
              ),
              _Section(l10n.settingsNotifications),
              SwitchListTile(
                title: Text(l10n.settingsNotificationsToggle),
                value: settings.notificationsEnabled,
                onChanged: (value) =>
                    change((s) => s.copyWith(notificationsEnabled: value)),
              ),
              _Section(l10n.settingsAdvanced),
              const _ShowLogFile(),
              _Section(l10n.settingsAbout),
              const _About(),
            ],
          );
        },
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
    child: Semantics(
      header: true,
      child: Text(
        title,
        style: Theme.of(context).textTheme.titleSmall?.copyWith(
          color: Theme.of(context).colorScheme.primary,
        ),
      ),
    ),
  );
}

class _ShowLogFile extends StatelessWidget {
  const _ShowLogFile();

  @override
  Widget build(BuildContext context) {
    final services = AppScope.of(context);
    final l10n = AppLocalizations.of(context);
    final path = services.engineLogPath;
    final revealer = services.revealer;
    return ListTile(
      title: Text(l10n.settingsShowLogFile),
      subtitle: path == null
          ? null
          : Text(path, maxLines: 1, overflow: TextOverflow.ellipsis),
      trailing: const Icon(Icons.folder_open),
      enabled: path != null && revealer != null,
      onTap: path == null || revealer == null
          ? null
          : () async {
              final messenger = ScaffoldMessenger.of(context);
              if (!await revealer.reveal(path)) {
                messenger.showSnackBar(
                  SnackBar(content: Text(l10n.settingsShowLogFileMissing)),
                );
              }
            },
    );
  }
}

class _About extends StatelessWidget {
  const _About();

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    return ListenableBuilder(
      listenable: engine,
      builder: (context, _) {
        final info = engine.engineInfo;
        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 4, 16, 24),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SelectableText(l10n.aboutLauncher(uiVersion)),
              if (info == null)
                Text(l10n.aboutEngineUnavailable)
              else ...[
                SelectableText(l10n.aboutEngine(info.engine)),
                SelectableText(l10n.aboutProtocol(info.version)),
              ],
            ],
          ),
        );
      },
    );
  }
}

/// A label with its drop-down underneath, so long choices never fight the label for width.
class _ChoiceTile extends StatelessWidget {
  const _ChoiceTile({required this.title, required this.child});

  final String title;
  final Widget child;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [Text(title), const SizedBox(height: 4), child],
    ),
  );
}
