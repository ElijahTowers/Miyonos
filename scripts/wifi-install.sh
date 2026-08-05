#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMMAND="./$(basename "$0")"
if [[ -n "${MIYONOS_RELEASE_ZIP:-}" ]]; then
  ZIP="$MIYONOS_RELEASE_ZIP"
elif [[ -f "$PROJECT_ROOT/VERSION" ]]; then
  VERSION="$(tr -d '[:space:]' < "$PROJECT_ROOT/VERSION")"
  ZIP="$PROJECT_ROOT/dist/Miyonos-$VERSION-OnionOS.zip"
else
  shopt -s nullglob
  RELEASES=("$SCRIPT_DIR"/Miyonos-*-OnionOS.zip)
  shopt -u nullglob
  if [[ ${#RELEASES[@]} -ne 1 ]]; then
    echo "Place this updater beside exactly one Miyonos OnionOS ZIP." >&2
    exit 1
  fi
  ZIP="${RELEASES[0]}"
fi
CHECKSUM="$ZIP.sha256"
MODE="install"

if [[ "${1:-}" == "--rollback" ]]; then
  MODE="rollback"
  shift
elif [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Usage: $COMMAND [--rollback] [MIYOO_IP]"
  echo "Environment: MIYOO_HOST, MIYOO_USER, MIYOO_APP_PATH, MIYOO_KEEP_BACKUP, MIYOO_SSH_MULTIPLEX"
  exit 0
fi

MIYOO_HOST="${1:-${MIYOO_HOST:-}}"
MIYOO_USER="${MIYOO_USER:-onion}"
MIYOO_APP_PATH="${MIYOO_APP_PATH:-/mnt/SDCARD/App/Miyonos}"
MIYOO_KEEP_BACKUP="${MIYOO_KEEP_BACKUP:-1}"
MIYOO_SSH_MULTIPLEX="${MIYOO_SSH_MULTIPLEX:-1}"

if [[ -z "$MIYOO_HOST" ]]; then
  echo "Give the Miyoo IP address, for example:" >&2
  echo "  $COMMAND 192.168.1.50" >&2
  exit 1
fi
if [[ ! "$MIYOO_HOST" =~ ^[A-Za-z0-9._:-]+$ ||
      ! "$MIYOO_USER" =~ ^[A-Za-z0-9._-]+$ ||
      ! "$MIYOO_APP_PATH" =~ ^/[A-Za-z0-9._/-]+$ ||
      "$MIYOO_APP_PATH" != */Miyonos ]]; then
  echo "Host, user, or application path is not safe for deployment." >&2
  exit 1
fi
if [[ "$MIYOO_KEEP_BACKUP" != 0 && "$MIYOO_KEEP_BACKUP" != 1 ]]; then
  echo "MIYOO_KEEP_BACKUP must be 0 or 1." >&2
  exit 1
fi
if [[ "$MIYOO_SSH_MULTIPLEX" != 0 && "$MIYOO_SSH_MULTIPLEX" != 1 ]]; then
  echo "MIYOO_SSH_MULTIPLEX must be 0 or 1." >&2
  exit 1
fi

APP_PARENT="${MIYOO_APP_PATH%/*}"
INCOMING="$APP_PARENT/.Miyonos.incoming"
BACKUP="$APP_PARENT/.Miyonos.previous"
DESTINATION="$MIYOO_USER@$MIYOO_HOST"
CONTROL_DIR="$(mktemp -d)"
CONTROL_SOCKET="$CONTROL_DIR/ssh"
TEMP_DIR=""
SSH_OPTIONS=(
  -o ConnectTimeout=8
  -o MACs=hmac-sha1
  -o StrictHostKeyChecking=accept-new
  -o NumberOfPasswordPrompts=1
)
if [[ "$MIYOO_SSH_MULTIPLEX" == 1 ]]; then
  SSH_OPTIONS+=(
    -o ControlMaster=auto
    -o ControlPersist=60
    -o "ControlPath=$CONTROL_SOCKET"
  )
fi

if [[ -n "${MIYOO_PASSWORD:-}" ]]; then
  ASKPASS="$CONTROL_DIR/askpass"
  printf '%s\n' \
    '#!/bin/sh' \
    'printf "%s\n" "$MIYOO_PASSWORD"' > "$ASKPASS"
  chmod 700 "$ASKPASS"
  export SSH_ASKPASS="$ASKPASS"
  export SSH_ASKPASS_REQUIRE=force
  export DISPLAY="${DISPLAY:-miyonos-local}"
fi

cleanup() {
  if [[ "$MIYOO_SSH_MULTIPLEX" == 1 ]]; then
    ssh "${SSH_OPTIONS[@]}" -O exit "$DESTINATION" >/dev/null 2>&1 || true
  fi
  if [[ -n "$TEMP_DIR" && -d "$TEMP_DIR" ]]; then
    rm -rf "$TEMP_DIR"
  fi
  rm -rf "$CONTROL_DIR"
}
trap cleanup EXIT INT TERM

echo "Connecting to $DESTINATION..."
ssh "${SSH_OPTIONS[@]}" "$DESTINATION" true

if [[ "$MODE" == "rollback" ]]; then
  ssh "${SSH_OPTIONS[@]}" "$DESTINATION" "
set -eu
if [ ! -d '$BACKUP' ]; then
  echo 'No previous Miyonos version is available.' >&2
  exit 3
fi
if pidof miyonos >/dev/null 2>&1; then
  killall -TERM miyonos 2>/dev/null || true
  sleep 1
fi
rm -rf '$INCOMING'
if [ -d '$MIYOO_APP_PATH/data' ]; then
  rm -rf '$BACKUP/data'
  mv '$MIYOO_APP_PATH/data' '$BACKUP/data'
fi
mv '$MIYOO_APP_PATH' '$INCOMING'
mv '$BACKUP' '$MIYOO_APP_PATH'
rm -rf '$INCOMING'
chmod +x '$MIYOO_APP_PATH/launch.sh' '$MIYOO_APP_PATH/miyonos'
"
  echo "Restored the previous Miyonos version. User data was preserved."
  exit 0
fi

if [[ ! -f "$ZIP" || ! -f "$CHECKSUM" ]]; then
  echo "The release ZIP or its .sha256 file is missing." >&2
  exit 1
fi
if [[ -z "${VERSION:-}" ]]; then
  VERSION="$(unzip -p "$ZIP" App/Miyonos/VERSION | tr -d '[:space:]')"
fi
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][A-Za-z0-9._-]+)?$ ]]; then
  echo "The release contains an invalid version identifier." >&2
  exit 1
fi
EXPECTED="$(awk 'NR == 1 {print $1}' "$CHECKSUM")"
ACTUAL="$(shasum -a 256 "$ZIP" | awk '{print $1}')"
if [[ ! "$EXPECTED" =~ ^[0-9a-fA-F]{64}$ || "$EXPECTED" != "$ACTUAL" ]]; then
  echo "The release ZIP checksum does not match; installation stopped." >&2
  exit 1
fi

TEMP_DIR="$(mktemp -d)"
unzip -q "$ZIP" 'App/Miyonos/*' -d "$TEMP_DIR"
SOURCE="$TEMP_DIR/App/Miyonos"
if [[ ! -x "$SOURCE/miyonos" || ! -x "$SOURCE/launch.sh" ||
      ! -f "$SOURCE/config.json" || -e "$SOURCE/data" ]]; then
  echo "The release archive has an invalid application layout." >&2
  exit 1
fi

INSTALLED="$(
  ssh "${SSH_OPTIONS[@]}" "$DESTINATION" \
    "if [ -f '$MIYOO_APP_PATH/VERSION' ]; then cat '$MIYOO_APP_PATH/VERSION'; else echo none; fi"
)"
echo "Installed version: $INSTALLED"
echo "Uploading Miyonos $VERSION..."

ssh "${SSH_OPTIONS[@]}" "$DESTINATION" \
  "rm -rf '$INCOMING' && mkdir -p '$INCOMING'"
tar -C "$SOURCE" -cf - . | \
  ssh "${SSH_OPTIONS[@]}" "$DESTINATION" "tar -xf - -C '$INCOMING'"

ssh "${SSH_OPTIONS[@]}" "$DESTINATION" "
set -eu
test -f '$INCOMING/config.json'
test -f '$INCOMING/libs/libSDL2-2.0.so.0'
test -f '$INCOMING/libs/libSDL2_image-2.0.so.0'
test -f '$INCOMING/libs/libjpeg.so.9'
test -f '$INCOMING/VERSION'
test -x '$INCOMING/miyonos'
test -x '$INCOMING/launch.sh'
if pidof miyonos >/dev/null 2>&1; then
  killall -TERM miyonos 2>/dev/null || true
  sleep 1
fi
rm -rf '$BACKUP'
if [ -d '$MIYOO_APP_PATH/data' ]; then
  mv '$MIYOO_APP_PATH/data' '$INCOMING/data'
fi
if [ -d '$MIYOO_APP_PATH' ]; then
  mv '$MIYOO_APP_PATH' '$BACKUP'
fi
if ! mv '$INCOMING' '$MIYOO_APP_PATH'; then
  if [ -d '$BACKUP' ]; then
    if [ -d '$INCOMING/data' ]; then
      mv '$INCOMING/data' '$BACKUP/data'
    fi
    mv '$BACKUP' '$MIYOO_APP_PATH'
  fi
  exit 1
fi
chmod +x '$MIYOO_APP_PATH/launch.sh' '$MIYOO_APP_PATH/miyonos'
if [ '$MIYOO_KEEP_BACKUP' = 0 ]; then
  rm -rf '$BACKUP'
fi
"

echo "Miyonos $VERSION is installed at $DESTINATION:$MIYOO_APP_PATH"
echo "Settings, logs, and artwork were preserved."
if [[ "$MIYOO_KEEP_BACKUP" == 1 && "$INSTALLED" != "none" ]]; then
  echo "Rollback: $COMMAND --rollback $MIYOO_HOST"
fi
echo "Open Apps > Miyonos on the device. A first install may require an Apps refresh."
