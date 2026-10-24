import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../l10n/app_localizations.dart';

class LogsScreen extends StatefulWidget {
  const LogsScreen({super.key});

  @override
  State<LogsScreen> createState() => _LogsScreenState();
}

class _LogsScreenState extends State<LogsScreen> {
  final ScrollController _scroll = ScrollController();
  LogLevel _minimum = LogLevel.info;
  bool _follow = true;

  @override
  void dispose() {
    _scroll.dispose();
    super.dispose();
  }

  bool _shown(LogEntry entry) => _severity(entry.level) >= _severity(_minimum);

  static int _severity(LogLevel level) => switch (level) {
    LogLevel.debug => 0,
    LogLevel.info => 1,
    LogLevel.warn => 2,
    LogLevel.error || LogLevel.except => 3,
    LogLevel.fatal => 4,
  };

  String _levelName(AppLocalizations l10n, LogLevel level) => switch (level) {
    LogLevel.debug => l10n.logsLevelDebug,
    LogLevel.info => l10n.logsLevelInfo,
    LogLevel.warn => l10n.logsLevelWarn,
    _ => l10n.logsLevelError,
  };

  @override
  Widget build(BuildContext context) {
    final logs = AppScope.of(context).engine.logs;
    final l10n = AppLocalizations.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text(l10n.logsTitle),
        actions: [
          DropdownButtonHideUnderline(
            child: DropdownButton<LogLevel>(
              value: _minimum,
              onChanged: (level) =>
                  setState(() => _minimum = level ?? _minimum),
              items: [
                for (final level in [
                  LogLevel.debug,
                  LogLevel.info,
                  LogLevel.warn,
                  LogLevel.error,
                ])
                  DropdownMenuItem(
                    value: level,
                    child: Text(l10n.logsLevelLabel(_levelName(l10n, level))),
                  ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          FilterChip(
            label: Text(l10n.logsFollow),
            selected: _follow,
            onSelected: (value) => setState(() => _follow = value),
          ),
          IconButton(
            tooltip: l10n.logsClear,
            icon: const Icon(Icons.delete_sweep_outlined),
            onPressed: logs.clear,
          ),
          IconButton(
            tooltip: l10n.logsCopy,
            icon: const Icon(Icons.copy_all),
            onPressed: () async {
              final messenger = ScaffoldMessenger.of(context);
              final text = logs.entries.where(_shown).map(_line).join('\n');
              await Clipboard.setData(ClipboardData(text: text));
              messenger.showSnackBar(SnackBar(content: Text(l10n.logsCopied)));
            },
          ),
          const SizedBox(width: 8),
        ],
      ),
      body: ListenableBuilder(
        listenable: logs,
        builder: (context, _) {
          final entries = logs.entries.where(_shown).toList();
          if (entries.isEmpty) {
            return Center(child: Text(l10n.logsEmpty));
          }
          if (_follow) {
            WidgetsBinding.instance.addPostFrameCallback((_) {
              if (_scroll.hasClients) {
                _scroll.jumpTo(_scroll.position.maxScrollExtent);
              }
            });
          }
          return Column(
            children: [
              Expanded(
                child: Scrollbar(
                  controller: _scroll,
                  child: ListView.builder(
                    controller: _scroll,
                    padding: const EdgeInsets.symmetric(
                      horizontal: 16,
                      vertical: 8,
                    ),
                    itemCount: entries.length,
                    itemBuilder: (context, index) =>
                        _LogLine(entry: entries[index]),
                  ),
                ),
              ),
              Padding(
                padding: const EdgeInsets.all(8),
                child: Text(
                  l10n.logsCount(entries.length),
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

String _time(DateTime time) {
  final local = time.toLocal();
  String two(int value) => value.toString().padLeft(2, '0');
  return '${two(local.hour)}:${two(local.minute)}:${two(local.second)}';
}

String _line(LogEntry entry) =>
    '${_time(entry.time)} [${entry.level.wire.toUpperCase()}] ${entry.message}';

class _LogLine extends StatelessWidget {
  const _LogLine({required this.entry});

  final LogEntry entry;

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    final color = switch (entry.level) {
      LogLevel.debug => scheme.onSurfaceVariant,
      LogLevel.info => scheme.onSurface,
      LogLevel.warn =>
        scheme.brightness == Brightness.light
            ? const Color(0xFF8A4600)
            : Colors.orange.shade300,
      LogLevel.error || LogLevel.except || LogLevel.fatal => scheme.error,
    };
    return Semantics(
      label: _line(entry),
      excludeSemantics: true,
      child: SelectableText(
        _line(entry),
        style: TextStyle(
          fontFamily: 'Menlo',
          fontSize: 12,
          height: 1.5,
          color: color,
        ),
      ),
    );
  }
}
