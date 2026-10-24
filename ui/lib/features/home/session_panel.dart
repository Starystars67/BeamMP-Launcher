import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:launcher_control/launcher_control.dart';

import '../../app/app_services.dart';
import '../../core/engine/engine_controller.dart';
import '../../l10n/app_localizations.dart';

/// The room codes use letters and digits without look-alikes (no 0 O 1 I L).
const String roomCodeCharacters = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';
const int roomCodeLength = 6;

bool isRoomCode(String code) =>
    code.length == roomCodeLength &&
    code.split('').every(roomCodeCharacters.contains);

/// "Play together": host a relay room or join a friend's with its code, and
/// who is in the room once there. Shown under a game that supports it.
class SessionPanel extends StatelessWidget {
  const SessionPanel({required this.game, super.key});

  final GameInfo game;

  @override
  Widget build(BuildContext context) {
    final engine = AppScope.of(context).engine;
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final session = engine.session;
    final mine = session.game == game.id;
    final elsewhere = session.active && !mine;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Semantics(
              header: true,
              child: Text(
                l10n.sessionTitle,
                style: theme.textTheme.titleMedium,
              ),
            ),
            const SizedBox(height: 12),
            if (elsewhere)
              Text(l10n.sessionOtherGame(session.game))
            else if (mine && session.state == SessionState.connecting)
              _Connecting(engine: engine)
            else if (mine && session.state == SessionState.inRoom)
              _InRoom(session: session, engine: engine)
            else
              _Idle(game: game, engine: engine, session: session),
          ],
        ),
      ),
    );
  }
}

class _Idle extends StatelessWidget {
  const _Idle({
    required this.game,
    required this.engine,
    required this.session,
  });

  final GameInfo game;
  final EngineController engine;
  final SessionInfo session;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final problem = engine.sessionProblem;
    final notice = switch (problem) {
      SessionRefused(:final message) => message,
      SessionNoAnswer() => l10n.sessionNoAnswer,
      null => session.game == game.id ? session.errorMessage : null,
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Wrap(
          spacing: 12,
          runSpacing: 8,
          children: [
            FilledButton(
              onPressed: () => _askToHost(context, engine, game),
              child: Text(l10n.sessionHost),
            ),
            OutlinedButton(
              onPressed: () => _askToJoin(context, engine, game),
              child: Text(l10n.sessionJoin),
            ),
          ],
        ),
        if (notice != null && notice.isNotEmpty) ...[
          const SizedBox(height: 8),
          Semantics(
            liveRegion: true,
            child: Text(
              notice,
              style: theme.textTheme.bodySmall?.copyWith(
                color: theme.colorScheme.error,
              ),
            ),
          ),
        ],
      ],
    );
  }
}

class _Connecting extends StatelessWidget {
  const _Connecting({required this.engine});

  final EngineController engine;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return Row(
      children: [
        const SizedBox(
          width: 20,
          height: 20,
          child: CircularProgressIndicator(strokeWidth: 2),
        ),
        const SizedBox(width: 12),
        Expanded(
          child: Semantics(
            liveRegion: true,
            child: Text(l10n.sessionConnecting),
          ),
        ),
        OutlinedButton(
          onPressed: engine.leaveRoom,
          child: Text(l10n.sessionCancel),
        ),
      ],
    );
  }
}

class _InRoom extends StatelessWidget {
  const _InRoom({required this.session, required this.engine});

  final SessionInfo session;
  final EngineController engine;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    final theme = Theme.of(context);
    final code = session.code ?? '';
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(l10n.sessionRoomCode, style: theme.textTheme.labelLarge),
            const SizedBox(width: 12),
            SelectableText(
              code,
              style: theme.textTheme.titleLarge?.copyWith(
                fontFamily: 'Menlo',
                letterSpacing: 2,
              ),
            ),
            IconButton(
              tooltip: l10n.sessionCopyCode,
              icon: const Icon(Icons.copy),
              onPressed: () async {
                final messenger = ScaffoldMessenger.of(context);
                await Clipboard.setData(ClipboardData(text: code));
                messenger.showSnackBar(
                  SnackBar(content: Text(l10n.sessionCodeCopied)),
                );
              },
            ),
          ],
        ),
        Text(
          session.isHost ? l10n.sessionYouAreHost : l10n.sessionYouJoined,
          style: theme.textTheme.bodySmall?.copyWith(
            color: theme.colorScheme.onSurfaceVariant,
          ),
        ),
        const SizedBox(height: 12),
        Semantics(
          header: true,
          child: Text(
            l10n.sessionPlayers(session.peers.length),
            style: theme.textTheme.labelLarge,
          ),
        ),
        for (final peer in session.peers)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 2),
            child: Row(
              children: [
                Icon(
                  peer.id == session.hostId ? Icons.star : Icons.person,
                  size: 16,
                  color: theme.colorScheme.onSurfaceVariant,
                ),
                const SizedBox(width: 8),
                Flexible(child: Text(peer.name)),
                if (peer.id == session.hostId)
                  Text(
                    ' (${l10n.sessionPlayerHost})',
                    style: theme.textTheme.bodySmall,
                  ),
                if (peer.id == session.selfId)
                  Text(
                    ' (${l10n.sessionPlayerYou})',
                    style: theme.textTheme.bodySmall,
                  ),
              ],
            ),
          ),
        const SizedBox(height: 12),
        OutlinedButton(
          onPressed: engine.leaveRoom,
          child: Text(l10n.sessionLeave),
        ),
      ],
    );
  }
}

Future<void> _askToHost(
  BuildContext context,
  EngineController engine,
  GameInfo game,
) async {
  final result = await showDialog<({int? maxPlayers, String password})>(
    context: context,
    builder: (_) => const _HostDialog(),
  );
  if (result != null) {
    await engine.createRoom(
      game.id,
      maxPlayers: result.maxPlayers,
      password: result.password,
    );
  }
}

Future<void> _askToJoin(
  BuildContext context,
  EngineController engine,
  GameInfo game,
) async {
  final result = await showDialog<({String code, String password})>(
    context: context,
    builder: (_) => const _JoinDialog(),
  );
  if (result != null) {
    await engine.joinRoom(game.id, result.code, password: result.password);
  }
}

class _HostDialog extends StatefulWidget {
  const _HostDialog();

  @override
  State<_HostDialog> createState() => _HostDialogState();
}

class _HostDialogState extends State<_HostDialog> {
  final TextEditingController _players = TextEditingController(text: '8');
  final TextEditingController _password = TextEditingController();

  @override
  void dispose() {
    _players.dispose();
    _password.dispose();
    super.dispose();
  }

  int? get _count => int.tryParse(_players.text.trim());
  bool get _valid => _count != null && _count! >= 2 && _count! <= 32;

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return AlertDialog(
      title: Text(l10n.sessionHostTitle),
      content: SizedBox(
        width: 360,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: _players,
              keyboardType: TextInputType.number,
              inputFormatters: [FilteringTextInputFormatter.digitsOnly],
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                labelText: l10n.sessionMaxPlayers,
                helperText: l10n.sessionMaxPlayersHelp,
                errorText: _valid ? null : l10n.sessionMaxPlayersHelp,
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _password,
              obscureText: true,
              maxLength: 64,
              decoration: InputDecoration(labelText: l10n.sessionPassword),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(l10n.sessionCancel),
        ),
        FilledButton(
          onPressed: _valid
              ? () => Navigator.of(
                  context,
                ).pop((maxPlayers: _count, password: _password.text))
              : null,
          child: Text(l10n.sessionCreate),
        ),
      ],
    );
  }
}

/// Upper case, only the characters a code can have.
class _CodeFormatter extends TextInputFormatter {
  @override
  TextEditingValue formatEditUpdate(
    TextEditingValue oldValue,
    TextEditingValue newValue,
  ) {
    final text = newValue.text
        .toUpperCase()
        .split('')
        .where(roomCodeCharacters.contains)
        .take(roomCodeLength)
        .join();
    return TextEditingValue(
      text: text,
      selection: TextSelection.collapsed(offset: text.length),
    );
  }
}

class _JoinDialog extends StatefulWidget {
  const _JoinDialog();

  @override
  State<_JoinDialog> createState() => _JoinDialogState();
}

class _JoinDialogState extends State<_JoinDialog> {
  final TextEditingController _code = TextEditingController();
  final TextEditingController _password = TextEditingController();

  @override
  void dispose() {
    _code.dispose();
    _password.dispose();
    super.dispose();
  }

  void _submit() {
    if (isRoomCode(_code.text)) {
      Navigator.of(context).pop((code: _code.text, password: _password.text));
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = AppLocalizations.of(context);
    return AlertDialog(
      title: Text(l10n.sessionJoinTitle),
      content: SizedBox(
        width: 360,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: _code,
              autofocus: true,
              autocorrect: false,
              enableSuggestions: false,
              textCapitalization: TextCapitalization.characters,
              inputFormatters: [_CodeFormatter()],
              onChanged: (_) => setState(() {}),
              onSubmitted: (_) => _submit(),
              style: const TextStyle(fontFamily: 'Menlo', letterSpacing: 2),
              decoration: InputDecoration(
                labelText: l10n.sessionCode,
                helperText: l10n.sessionCodeHelp,
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _password,
              obscureText: true,
              maxLength: 64,
              decoration: InputDecoration(labelText: l10n.sessionPassword),
              onSubmitted: (_) => _submit(),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(l10n.sessionCancel),
        ),
        FilledButton(
          onPressed: isRoomCode(_code.text) ? _submit : null,
          child: Text(l10n.sessionJoinAction),
        ),
      ],
    );
  }
}
