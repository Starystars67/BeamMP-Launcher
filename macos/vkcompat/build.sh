#!/usr/bin/env bash
# Builds the BeamNG macOS Vulkan compat layer (libbmcompat_layer.dylib) next to its manifest.
# x86_64 on purpose, Wine runs under Rosetta so the Vulkan loader it uses is x86_64 too.
# Grabs the Vulkan headers it builds against on first run.
set -euo pipefail
cd "$(dirname "$0")"

HEADERS_TAG="v1.4.350"
if [ ! -d Vulkan-Headers ]; then
	echo "Downloading Vulkan-Headers $HEADERS_TAG..."
	curl -fsSL -o headers.tar.gz "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/$HEADERS_TAG.tar.gz"
	tar -xzf headers.tar.gz
	mv Vulkan-Headers-* Vulkan-Headers
	rm headers.tar.gz
fi

clang -arch x86_64 -O2 -Wall -Wextra -Wno-unused-parameter -fvisibility=hidden -dynamiclib \
	-I Vulkan-Headers/include -o libbmcompat_layer.dylib bmcompat_layer.c
echo "built $(pwd)/libbmcompat_layer.dylib"
