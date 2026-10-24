# BeamMP Launcher UI - Design

Status: **design agreed in outline, nothing implemented yet.** Written 2026-10-01.
Items marked **(open)** are undecided; items marked **(verify)** are assumptions to confirm while implementing.

## 1. Goals

- A Flutter desktop UI that becomes "the BeamMP Launcher": Windows, macOS and Linux from day one.
- The console window disappears for normal use. The original console app stays available.
- Launching a game from the UI minimises the UI to the system tray, unless the user disables the tray, in which case the window stays open (or is minimised, by setting).
- The UI owns the account and login flow, so credentials never pass through the game process.
- The design must not assume BeamNG.drive: the launcher is intended to support more games later through a standard protocol and per-game adapters.

Non-goals for this phase (deferred, see section 13): update/distribution mechanism, a server browser in the UI, any change to the legacy in-game login endpoint.

## 2. Decisions so far

| Topic | Decision |
|---|---|
| Binaries | `BeamMP-Launcher` = Flutter UI. `BeamMP-Launcher-Console` = today's app, kept as the engine and as a standalone console tool. |
| Process model | The UI spawns and owns the engine. In UI mode the engine stays alive between games. |
| Platforms | Windows, macOS, Linux from the start. Tray features degrade gracefully where a platform lacks them. |
| Repo | UI lives in `ui/` inside this repository so protocol and versions move together. |
| Engine language | C++20, in the existing style (section 12). |
| Login | Moves into the launcher UI. The legacy in-game login stays untouched for the console launcher and old mod versions. |
| Updates | Bundled into launcher updates eventually, but out of scope for now. |

## 3. What the engine does today (from the code)

- One process. `main()` (src/main.cpp) runs linearly: `InitLog`, `ConfigInit`, `InitOptions`, `InitLauncher` (name check, saved-key check, update check), `LegitimacyCheck`, `HTTP::StartProxy`, `PreGame` (mod download), `InitGame`, then `CoreNetwork()`, which is `[[noreturn]]`.
- Launcher lifetime equals game lifetime. `StartGame` (src/GameStart.cpp) waits for the game process, logs "Game Closed! launcher closing soon", sleeps 5 s and calls `exit(2)`. `fatal()` sleeps 5 s then `exit(1)`. The in-game `QG` packet also calls `exit(2)`.
- Status is plain globals: `Username`, `UserRole`, `UserID`, `LoginAuth`, `ping`, `UlStatus`, `MStatus`, `ModLoaded`, `Terminate`, `TCPTerminate`, `GamePID`. They are written from several threads with no locking.
- Logging (src/Logger.cpp) writes to `std::wcout`/`std::cout` and re-opens and appends to `Launcher.log` on every call. No sink abstraction, no locking.
- The game talks to the engine over TCP on `127.0.0.1:<port>` (default 4444, `--port`). One game connection at a time. Packet codes handled in `Parse` (src/Network/Core.cpp): `A B C O P U M Q R Z N W I`. `N` carries the login fields, which are posted to `https://auth.beammp.com/userlogin`; the private key is kept in a plain `key` file.
- The console is never read from (no `std::cin`). Console touchpoints are few: `system("cls"/"clear")` in `main`, `SetConsoleTitleA`/`SetConsoleOutputCP`/`_setmode` in `InitLauncher` (Windows), and `ReLaunch`/`URelaunch`, which call `ShowWindow(GetConsoleWindow(), 0)`.
- The executable name is hard-coded: `GetEN()` returns `BeamMP-Launcher(.exe)` and `CheckName()` renames the running binary to that name and relaunches. See section 5.1.
- The repo has no tests. Build is CMake + vcpkg (cpp-httplib, nlohmann-json, zlib, openssl, curl), C++20, static runtime on Windows.
- Linux/macOS: the game is started with `posix_spawn` (stdout/stderr closed). Windows: `CreateProcessW`. The code explicitly handles running under Wine/Proton (`LinuxPatch`).

## 4. Process model

```
 BeamMP-Launcher (Flutter UI)                BeamMP-Launcher-Console (engine)
 ----------------------------                ---------------------------------
 spawns engine  ------------------------->   --headless --control-port 0
   env: BEAMMP_CONTROL_TOKEN=<random>        prints {"control_port":N} on stdout
 reads control_port from stdout  <---------
 connects 127.0.0.1:N, sends hello(token) -> validates token, one client only
 <------ events: engine.state, log, game.state, progress, account.state
 ------> requests: game.launch, game.stop, log.subscribe, engine.shutdown ...
                                             game <--TCP 127.0.0.1:4444--> engine
```

Modes of the engine binary:

| Mode | Flags | Behaviour |
|---|---|---|
| Console (default, unchanged) | none | Exactly today's behaviour, including exiting after the game closes. |
| Headless | `--headless` | No console output, logs to file only, no UI. For scripts, servers, Linux. Exits after the game closes unless a controller is attached. |
| Controlled | `--headless --control-port <n>` | UI mode. Stays alive between games, reports state over the control channel. |

Windows: the UI starts the console-subsystem engine with `CREATE_NO_WINDOW`, so the engine needs no GUI-subsystem build and no `AttachConsole` logic. The UI also receives engine stdout/stderr, which is useful for crashes before the control channel is up.

macOS: the engine is a helper executable inside the app bundle. Linux: installed next to the UI binary. The UI locates the engine relative to its own path.

### 4.1 Lifetime rules

- UI start: look for a running engine (section 4.2) and re-attach; otherwise spawn one.
- UI quit: stop the engine. If a game is running, confirm first.
- UI crash while a game runs: the engine keeps serving the game (the game must not be interrupted). It continues headless, logging to file, until the game exits, then exits. Restarting the UI re-attaches.
- Engine crash: the UI shows an error state with the log tail and a restart button; it does not kill a running game.

### 4.2 Single instance and re-attach

Only one launcher engine can exist per user (port 4444, `Launcher.log`, `key` file). Both binaries honour a per-user lock and runtime file:

- Lock + runtime file in the per-user runtime directory (`%LOCALAPPDATA%\BeamMP\run`, `$XDG_RUNTIME_DIR/beammp`, `~/Library/Application Support/BeamMP/run`), permissions restricted to the user.
- The runtime file holds `{pid, control_port, token}` and is deleted on clean exit. A stale file (pid not alive) is ignored.
- If a console launcher is already running, the UI says so and offers to close it. It never starts a second engine.

## 5. Engine changes (C++)

Each is a separate, reviewable change. The first two must not change behaviour.

### 5.1 Executable name handling

`GetEN()` and `CheckName()` assume the file is called `BeamMP-Launcher(.exe)`. A binary named `BeamMP-Launcher-Console` would be renamed over the UI's file. Make the name data-driven: derive it from the running executable, and skip `CheckName()` when the name is intentionally different. The update code that uses `GetEN()` is deferred but must not be broken meanwhile.

### 5.2 Options

Add to `Options` and the existing parser, plus `--help` text:

- `--headless`
- `--control-port <n>` (0 = pick an ephemeral port)
- token via `BEAMMP_CONTROL_TOKEN` (never argv: argv is visible to other users and in logs)

`Options` already records `argv` for relaunch; keep the new flags there so relaunch preserves them.

### 5.3 Logger sinks

Keep the free functions (`info`, `warn`, `error`, `debug`, `fatal`, `except`) so no call site changes. Behind them: a list of sinks (console, file, control stream), and a mutex around emission. The log file keeps its current open-append-per-line behaviour. `--headless` removes the console sink. `fatal()` in controlled mode emits a `fatal` event to the controller before the 5 s wait and exit.

### 5.4 Status snapshot

The status globals (`ping`, `UlStatus`, `MStatus`, `GamePID`) become `std::atomic` or a mutex-guarded `SharedString`, so existing call sites compile unchanged. The login fields (authenticated, username, role, id) move into one `Account` value read and written as a unit, so a reader never sees half a login. `GetStatusSnapshot()` returns a copy of everything (`include/Status.h`). `Terminate` and `ModLoaded` are control flow, not status, and are left alone for now. This is needed before a control thread starts reading them.

### 5.5 Lifecycle as stages

Split `main()` into named stages that report state, without changing their order or logic in console mode:

`Starting -> Updating (optional) -> PreparingGame -> Ready -> LaunchingGame -> GameRunning -> Ready ...`, with `Error` (recoverable) and `Fatal`.

- `InitGame` no longer launches implicitly in controlled mode: launching is a command. In console mode it launches immediately, as today.
- `StartGame` returns an exit status and notifies the state machine instead of calling `exit(2)`. In console mode the caller still waits 5 s and exits, preserving today's behaviour.
- Remove stray `exit()` calls from library-style code paths used in controlled mode (`QG` handling, `ReLaunch`, `URelaunch`), turning them into state transitions or events.
- `ReLaunch`/`URelaunch` stop touching the console window in headless mode.

### 5.6 Control server

New module under `src/Control/` + `include/Control/`.

- Loopback TCP on `127.0.0.1`, ephemeral port, exclusive bind; one client at a time (a second is refused).
- Newline-delimited JSON, UTF-8, 1 MiB maximum line.
- First message must be a valid `hello` carrying the token (constant-time comparison); anything else closes the connection. Browsers can reach loopback ports, so a non-JSON or unauthenticated first line is dropped immediately.
- One reader thread, one writer thread with a bounded queue (drop oldest log lines under backpressure, never block the engine).
- Unix domain sockets / Windows named pipes are a later hardening option **(open)**; loopback + token is the baseline because it works identically in C++ and Dart on all three platforms.
- JSON via nlohmann (already a dependency).

## 6. Control protocol (UI <-> engine)

The implemented, authoritative specification is `docs/CONTROL_PROTOCOL.md`; this section is the original design. Differences: `game.state` also has an `error` state, `engine.state` carries an `Updating`/`PreparingGame`/... state set, `log.unsubscribe` and `fatal`-state handling were added, `progress` events and `game.settings.*` are not implemented yet, and an unauthenticated peer is dropped silently instead of receiving an error.

Version 1. Every message is one JSON object per line:

```json
{"v":1,"id":7,"type":"request","name":"game.launch","data":{"game":"beamng","args":[]}}
{"v":1,"id":7,"type":"response","ok":true,"data":{}}
{"v":1,"id":7,"type":"response","ok":false,"error":{"code":"game_not_ready","message":"..."}}
{"v":1,"type":"event","name":"game.state","data":{"game":"beamng","state":"running","pid":4242}}
```

Handshake:

```json
{"v":1,"id":1,"type":"request","name":"hello","data":{"token":"...","client":"beammp-ui/0.1.0","versions":[1]}}
{"v":1,"id":1,"type":"response","ok":true,"data":{"version":1,"engine":"2.8.1","capabilities":["logs","games","account"],"games":["beamng"]}}
```

If no common version exists the engine replies `incompatible_protocol` and closes; the UI shows "Update the launcher".

### 6.1 Requests (UI -> engine)

| Name | Purpose |
|---|---|
| `engine.state` | Current state and `EngineStatus` snapshot |
| `engine.shutdown` | Orderly exit (refuses while a game runs unless `force`) |
| `log.subscribe` | Start streaming logs, with `level` and `backlog` |
| `games.list` | Adapters and per-game status/manifest |
| `game.launch` / `game.stop` | Namespaced by game id |
| `game.settings.get` / `.set` | Engine-owned per-game settings (paths, args) |
| `account.state` / `account.sign_out` | Account status (login itself: section 9) |

### 6.2 Events (engine -> UI)

| Name | Data |
|---|---|
| `engine.state` | `{state, detail}` |
| `game.state` | `{game, state, pid?}`; states `not_found`, `preparing`, `ready`, `launching`, `running`, `exited` |
| `progress` | `{task, fraction?, text}` (e.g. mod download) |
| `log` | `{ts, level, message}` |
| `account.state` | `{signed_in, username?}` |
| `fatal` | `{code, message}` sent immediately before the engine exits |

### 6.3 Rules

- Game ids are strings (`beamng`); nothing in the protocol names BeamNG concepts (mods folder, `pk`, ports).
- Unknown request names return `unknown_request`; unknown event names are ignored by the UI (forward compatibility).
- Additive changes keep version 1. Breaking changes bump `v`.
- Secrets never cross this channel in either direction except the one-time token in `hello`.

## 7. Game abstraction (design for multiple games, build for one)

Three layers:

1. **UI <-> engine**: section 6, namespaced by game id.
2. **Engine <-> game adapter**: the contract a game implements.
3. **Adapter <-> game**: the game-specific bridge. For BeamNG this is today's TCP protocol with the Lua mod (`Core.cpp` packet codes), unchanged.

Adapter contract (capabilities, not signatures yet):

- identity: id, display name, icon, ABI version
- `Detect`: find installs (path, version, store/Steam/other)
- `Prepare`: install/update the game's multiplayer component, reporting progress
- `Launch` / `Stop`: start the game with arguments and environment, supervise the process
- `Bridge`: start/stop the game-side channel
- settings schema (what the UI should render for this game)

Decisions:

- Ship BeamNG as a **built-in adapter behind the interface first**. Add dynamic loading (versioned C ABI, `extern "C"` function table) only when the second game exists.
- Adapters never receive credentials or long-lived tokens. They receive short-lived, scoped session tickets from the engine **(open: ticket format)**.
- In-process libraries are acceptable for first-party, signed adapters. Anything third-party runs out-of-process.
- Today's BeamNG-specific code (`Security/BeamNG.cpp`, `GameStart.cpp`, `Resources.cpp`) is what becomes the BeamNG adapter. Do not move it until the interface is exercised by a real use; keep the boundary clean first.

## 8. UI design

### 8.1 Structure

Game-agnostic shell; each game page is driven by the adapter's manifest (name, icon, capabilities, settings schema, localized strings), not hard-coded.

| Screen | Contents |
|---|---|
| Startup | Engine starting / preparing; errors with log tail and retry |
| Home (Library) | One card per game: status, primary action (Play / Launching... / Running / Repair), version |
| Game | Install location, version, launch arguments, per-game settings |
| Account | Signed-in user, sign in / out |
| Logs | Live log with level filter, copy, export, open log folder (replaces the console window) |
| Settings | General, Games, Advanced, About |

### 8.2 Tray and window behaviour

Settings (UI-owned, stored by the UI):

| Setting | Values | Default |
|---|---|---|
| `tray.enabled` | on / off | on |
| `launch.windowBehavior` | hide to tray / stay open / minimise | hide to tray if tray enabled, otherwise stay open |
| `close.behavior` | quit / close to tray (only when tray enabled) | quit |
| `gameExit.restoreWindow` | on / off | on (without stealing focus) |

Rules:

- On `game.launch` success: tray enabled -> hide to tray. Tray disabled -> keep the window open, or minimise if the user chose that.
- Tray menu: Show/Hide, status line (e.g. "BeamNG.drive - running"), Play (when ready), Open logs, Quit.
- Quit with a game running asks for confirmation; confirming stops the engine, not the game.
- The tray is **probed at runtime**. If unavailable (GNOME without the AppIndicator extension, headless Wayland sessions), `tray.enabled` is treated as off and Settings shows why. The window behaviour falls back to minimise/stay open. No crash, no hidden window that cannot be restored.

Platform notes: Windows notification area; macOS menu bar (the Dock icon is hidden only while the window is hidden); Linux needs `libayatana-appindicator` and, on GNOME, the AppIndicator extension. Wayland restricts programmatic window positioning, but hide/minimise/show are fine **(verify)**.

### 8.3 Flutter architecture

- Feature-first folders under `ui/lib/`: `app/`, `features/{home,game,account,logs,settings}/`, `core/{engine,tray,l10n,theme}/`.
- The control client is a **pure Dart package** (`ui/packages/launcher_control`) with no Flutter imports: framing, handshake, request/response correlation, event stream, reconnect. Unit-testable without a device.
- State: Riverpod with Dart 3 sealed classes for engine/game state. Immutable models. No business logic in widgets.
- Candidate packages **(verify each on all three platforms)**: `window_manager`, `tray_manager`, `local_notifier`, `flutter_secure_storage`, `url_launcher`, `path_provider`, `flutter_localizations` (ARB).
- Lints: `flutter_lints` or stricter `very_good_analysis`, enforced in CI. Null safety, no `dynamic` in the control client.
- Localisation from the first screen (ARB). The Accounts site already has English strings to align terminology.
- Accessibility: keyboard navigation, semantics on status and actions, respect system text scaling and reduced motion.
- Theming: BeamMP branding, light and dark.

## 9. Account and login (proposed; needs API work)

Today the in-game Lua UI sends username/password in an `N` packet; the engine posts them to the legacy endpoint and stores a key. The legacy path stays as is for the console launcher and old mod versions (decision: not touched now).

Proposed for the UI launcher:

- **System browser sign-in**: authorization code with PKCE (RFC 8252 native app flow) against Accounts. Password, 2FA and any future passkey happen in the browser; the launcher never sees them. The recently built OAuth endpoints already support PKCE and public clients.
- Missing on the API side **(open)**: loopback redirect with a variable port (redirect URIs are exact-match today), long-lived login (access tokens are 1 hour, no refresh tokens by design), and exchanging the OAuth login for the game session the mod needs.
- A device authorization flow (RFC 8628) for headless/console use **(open)**.
- Long-lived credentials are stored in the OS keychain (`flutter_secure_storage` in the UI; the engine asks the UI or uses a platform keystore wrapper **(open)**), not in a plain file.
- The game receives only short-lived, per-server tickets. Never a password, never a long-lived key.
- Mod/launcher capability negotiation (via the existing `Z`/`N` exchange) so the mod knows whether login is launcher-handled or legacy. Mod changes are a coordinated release.

Honest limit: code running inside the game process can still talk to whatever the engine's bridge exposes. This design removes password and long-lived-key exposure; it cannot make an arbitrary in-process mod harmless.

## 10. Platform matrix

| | Windows | macOS | Linux |
|---|---|---|---|
| Engine spawn | `CREATE_NO_WINDOW` | `posix_spawn`/`Process.start` | same |
| Tray | notification area | menu bar | AppIndicator, probed |
| Engine location | next to UI exe | inside app bundle | next to UI binary |
| Game start | `CreateProcessW` (existing) | `posix_spawn` (existing) | `posix_spawn`, Steam/Proton |
| Notes | Wine/Proton: Flutter's Windows renderer there is unproven **(verify)**; auto-fall back to console mode when Wine is detected (`LinuxPatch` already detects it) | signing and notarisation later | GTK3 required; Wayland/X11 both |

Packaging and distribution per platform are deferred (section 13).

## 11. Testing

- **Engine (C++)**: an opt-in doctest target (`-DBEAMMP_BUILD_TESTS=ON`, off by default, not part of any workflow yet) covering only new code: control protocol framing/parsing, handshake and token rejection, state machine transitions, logger sink fan-out and thread safety. No retrofitting tests to old code in this phase.
- **Integration**: a fake UI client against the real control server, and a fake engine (Dart or small C++) against the Flutter app, run in CI.
- **Flutter**: unit tests for the control client and state; widget tests per screen; golden tests for Home/Settings in light/dark; tray behaviour logic tested behind an interface with a fake tray.
- **CI**: deferred. Everything is built and tested locally on macOS for now; the intended end state is engine and UI on Windows, macOS and Linux with all tests and lints on every PR.

## 12. C++ style for new engine code

Match the existing code, minus its known weak spots.

- License header on every file (copy from existing). `#pragma once`. Headers in `include/<Area>/`, sources in `src/<Area>/`.
- 4-space indent. Public functions `CamelCase` (as `InitLauncher`, `GetGamePath`). `Options` fields and locals `snake_case`. Types `CamelCase`.
- Logging through `info()/warn()/error()/debug()`; paths through `beammp_fs_string`/`beammp_wide`.
- No new global mutable state, no `new[]` without RAII, no `exit()` in code reachable in controlled mode, no exceptions crossing thread boundaries. Everything touched by two threads is behind a mutex or atomic.
- Platform code under `#if defined(_WIN32)` / `#elif (defined(__linux__) || defined(__APPLE__))`, kept as small as the existing files do.
- Comments are one short line and explain *why*. No multi-line comment blocks, no restating the code.
- No unrelated refactors in the same change. Behaviour-preserving refactors land first, on their own.

## 13. Deferred

- **Updates and distribution**: bundling the UI with launcher updates, a signed update manifest, installers, notarisation, Flatpak/AppImage choices. Constraints already known: existing launchers fetch a single file from the backend `launcher` channel and force the name `BeamMP-Launcher.exe`, so that channel keeps serving the console build for existing installs; the UI bundle will need its own path **(open)**.
- Server browser in the UI.
- Unix socket / named pipe control transport.
- Dynamic adapter loading.

## 14. Phases

1. **Prepare** (done): test target, name handling (5.1), options (5.2), logger sinks (5.3), status snapshot (5.4). No behaviour change; console mode identical.
2. **Engine lifecycle + control server** (done, macOS-verified): 5.5, 5.6, protocol spec as a standalone doc, headless mode. Console mode still identical. Not part of it yet: the per-user lock and runtime file for re-attaching (4.2), `progress` events, `game.settings.*`, and removing the remaining `exit()` calls in the `ReLaunch`/`URelaunch` update path.
3. **UI skeleton** (done on macOS): Flutter app in `ui/`, control client package, Startup + Home + Logs + Account + Settings, launch/stop, window and tray behaviour with runtime probing. State management is plain `ChangeNotifier` controllers and the tray/window are small native macOS code behind two Dart interfaces (`WindowHost`, `TrayHost`), so no plugins were needed; Riverpod and cross-platform tray plugins are still open. Windows and Linux hosts are not written. Account sign-in from the UI is not part of it (section 9).
4. **Settings and polish** (done on macOS): settings screen (appearance, window behaviour, notifications, extra game arguments, log file, about), native macOS notifications, localisation hygiene checks (English only; adding a locale is an ARB file that the tests validate), accessibility pass (labels, contrast, keyboard shortcuts, text scaling to 200% in the smallest window). Not done: engine-owned per-game settings (`game.settings.*`, game path), exporting logs to a file, a second language.
5. **Account** (done locally on macOS, see `docs/LAUNCHER_AUTH.md`): browser sign-in with PKCE on a loopback redirect, rotating refresh token in the keychain, restore on start, game session from an OAuth token. The API changes (loopback redirect, `game`/`offline_access` scopes, refresh tokens, `game-session`) are written and tested but uncommitted and not deployed. Differences from the proposal: the engine, not the UI, runs the flow (so no secret ever crosses the control channel); the game keys are still the legacy public/private key pair, renewed from the session before each server join; the per-server ticket idea is not built. Not done: device flow, Windows/Linux keychains.
   GearBlocks is already listed (id `gearblocks`, state `unavailable`, `game_unavailable` on launch) as the first second entry, so the UI and protocol are exercised with two games; it has no adapter, so nothing can be launched.
6. **Adapter extraction**: move BeamNG-specific code behind the adapter interface.

Acceptance for each phase: existing console launcher behaviour unchanged, all tests and lints green on all three platforms, documented protocol changes.

## 15. Risks and open questions

- Flutter under Wine/Proton (many Linux users run the Windows launcher this way): keep console mode first-class and auto-detect.
- Linux tray availability is inconsistent; design assumes it may be missing.
- Existing status globals are racy today; phase 1 must fix access before anything new reads them.
- Several `exit()` paths and the 5-second `fatal()` need controlled-mode behaviour (5.5).
- Two launchers can never run at once (port 4444); the UI must handle a console launcher already running.
- Control channel hardening (token handling, loopback exposure) needs a security review before release.
- Ticket format, OAuth loopback redirect, refresh tokens and the game-session exchange all depend on API work that is not scheduled.
- Whether the UI keeps a separate `ui/` update path or folds into the existing channel later (section 13).
