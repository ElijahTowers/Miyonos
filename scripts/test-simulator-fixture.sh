#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/desktop"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
mkdir -p "$BUILD"

cd "$ROOT"
c++ -std=c++17 -O2 -g -Wall -Wextra -Wpedantic \
  "-DMIYONOS_VERSION=\"$VERSION\"" -Isrc \
  tests/simulator/fixture_tests.cpp src/simulator/mock_sonos.cpp \
  src/network/http.cpp src/network/https_artwork.cpp src/sonos/xml.cpp src/sonos/protocol.cpp \
  src/platform/logger.cpp \
  -pthread -lcurl -o "$BUILD/miyonos_simulator_fixture_tests"
"$BUILD/miyonos_simulator_fixture_tests"
