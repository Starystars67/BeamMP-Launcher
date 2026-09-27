#!/usr/bin/env bash
# Runs BeamNG.drive on macOS (Apple Silicon) through Wine staging + Rosetta 2, set up by setup-wine.sh.
#   ./run-beamng.sh            -> Vulkan renderer through our MoltenVK compat layer (./vkcompat), recommended
#   ./run-beamng.sh dx11       -> D3D11 (wined3d)
#   ./run-beamng.sh d3d12      -> D3D12 (vkd3d), menu works but crashes on the first gameplay frame for now
# Anything after the renderer goes to the game, e.g. ./run-beamng.sh vk -enablemcp
#
# Run it from a terminal and BeamNG's console lives in that terminal, with colours and the GE Lua prompt.
# Without a terminal it logs to $BEAMMP_WINE_HOME/logs instead.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WINE_HOME="${BEAMMP_WINE_HOME:-$HOME/Wine}"
GAME_DIR="${BEAMNG_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/BeamNG.drive}/Bin64"
WINE_APP="$WINE_HOME/Wine Staging BeamNG.app/Contents/Resources/wine"
export WINEPREFIX="$WINE_HOME/beamng-prefix"

[ -d "$WINEPREFIX/drive_c" ] || { echo "Run $HERE/setup-wine.sh first"; exit 1; }

GFX="${1:-vk}"; shift || true

# Wine's own VC++ runtime is missing bits the game uses, use the real Microsoft DLLs (installed by setup-wine.sh)
export WINEDLLOVERRIDES="msvcp140,msvcp140_1,msvcp140_2,msvcp140_atomic_wait,msvcp140_codecvt_ids,vcruntime140,vcruntime140_1,concrt140=n,b"
# makes the game's console attach to this terminal (patched kernelbase.dll, see wine/allocconsole-attach-parent.patch)
export WINE_ALLOCCONSOLE_ATTACH_PARENT=1

if [ "$GFX" = "vk" ]; then
	# BeamNG's Vulkan renderer needs a few features MoltenVK doesn't have, the layer fills them in
	[ -f "$HERE/vkcompat/libbmcompat_layer.dylib" ] || "$HERE/vkcompat/build.sh"
	export VK_LAYER_PATH="$HERE/vkcompat"
	export VK_INSTANCE_LAYERS="VK_LAYER_BEAMNG_macos_compat"
	export VK_LOADER_LAYERS_ENABLE="VK_LAYER_BEAMNG_macos_compat"
fi

cd "$GAME_DIR"
if [ -t 0 ] && [ -t 1 ]; then
	# keep Wine/MoltenVK chatter out of the game console
	export WINEDEBUG="${WINEDEBUG:--all}"
	export MVK_CONFIG_LOG_LEVEL="${MVK_CONFIG_LOG_LEVEL:-1}"
	export TERM="${TERM:-xterm-256color}"
	export COLORTERM="${COLORTERM:-truecolor}"
	echo "Starting BeamNG.drive with -gfx $GFX, console in this terminal"
	# cmd is a console program so Wine hands it this terminal, the game then attaches to it
	exec "$WINE_APP/bin/wine" cmd /c BeamNG.drive.x64.exe -gfx "$GFX" -console "$@"
fi

export WINEDEBUG="${WINEDEBUG:-fixme-all,err+all}"
mkdir -p "$WINE_HOME/logs"
LOG="$WINE_HOME/logs/beamng-$GFX-$(date +%Y%m%d-%H%M%S).log"
echo "Starting BeamNG.drive with -gfx $GFX, wine log: $LOG"
exec "$WINE_APP/bin/wine" BeamNG.drive.x64.exe -gfx "$GFX" -console "$@" > "$LOG" 2>&1
