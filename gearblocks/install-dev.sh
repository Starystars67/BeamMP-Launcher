#!/usr/bin/env bash
# Development install of the BeamMP plugin and mod into a GearBlocks folder (the one that has BepInEx), and the mod
# into the Wine prefix it runs in. Builds the plugin first (needs the .NET SDK, see gearblocks/plugin).
#   gearblocks/install-dev.sh [game folder] [wine prefix]
# Defaults: ~/Wine/gearblocks-copy and ~/Wine/gearblocks-prefix (what macos/run-gearblocks.sh uses with GEARBLOCKS_DIR).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="${1:-$HOME/Wine/gearblocks-copy}"
PREFIX="${2:-$HOME/Wine/gearblocks-prefix}"
export PATH="$HOME/.dotnet:$PATH" DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1

[ -d "$GAME/BepInEx" ] || { echo "No BepInEx in $GAME (see docs/GEARBLOCKS.md)"; exit 1; }

( cd "$HERE/plugin" && dotnet build -c Release -p:GameDir="$GAME" | grep -E "error|Build succeeded" )
mkdir -p "$GAME/BepInEx/plugins"
cp "$HERE/plugin/bin/Release/net6.0/BeamMP.GearBlocks.dll" "$GAME/BepInEx/plugins/"
echo "Plugin installed in $GAME/BepInEx/plugins"

SCRIPTS="$PREFIX/drive_c/users/$USER/AppData/LocalLow/SmashHammer Games/GearBlocks/ScriptMods"
mkdir -p "$SCRIPTS"
rm -rf "$SCRIPTS/BeamMP"
cp -R "$HERE/mod/BeamMP" "$SCRIPTS/BeamMP"
echo "Mod installed in $SCRIPTS/BeamMP (start it from the game's Script Mods menu)"
