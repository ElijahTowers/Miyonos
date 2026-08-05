#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD="$ROOT/build/desktop"
mkdir -p "$BUILD"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "SDL2 development files are required (sdl2-config was not found)." >&2
  exit 1
fi

SOURCES=(
  src/main.cpp src/app/runtime.cpp
  src/network/http.cpp src/network/https_artwork.cpp src/sonos/xml.cpp src/sonos/protocol.cpp
  src/storage/settings.cpp src/storage/artwork_cache.cpp src/platform/logger.cpp
  src/platform/battery.cpp
  src/platform/input.cpp src/platform/frame_presenter.cpp
  src/simulator/mock_sonos.cpp src/simulator/simulator_shell.cpp
  src/app/controller.cpp src/ui/bitmap_font.cpp src/ui/renderer.cpp
)

cd "$ROOT"
COMMON=(-std=c++17 -O2 -g -Wall -Wextra -Wpedantic
  "-DMIYONOS_VERSION=\"$VERSION\"" -DMIYONOS_ENABLE_SIMULATOR=1 -Isrc)
read -r -a SDL_CFLAGS <<<"$(sdl2-config --cflags)"
read -r -a SDL_LIBS <<<"$(sdl2-config --libs)"

CXX=(c++)
EXTRA_LIBS=()
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]]; then
  SDL_LIBRARY="$(sdl2-config --prefix)/lib/libSDL2.dylib"
  if [[ -f "$SDL_LIBRARY" ]] &&
      file "$SDL_LIBRARY" | grep -q 'x86_64' &&
      ! file "$SDL_LIBRARY" | grep -q 'arm64'; then
    echo "Using the installed Intel SDL2 through Rosetta."
    CXX=(clang++ -arch x86_64)
  fi
fi
if [[ "$(uname -s)" == "Darwin" ]]; then
  EXTRA_LIBS=(-lcurl -framework ImageIO -framework CoreGraphics -framework CoreFoundation)
else
  EXTRA_LIBS=(-lcurl)
fi
"${CXX[@]}" "${COMMON[@]}" "${SDL_CFLAGS[@]}" "${SOURCES[@]}" \
  "${SDL_LIBS[@]}" "${EXTRA_LIBS[@]}" -o "$BUILD/miyonos"

file "$BUILD/miyonos"
SDL_VIDEODRIVER=dummy MIYONOS_DEVICE=1 \
  "$BUILD/miyonos" --frame-presenter-smoke
echo "Device frame-presenter smoke check passed."
echo "Desktop build: $BUILD/miyonos"
