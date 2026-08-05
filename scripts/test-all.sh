#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT/scripts/run-tests.sh"
"$ROOT/scripts/test-simulator-input.sh"
"$ROOT/scripts/test-simulator-fixture.sh"
"$ROOT/scripts/build-desktop.sh"
"$ROOT/scripts/test-simulator-screenshots.sh"
"$ROOT/scripts/build-simulator-app.sh"

if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
  "$ROOT/scripts/build-miyoo.sh"
else
  echo "Docker is unavailable; the ARM and OnionOS package build was skipped." >&2
fi
