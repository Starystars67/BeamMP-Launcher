# GearBlocks: what the install shows

Inspected read-only on 2026-10-01: `~/Library/Application Support/Steam/steamapps/common/GearBlocks` (Steam app 1305080, build 25070119, 1.5 GB, Windows build). The game was not started.

## The game

- By SmashHammer Games. Unity **6000.0.82f1**, **IL2CPP** (`GameAssembly.dll`, `il2cpp_data/Metadata/global-metadata.dat`, no `Managed/` folder), Direct3D (`D3D12/`).
- Game code in `Assembly-CSharp.dll` and `SmashHammer.GearBlocks.dll` (compiled into `GameAssembly.dll`).
- Third-party assemblies: **Photon** (Realtime, PUN, Chat), **MoonSharp** (Lua interpreter), **Roslyn** (`Microsoft.CodeAnalysis.CSharp`), `websocket-sharp`, Steamworks (Facepunch), Newtonsoft.Json.

## Scripting (official)

- `GearBlocks_Data/StreamingAssets/ScriptMods/<Name>/` with `main.lua` and `meta.json` (`name`, `desc`, `tags`). 25 example mods ship with the game (about 2,800 lines of Lua).
- Entry points seen: top-level code, `Cleanup()`, `FixedUpdate()`; unload with `UnloadScript.Raise( ScriptName )`.
- Lua API namespaces used by the examples: `Windows`, `Input`, `Time`, `Math`, `Profiler`, `LocalPlayer`, `Players`, `Constructions`, `ConstructionOps`, `ParentConstruction`, `Toolbox`, `Targeter`, `TargetedPart`, `UndoHistory`, `TextObjects`, `Texture`.
- **None of the examples touch sockets, HTTP, files or JSON**, so it is not known whether a script can reach a localhost TCP port the way BeamMP's Lua mod reaches the launcher. MoonSharp's sandbox presets (`Preset_HardSandbox` ... `Preset_Complete`) exist in the build; which one the game uses is not visible from the files.

## Multiplayer

The game appears to have **its own multiplayer on Photon**: `IsMultiplayer`, `NetworkConstructions`, `PlayerPositionState`, `PlayerOrientationState`, `connectedClientLoadingLevelEvent`, `clientDisconnectedEvent`, `Photon*` types. **Checked in the running game (build 0.8.9740, 2026-10-02): there is no host or join UI** (main menu, Begin Game, New Scene dialog and pause menu were all looked at). The networking code and the Lua events/settings exist, but nothing in this build reaches them. A BeamMP style mod would have to bring its own transport and state replication.

## What decides the approach

1. Does the game start at all on this Mac (Wine, as BeamNG does)? Unity 6 with D3D12/D3D11 may need the same kind of Vulkan compatibility work.
2. Can a Lua script open a localhost connection (or otherwise talk to the launcher)? That would allow a mod shaped like BeamMP's.
3. If not: an injected plugin (BepInEx IL2CPP or MelonLoader) instead of a script mod, which is much heavier and riskier under Wine.
4. What the native Photon multiplayer already does.

## Running it on this Mac (2026-10-01)

- `macos/run-gearblocks.sh` starts it. Own Wine prefix `~/Wine/gearblocks-prefix`.
- **Renderer**: only Direct3D 11 works, through **DXVK** (copied into that prefix from Whisky's copy). Wine's own wined3d fails (Apple's OpenGL 4.1 gives no D3D feature level 11), D3D12 fails (Unity needs `D3D11On12`, vkd3d has none), Unity's Vulkan renderer is not in the player build.
- **Steam**: the game calls `SteamAPI_Init` and stops without a Steam client. The Windows Steam client is installed in the same prefix and must be signed in. Its own window stays black (Steam draws it with a Vulkan path that does not present under Wine on macOS), but its login page renders inside Steam: it was reached with Steam's remote debugging port (marker file `.cef-enable-remote-debugging` in the Steam folder, `http://127.0.0.1:8080`) and signed in with the QR code. Signing in here can sign the native macOS Steam out.
- The game needs `steam_appid.txt` (`1305080`) in its folder, written by the run script.
- Menu automation: a tiny Win32 helper (`winput.exe`, sends clicks and keys inside the Wine session) was used to open **Script Mods** in the game's menu.

## Lua script environment (measured with a probe mod)

User scripts are read from `C:\users\<user>\AppData\LocalLow\SmashHammer Games\GearBlocks\ScriptMods\<Name>\` (`main.lua`, `meta.json`, optional `preview.jpg`), listed on the "Locally Saved" tab and started from there. The game scans the folder at startup. The shipped examples are under `StreamingAssets/ScriptMods` ("Built-in Examples" tab).

MoonSharp (`_MOONSHARP`), 333 globals. Available: `os` (at least `os.time`), `package`, `require`, `load`, `dofile`, `loadfile`, `coroutine`, `string`, `table`, `math`, `bit32`, `json` (`parse`, `serialize`, `isnull`, `null`), `dynamic` (`eval`, `prepare`). **Not available: `io`, `socket`, `http`, `Network`, `File*`, `debug`, `loadstring`.** `require('socket')` fails with "module not found" and only looks next to the script.

`os` is reduced to `clock`, `date`, `difftime`, `time`. `package` only has `config` and `loaded` (no `path`/`cpath`). `os.getenv`, `tmpname`, `remove`, `rename`, `execute`, `exit`, `setlocale` are nil.

So a script cannot open a connection to the launcher and cannot write files. Output exists through `print`, which lands in the Unity player log as `Lua: ...`.

### Reading files works (bridge probe, 2026-10-02)

`loadfile` reads real files from the disk, with no path restriction. All three forms worked: a relative path (resolved against the game folder), `C:\users\<user>\...` and `Z:\tmp\...` (a Mac path under Wine). A missing file does not return an error: it throws a .NET `FileNotFoundException` that `pcall` does **not** catch and that aborts the script's load, so the inbox file must exist before the script starts (create it first, replace it atomically).

Measured round trip with the probe polling every 5 `FixedUpdate` ticks (about 100 ms): a new file written from the Mac showed up as a `Lua: BRIDGE: RECEIVED ...` line in the player log in **70 to 100 ms** (three runs). The player log is written without noticeable delay, so a launcher can tail it.

That gives a working, low-rate bridge without any plugin:

- game to launcher: `print` lines in the player log (the launcher starts the game and knows the log path via `-logFile`),
- launcher to game: the launcher replaces an inbox file that the script re-reads with `loadfile` each poll.

Limits: text only, about 10 messages per second per direction at this polling rate (the poll can be faster), the log grows, `loadfile` compiles Lua on every read (a hostile or buggy launcher message would run as Lua inside the sandbox, so the inbox must only ever be written by the launcher and contain data, not code from the network), and one malformed read aborts the script.

Game API globals seen (userdata, members not listable from Lua): `Players`, `LocalPlayer`, `LocalPlayerNetworkID`, `PlayerName`, `Constructions`, `ConstructionOps`, `ConstructionSpawned`, `ConstructionDestroyed`, `SpawnConstruction`, `Parts`, `PartBehaviourOps`, `Physics`, `Time`, `Input`, `Windows`, `LoadScene`, `LoadScenario`, `LevelLoaded`, `GameLoaded`, `GameReady`, plus **multiplayer events and settings**: `ServerStarted`, `ServerDisconnected`, `ClientConnected`, `ClientDisconnected`, `ClientReady`, `ConnectedClientLoadingLevel`, `DisconnectingServer`, `ServerSendRate`, `ClientInputSendRate`.

## Plugin route (BepInEx), tested 2026-10-02

BepInEx 6 bleeding edge be.788 (IL2CPP, win-x64, from builds.bepinex.dev) works in this game under Wine on macOS, with Unity 6000.0.82. Install = unzip into the game folder (`winhttp.dll` Doorstop proxy + `BepInEx/`, `dotnet/`, `doorstop_config.ini`), and run with `WINEDLLOVERRIDES=winhttp=n,b` (`macos/run-gearblocks.sh` does that when `winhttp.dll` is in the game folder; `GEARBLOCKS_DIR` points it at another folder). The first start generated the interop assemblies (about a minute) and fetched Unity's base libraries (`BepInEx/unity-libs`, from the BepInEx project). A working copy of the game lives in `~/Wine/gearblocks-copy` so the Steam install stays untouched.

The plugin is in `gearblocks/plugin` (.NET 8 SDK, target net6.0, references the generated interop assemblies of the game copy). It does what Lua cannot: it adds a `BeamMP` table to every Lua script:

| Call | Meaning |
|---|---|
| `BeamMP.connect()` | `true`, or `false, reason`. Starts the loopback connection once |
| `BeamMP.connected()` | bool |
| `BeamMP.send(line)` | queue one line (no newline inside, at most 1 MiB), returns bool |
| `BeamMP.poll()` | next line from the launcher, or `nil` |
| `BeamMP.error()` | last transport error |

The script never chooses where to connect: the host is always `127.0.0.1`, the port and a token (at least 16 characters, sent as the first line) come from the launcher's environment (`BEAMMP_BRIDGE_PORT`, `BEAMMP_BRIDGE_TOKEN`). Without them `connect()` fails. The transport runs on its own threads; lines are newline delimited UTF-8.

How it is hooked: constructors cannot be patched with the IL2CPP backend, so a Harmony prefix on `MoonSharp.Interpreter.Script.Call(DynValue, DynValue[])` adds the table the first time a script's main chunk is called (before the script's own code).

Measured with `SocketProbe` (a script mod) against a fake launcher on `127.0.0.1`: token accepted, hello received, five launcher messages delivered to Lua, each answered by Lua with an ack in 6 to 16 ms, disconnect noticed by the script.

Open points: every script in the game gets `BeamMP` (the plugin should only connect for the BeamMP mod), the token sits in the game's environment (the game process is trusted), and the plugin has to be installed into the user's game folder (BepInEx files beside the exe).

## Launcher adapter (2026-10-02)

- `src/Games/GameBridge.cpp`: the loopback listener the plugin connects to (127.0.0.1 only, token as the first line, anything else dropped silently, one connection, lines up to 1 MiB, a silent squatter cannot lock the real plugin out).
- `src/Games/GearBlocks.cpp`: `GearBlocksAdapter` checks the install (game, BepInEx, plugin), opens a bridge with a fresh token per launch, starts the game with `BEAMMP_BRIDGE_PORT`/`BEAMMP_BRIDGE_TOKEN` in its environment (own process group, stdout/stderr to /dev/null because stdout carries the control protocol), follows the process in the lifecycle (`GameRunning`, then `Ready` with the exit code) and stops it with its whole group.
- The controller now has a game registry (`RegisterGame`), one active game, `game.send` and `game.message` (see `docs/CONTROL_PROTOCOL.md`).
- Tested with fakes (`tests/GameBridgeTests.cpp`, `tests/GearBlocksTests.cpp`, the multi-game cases in `tests/ControllerTests.cpp`) and once for real: the adapter started GearBlocks through `macos/run-gearblocks.sh`, the plugin connected with the per-launch token, the SocketProbe script sent `{"type":"hello"}`, the launcher answered `welcome`, Lua replied `ack`, and `Stop` ended the game (exit 143) and returned the lifecycle to `Ready`.
- Since the game modules (`docs/GAME_MODULES.md`) the whole engine works with it: with no BeamNG installed the engine reports BeamNG as `not_found`, stays up, and launches GearBlocks. Verified once for real with the actual engine binary (sandboxed, loopback only): launch through `macos/run-gearblocks.sh`, SocketProbe hello, `game.send` welcome answered by an ack in 5 ms, `game.stop` (`exited`, code 143), engine exit 0.

## Plugin and mod for development

`gearblocks/install-dev.sh [game folder] [wine prefix]` builds the plugin and copies it into a GearBlocks folder that has BepInEx, and copies the mod (`gearblocks/mod/BeamMP`) into the Wine prefix's script folder. Start the game (engine: `--game-dir gearblocks=<folder> --game-command gearblocks=macos/run-gearblocks.sh --relay 127.0.0.1:7777`), then Script Mods, BeamMP, run. The mod's window shows the room the launcher is in.

## Consequences

- A script on its own cannot open a connection (no `socket`, no `io`).
- The game's own multiplayer cannot be used: there is no host/join UI in this build.
- **Chosen direction: the BepInEx plugin above, plus a Lua mod written like BeamMP's.** The log + `loadfile` bridge stays as a fallback that needs no plugin (low rate, fragile).

## Installing for a player (2026-10-03)

The launcher installs and updates everything itself, from a signed manifest (`game.install`, see `docs/CONTROL_PROTOCOL.md`), and nothing is bundled in it. Two parts, like BeamNG's mod flow for the second: BepInEx is an explicit install step (and again after a game update that removes it), the BeamMP plugin and script mod are synced with the manifest at every launch. `src/Games/GearBlocksInstaller.cpp` checks the game folder and its build (read from `GearBlocks_Data/globalgamemanagers`), fetches `manifest.json` and its Ed25519 signature (`src/Net/Ed25519.cpp`, keys from `--gearblocks-trust`), picks the highest release for the game's build and bridge protocol (`src/Games/GearBlocksManifest.cpp`), downloads only what differs by SHA-256 (https only, unpacked with protection against paths that leave the target folder), checks every file against the signed checksums before touching the game's folder, copies the plugin and the script mod (into the game's shipped `ScriptMods` folder, so nothing is written outside the game's folder), and records what it wrote (`beammp-install.json`, with a checksum for every installed file, which is also how a changed plugin or mod is noticed) for uninstalling. A manifest with a lower `sequence` than the last one seen is refused (replay), and when there is no network the last verified manifest is used.

Publishing: `gearblocks/release.py keygen <private> <trusted keys file>` makes the signing key (the private half belongs to whoever publishes releases and stays off shared machines; the public line goes to launchers), `gearblocks/release.py publish <dir> --base-url <https://...> --key <private>` builds the plugin, zips the mod and writes `plugin-<version>.dll`, `mod-<version>.zip`, `manifest.json` and `manifest.json.sig` for upload; releases that are already in the folder's manifest stay. Where it is hosted and who holds the key are open decisions: the launcher only needs the address and the key file. `gearblocks/install-dev.sh` stays for development under Wine (without a configured source the launcher leaves a plugin and mod that are installed alone and does not sync).

The plugin starts the script mod itself (`gearblocks/plugin/ModStarter.cs`): when a game is ready and the mod is not running (the game lists its script mods a little later, so it looks once a second for up to three minutes) it raises the game's own `LoadScriptMod` event for `BeamMP` (the shipped copy wins over one in the player's folder). The mod stays running across scenes; a player who closes its window gets it back with the next game.

Tests: `tests/GearBlocksInstallerTests.cpp` (an in-memory file server and a signed manifest: fresh install, sync, a newer release, repair of a changed plugin or mod, offline use of the last manifest, bad signature, replay, bad checksums, a mod zip that escapes its folder, unsupported build, launcher too old, conflict, cancel, not writable, uninstall, a loader BeamMP did not install), `tests/GearBlocksManifestTests.cpp`, `tests/Ed25519Tests.cpp` (RFC 8032 vectors), `tests/DownloadTests.cpp`, `tests/UnzipTests.cpp`, the install cases in `tests/ControllerTests.cpp`; `BEAMMP_TEST_BEPINEX_ZIP` and `BEAMMP_TEST_LOADER_JSON` turn on a test with the real archive. Checked with the real game against a local server (`http://127.0.0.1`, a development key): uninstall, install, a changed plugin repaired by the launch, the mod starting by itself. Not exercised: the download itself from `builds.bepinex.dev` (nothing is downloaded without the user's say-so), a Windows machine, a read-only Steam library.

