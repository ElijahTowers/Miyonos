#!/usr/bin/env bash
set -euo pipefail

[[ "${MIYOO_USER:-}" == "onion" ]]
[[ "${MIYOO_PASSWORD:-}" == "onion" ]]
[[ "${MIYOO_KEEP_BACKUP:-}" == "1" ]]

if [[ "${1:-}" == "--rollback" ]]; then
  [[ "${2:-}" == "192.168.1.50" ]]
  echo "Connecting to onion@${2}..."
  echo "Restored the previous Miyonos version. User data was preserved."
else
  [[ "${1:-}" == "192.168.1.50" ]]
  echo "Connecting to onion@${1}..."
  echo "Installed version: none"
  echo "Uploading Miyonos 0.1.1..."
  echo "Miyonos 0.1.1 is installed at onion@${1}:/mnt/SDCARD/App/Miyonos"
  echo "Settings, logs, and artwork were preserved."
fi
