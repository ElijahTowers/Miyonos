#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TOOLCHAIN_DIR="$("$ROOT/scripts/bootstrap-toolchain.sh")"
DEPS="$ROOT/build/miyoo-deps"
BUILD="$ROOT/build/miyoo"
SDL_COMMIT="0631abc8e8916db6f9bc7e2afd0c22913d092a29"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required for the reproducible linux/amd64 target build." >&2
  exit 1
fi
mkdir -p "$DEPS" "$BUILD"
if [[ ! -d "$DEPS/sdl2/.git" ]]; then
  git clone https://github.com/steward-fu/sdl2.git "$DEPS/sdl2"
fi
git -C "$DEPS/sdl2" fetch --quiet origin "$SDL_COMMIT"
git -C "$DEPS/sdl2" checkout --quiet --detach "$SDL_COMMIT"

docker build --platform linux/amd64 -t miyonos-miyoo-build:0.1 \
  -f "$ROOT/packaging/docker/Dockerfile.miyoo" "$ROOT/packaging/docker"

docker run --rm --platform linux/amd64 \
  -v "$ROOT:/work" \
  -v "$TOOLCHAIN_DIR:/toolchain:ro" \
  -w /work miyonos-miyoo-build:0.1 sh -lc '
set -eu
CXX=$(find /toolchain -type f -name arm-linux-gnueabihf-g++ | head -n 1)
STRIP=$(find /toolchain -type f -name arm-linux-gnueabihf-strip | head -n 1)
SYSROOT=$(find /toolchain -type d -path "*/sysroot" | head -n 1)
test -n "$CXX"
test -n "$STRIP"
test -n "$SYSROOT"
SDL_ROOT=/work/build/miyoo-deps/sdl2
SDL_LIB=$SDL_ROOT/prebuilt/640x480
mkdir -p /work/build/miyoo
"$CXX" --sysroot="$SYSROOT" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wno-psabi \
  -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
  -DMIYONOS_VERSION="\"'"$VERSION"'\"" -DMIYONOS_ONIONOS=1 \
  -I/work/src -I"$SDL_ROOT/sdl2/include" \
  /work/src/main.cpp /work/src/app/runtime.cpp \
  /work/src/network/http.cpp /work/src/network/https_artwork.cpp /work/src/sonos/xml.cpp \
  /work/src/sonos/protocol.cpp /work/src/storage/settings.cpp \
  /work/src/storage/artwork_cache.cpp /work/src/platform/logger.cpp \
  /work/src/platform/battery.cpp \
  /work/src/platform/input.cpp /work/src/platform/frame_presenter.cpp \
  /work/src/app/controller.cpp \
  /work/src/ui/bitmap_font.cpp /work/src/ui/renderer.cpp \
  -L"$SDL_LIB" -Wl,-rpath,\$ORIGIN/libs -Wl,--allow-shlib-undefined \
  -L"$SYSROOT/usr/lib" -l:libSDL2-2.0.so.0 -Wl,-Bstatic -l:libssl.a -l:libcrypto.a \
  -l:libatomic.a -Wl,-Bdynamic -pthread -ldl -lz -lstdc++fs \
  -o /work/build/miyoo/miyonos.debug
cp /work/build/miyoo/miyonos.debug /work/build/miyoo/miyonos
"$STRIP" --strip-unneeded /work/build/miyoo/miyonos
RUNTIME=/work/build/miyoo/runtime
mkdir -p "$RUNTIME"
find "$RUNTIME" -type f -delete
for library in \
  "$SDL_LIB/libSDL2-2.0.so.0" \
  "$SDL_LIB/libEGL.so" \
  "$SDL_LIB/libGLESv2.so" \
  "$SDL_ROOT/examples/libSDL2_image-2.0.so.0" \
  "$SDL_ROOT/examples/libpng16.so.16" \
  "$SDL_ROOT/examples/libz.so.1" \
  "$SDL_ROOT/examples/libjson-c.so.5"; do
  test -f "$library" && cp "$library" "$RUNTIME/"
done
LIBSTDCXX=$(find /toolchain -path "*/sysroot/usr/lib/libstdc++.so.6.0.25" -type f | head -n 1)
LIBGCC=$(find /toolchain -path "*/sysroot/usr/lib/libgcc_s.so.1" -type f | head -n 1)
LIBJPEG=$(find /toolchain -path "*/sysroot/usr/lib/libjpeg.so.9.4.0" -type f | head -n 1)
test -n "$LIBSTDCXX"
test -n "$LIBGCC"
test -n "$LIBJPEG"
cp "$LIBSTDCXX" "$RUNTIME/libstdc++.so.6"
cp "$LIBGCC" "$RUNTIME/libgcc_s.so.1"
cp "$LIBJPEG" "$RUNTIME/libjpeg.so.9"
for library in "$RUNTIME"/*; do
  "$STRIP" --strip-unneeded "$library" 2>/dev/null || true
done
file /work/build/miyoo/miyonos
'

"$ROOT/scripts/package-onion.sh"
