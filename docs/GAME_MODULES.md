# Game modules

Everything that is specific to one game lives in a module (`include/Games/GameModule.h`). The engine (lifecycle, account, control protocol, UI) only talks to that interface, so adding a game means writing a module and registering it.

| Member | Meaning |
|---|---|
| `Id()` / `Name()` | Stable id used in the control protocol (`beamng`, `gearblocks`) and the name the UI shows |
| `Configure()` / `Config()` | The user's settings for the game (`GameConfig`: folder, start command, user folder); they apply with the next `Prepare()` |
| `Prepare()` | Finds the game and makes it ready (updates, mods, plugins). May block and use the network. Returns `PrepareResult`: `Available`, `NotFound`, `Unavailable` or `Failed`, with a `detail`. Never throws |
| `Launch(args)` | The lifecycle is already `LaunchingGame`; the module moves it to `GameRunning` and, when the game ends, to `Ready` (with the exit code as detail). Throws `std::runtime_error` when it cannot start at all |
| `Stop()` | Ends the running game |
| `HasMessageChannel()`, `Send()`, `SetMessageHandler()` | Optional: JSON messages between the game's mod and a controller (`game.send` / `game.message`) |

## How the engine uses them

1. `Games::BuildBuiltInGames()` (`src/Games/BuiltIn.cpp`) creates the modules: BeamNG always, GearBlocks only for a launcher that has a controller.
2. A controlled engine registers each module with the `Controller`, which lists them in `games.list` and routes `game.launch`, `game.stop` and `game.send` to the right module. One game runs at a time, the others are `busy`.
3. At startup `main.cpp` calls `Prepare()` of every module and tells the controller the result (`SetGameStatus`), so one game that is missing does not stop the rest. The console launcher (no controller) has only BeamNG and still ends if it cannot be prepared.
4. BeamNG's network side (`CoreNetwork`, the game's proxy) is still the launcher's core and runs once BeamNG is prepared; without BeamNG the engine just waits for the controller.

## Settings and preparing again

`GamePreparer` (`src/Games/Preparer.cpp`) runs `Prepare()` for each module at startup and again when `game.configure` changes a game's settings; one preparation at a time, each reported to the controller (`preparing`, then the result). The launcher's BeamNG network core starts as soon as BeamNG is ready, also when that only happens after a change of settings. `ManagedProcess` runs a game as a child process and keeps the lifecycle in step with it; both modules use it, with the user's start command when there is one.

## The modules

- `BeamNGModule` (`src/Games/BeamNG.cpp`): wraps the existing code (legitimacy check, proxy, `PreGame`, `LaunchGame`, `StopGame`).
- `GearBlocksModule` (`src/Games/GearBlocks.cpp`): see `docs/GEARBLOCKS.md`.

## Not decided yet

- **Loading modules from DLLs/shared libraries at runtime.** A module runs inside the launcher with the user's account tokens, so loading code from disk needs a trust model (signing or an allowlist) first. Today the modules are compiled in; the interface is what a loadable module would implement behind a C ABI.
- **The shared game protocol.** The mod-side channel is JSON lines for now; the plan is to move it to the protocol of `protocol-prototype` (state keys with permissions and update rates) once that is settled.
