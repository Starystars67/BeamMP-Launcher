#!/usr/bin/env bash
# Starts the native macOS BeamMP Launcher and then BeamNG.drive under Wine (run-beamng.sh).
# The launcher installs / updates the BeamMP mod in the game's user folder inside the Wine prefix and connects the
# game to servers over 127.0.0.1:4444 (Wine shares the Mac's localhost), then you join from the in game server list.
#   ./run-beammp.sh            -> launcher + game (Vulkan renderer)
#   ./run-beammp.sh dx11       -> launcher + game with another renderer
#   BEAMMP_LAUNCHER_ARGS="--no-download" ./run-beammp.sh -> extra launcher arguments, e.g. to test a local mod build
# Launcher log: build-macos/Launcher.log
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WINE_HOME="${BEAMMP_WINE_HOME:-$HOME/Wine}"
LAUNCHER_DIR="${BEAMMP_LAUNCHER_DIR:-$HERE/../build-macos}"
USER_FOLDER="$WINE_HOME/beamng-prefix/drive_c/users/$USER/AppData/Local/BeamNG/BeamNG.drive" # the folder that contains "current"
GFX="${1:-vk}"

[ -x "$LAUNCHER_DIR/BeamMP-Launcher" ] || { echo "Build the launcher first, see README-macOS.md"; exit 1; }
[ -d "$USER_FOLDER" ] || { echo "Start the game once with $HERE/run-beamng.sh so it creates its user folder"; exit 1; }

pkill -f "BeamMP-Launcher --no-launch" 2>/dev/null || true
mkdir -p "$WINE_HOME/logs"

cd "$LAUNCHER_DIR"
# --no-launch: the launcher can't start the Windows game itself on macOS, run-beamng.sh does that below
# --no-update: the self updater would swap this native build for a Windows / Linux one
# shellcheck disable=SC2086
TERM="${TERM:-xterm-256color}" ./BeamMP-Launcher --no-launch --no-update ${BEAMMP_LAUNCHER_ARGS:-} --user-path "$USER_FOLDER" > "$WINE_HOME/logs/launcher-stdout.log" 2>&1 &
echo "Launcher started (pid $!), waiting for it to listen on 4444..."
for _ in $(seq 1 60); do
	lsof -nP -iTCP:4444 -sTCP:LISTEN >/dev/null 2>&1 && break
	sleep 1
done
lsof -nP -iTCP:4444 -sTCP:LISTEN >/dev/null 2>&1 || { echo "Launcher didn't start, see $LAUNCHER_DIR/Launcher.log"; exit 1; }
echo "Launcher ready, starting the game"
exec "$HERE/run-beamng.sh" "$GFX"
