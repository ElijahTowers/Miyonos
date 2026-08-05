#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
TEST_ROOT="$(mktemp -d)"
cleanup() {
  rm -rf "$TEST_ROOT"
}
trap cleanup EXIT INT TERM

mkdir -p "$TEST_ROOT/bin" "$TEST_ROOT/device/App" "$TEST_ROOT/release"
cp "$ROOT/tests/integration/fake_ssh.sh" "$TEST_ROOT/bin/ssh"
chmod +x "$TEST_ROOT/bin/ssh"
cp "$ROOT/dist/Miyonos-$VERSION-OnionOS.zip" "$TEST_ROOT/release/"
cp "$ROOT/dist/Miyonos-$VERSION-OnionOS.zip.sha256" "$TEST_ROOT/release/"
cp "$ROOT/scripts/wifi-install.sh" \
  "$TEST_ROOT/release/Miyonos-wifi-install.sh"
chmod +x "$TEST_ROOT/release/Miyonos-wifi-install.sh"
APP_PATH="$TEST_ROOT/device/App/Miyonos"
UPDATER="$TEST_ROOT/release/Miyonos-wifi-install.sh"

PATH="$TEST_ROOT/bin:$PATH" \
MIYOO_USER=test \
MIYOO_APP_PATH="$APP_PATH" \
"$UPDATER" 127.0.0.1 >/dev/null

test -x "$APP_PATH/miyonos"
test "$(tr -d '[:space:]' < "$APP_PATH/VERSION")" = "$VERSION"
mkdir -p "$APP_PATH/data"
echo "volume_step=3" > "$APP_PATH/data/settings.ini"
touch "$APP_PATH/OLD_MARKER"

PATH="$TEST_ROOT/bin:$PATH" \
MIYOO_USER=test \
MIYOO_APP_PATH="$APP_PATH" \
"$UPDATER" 127.0.0.1 >/dev/null

test -f "$APP_PATH/data/settings.ini"
test ! -e "$APP_PATH/OLD_MARKER"
test -e "$TEST_ROOT/device/App/.Miyonos.previous/OLD_MARKER"
test ! -e "$TEST_ROOT/device/App/.Miyonos.previous/data"

PATH="$TEST_ROOT/bin:$PATH" \
MIYOO_USER=test \
MIYOO_APP_PATH="$APP_PATH" \
"$UPDATER" --rollback 127.0.0.1 >/dev/null

test -e "$APP_PATH/OLD_MARKER"
test -f "$APP_PATH/data/settings.ini"
test ! -e "$TEST_ROOT/device/App/.Miyonos.previous"
echo "Wi-Fi install, update, data preservation, and rollback passed."
