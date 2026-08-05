#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD="$ROOT/build/desktop"
OUTPUT="$ROOT/docs/images/now-playing"
mkdir -p "$BUILD" "$ROOT/docs/images"
read -r -a SDL_CFLAGS <<<"$(sdl2-config --cflags)"
read -r -a SDL_LIBS <<<"$(sdl2-config --libs)"

SOURCES=(
  tests/ui/render_preview.cpp src/ui/renderer.cpp src/ui/bitmap_font.cpp
  src/network/http.cpp src/network/https_artwork.cpp src/sonos/xml.cpp src/sonos/protocol.cpp
  src/storage/settings.cpp src/storage/artwork_cache.cpp src/platform/logger.cpp
  src/platform/battery.cpp
  src/app/controller.cpp
)
cd "$ROOT"
COMPILER=(c++)
EXTRA_LIBS=()
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" ]] &&
   file /usr/local/lib/libSDL2.dylib 2>/dev/null | grep -q x86_64; then
  COMPILER=(clang++ -arch x86_64)
fi
if [[ "$(uname -s)" == "Darwin" ]]; then
  EXTRA_LIBS=(-lcurl -framework ImageIO -framework CoreGraphics -framework CoreFoundation)
else
  EXTRA_LIBS=(-lcurl)
fi
"${COMPILER[@]}" -std=c++17 -O2 "-DMIYONOS_VERSION=\"$VERSION\"" \
  -Isrc "${SDL_CFLAGS[@]}" "${SOURCES[@]}" "${SDL_LIBS[@]}" "${EXTRA_LIBS[@]}" \
  -o "$BUILD/render_preview"
SDL_VIDEODRIVER=dummy "$BUILD/render_preview" "$OUTPUT.bmp"
if command -v sips >/dev/null 2>&1; then
  sips -s format png "$OUTPUT.bmp" --out "$OUTPUT.png" >/dev/null
  rm "$OUTPUT.bmp"
  echo "$OUTPUT.png"
else
  echo "$OUTPUT.bmp"
fi
