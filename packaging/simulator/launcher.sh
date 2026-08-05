#!/usr/bin/env bash
set -euo pipefail

CONTENTS="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$CONTENTS/MacOS/miyonos-bin"
export MIYONOS_TLS_CA_FILE="$CONTENTS/Resources/trusted-spotify-artwork-roots.pem"
SUPPORT_ROOT="$HOME/Library/Application Support/Miyonos Simulator"
SCENARIO="grouped"
LIVE_SONOS=0
SCREEN_ONLY=0

if [[ $# -eq 0 && "${MIYONOS_SIMULATOR_DIRECT:-0}" != 1 ]]; then
  CHOICE="$(osascript <<'APPLESCRIPT'
set simulatorModes to {"Safe - Grouped rooms (recommended)", "Safe - Single room", "Safe - Multiple separate rooms", "Safe - Long queue", "Safe - Missing artwork", "Safe - Slow speaker", "Safe - Offline speakers", "Safe - Coordinator change", "Live - My Sonos system on this Wi-Fi"}
set selectedMode to choose from list simulatorModes with title "Miyonos Simulator" with prompt "Choose what you want to test. Safe modes never contact real speakers." default items {item 1 of simulatorModes} OK button name "Open" cancel button name "Cancel"
if selectedMode is false then return ""
return item 1 of selectedMode
APPLESCRIPT
)"
  case "$CHOICE" in
    "Safe - Grouped rooms (recommended)") SCENARIO="grouped" ;;
    "Safe - Single room") SCENARIO="normal" ;;
    "Safe - Multiple separate rooms") SCENARIO="multi-room" ;;
    "Safe - Long queue") SCENARIO="long-queue" ;;
    "Safe - Missing artwork") SCENARIO="no-artwork" ;;
    "Safe - Slow speaker") SCENARIO="slow" ;;
    "Safe - Offline speakers") SCENARIO="offline" ;;
    "Safe - Coordinator change") SCENARIO="coordinator-change" ;;
    "Live - My Sonos system on this Wi-Fi")
      CONFIRM="$(osascript -e 'button returned of (display alert "Use Live Sonos?" message "This mode connects to real Sonos speakers on the same Wi-Fi. Buttons such as play, volume, next, and grouping will control them immediately." as warning buttons {"Cancel", "Use Live Sonos"} default button "Cancel" cancel button "Cancel")' 2>/dev/null || true)"
      [[ "$CONFIRM" == "Use Live Sonos" ]] || exit 0
      LIVE_SONOS=1
      ;;
    *) exit 0 ;;
  esac
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scenario)
      [[ $# -ge 2 ]] || exit 2
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
    *)
      shift
      ;;
  esac
done

MODE_NAME="fixture-$SCENARIO"
[[ "$LIVE_SONOS" == 0 ]] || MODE_NAME="live-sonos"
DATA_DIR="$SUPPORT_ROOT/SDCARD/$MODE_NAME/App/Miyonos/data"
LOG_DIR="$SUPPORT_ROOT/logs"
mkdir -p "$DATA_DIR" "$LOG_DIR"

ARGS=(--simulator --scenario "$SCENARIO" --data-dir "$DATA_DIR")
if [[ "$LIVE_SONOS" == 1 ]]; then
  ARGS+=(--live-sonos)
fi
if [[ "$SCREEN_ONLY" == 1 ]]; then
  ARGS+=(--screen-only)
fi
set +e
"$BINARY" "${ARGS[@]}" >"$LOG_DIR/simulator.log" 2>&1
STATUS=$?
set -e
if [[ "$STATUS" -ne 0 ]]; then
  osascript -e 'display alert "Miyonos Simulator" message "The simulator could not start. Close any other simulator and try again. Details were saved in the simulator log." as critical'
fi
exit "$STATUS"
