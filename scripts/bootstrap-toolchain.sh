#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCHIVE="$ROOT/build/toolchains/mini_toolchain-v1.0.tar.gz"
DESTINATION="$ROOT/build/toolchains/mini_toolchain-v1.0"
URL="https://github.com/steward-fu/website/releases/download/miyoo-mini/mini_toolchain-v1.0.tar.gz"
# Filled from the upstream release asset and intentionally checked before use.
EXPECTED_SHA256="8addff71be4b015a4e1aef51e43635e50978d558a1675f5b1664124e8437d071"

mkdir -p "$(dirname "$ARCHIVE")"
if [[ ! -f "$ARCHIVE" ]]; then
  curl -L --fail --retry 3 -o "$ARCHIVE" "$URL"
fi
ACTUAL_SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{print $1}')"
if [[ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]]; then
  echo "Toolchain checksum mismatch." >&2
  echo "Expected: $EXPECTED_SHA256" >&2
  echo "Actual:   $ACTUAL_SHA256" >&2
  exit 1
fi
if ! find "$DESTINATION" -type f -name arm-linux-gnueabihf-g++ -print -quit \
    2>/dev/null | grep -q .; then
  rm -rf "$DESTINATION"
  mkdir -p "$DESTINATION"
  # The archive contains terminfo aliases that differ only by case. They cannot
  # coexist on a default macOS filesystem and are not used by the compiler.
  tar -xzf "$ARCHIVE" -C "$DESTINATION" \
    --exclude='*/usr/share/terminfo/*'
fi
echo "$DESTINATION"
