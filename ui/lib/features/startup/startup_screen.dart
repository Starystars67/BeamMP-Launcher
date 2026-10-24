import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../app/brand.dart';
import '../../core/engine/engine_controller.dart';
import '../../l10n/app_localizations.dart';

/// Shown until the engine is ready, and when it fails.
class StartupScreen extends StatelessWidget {
  const StartupScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    return ListenableBuilder(
      listenable: engine,
      builder: (context, _) {
        final connection = engine.connection;
        return Scaffold(
          body: Center(
            child: SingleChildScrollView(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 560),
                child: Padding(
                  padding: const EdgeInsets.all(32),
                  child: switch (connection) {
                    EngineFailed() => _Failure(
                      failed: connection,
                      onRetry: engine.start,
                    ),
                    EngineOnline(:final state, :final detail)
                        when state == EngineState.fatal ||
                            state == EngineState.error =>
                      _Failure(
                        failed: EngineFailed(FailureKind.fatal, detail),
                        onRetry: engine.start,
                      ),
                    _ => _Progress(connection: connection),
                  },
                ),
              ),
            ),
          ),
        );
      },
    );
  }
}

class _Progress extends StatelessWidget {
  const _Progress({required this.connection});

  final EngineConnection connection;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final message = switch (connection) {
      EngineOnline(:final state) when state == EngineState.updating =>
        l10n.startupUpdating,
      EngineOnline(:final state) when state == EngineState.preparingGame =>
        l10n.startupPreparing,
      _ => l10n.startupConnecting,
    };
    final detail = connection is EngineOnline
        ? (connection as EngineOnline).detail
        : '';
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const BeamMpLogo(width: 220),
        const SizedBox(height: 8),
        Text(
          l10n.startupTitle,
          style: Theme.of(context).textTheme.headlineMedium,
        ),
        const SizedBox(height: 24),
        const SizedBox(
          width: 40,
          height: 40,
          child: CircularProgressIndicator(),
        ),
        const SizedBox(height: 24),
        Semantics(
          liveRegion: true,
          child: Text(message, textAlign: TextAlign.center),
        ),
        if (detail.isNotEmpty) ...[
          const SizedBox(height: 8),
          Text(
            detail,
            textAlign: TextAlign.center,
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ],
      ],
    );
  }
}

class _Failure extends StatefulWidget {
  const _Failure({required this.failed, required this.onRetry});

  final EngineFailed failed;
  final Future<void> Function() onRetry;

  @override
  State<_Failure> createState() => _FailureState();
}

class _FailureState extends State<_Failure> {
  bool _details = false;

  String _message(AppLocalizations l10n) => switch (widget.failed.kind) {
    FailureKind.notFound => l10n.startupFailedNotFound,
    FailureKind.incompatible => l10n.startupFailedIncompatible,
    FailureKind.lost => l10n.startupFailedLost,
    FailureKind.fatal =>
      widget.failed.message.isEmpty
          ? l10n.startupFailedLost
          : widget.failed.message,
    FailureKind.startFailed => l10n.startupFailedLost,
  };

  /// The raw reason of a failed start belongs in the details, not the headline.
  List<String> get _detailLines => [
    if (widget.failed.kind == FailureKind.startFailed &&
        widget.failed.message.isNotEmpty)
      widget.failed.message,
    ...widget.failed.output,
  ];

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final output = _detailLines;
    final text = output.join('\n');
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Center(child: BeamMpLogo(width: 180)),
        const SizedBox(height: 8),
        Row(
          children: [
            Icon(Icons.error_outline, color: theme.colorScheme.error, size: 32),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                l10n.startupFailedTitle,
                style: theme.textTheme.headlineSmall,
              ),
            ),
          ],
        ),
        const SizedBox(height: 16),
        Semantics(liveRegion: true, child: Text(_message(l10n))),
        const SizedBox(height: 24),
        Wrap(
          spacing: 12,
          children: [
            FilledButton(
              onPressed: widget.onRetry,
              child: Text(l10n.startupRetry),
            ),
            if (output.isNotEmpty)
              OutlinedButton(
                onPressed: () => setState(() => _details = !_details),
                child: Text(
                  _details ? l10n.startupHideDetails : l10n.startupShowDetails,
                ),
              ),
            if (output.isNotEmpty)
              TextButton(
                onPressed: () => Clipboard.setData(ClipboardData(text: text)),
                child: Text(l10n.startupCopyDetails),
              ),
          ],
        ),
        if (_details && output.isNotEmpty) ...[
          const SizedBox(height: 16),
          Container(
            constraints: const BoxConstraints(maxHeight: 220),
            width: double.infinity,
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: theme.colorScheme.surfaceContainerHighest,
              borderRadius: BorderRadius.circular(8),
            ),
            child: SingleChildScrollView(
              child: SelectableText(
                text,
                style: const TextStyle(fontFamily: 'Menlo', fontSize: 12),
              ),
            ),
          ),
        ],
      ],
    );
  }
}
