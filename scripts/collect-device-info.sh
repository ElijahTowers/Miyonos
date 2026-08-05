#!/usr/bin/env bash
set -euo pipefail

: "${MIYOO_HOST:?Set MIYOO_HOST to the Miyoo Mini Plus IP address.}"
MIYOO_USER="${MIYOO_USER:-onion}"
if [[ ! "$MIYOO_HOST" =~ ^[A-Za-z0-9._:-]+$ ||
      ! "$MIYOO_USER" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "Host or user contains unsupported characters." >&2
  exit 1
fi
DESTINATION="$MIYOO_USER@$MIYOO_HOST"

ssh -o ConnectTimeout=8 -o MACs=hmac-sha1 "$DESTINATION" '
echo "Kernel: $(uname -a)"
echo "MainUI: $(pidof MainUI 2>/dev/null || echo not-running)"
echo "Input devices:"
cat /proc/bus/input/devices 2>/dev/null || true
echo "SDL libraries:"
find /mnt/SDCARD/.tmp_update /config/lib /customer/lib -maxdepth 3 -name "*SDL*" 2>/dev/null
echo "Onion version:"
cat /mnt/SDCARD/.tmp_update/.version 2>/dev/null || true
echo "Free memory:"
free -m 2>/dev/null || true
'
