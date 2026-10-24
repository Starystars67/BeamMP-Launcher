#!/usr/bin/env bash
# Builds the launcher for macOS (Apple Silicon) as one zip you can hand to someone: the Flutter app with the engine
# inside it (Contents/Helpers/BeamMP-Launcher-Console) and the GearBlocks files (plugin and script mod, signed) next
# to the engine (Contents/Helpers/gearblocks), so GearBlocks multiplayer installs without any server.
#
#   packaging/package-macos.sh [output folder]      (default ~/Downloads)
#
# Needs: cmake, ninja, Homebrew openssl@3 (linked statically), Flutter, the .NET SDK (for the plugin) and a GearBlocks
# folder with BepInEx to build the plugin against ($GEARBLOCKS_REFS, default ~/Wine/gearblocks-copy).
# The bundle is signed with an alpha key made on first use in ~/.beammp-alpha (the private half never goes into the
# zip). It is only good for builds you hand out yourself; BeamMP's real signing key is a separate, open decision.
# The app is signed ad hoc only (no Apple developer identity): see README-share.txt in the zip for opening it.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$HOME/Downloads}"
REFS="${GEARBLOCKS_REFS:-$HOME/Wine/gearblocks-copy}"
WORK="${PACKAGE_WORK:-${TMPDIR:-/tmp}/beammp-package-macos}"
KEYS="$HOME/.beammp-alpha"
export PATH="$HOME/Downloads/flutter/bin:$HOME/.dotnet:$PATH"

VERSION="$(sed -n 's/^version: *\([0-9.]*\).*/\1/p' "$HERE/ui/pubspec.yaml")"
NAME="BeamMP-Launcher-macos-arm64-$VERSION"
mkdir -p "$WORK" "$OUT"

echo "== engine"
cmake -S "$HERE" -B "$WORK/engine" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBEAMMP_EXECUTABLE_NAME=BeamMP-Launcher-Console \
	-DOPENSSL_USE_STATIC_LIBS=ON -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
	-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 >/dev/null
cmake --build "$WORK/engine" >/dev/null
if otool -L "$WORK/engine/BeamMP-Launcher-Console" | grep -q "/opt/homebrew"; then
	echo "The engine links a Homebrew library, it would not run elsewhere"; exit 1
fi

echo "== app"
( cd "$HERE/ui" && rm -rf build/macos/Build/Products/Release && flutter build macos --release >/dev/null )
APP="$HERE/ui/build/macos/Build/Products/Release/BeamMP-Launcher.app"

echo "== GearBlocks files"
if [ ! -f "$KEYS/gearblocks-alpha.pem" ]; then
	mkdir -p "$KEYS" && chmod 700 "$KEYS"
	python3 "$HERE/gearblocks/release.py" keygen "$KEYS/gearblocks-alpha.pem" "$KEYS/trust.txt"
fi
rm -rf "$WORK/gearblocks"
python3 "$HERE/gearblocks/release.py" publish "$WORK/gearblocks" --base-url bundle: --key "$KEYS/gearblocks-alpha.pem" \
	--game "$REFS" --sequence "$(date +%s)"
cp "$KEYS/trust.txt" "$WORK/gearblocks/trust.txt"
python3 "$HERE/gearblocks/release.py" verify "$WORK/gearblocks/manifest.json" "$WORK/gearblocks/trust.txt"

echo "== assemble"
STAGE="$WORK/stage"
rm -rf "$STAGE" && mkdir -p "$STAGE/$NAME"
ditto "$APP" "$STAGE/$NAME/BeamMP-Launcher.app"
HELPERS="$STAGE/$NAME/BeamMP-Launcher.app/Contents/Helpers"
mkdir -p "$HELPERS"
cp "$WORK/engine/BeamMP-Launcher-Console" "$HELPERS/"
ditto "$WORK/gearblocks" "$HELPERS/gearblocks"
codesign --force --deep --sign - "$STAGE/$NAME/BeamMP-Launcher.app" 2>&1 | grep -v "replacing existing signature" || true
cp "$HERE/packaging/README-share-macos.txt" "$STAGE/$NAME/README-share.txt"

echo "== zip"
rm -f "$OUT/$NAME.zip"
( cd "$STAGE" && ditto -c -k --keepParent "$NAME" "$OUT/$NAME.zip" )
ls -la "$OUT/$NAME.zip"
