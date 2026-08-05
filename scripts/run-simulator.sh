#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCENARIO="grouped"
LIVE_SONOS=0
SCREEN_ONLY=0
SKIP_BUILD=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      [[ $# -ge 2 ]] || { echo "--scenario requires a name" >&2; exit 2; }
      SCENARIO="$2"
      shift 2
      ;;
    --live-sonos)
      LIVE_SONOS=1
      shift
      ;;
    --screen-only)
      SCREEN_ONLY=1
      shift
      ;;
    --no-build)
      SKIP_BUILD=1
      shift
      ;;
    --help)
      echo "Usage: ./scripts/run-simulator.sh [--scenario NAME] [--live-sonos] [--screen-only]"
      echo "Scenarios: normal, multi-room, grouped, long-queue, no-artwork, slow, offline, coordinator-change"
      exit 0
      ;;
    *)
      echo "Unknown simulator option: $1" >&2
      exit 2
      ;;
  esac
done

case "$SCENARIO" in
  normal|multi-room|grouped|long-queue|no-artwork|slow|offline|coordinator-change) ;;
  *) echo "Unknown simulator scenario: $SCENARIO" >&2; exit 2 ;;
esac

if [[ "$SKIP_BUILD" == 0 ]]; then
  "$ROOT/scripts/build-desktop.sh"
fi

BINARY="$ROOT/build/desktop/miyonos"
[[ -x "$BINARY" ]] || { echo "Desktop build is missing." >&2; exit 1; }
SIMULATOR_ROOT="$ROOT/build/simulator/sdcard"
MODE_NAME="fixture-$SCENARIO"
[[ "$LIVE_SONOS" == 0 ]] || MODE_NAME="live-sonos"
DATA_DIR="$SIMULATOR_ROOT/$MODE_NAME/App/Miyonos/data"
mkdir -p "$DATA_DIR" "$ROOT/build/simulator/logs"

ARGS=(--simulator --scenario "$SCENARIO" --data-dir "$DATA_DIR")
if [[ "$SCREEN_ONLY" == 1 ]]; then
  ARGS+=(--screen-only)
fi

if [[ "$LIVE_SONOS" == 1 ]]; then
  ARGS+=(--live-sonos)
  "$BINARY" "${ARGS[@]}"
  exit $?
fi

"$BINARY" "${ARGS[@]}"
