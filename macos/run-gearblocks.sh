#!/usr/bin/env bash
# Runs GearBlocks (Windows build, Unity 6 / IL2CPP) on macOS through Wine staging + Rosetta 2.
# It gets a Wine prefix of its own, so the BeamNG prefix is never touched.
#   ./run-gearblocks.sh            -> Direct3D 11 through DXVK (Vulkan on MoltenVK), the one that works
#   ./run-gearblocks.sh wined3d    -> Direct3D 11 through Wine's own wined3d (fails: Apple's OpenGL 4.1 is too old)
#   ./run-gearblocks.sh vulkan     -> Unity's Vulkan renderer (MoltenVK)
#   ./run-gearblocks.sh d3d12      -> Direct3D 12 (vkd3d)
# GearBlocks needs a running Steam client (SteamAPI_Init), so the Windows Steam client installed in this
# prefix is started first if it is not running; sign in to it once, it remembers you after that.
# Anything after the renderer goes to the game. Logs: $BEAMMP_WINE_HOME/logs.
#   ./run-gearblocks.sh steam      -> only start Steam (to sign in), do not start the game
set -euo pipefail

WINE_HOME="${BEAMMP_WINE_HOME:-$HOME/Wine}"
GAME_DIR="${GEARBLOCKS_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/GearBlocks}"
WINE_APP="$WINE_HOME/Wine Staging.app/Contents/Resources/wine"
export WINEPREFIX="$WINE_HOME/gearblocks-prefix"

[ -x "$WINE_APP/bin/wine" ] || { echo "Run macos/setup-wine.sh first ($WINE_APP is missing)"; exit 1; }
[ -f "$GAME_DIR/GearBlocks.exe" ] || { echo "GearBlocks.exe not found in $GAME_DIR (set GEARBLOCKS_DIR)"; exit 1; }

GFX="${1:-dxvk}"; shift || true
STEAM_ONLY=0
if [ "$GFX" = "steam" ]; then STEAM_ONLY=1; GFX=dxvk; fi
case "$GFX" in
	dxvk|wined3d) RENDERER=(-force-d3d11) ;;
	vulkan) RENDERER=(-force-vulkan) ;;
	d3d12) RENDERER=(-force-d3d12) ;;
	*) echo "Unknown renderer '$GFX' (dxvk, wined3d, vulkan, d3d12)"; exit 1 ;;
esac

# DXVK: copy its DLLs into this prefix (only this one; originals are kept as *.wine-orig).
# Taken from $DXVK_DIR, by default the copy that Whisky ships.
DLL_OVERRIDES="mscoree,mshtml="
if [ "$GFX" = "dxvk" ]; then
	DXVK_DIR="${DXVK_DIR:-$HOME/Library/Application Support/com.isaacmarovitz.Whisky/Libraries/DXVK}"
	[ -f "$DXVK_DIR/x64/d3d11.dll" ] || { echo "No DXVK in $DXVK_DIR (set DXVK_DIR to a folder with x64/d3d11.dll)"; exit 1; }
	SYS32="$WINEPREFIX/drive_c/windows/system32"
	for dll in d3d11 d3d10core dxgi; do
		if [ -f "$SYS32/$dll.dll" ] && [ ! -f "$SYS32/$dll.dll.wine-orig" ]; then cp "$SYS32/$dll.dll" "$SYS32/$dll.dll.wine-orig"; fi
		cp "$DXVK_DIR/x64/$dll.dll" "$SYS32/$dll.dll"
	done
	DLL_OVERRIDES="$DLL_OVERRIDES;d3d11,d3d10core,dxgi=n,b"
	export DXVK_LOG_LEVEL="${DXVK_LOG_LEVEL:-warn}"
	export DXVK_LOG_PATH="$WINE_HOME/logs"
fi

# no Mono / Gecko download prompts on first start: a Unity IL2CPP game needs neither
# BepInEx loads through a winhttp.dll proxy (Doorstop); Wine must prefer that one
[ -f "$GAME_DIR/winhttp.dll" ] && DLL_OVERRIDES="$DLL_OVERRIDES;winhttp=n,b"
export WINEDLLOVERRIDES="$DLL_OVERRIDES"
export WINEDEBUG="${WINEDEBUG:-fixme-all,err+all}"
# MoltenVK warns about its descriptor pool on every frame in busy scenes (gigabytes of log in minutes): errors only.
export MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-1}"

mkdir -p "$WINE_HOME/logs"

STEAM_DIR="$WINEPREFIX/drive_c/Program Files (x86)/Steam"
steam_running() { pgrep -f "steamwebhelper.exe" >/dev/null 2>&1 && pgrep -f "gearblocks-prefix|steam.exe" >/dev/null 2>&1; }
if [ -f "$STEAM_DIR/steam.exe" ]; then
	# the game is told its app id so it can start without being launched by Steam itself
	[ -f "$GAME_DIR/steam_appid.txt" ] || printf '1305080' > "$GAME_DIR/steam_appid.txt"
	if ! steam_running; then
		echo "Starting the Windows Steam client in this prefix (sign in if it asks)"
		( cd "$STEAM_DIR" && exec "$WINE_APP/bin/wine" steam.exe -no-cef-sandbox -silent > "$WINE_HOME/logs/gearblocks-steam.log" 2>&1 ) &
		for _ in $(seq 1 60); do sleep 2; steam_running && break; done
		sleep 10
	fi
else
	echo "Warning: Steam is not installed in this prefix, GearBlocks will stop at SteamAPI_Init"
fi
[ "$STEAM_ONLY" = 1 ] && { echo "Steam is running; start the game with ./run-gearblocks.sh"; exit 0; }

STAMP="$(date +%Y%m%d-%H%M%S)"
WINE_LOG="$WINE_HOME/logs/gearblocks-$GFX-$STAMP.log"
PLAYER_LOG="$WINE_HOME/logs/gearblocks-$GFX-$STAMP-player.log"
echo "Starting GearBlocks with $GFX"
echo "  wine log:   $WINE_LOG"
echo "  player log: $PLAYER_LOG"

cd "$GAME_DIR"
exec "$WINE_APP/bin/wine" GearBlocks.exe "${RENDERER[@]}" -screen-fullscreen 0 -screen-width 1280 -screen-height 720 -logFile "$PLAYER_LOG" "$@" > "$WINE_LOG" 2>&1
