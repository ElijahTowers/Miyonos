#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/desktop"
mkdir -p "$BUILD"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "SDL2 development files are required (sdl2-config was not found)." >&2
  exit 1
fi
read -r -a SDL_CFLAGS <<<"$(sdl2-config --cflags)"
read -r -a SDL_LIBS <<<"$(sdl2-config --libs)"
CXX=(c++)
if [[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]]; then
  SDL_LIBRARY="$(sdl2-config --prefix)/lib/libSDL2.dylib"
  if [[ -f "$SDL_LIBRARY" ]] && file "$SDL_LIBRARY" | grep -q x86_64 &&
      ! file "$SDL_LIBRARY" | grep -q arm64; then
    CXX=(clang++ -arch x86_64)
  fi
fi

cd "$ROOT"
"${CXX[@]}" -std=c++17 -O2 -g -Wall -Wextra -Wpedantic \
  -DMIYONOS_ENABLE_SIMULATOR=1 -Isrc "${SDL_CFLAGS[@]}" \
  tests/simulator/input_tests.cpp src/platform/input.cpp \
  src/simulator/simulator_shell.cpp src/ui/bitmap_font.cpp \
  "${SDL_LIBS[@]}" -o "$BUILD/miyonos_simulator_input_tests"
"$BUILD/miyonos_simulator_input_tests"
