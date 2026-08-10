#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD="$ROOT/build/desktop"
mkdir -p "$BUILD"
cd "$ROOT"

if ! command -v sdl2-config >/dev/null 2>&1; then
  echo "SDL2 headers are required for the shared input abstraction." >&2
  exit 1
fi
read -r -a SDL_CFLAGS <<<"$(sdl2-config --cflags)"

c++ -std=c++17 -O2 -g -Wall -Wextra -Wpedantic \
  "-DMIYONOS_VERSION=\"$VERSION\"" "-DMIYONOS_SOURCE_DIR=\"$ROOT\"" \
  -Isrc "${SDL_CFLAGS[@]}" \
  tests/unit/tests.cpp src/network/http.cpp src/network/https_artwork.cpp src/sonos/xml.cpp \
  src/sonos/protocol.cpp src/storage/settings.cpp \
  src/storage/artwork_cache.cpp src/platform/logger.cpp \
  src/platform/battery.cpp \
  src/app/controller.cpp -lcurl -o "$BUILD/miyonos_tests"

MOCK_PID=""
cleanup() {
  if [[ -n "$MOCK_PID" ]]; then
    kill "$MOCK_PID" 2>/dev/null || true
    wait "$MOCK_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if curl -fsS --max-time 1 http://127.0.0.1:1400/xml/device_description.xml \
    >/dev/null 2>&1; then
  echo "Using the Sonos-compatible fixture already listening on port 1400."
else
  python3 tests/mock_sonos/mock_server.py --scenario grouped \
    >"$BUILD/mock-sonos.log" 2>&1 &
  MOCK_PID=$!
  for _ in {1..30}; do
    if curl -fsS --max-time 1 \
        http://127.0.0.1:1400/xml/device_description.xml >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done
fi

# Keep the controller integration test pinned to its local fixture. A real
# Sonos player may also reply to SSDP on the developer's Wi-Fi.
MIYONOS_DISABLE_SSDP=1 MIYONOS_INTEGRATION_IP=127.0.0.1 "$BUILD/miyonos_tests"

if [[ -f "$ROOT/dist/Miyonos-$VERSION-OnionOS.zip" ]]; then
  "$ROOT/tests/integration/test_wifi_installer.sh"
else
  echo "Wi-Fi lifecycle test skipped until a target package is built."
fi

python3 "$ROOT/tests/integration/test_wifi_web.py"
python3 "$ROOT/tests/integration/test_universal_browser_installer.py"
