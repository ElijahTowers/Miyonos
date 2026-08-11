#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/desktop"
OUTPUT="$ROOT/build/simulator/test-screenshots"
REFERENCE="$ROOT/tests/simulator/reference-hashes.txt"
mkdir -p "$OUTPUT"

[[ -x "$BUILD/miyonos" ]] || "$ROOT/scripts/build-desktop.sh"
RUN_ROOT="$(mktemp -d "$ROOT/build/simulator/screenshot-run.XXXXXX")"
DATA_DIR="$RUN_ROOT/SDCARD/App/Miyonos/data"
mkdir -p "$DATA_DIR"
printf 'schema_version=2\ncontent_defaults_version=1\nvolume_step=3\nconfirm_exit=1\nofficial_sonos_product_photos=0\n' \
  > "$DATA_DIR/settings.ini"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/grouped-frame.bmp"

EXPECTED="$(awk '$1 ~ /^grouped-frame-/ {print $2}' "$REFERENCE")"
ACTUAL="$(shasum -a 256 "$OUTPUT/grouped-frame.bmp" | awk '{print $1}')"
grep -qx "$ACTUAL" <<<"$EXPECTED" || {
  echo "The grouped 640 x 480 reference image changed." >&2
  echo "Expected one of:" >&2
  sed 's/^/  /' <<<"$EXPECTED" >&2
  echo "Actual:   $ACTUAL" >&2
  exit 1
}
file "$OUTPUT/grouped-frame.bmp" | grep -q '640 x 480 x 32'

IDLE_DATA="$RUN_ROOT/idle/SDCARD/App/Miyonos/data"
mkdir -p "$IDLE_DATA"
printf 'schema_version=2\ncontent_defaults_version=1\nvolume_step=3\nconfirm_exit=1\nofficial_sonos_product_photos=0\n' > "$IDLE_DATA/settings.ini"
MIYONOS_SCREENSHOT_TIME_MS=1000 MIYONOS_TEST_IDLE_SAVER_DELAY_MS=3000 \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario grouped \
  --data-dir "$IDLE_DATA" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/idle-battery-saver.bmp"
IDLE_FRAME="$(shasum -a 256 "$OUTPUT/idle-battery-saver.bmp" | awk '{print $1}')"
EXPECTED_IDLE="$(awk '$1 == "idle-battery-saver.bmp" {print $2}' "$REFERENCE")"
[[ "$IDLE_FRAME" == "$EXPECTED_IDLE" ]] || {
  echo "The idle battery saver reference image changed." >&2
  echo "Expected: ${EXPECTED_IDLE:-missing}" >&2
  echo "Actual:   $IDLE_FRAME" >&2
  exit 1
}
file "$OUTPUT/idle-battery-saver.bmp" | grep -q '640 x 480 x 32'

MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --show-controls --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/controls-overlay.bmp"
CONTROLS_FRAME="$(shasum -a 256 "$OUTPUT/controls-overlay.bmp" | awk '{print $1}')"
EXPECTED_CONTROLS="$(awk '$1 == "controls-overlay.bmp" {print $2}' "$REFERENCE")"
[[ "$CONTROLS_FRAME" == "$EXPECTED_CONTROLS" ]] || {
  echo "The controls overlay reference image changed." >&2
  echo "Expected: ${EXPECTED_CONTROLS:-missing}" >&2
  echo "Actual:   $CONTROLS_FRAME" >&2
  exit 1
}
file "$OUTPUT/controls-overlay.bmp" | grep -q '640 x 480 x 32'

QUEUE_DATA="$RUN_ROOT/queue/SDCARD/App/Miyonos/data"
mkdir -p "$QUEUE_DATA"
printf 'schema_version=2\ncontent_defaults_version=1\nvolume_step=3\nconfirm_exit=1\nofficial_sonos_product_photos=0\n' > "$QUEUE_DATA/settings.ini"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --show-queue --scenario grouped \
  --data-dir "$QUEUE_DATA" --capture-after-ms 6000 \
  --capture-frame "$OUTPUT/queue-layout.bmp"
QUEUE_FRAME="$(shasum -a 256 "$OUTPUT/queue-layout.bmp" | awk '{print $1}')"
EXPECTED_QUEUE="$(awk '$1 == "queue-layout.bmp" {print $2}' "$REFERENCE")"
[[ "$QUEUE_FRAME" == "$EXPECTED_QUEUE" ]] || {
  echo "The Queue layout reference image changed." >&2
  echo "Expected: ${EXPECTED_QUEUE:-missing}" >&2
  echo "Actual:   $QUEUE_FRAME" >&2
  exit 1
}
file "$OUTPUT/queue-layout.bmp" | grep -q '640 x 480 x 32'

SPEAKERS_DATA="$RUN_ROOT/speakers/SDCARD/App/Miyonos/data"
mkdir -p "$SPEAKERS_DATA"
printf 'schema_version=2\ncontent_defaults_version=1\nvolume_step=3\nconfirm_exit=1\nofficial_sonos_product_photos=0\n' > "$SPEAKERS_DATA/settings.ini"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --show-speakers --scenario grouped \
  --data-dir "$SPEAKERS_DATA" --capture-after-ms 6000 \
  --capture-frame "$OUTPUT/speakers-overview.bmp"
SPEAKERS_FRAME="$(shasum -a 256 "$OUTPUT/speakers-overview.bmp" | awk '{print $1}')"
EXPECTED_SPEAKERS="$(awk '$1 == "speakers-overview.bmp" {print $2}' "$REFERENCE")"
[[ "$SPEAKERS_FRAME" == "$EXPECTED_SPEAKERS" ]] || {
  echo "The Speaker Volumes reference image changed." >&2
  echo "Expected: ${EXPECTED_SPEAKERS:-missing}" >&2
  echo "Actual:   $SPEAKERS_FRAME" >&2
  exit 1
}
file "$OUTPUT/speakers-overview.bmp" | grep -q '640 x 480 x 32'

MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --show-playlist --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 6000 \
  --capture-frame "$OUTPUT/playlist-now-playing.bmp"
PLAYLIST_FRAME="$(shasum -a 256 "$OUTPUT/playlist-now-playing.bmp" | awk '{print $1}')"
EXPECTED_PLAYLIST="$(awk '$1 == "playlist-now-playing.bmp" {print $2}' "$REFERENCE")"
[[ "$PLAYLIST_FRAME" == "$EXPECTED_PLAYLIST" ]] || {
  echo "The active playlist Now Playing reference image changed." >&2
  echo "Expected: ${EXPECTED_PLAYLIST:-missing}" >&2
  echo "Actual:   $PLAYLIST_FRAME" >&2
  exit 1
}
file "$OUTPUT/playlist-now-playing.bmp" | grep -q '640 x 480 x 32'

TAIL_DATA="$RUN_ROOT/mixed-favorites/SDCARD/App/Miyonos/data"
mkdir -p "$TAIL_DATA"
printf 'schema_version=2\ncontent_defaults_version=1\nvolume_step=3\nconfirm_exit=1\nofficial_sonos_product_photos=0\n' > "$TAIL_DATA/settings.ini"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --show-playlist-tail \
  --scenario mixed-favorites --data-dir "$TAIL_DATA" --capture-after-ms 7000 \
  --capture-frame "$OUTPUT/mixed-favorite-playlists-tail.bmp"
TAIL_FRAME="$(shasum -a 256 "$OUTPUT/mixed-favorite-playlists-tail.bmp" | awk '{print $1}')"
EXPECTED_TAIL="$(awk '$1 == "mixed-favorite-playlists-tail.bmp" {print $2}' "$REFERENCE")"
[[ "$TAIL_FRAME" == "$EXPECTED_TAIL" ]] || {
  echo "The mixed Favorite Playlists paging reference image changed." >&2
  echo "Expected: ${EXPECTED_TAIL:-missing}" >&2
  echo "Actual:   $TAIL_FRAME" >&2
  exit 1
}
file "$OUTPUT/mixed-favorite-playlists-tail.bmp" | grep -q '640 x 480 x 32'

MIYONOS_SCREENSHOT_TIME_MS=1000 MIYONOS_DISABLE_IMAGE_DECODER=1 \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/grouped-frame-fallback.bmp"
FALLBACK_FRAME="$(shasum -a 256 "$OUTPUT/grouped-frame-fallback.bmp" | awk '{print $1}')"
grep -qx "$FALLBACK_FRAME" <<<"$EXPECTED" || {
  echo "The grouped fallback-art reference image changed." >&2
  echo "Expected one of:" >&2
  sed 's/^/  /' <<<"$EXPECTED" >&2
  echo "Actual:   $FALLBACK_FRAME" >&2
  exit 1
}
[[ "$FALLBACK_FRAME" != "$ACTUAL" ]] || {
  echo "The fallback-art frame unexpectedly matches the decoded-art frame." >&2
  exit 1
}
file "$OUTPUT/grouped-frame-fallback.bmp" | grep -q '640 x 480 x 32'

MIYONOS_SCREENSHOT_TIME_MS=1000 MIYONOS_BATTERY_PERCENT=76 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/grouped-frame-battery.bmp"
BATTERY_FRAME="$(shasum -a 256 "$OUTPUT/grouped-frame-battery.bmp" | awk '{print $1}')"
[[ "$BATTERY_FRAME" != "$ACTUAL" ]] || {
  echo "The simulated battery reading did not change the Now Playing frame." >&2
  exit 1
}
file "$OUTPUT/grouped-frame-battery.bmp" | grep -q '640 x 480 x 32'

SETTINGS_AFTER_FIRST_RUN="$(shasum -a 256 "$DATA_DIR/settings.ini" | awk '{print $1}')"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario grouped \
  --data-dir "$DATA_DIR" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/grouped-frame-after-restart.bmp"
SETTINGS_AFTER_RESTART="$(shasum -a 256 "$DATA_DIR/settings.ini" | awk '{print $1}')"
[[ "$SETTINGS_AFTER_FIRST_RUN" == "$SETTINGS_AFTER_RESTART" ]] || {
  echo "The simulator did not preserve settings across a restart." >&2
  exit 1
}
RESTART_FRAME="$(shasum -a 256 "$OUTPUT/grouped-frame-after-restart.bmp" | awk '{print $1}')"
[[ "$RESTART_FRAME" == "$ACTUAL" ]] || {
  echo "The 640 x 480 frame changed after restarting with saved settings." >&2
  exit 1
}

OFFLINE_DATA="$RUN_ROOT/offline/SDCARD/App/Miyonos/data"
mkdir -p "$OFFLINE_DATA"
MIYONOS_SCREENSHOT_TIME_MS=1000 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$BUILD/miyonos" --screen-only --scenario offline \
  --data-dir "$OFFLINE_DATA" --capture-after-ms 4500 \
  --capture-frame "$OUTPUT/offline-frame.bmp"
file "$OUTPUT/offline-frame.bmp" | grep -q '640 x 480 x 32'

if curl -fsS --max-time 1 http://127.0.0.1:1400/__simulator__/health \
    >/dev/null 2>&1; then
  echo "The simulator fixture was left running after the app exited." >&2
  exit 1
fi
echo "Simulator screenshots and isolated storage checks passed."
