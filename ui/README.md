# BeamMP Launcher UI

Flutter front end for the launcher. It starts the launcher engine (`BeamMP-Launcher-Console`) as a child process and controls it over the [control protocol](../docs/CONTROL_PROTOCOL.md). See `../DESIGN.md` for the design.

```
ui/
  lib/app/            MaterialApp, shell, theme, shared services
  lib/core/engine/    engine process, controller (state + events), log buffer
  lib/core/window/    window and tray behaviour, native shell channel
  lib/core/settings/  UI settings (JSON file in the user's config folder)
  lib/features/       startup, home, logs, account, settings screens
  lib/l10n/           ARB strings (English)
  packages/launcher_control/   pure Dart client for the control protocol
  macos/              macOS runner (window and menu bar icon in MainFlutterWindow.swift)
```

Only macOS is wired up so far. Elsewhere the window behaviour falls back to staying open.

## Run it locally (macOS)

Build the engine under its UI-mode name (from the repository root, with the usual dependencies):

```
cmake -S . -B build-engine -DBEAMMP_EXECUTABLE_NAME=BeamMP-Launcher-Console -DCMAKE_BUILD_TYPE=Release
cmake --build build-engine
```

Then run the UI with the engine pointed to explicitly (in a packaged app the engine is found in `Contents/Helpers`):

```
cd ui
BEAMMP_ENGINE=../build-engine/BeamMP-Launcher-Console \
BEAMMP_ENGINE_ARGS="--no-update --no-download --no-launch" \
flutter run -d macos
```

`BEAMMP_ENGINE_ARGS` are extra engine flags (quotes keep a path with spaces together, the games' folders and start commands can be given here as `--game-dir beamng=/path` and so on; normally the UI passes the ones saved in each game's settings page). `--no-launch` also makes the UI's Play button fail with "launching is disabled", which is what you want while developing.

The app is not sandboxed: it has to start the engine, which in turn needs access to the game folders.

## Using it

- Settings: theme, tray icon, what the window does when the game starts or closes, notifications, extra game arguments (shell-style quoting, passed at every launch), reveal the engine's log file, versions.
- Notifications (macOS) only appear while the window is hidden or minimised: when the game closed and the window was not brought back, and when the engine stopped (the window is brought back then as well).
- Account: Sign in with BeamMP opens the BeamMP website in the browser; the launcher never sees the password. The sign-in is kept in the macOS keychain and restored at the next start. See `../docs/LAUNCHER_AUTH.md`.
- Keyboard: Cmd+1..4 (Ctrl+1..4 elsewhere) switch between Play, Logs, Account and Settings; everything is reachable with Tab.

## Tests

```
cd ui && flutter test              # controller, window behaviour, screens, goldens
cd ui/packages/launcher_control && dart test
BEAMMP_ENGINE=/path/to/engine dart test -t integration   # against the real engine
```

The goldens in `ui/test/goldens` are rendered with Roboto from the Flutter SDK; regenerate with `flutter test --update-goldens test/golden_test.dart`.
