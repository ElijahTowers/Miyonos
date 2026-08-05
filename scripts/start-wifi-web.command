#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -f "$SCRIPT_DIR/wifi-web.py" &&
      -f "$SCRIPT_DIR/Miyonos-wifi-install.sh" ]]; then
  SERVER="$SCRIPT_DIR/wifi-web.py"
  INSTALLER="$SCRIPT_DIR/Miyonos-wifi-install.sh"
  WEB_ROOT="$SCRIPT_DIR/web"
  ICON="$SCRIPT_DIR/icon.png"
else
  PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
  SERVER="$SCRIPT_DIR/wifi-web.py"
  INSTALLER="$SCRIPT_DIR/wifi-install.sh"
  WEB_ROOT="$PROJECT_ROOT/web/wifi-installer"
  ICON="$PROJECT_ROOT/packaging/onion/App/Miyonos/icon.png"
fi

if ! command -v python3 >/dev/null 2>&1; then
  osascript -e 'display alert "Python 3 is missing" message "Install Python 3 to use the local Miyonos browser installer." as critical'
  exit 1
fi

LOG_FILE="${TMPDIR:-/tmp}/miyonos-wifi-installer.log"
nohup python3 "$SERVER" \
  --installer "$INSTALLER" \
  --web-root "$WEB_ROOT" \
  --icon "$ICON" \
  --open >"$LOG_FILE" 2>&1 &
disown
