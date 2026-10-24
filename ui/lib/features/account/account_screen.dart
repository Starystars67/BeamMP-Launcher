import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../app/app_services.dart';
import '../../core/engine/engine_controller.dart';
import '../../l10n/app_localizations.dart';

class AccountScreen extends StatelessWidget {
  const AccountScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l10n.accountTitle)),
      body: ListenableBuilder(
        listenable: engine,
        builder: (context, _) {
          final connection = engine.connection;
          final account = connection is EngineOnline
              ? connection.account
              : null;
          return ListView(
            padding: const EdgeInsets.all(24),
            children: [
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(24),
                  child: account != null && account.signedIn
                      ? _SignedIn(
                          username: account.username ?? '',
                          role: account.role,
                          onSignOut: engine.signOut,
                        )
                      : _SignedOut(engine: engine),
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _SignedIn extends StatelessWidget {
  const _SignedIn({
    required this.username,
    required this.role,
    required this.onSignOut,
  });

  final String username;
  final String? role;
  final Future<void> Function() onSignOut;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return Row(
      children: [
        const CircleAvatar(radius: 28, child: Icon(Icons.person)),
        const SizedBox(width: 16),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                l10n.accountSignedInAs(username),
                style: Theme.of(context).textTheme.titleMedium,
              ),
              if (role != null && role!.isNotEmpty)
                Text(l10n.accountRole(role!)),
            ],
          ),
        ),
        OutlinedButton(onPressed: onSignOut, child: Text(l10n.accountSignOut)),
      ],
    );
  }
}

class _SignedOut extends StatelessWidget {
  const _SignedOut({required this.engine});

  final EngineController engine;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final status = engine.signIn;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            const CircleAvatar(radius: 28, child: Icon(Icons.person_outline)),
            const SizedBox(width: 16),
            Expanded(
              child: Text(
                l10n.accountSignedOut,
                style: theme.textTheme.titleMedium,
              ),
            ),
          ],
        ),
        const SizedBox(height: 16),
        switch (status) {
          SignInIdle() => _Idle(engine: engine),
          SignInWaiting(:final url) => _Waiting(engine: engine, url: url),
          SignInExchanging() => Semantics(
            liveRegion: true,
            child: Row(
              children: [
                const SizedBox(
                  width: 20,
                  height: 20,
                  child: CircularProgressIndicator(strokeWidth: 2),
                ),
                const SizedBox(width: 12),
                Text(l10n.accountExchanging),
              ],
            ),
          ),
          SignInFailed(:final reason) => _Failed(
            engine: engine,
            reason: reason,
          ),
        },
      ],
    );
  }
}

class _Idle extends StatelessWidget {
  const _Idle({required this.engine});

  final EngineController engine;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          l10n.accountSignedOutHint,
          style: Theme.of(context).textTheme.bodySmall,
        ),
        const SizedBox(height: 16),
        FilledButton(
          onPressed: engine.startSignIn,
          child: Text(l10n.accountSignIn),
        ),
      ],
    );
  }
}

class _Waiting extends StatelessWidget {
  const _Waiting({required this.engine, required this.url});

  final EngineController engine;
  final String url;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final services = AppScope.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Semantics(
          liveRegion: true,
          child: Row(
            children: [
              const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  l10n.accountWaiting,
                  style: Theme.of(context).textTheme.titleSmall,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 8),
        Text(
          l10n.accountWaitingHint,
          style: Theme.of(context).textTheme.bodySmall,
        ),
        const SizedBox(height: 16),
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: [
            if (url.isNotEmpty && services.urlOpener != null)
              FilledButton(
                onPressed: () => unawaited(services.urlOpener!.open(url)),
                child: Text(l10n.accountOpenAgain),
              ),
            if (url.isNotEmpty)
              OutlinedButton(
                onPressed: () async {
                  final messenger = ScaffoldMessenger.of(context);
                  await Clipboard.setData(ClipboardData(text: url));
                  messenger.showSnackBar(
                    SnackBar(content: Text(l10n.accountLinkCopied)),
                  );
                },
                child: Text(l10n.accountCopyLink),
              ),
            TextButton(
              onPressed: engine.cancelSignIn,
              child: Text(l10n.accountCancel),
            ),
          ],
        ),
      ],
    );
  }
}

class _Failed extends StatelessWidget {
  const _Failed({required this.engine, required this.reason});

  final EngineController engine;
  final String reason;

  String _text(AppLocalizations l10n) => switch (reason) {
    'cancelled' => l10n.accountFailedCancelled,
    'timeout' => l10n.accountFailedTimeout,
    'denied' => l10n.accountFailedDenied,
    'network' => l10n.accountFailedNetwork,
    'server' => l10n.accountFailedServer,
    'rejected' => l10n.accountFailedRejected,
    'expired' => l10n.accountFailedExpired,
    _ => l10n.accountFailedInternal,
  };

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Semantics(
          liveRegion: true,
          child: Text(
            _text(l10n),
            style: theme.textTheme.bodyMedium?.copyWith(
              color: reason == 'cancelled' ? null : theme.colorScheme.error,
            ),
          ),
        ),
        const SizedBox(height: 16),
        Wrap(
          spacing: 12,
          children: [
            FilledButton(
              onPressed: engine.startSignIn,
              child: Text(l10n.accountTryAgain),
            ),
            TextButton(
              onPressed: engine.dismissSignIn,
              child: Text(l10n.accountCancel),
            ),
          ],
        ),
      ],
    );
  }
}
