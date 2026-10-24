# BeamMP Launcher control protocol, version 1

How a UI controls the launcher engine (`BeamMP-Launcher-Console`). Implemented in `src/Control/`, tested in `tests/`.

## Starting the engine

```
BEAMMP_CONTROL_TOKEN=<token> BeamMP-Launcher-Console --control-port <n> [options]
```

- `--control-port 0` picks a free port. `--control-port` implies `--headless` (no console output, log file only).
- The token is **only** read from the `BEAMMP_CONTROL_TOKEN` environment variable (at least 16 characters, use 32+ random bytes) and removed from the environment, so the game does not inherit it. It is never passed on the command line.
- Once listening, the engine prints one line on stdout: `{"control_port":51696}`. Nothing else is written to stdout.
- A controlled engine does not launch the game by itself and stays alive between games.
- With `--no-launch` (also set by `--dev`) `game.launch` fails with `launch_failed`.

Exit rules:

| Situation | Result |
|---|---|
| `engine.shutdown` accepted | Queued messages are flushed, exit code 0 |
| Controller disconnects, no game running | Exit code 0 |
| Controller disconnects during a game | Keeps serving the game, exits when it ends |
| Nobody completes the handshake within 30 s | Exit code 0 |
| Missing or short token | `fatal` log line, exit code 1 |

## Transport

- TCP on `127.0.0.1` only, exclusive bind. One client at a time: further connections are closed immediately.
- Newline-delimited JSON, UTF-8, one object per line, at most 1 MiB per line (4 KiB before the handshake).
- The engine drops, **without a reply**, any connection whose first message is not a valid `hello` with the right token, or that does not finish the handshake in 5 s. Web pages can reach loopback ports, so nothing is revealed to them.
- The control socket is not inherited by the game process.
- A client that stops reading never blocks the engine: when its queue (1024 messages) is full the oldest `log` events are dropped first, and the connection is closed only if nothing droppable is left.

## Messages

```json
{"v":1,"id":7,"type":"request","name":"game.launch","data":{"game":"beamng","args":[]}}
{"v":1,"id":7,"type":"response","ok":true,"data":{}}
{"v":1,"id":7,"type":"response","ok":false,"error":{"code":"game_not_ready","message":"..."}}
{"v":1,"type":"event","name":"game.state","data":{"game":"beamng","state":"running","pid":4242}}
```

- `id` is an integer chosen by the client; responses carry it. Requests are answered in order.
- `data` is always an object (or absent). Unknown fields are ignored; unknown events must be ignored by the client.
- A malformed line after the handshake gets `{"ok":false,"id":null,"error":{"code":"bad_request",...}}` and the connection stays open.
- Secrets never cross this channel except the token in `hello`.

## Handshake

```json
{"v":1,"id":1,"type":"request","name":"hello","data":{"token":"...","client":"beammp-ui/0.1.0","versions":[1]}}
{"v":1,"id":1,"type":"response","ok":true,"data":{"version":1,"engine":"2.8.1","capabilities":["logs","games","account","signin","game_messages"],"games":["beamng","gearblocks"]}}
```

The response is always the first message after the request. If no offered version is supported the engine replies `incompatible_protocol` and closes ("update the launcher").

Right after the response the engine sends the current `engine.state`, `account.state` and `game.state` events, so a client never needs to poll to learn the starting situation. A repeated `hello` gets `already_authenticated`.

## Requests

| Name | Data | Response data |
|---|---|---|
| `engine.state` | | `{state, detail, status}` (below) |
| `engine.shutdown` | `{force?: bool}` | `{}`. `game_running` while a game runs unless `force` |
| `log.subscribe` | `{level?: "debug"\|"info"\|"warn"\|"error"\|"fatal" (default "info"), backlog?: 0..1000 (default 200)}` | `{level, sent}`; the backlog arrives as `log` events before the response |
| `log.unsubscribe` | | `{}` |
| `games.list` | | `{games: [{id, name, state, pid?}]}` |
| `game.launch` | `{game, args?: string[]}` (at most 64 arguments of 4096 characters) | `{}` |
| `game.stop` | `{game}` | `{}` (terminates the game process; `game_not_running` unless that game is the one running) |
| `game.configure` | `{game, directory?, command?, user_path?}` (strings, at most 4096 characters; what is left out is cleared, so always send all of it) | `{}`. Sets where the game is and how it starts, then prepares it again on a worker: its state goes `preparing`, then `ready`, `not_found` or `error` as `game.state` events. `game_not_ready` while the engine is not `ready` (a game starts or runs) or the game is being prepared |
| `game.install` | `{game}` | `{}`. Sets up the game's multiplayer support (GearBlocks: BepInEx, the BeamMP plugin and mod) on a worker; progress and the end follow as `game.install` events, then the game is prepared again. See "Installing multiplayer support" |
| `game.uninstall` | `{game, remove_loader?: bool}` | `{}`. Takes it out again (the loader only if BeamMP installed it and `remove_loader` is set); same events |
| `game.install_cancel` | `{game}` | `{}`. Stops a running install at the next chance (it ends as `cancelled`); nothing to do when none runs |
| `game.send` | `{game, message: object}` | `{}`. Hands a message to the running game's mod (see "Game messages") |
| `session.create` | `{game, max_players?: 2..32, password?: string (at most 64)}` | `{}`. Connects to the relay and creates a room, the player is its host; progress follows as `session.state` events |
| `session.join` | `{game, code, password?}` | `{}`. Joins the room with that 6 character code |
| `session.leave` | | `{}`. Leaves the room and the relay (nothing to do when not in one) |
| `session.state` | | the current session (below) |
| `account.state` | | `{signed_in, username?, role?, id?}` |
| `account.sign_in` | | `{}`; progress follows as `account.signin` events and the result as `account.state` |
| `account.cancel_sign_in` | | `{}` |
| `account.sign_out` | | the new account state; forgets the saved sign-in and revokes it on the server |

`status` in `engine.state`: `{account, ping, upload_status, mod_status, game_pid}`. `upload_status` and `mod_status` are the strings the launcher already exchanges with the in-game mod.

A subscription is per connection and starts unsubscribed. Per request, `data` is validated: wrong types give `invalid_argument`.

### Errors

| Code | Meaning |
|---|---|
| `bad_request` | Not a valid request line |
| `unknown_request` | No such request name |
| `already_authenticated` | `hello` sent twice |
| `incompatible_protocol` | No common version (connection closes) |
| `invalid_argument` | A field has the wrong type or range |
| `unknown_game` | `game` is missing or not a known id |
| `game_unavailable` | The game is listed but has nothing to launch (no support yet), or its preparation failed (the message says why) |
| `game_not_ready` | (also) `game.configure`, `game.install` and `game.uninstall` while a game starts or runs, or the game is already being prepared |
| `game_busy` | `game.launch` while multiplayer support is being installed or removed |
| `install_busy` | `game.install` or `game.uninstall` while one runs already |
| `install_unsupported` | `game.install` for a game that has nothing to install (BeamNG) |
| `game_not_found` | The game is not installed (or its mod/plugin is not); the message says what is missing |
| `game_not_ready` | Launch attempted outside the `ready` state (including a second launch in progress) |
| `game_running` | `engine.shutdown` without `force` during a game |
| `game_not_running` | `game.stop` without a launching or running game |
| `game_not_connected` | `game.send` while the game's mod has not connected (or is gone) |
| `launch_failed` | The game could not be started; the engine is back to `ready` and `engine.state.detail` has the reason |
| `stop_failed` | The game process could not be signalled |
| `internal_error` | Unexpected failure in the engine |

## Events

| Name | Data |
|---|---|
| `engine.state` | `{state, detail}` |
| `game.state` | `{game, state, pid?, detail?}` |
| `game.install` | `{game, state: running\|done\|failed\|cancelled, step, message, fraction?, uninstall?, error?: {code, message}}` while an install or uninstall runs and when it ends (below) |
| `account.state` | `{signed_in, username?, role?, id?}` |
| `log` | `{ts, level, message}`, `ts` is ISO 8601 UTC with milliseconds |
| `session.state` | `{game, state: idle\|connecting\|in_room, role?: host\|guest, code?, self_id?, host_id?, peers?: [{id, name}], error?: {code, message}}`, also sent to a controller that attaches |
| `game.message` | `{game, message: object}`: a message the game's mod sent (below). Droppable, like `log` |
| `account.signin` | `{state, url?, reason?}` while a sign-in runs (below) |
| `fatal` | `{code, message}`, sent just before the engine exits after a fatal error |

`progress` (mod download and similar) is reserved and not sent yet.

### Signing in

The password never reaches the launcher. `account.sign_in` makes the engine open the BeamMP sign-in page in the system browser (an authorization code flow with PKCE and a loopback redirect, RFC 8252) unless it was started with `--no-browser`, in which case the controller opens the `url` of the first event itself.

`account.signin` states:

| `state` | Meaning |
|---|---|
| `waiting_for_browser` | `url` is the page to open; waits up to 5 minutes |
| `exchanging` | The browser came back, tokens and the game session are being fetched |
| `failed` | `reason`: `cancelled`, `timeout`, `denied`, `network`, `server`, `rejected`, `expired`, `internal` |
| `idle` | Finished successfully; `account.state` carries the result |

A controller that attaches while a sign-in is waiting is sent the current `account.signin` right after the handshake. A successful sign-in, a restored one, and `account.sign_out` all produce `account.state`.

The refresh token of the sign-in is kept in the operating system's secret store (the macOS keychain; other platforms have no backend yet and ask for a sign-in at every start). The next start restores it without the browser; a restore that fails produces `account.signin` with state `failed` and reason `expired` (the saved sign-in ended) or `network` (it is kept for next time). The game keys are renewed from this sign-in before every server join; the old password path of the game keeps working next to it.

Errors of `account.sign_in`: `sign_in_busy`, `already_signed_in`, `sign_in_unavailable`. Asking while a saved sign-in is being restored waits for the restore.

For development the engine reads `BEAMMP_API_URL` and `BEAMMP_ACCOUNTS_URL` (https, or http on a loopback host only) and `BEAMMP_KEYCHAIN_SERVICE`.

### Engine states

`starting` → `updating` (skipped with `--no-update`) → `preparing_game` → `ready` ⇄ `launching_game` → `game_running` → `ready`.
`error` is recoverable (back to `starting`, `preparing_game` or `ready`), `fatal` is final.

A launch that fails returns from `launching_game` straight to `ready` with the reason in `detail`.

### Game states

`unavailable` (listed, but nothing to launch yet), `not_found` (not installed, or its mod/plugin is not), `busy` (another game is starting or running, the engine runs one game at a time), `preparing`, `ready`, `launching`, `running`, `exited` (sent when a game that was launching or running ends, with `detail`, always followed by `ready`), `error` (preparing it failed). `games.list` and `game.state` carry a `detail` with the reason for `not_found` and `error`. `games.list` also carries `steam_app_id` for a game that has one (a UI can show the cover from the user's own Steam library cache; nothing is bundled or downloaded) and `config` (`directory`, `command`, `user_path`, only what the user set, never what was detected). A game that is prepared again after a change of its settings is `preparing`. A game id is a plain string (`beamng`); nothing in the protocol names game-specific concepts.

### Sessions (rooms at the relay)

When the engine has a relay (`--relay host:port` or `BEAMMP_RELAY`) the `hello` capabilities include `sessions` and the games whose modules support it have `sessions: true` in `games.list`. A player creates a **room** at the relay and gets a code, friends join with it (the relay: `relay/README.md`). The room is the player's, independent of whether the game runs; the game's mod is told about every change of it (a `{"type":"session","session":{...}}` message, and when it connects). The state `idle` carries `error` when a session ended or could not start: `relay_unreachable`, `connection_lost`, `room_closed` (the host left), or the relay's own codes (`room_not_found`, `bad_password`, `room_full`, `too_many_attempts`, ...). `peers` lists everybody in the room, the host first, the player included.

Game data: while a room exists for a game, its mod can send bytes to the other players and receives theirs, through the game bridge and never through the control channel (a user interface does not see them). Mod to engine `{"type":"data","kind":"host"|"all"|"peer"|"others","target":<peer id, for "peer">,"data":"<base64>"}`, engine to mod `{"type":"data","from":<peer id>,"kind":<as sent>,"data":"<base64>"}`; the kinds are the relay's (`relay/README.md`). The engine does not look inside; for GearBlocks the bytes are the game's remote calls (`docs/GEARBLOCKS_MULTIPLAYER.md`). Malformed data, or data without a room for that game, is dropped.

Errors of the requests: `session_unavailable` (no relay, or one that cannot be used: until the launcher has TLS only this machine and private networks are accepted without `--relay-insecure`), `session_busy` (already in a session), `session_unsupported` (the game has no rooms), `game_not_found`/`game_unavailable`, `invalid_argument` (bad code, number of players or password).

### Game messages

A game that has a message channel (`GearBlocks` has one through its BepInEx plugin, see `docs/GEARBLOCKS.md`) lets the controller and the game's mod talk through the engine: JSON objects only, `game.send` towards the mod and `game.message` events from it. The engine does not interpret them; they exist so the game-specific mod and a controller have a channel before the shared game protocol is in place. `game.state` is sent for every launchable game, each with its own state.

### Installing multiplayer support

A game whose module has an install (`GearBlocks`) has `install` in `games.list` (and the `hello` capability `game_install`): `{state, busy, version?, latest_version?, game_build?, files_available}`, where `state` is `installed`, `needs_install` (the game is there, BepInEx, the plugin or the mod is not), `outdated` (installed, but not the release the last verified manifest names for this game build, or changed on disk since), `unsupported_build` (the last verified manifest has no release for the game's build), `game_missing` (no `GearBlocks.exe` in the game's folder) or `unknown`; `busy` is true while an install or uninstall of that game runs. `version` is the release the launcher installed, `latest_version` the one the last verified manifest names. `files_available` is false when the launcher has nothing to get the files from (no manifest address or no trusted key, below). The state is worked out from what is on disk when the game is prepared and after an install; it does not use the network.

`game.install` runs on a worker and sends `game.install` events: `running` with the current `step` (`check`, `manifest`, `loader`, `download`, `unpack`, `plugin`, `mod`, `record`), a `message` in English and a `fraction` from 0 to 1 (these may be dropped when the controller is slow), then one last event that is never dropped: `done`, `failed` (with `error.code`: `manifest_unconfigured` (no manifest address or trusted key), `game_not_found`, `unsupported_build` (no release for the game's build), `launcher_too_old` (there is one, for a newer bridge protocol than this launcher speaks), `bad_signature` (the manifest is not signed by a trusted key; nothing from it is used), `manifest_rollback` (its sequence number is lower than one already seen), `bad_manifest`, `loader_conflict` (a `winhttp.dll` that is not BepInEx's is in the game's folder and is left alone), `download_failed`, `bad_checksum` (a download is not the file the signed manifest names and is discarded), `extract_failed`, `not_writable`, `io_error`, `not_ours` (uninstalling a loader BeamMP did not install)) or `cancelled`. When it has ended the game is prepared again, so `game.state` and `install` follow what is on disk. Only one install runs at a time; the game cannot be launched or reconfigured meanwhile. `game.install` is the two parts together: BepInEx when it is not there, then the sync below.

Where the files come from: nothing is built into the launcher. `--gearblocks-manifest <url>` (or `BEAMMP_GEARBLOCKS_MANIFEST`) is a signed manifest, `<url>.sig` its detached Ed25519 signature (one Base64 line per signature), and `--gearblocks-trust <file>` (or `BEAMMP_GEARBLOCKS_TRUST`) the public keys it may be signed by, `ed25519:<Base64 of the 32 byte key>` per line (several lines allow a key to be replaced). The manifest (`gearblocks/release.py` makes and signs it; `include/Games/GearBlocksManifest.h` is the format) names the BepInEx archive and, per release, the plugin DLL and a zip of the script mod with a SHA-256 each, the game builds the release was made for and the bridge protocol it speaks; it has a `sequence` that only goes up, so an old signed manifest that is served again is refused. Addresses are https (plain http to this machine only with `--gearblocks-allow-loopback-http`, for development). Nothing that is downloaded is used before the signature of the manifest and the checksum of the file are checked, and nothing in the game's folder is touched until everything of a sync has been downloaded and checked. The last verified manifest is kept in the game's folder (`beammp-manifest.json` and `.sig`, checked again against the trusted keys each time) so that what is installed can be checked without a network and for the replay check.

Two parts with different lives. BepInEx 6 (IL2CPP, win-x64, the pinned build in the manifest) is installed on the player's request (`game.install`) and again if a game update removes it. The plugin (`BepInEx/plugins/BeamMP.GearBlocks.dll`) and the script mod (`GearBlocks_Data/StreamingAssets/ScriptMods/BeamMP`) are synced: `game.install` does it, and so does every `game.launch` of GearBlocks while BepInEx is installed and a source is configured: the manifest is fetched (short timeouts), the highest release for the game's build that this launcher can talk to is chosen, and what is installed is compared with it by checksum (the plugin file, and every file of the mod as recorded); only what differs is downloaded and replaced. When the network or the manifest is not usable at a launch, what is installed runs as it is (a warning in the log). What was written is recorded in `beammp-install.json` in the game's folder, so uninstalling removes exactly that. The first start of the game after BepInEx is installed takes a few minutes longer: BepInEx generates its interop assemblies and fetches the Unity libraries it needs.

### Game settings

Per game (`beamng`, `gearblocks`): the game's folder, a start command, and (BeamNG) the user data folder. They are given at startup with `--game-dir <id>=<path>`, `--game-command <id>=<program>` and `--game-user-path <id>=<path>` (the UI passes its saved settings that way) and changed while running with `game.configure`. The start command is a program or script that starts the game instead of its own executable, which is how a game runs through Wine; it gets the user's launch arguments, the game folder in `<ID>_DIR` and `BEAMMP_GAME_DIR`, and the user folder in `BEAMMP_USER_PATH`. GearBlocks is `not_found` (with the reason) until its folder holds `GearBlocks.exe`, BepInEx and the BeamMP plugin (`game.install` puts them there); BeamNG until the folder holds `integrity.json`.

### Games are independent

Every game is a module (`docs/GAME_MODULES.md`) and the engine prepares each one at startup. A game that is missing or fails to prepare is reported as `not_found` or `error` for that game only: the engine still reaches `ready`, the other games can be launched and the account works. (A launcher without a controller is the old console launcher and still ends when BeamNG cannot be prepared.)

## Compatibility

Additive changes (new requests, events, fields, error codes, states) keep version 1. Anything else bumps `v`.
