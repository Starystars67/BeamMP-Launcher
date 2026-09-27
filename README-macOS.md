# BeamMP on macOS

This gets BeamMP running on a Mac. The launcher runs **natively** on macOS, and the game (the normal Windows build of BeamNG.drive) runs through **Wine** with a small Vulkan compat layer we wrote for it. No Parallels, no VM, no CrossOver.

**Short version:** it works. We've played on the official servers with it, spawning, syncing, chat, the lot. It's still early though, so expect some rough edges (see [Known issues](#known-issues)).

| | Tested with |
|---|---|
| Mac | MacBook Pro, Apple M3 Max, 48 GB |
| macOS | macOS 26.6 |
| Game | BeamNG.drive 0.39.4.0 (Windows build), Vulkan renderer |
| Wine | WineHQ staging 11.18 ([Gcenx's macOS build](https://github.com/Gcenx/macOS_Wine_builds)) + Rosetta 2 |
| Launcher | this branch, built natively (arm64) |

Heads up: this is **Apple Silicon only** for now. Intel Macs might work but nobody has tried.

---

## What you need

- An Apple Silicon Mac, ~50 GB free for the game plus ~2 GB for Wine
- **BeamNG.drive** on Steam (you need to own it, the game has no DRM so Wine is fine with it)
- **Rosetta 2**: `softwareupdate --install-rosetta --agree-to-license`
- **Xcode command line tools**: `xcode-select --install`
- **[Homebrew](https://brew.sh)**, then:

```bash
brew install cmake ninja openssl@3 cpp-httplib nlohmann-json mingw-w64 bison flex
```

`cmake`, `ninja`, `openssl@3`, `cpp-httplib` and `nlohmann-json` are for building the launcher. `mingw-w64`, `bison` and `flex` are for building the one patched Wine DLL (see [How it works](#how-it-works)).

---

## 1. Get the game files

Steam on macOS won't install BeamNG.drive because there's no Mac build, so you need the **Windows** files. The scripts look for them in Steam's usual folder:

```
~/Library/Application Support/Steam/steamapps/common/BeamNG.drive
```

(so `.../BeamNG.drive/Bin64/BeamNG.drive.x64.exe` should exist). If you keep them somewhere else, set `BEAMNG_DIR` to that folder when running the scripts.

Two ways to get them:

- **Copy them from a Windows PC** that has the game installed. This is what we did.
- **SteamCMD**, which can download the Windows build on a Mac (not tested by us yet):

```bash
brew install --cask steamcmd
steamcmd +@sSteamCmdForcePlatformType windows +force_install_dir "$HOME/Library/Application Support/Steam/steamapps/common/BeamNG.drive" +login YOUR_STEAM_USERNAME +app_update 284160 validate +quit
```

## 2. Build the launcher

From the root of this repo:

```bash
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) -DCMAKE_PREFIX_PATH=$(brew --prefix)
cmake --build build-macos
```

You end up with `build-macos/BeamMP-Launcher`, a native arm64 binary.

## 3. Set up Wine

```bash
./macos/setup-wine.sh
```

This puts everything in `~/Wine` (set `BEAMMP_WINE_HOME` to use another folder), nothing gets installed system wide. It:

1. downloads WineHQ staging 11.18
2. builds a patched `kernelbase.dll` so the game's console works in your terminal, and puts it in a copy of Wine (`Wine Staging BeamNG.app`), the original stays untouched. This bit takes a few minutes
3. creates a Wine prefix (`~/Wine/beamng-prefix`) and installs the VC++ runtime that ships with the game
4. builds the Vulkan compat layer (`macos/vkcompat`)

It's safe to run again, it skips whatever is already done.

## 4. Start the game once

Run this **from Terminal** (Terminal.app, iTerm2, Ghostty, whatever):

```bash
./macos/run-beamng.sh
```

The first start takes a while (shaders). The game's console shows up **in the terminal you started it from**, with colours, and you can type GE Lua straight into it. Once you get to the main menu the game has created its user folder, so you can quit.

## 5. Play BeamMP

```bash
./macos/run-beammp.sh
```

This starts the launcher, waits for it to be ready, then starts the game. The launcher downloads the BeamMP mod into the game on its own, same as on Windows. Log in and pick a server in the in game server list and you're good to go.

---

## Tips

- **Renderer:** Vulkan is the default and the one to use. `./macos/run-beamng.sh dx11` gives you D3D11 if you want to compare. Anything after the renderer goes to the game, e.g. `./macos/run-beamng.sh vk -enablemcp`
- **Logs:**
  - game: `~/Wine/beamng-prefix/drive_c/users/$USER/AppData/Local/BeamNG/BeamNG.drive/current/beamng.log`
  - launcher: `build-macos/Launcher.log`
  - Wine (when not started from a terminal): `~/Wine/logs`
- **Extra launcher arguments:** `BEAMMP_LAUNCHER_ARGS="--verbose" ./macos/run-beammp.sh`. With `--no-download` the launcher leaves the mod alone, handy when testing your own build of the BeamMP mod
- **Game hangs on start:** it very occasionally gets stuck on a black window in Vulkan mode (a Wine heap lock at startup, we haven't found the cause yet). Kill it and start again:

```bash
WINEPREFIX=~/Wine/beamng-prefix ~/Wine/"Wine Staging BeamNG.app"/Contents/Resources/wine/bin/wineserver -k
```

---

## Known issues

- **D3D12 crashes** on the first gameplay frame (the main menu is fine). Use Vulkan.
- **Occasional hang on start** in Vulkan mode, see the tip above, a restart fixes it.
- **Performance:** it's playable, but it's Rosetta + Wine + MoltenVK, so don't expect Windows numbers. On the M3 Max it's roughly 25–45 fps (~40 on average) in freeroam with a few players, and it's limited by rendering rather than CPU.
- The launcher's **self updater is off** on macOS (`--no-update`), otherwise it would swap this build for a Windows or Linux one. Pull and rebuild to update.
- The launcher **can't start the game itself** on macOS (`--no-launch`), that's what `run-beammp.sh` is for.
- `Could not initialize online service provider: Steam` in the game log is normal, Steam isn't running inside Wine. BeamMP doesn't need it.

---

## How it works

### Launcher changes

The launcher already had Linux support, so macOS mostly takes the same code paths:

- `__APPLE__` goes through the Linux (POSIX) branches, with the few Mac bits it needs: `environ` isn't declared by `unistd.h`, `siginfo_t` comes from `<signal.h>`, `__try` isn't mapped by libc++
- `cpp-httplib` is used header only, so Homebrew's zlib enabled build doesn't clash with the bundled miniz. CoreFoundation and Security get linked because httplib reads the system keychain for TLS
- the macOS Steam library folder is added to the game path search
- `--user-path` now works on Linux / macOS too (it was ignored outside Windows before), that's how the launcher finds the game's user folder inside the Wine prefix

The launcher talks to the game over `127.0.0.1:4444`, and Wine shares the Mac's localhost, so that part needs no changes at all.

### Vulkan compat layer (`macos/vkcompat`)

BeamNG's Vulkan renderer needs four device features that MoltenVK (Vulkan on top of Metal) doesn't have. `VK_LAYER_BEAMNG_macos_compat` sits between Wine and MoltenVK and fills them in:

| Feature | What the layer does |
|---|---|
| `robustBufferAccess2` | reports it as supported, strips it before MoltenVK sees it |
| `sparseBinding`, `sparseResidencyBuffer` | sparse buffers are created as normal buffers, fully backed by memory up front, so everything is always "resident". `vkQueueBindSparse` becomes an empty submit that still waits / signals |
| `drawIndirectCount` | `vkCmdDraw(Indexed)IndirectCount` becomes `vkCmdDraw(Indexed)Indirect` with the max draw count |

`run-beamng.sh` loads it through the Vulkan loader (`VK_LAYER_PATH` / `VK_INSTANCE_LAYERS`), nothing in the game or Wine gets modified. It's built x86_64 because Wine runs under Rosetta. `BMCOMPAT_LOG=1` logs everything it intercepts.

### Console (`macos/wine/allocconsole-attach-parent.patch`)

The game's `-console` is a GUI app calling `AllocConsole()`. Under Wine that normally opens a new console window with its own theme, and BeamNG's colour codes show up as raw text. The patch makes `AllocConsole()` attach to the parent console instead when `WINE_ALLOCCONSOLE_ATTACH_PARENT=1` is set. `run-beamng.sh` starts the game through `wine cmd /c`, which gets your terminal as its console, so the game ends up with a real terminal: proper colours and the Lua prompt reading your input.

Only `kernelbase.dll` is rebuilt, and it goes into a copy of Wine (an APFS clone, so it takes no extra space). The original Wine install isn't touched.
