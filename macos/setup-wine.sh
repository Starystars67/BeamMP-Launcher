#!/usr/bin/env bash
# Sets up everything BeamNG.drive needs to run on macOS (Apple Silicon), all inside one folder, nothing system wide:
#   1. WineHQ staging 11.18 (Gcenx's macOS build)
#   2. a copy of it with a patched kernelbase.dll so the game's -console works properly in your terminal
#   3. a Wine prefix with the Microsoft VC++ runtime the game needs
#   4. the Vulkan compat layer (./vkcompat)
#
# Needs: Rosetta 2, Xcode command line tools, Homebrew with: mingw-w64 bison flex
#
#   BEAMMP_WINE_HOME  where everything goes (default ~/Wine)
#   BEAMNG_DIR        the Windows build of the game (default: Steam's folder)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WINE_HOME="${BEAMMP_WINE_HOME:-$HOME/Wine}"
GAME_DIR="${BEAMNG_DIR:-$HOME/Library/Application Support/Steam/steamapps/common/BeamNG.drive}"
WINE_VERSION="11.18"

WINE_APP="$WINE_HOME/Wine Staging.app"
WINE_BEAMNG_APP="$WINE_HOME/Wine Staging BeamNG.app"
WINEPREFIX="$WINE_HOME/beamng-prefix"

# ============================================================================
# Checks
# ============================================================================

arch -x86_64 /usr/bin/true 2>/dev/null || { echo "Rosetta 2 is needed: softwareupdate --install-rosetta --agree-to-license"; exit 1; }
command -v clang >/dev/null || { echo "Xcode command line tools are needed: xcode-select --install"; exit 1; }
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
for tool in x86_64-w64-mingw32-gcc "$BREW_PREFIX/opt/bison/bin/bison" "$BREW_PREFIX/opt/flex/bin/flex"; do
	command -v "$tool" >/dev/null || { echo "Missing $tool, run: brew install mingw-w64 bison flex"; exit 1; }
done
[ -f "$GAME_DIR/Bin64/BeamNG.drive.x64.exe" ] || { echo "Can't find the game in $GAME_DIR (set BEAMNG_DIR), see README-macOS.md"; exit 1; }

mkdir -p "$WINE_HOME"
cd "$WINE_HOME"

# ============================================================================
# 1. Wine
# ============================================================================

if [ ! -d "$WINE_APP" ]; then
	[ -f "wine-staging-$WINE_VERSION-osx64.tar.xz" ] || curl -fL --progress-bar -o "wine-staging-$WINE_VERSION-osx64.tar.xz" \
		"https://github.com/Gcenx/macOS_Wine_builds/releases/download/$WINE_VERSION/wine-staging-$WINE_VERSION-osx64.tar.xz"
	tar -xf "wine-staging-$WINE_VERSION-osx64.tar.xz"
	xattr -dr com.apple.quarantine "$WINE_APP" 2>/dev/null || true
	echo "Wine $WINE_VERSION installed"
fi

# ============================================================================
# 2. Patched kernelbase.dll (console)
# ============================================================================
# BeamNG is a GUI program that opens its console with AllocConsole(), under Wine that's a new window with its own
# theme that shows the colour codes as text. With the patch (and WINE_ALLOCCONSOLE_ATTACH_PARENT=1, set by
# run-beamng.sh) it attaches to the terminal you started it from instead, so you get colours and the Lua prompt.
# Only kernelbase.dll is built from source and dropped into a copy of Wine, the original stays untouched.

if [ ! -d "$WINE_BEAMNG_APP" ]; then
	mkdir -p src
	cd src
	[ -f "wine-$WINE_VERSION.tar.xz" ] || curl -fL --progress-bar -o "wine-$WINE_VERSION.tar.xz" "https://dl.winehq.org/wine/source/11.x/wine-$WINE_VERSION.tar.xz"
	rm -rf "wine-$WINE_VERSION"
	tar -xf "wine-$WINE_VERSION.tar.xz"
	cd "wine-$WINE_VERSION"
	patch -p1 < "$HERE/wine/allocconsole-attach-parent.patch"
	mkdir -p build
	cd build
	echo "Building kernelbase.dll (takes a few minutes)..."
	export PATH="$BREW_PREFIX/opt/bison/bin:$BREW_PREFIX/opt/flex/bin:$PATH"
	../configure --enable-archs=x86_64 --disable-tests --without-x --without-freetype --without-gstreamer --without-sdl \
		--without-cups --without-gphoto --without-sane --without-krb5 --without-netapi --without-pcap --without-usb \
		--without-v4l2 --without-vulkan --without-opencl --without-gnutls --without-dbus --without-capi --without-coreaudio \
		--without-inotify --without-unwind --without-ffmpeg > ../configure.log 2>&1 || { echo "configure failed, see $(pwd)/../configure.log"; exit 1; }
	make -j"$(sysctl -n hw.ncpu)" dlls/kernelbase/all > ../make.log 2>&1 || { echo "build failed, see $(pwd)/../make.log"; exit 1; }
	cd "$WINE_HOME"
	cp -cR "$WINE_APP" "$WINE_BEAMNG_APP" # APFS clone, takes no extra space
	cp "src/wine-$WINE_VERSION/build/dlls/kernelbase/x86_64-windows/kernelbase.dll" \
		"$WINE_BEAMNG_APP/Contents/Resources/wine/lib/wine/x86_64-windows/kernelbase.dll"
	echo "Patched Wine ready: $WINE_BEAMNG_APP"
fi

# ============================================================================
# 3. Prefix + VC++ runtime
# ============================================================================

if [ ! -d "$WINEPREFIX/drive_c" ]; then
	echo "Creating the Wine prefix and installing the VC++ runtime..."
	export WINEPREFIX
	export WINEDEBUG=-all
	export MVK_CONFIG_LOG_LEVEL=1 # MoltenVK prints every extension otherwise
	"$WINE_BEAMNG_APP/Contents/Resources/wine/bin/wineboot" -i
	(cd "$GAME_DIR/Bin64" && "$WINE_BEAMNG_APP/Contents/Resources/wine/bin/wine" VC_redist.x64.exe /install /quiet /norestart)
	"$WINE_BEAMNG_APP/Contents/Resources/wine/bin/wineserver" -w
	echo "Prefix ready: $WINEPREFIX"
fi

# ============================================================================
# 4. Vulkan compat layer
# ============================================================================

[ -f "$HERE/vkcompat/libbmcompat_layer.dylib" ] || "$HERE/vkcompat/build.sh"

echo "All done, start the game with $HERE/run-beamng.sh or BeamMP with $HERE/run-beammp.sh"
