#!/bin/sh

APP_DIR=$(CDPATH= cd "$(dirname "$0")" 2>/dev/null && pwd -P)
if [ -z "$APP_DIR" ] || [ ! -x "$APP_DIR/miyonos" ]; then
    exit 1
fi

MAINUI_PID=$(pidof MainUI 2>/dev/null)

cleanup() {
    rm -f /tmp/stay_awake
    if [ -n "$MAINUI_PID" ]; then
        kill -CONT $MAINUI_PID 2>/dev/null
    fi
}

trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$APP_DIR/data/logs"
if [ -f "$APP_DIR/data/logs/launcher.log" ]; then
    LOG_SIZE=$(wc -c < "$APP_DIR/data/logs/launcher.log" 2>/dev/null)
    if [ "${LOG_SIZE:-0}" -gt 262144 ]; then
        mv -f "$APP_DIR/data/logs/launcher.log" "$APP_DIR/data/logs/launcher.log.1"
    fi
fi
if [ -n "$MAINUI_PID" ]; then
    kill -STOP $MAINUI_PID 2>/dev/null
fi

cd "$APP_DIR" || exit 1
export MIYONOS_DEVICE=1
export MIYONOS_DATA_DIR="$APP_DIR/data"
export MIYONOS_TLS_CA_FILE="$APP_DIR/certificates/trusted-spotify-artwork-roots.pem"
export SDL_VIDEODRIVER=Mini
export LD_LIBRARY_PATH="$APP_DIR/libs:/config/lib:/customer/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$APP_DIR/miyonos" >>"$APP_DIR/data/logs/launcher.log" 2>&1
STATUS=$?
exit $STATUS
