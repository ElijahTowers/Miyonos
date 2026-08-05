#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD_ROOT="$ROOT/build/simulator/app-bundle"
APP="$BUILD_ROOT/Miyonos Simulator.app"
DIST_APP="$ROOT/dist/Miyonos Simulator.app"
DIST_ZIP="$ROOT/dist/Miyonos-Simulator-$VERSION-macOS.zip"

"$ROOT/scripts/build-desktop.sh"
case "$APP" in
  "$ROOT/build/simulator/"*) ;;
  *) echo "Refusing to replace an unexpected app staging path." >&2; exit 1 ;;
esac
if [[ -e "$APP" ]]; then
  chmod -R u+w "$APP"
  rm -rf "$APP"
fi
if [[ -e "$BUILD_ROOT/icon.iconset" ]]; then
  chmod -R u+w "$BUILD_ROOT/icon.iconset"
  rm -rf "$BUILD_ROOT/icon.iconset"
fi
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" \
  "$APP/Contents/Frameworks" "$ROOT/dist" "$BUILD_ROOT/icon.iconset"
cp "$ROOT/packaging/simulator/Info.plist" "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" \
  "$APP/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $VERSION" \
  "$APP/Contents/Info.plist"
cp "$ROOT/packaging/simulator/launcher.sh" \
  "$APP/Contents/MacOS/Miyonos Simulator"
cp "$ROOT/build/desktop/miyonos" "$APP/Contents/MacOS/miyonos-bin"
cp "$ROOT/packaging/onion/App/Miyonos/certificates/trusted-spotify-artwork-roots.pem" \
  "$APP/Contents/Resources/trusted-spotify-artwork-roots.pem"
chmod +x "$APP/Contents/MacOS/Miyonos Simulator" \
  "$APP/Contents/MacOS/miyonos-bin"

SDL_LIBRARY="$(otool -L "$APP/Contents/MacOS/miyonos-bin" | \
  awk '/libSDL2[^_].*dylib|libSDL2-[0-9].*dylib/ {print $1; exit}')"
if [[ -z "$SDL_LIBRARY" || ! -f "$SDL_LIBRARY" ]]; then
  echo "The linked SDL2 library could not be located." >&2
  exit 1
fi
SDL_NAME="$(basename "$SDL_LIBRARY")"
cp "$SDL_LIBRARY" "$APP/Contents/Frameworks/$SDL_NAME"
install_name_tool -change "$SDL_LIBRARY" \
  "@executable_path/../Frameworks/$SDL_NAME" \
  "$APP/Contents/MacOS/miyonos-bin"
install_name_tool -id "@rpath/$SDL_NAME" "$APP/Contents/Frameworks/$SDL_NAME"

ICON_SOURCE="$ROOT/packaging/onion/App/Miyonos/icon.png"
for specification in \
  "16 icon_16x16.png" "32 icon_16x16@2x.png" \
  "32 icon_32x32.png" "64 icon_32x32@2x.png" \
  "128 icon_128x128.png" "256 icon_128x128@2x.png" \
  "256 icon_256x256.png" "512 icon_256x256@2x.png" \
  "512 icon_512x512.png" "1024 icon_512x512@2x.png"; do
  size="${specification%% *}"
  name="${specification#* }"
  sips -z "$size" "$size" "$ICON_SOURCE" \
    --out "$BUILD_ROOT/icon.iconset/$name" >/dev/null
done
iconutil -c icns "$BUILD_ROOT/icon.iconset" \
  -o "$APP/Contents/Resources/MiyonosSimulator.icns"
codesign --force --deep --sign - "$APP" >/dev/null

case "$DIST_APP" in
  "$ROOT/dist/"*) ;;
  *) echo "Refusing to replace an unexpected app distribution path." >&2; exit 1 ;;
esac
if [[ -e "$DIST_APP" ]]; then
  chmod -R u+w "$DIST_APP"
  rm -rf "$DIST_APP"
fi
rm -f "$DIST_ZIP"
ditto "$APP" "$DIST_APP"
ditto -c -k --sequesterRsrc --keepParent "$DIST_APP" "$DIST_ZIP"
shasum -a 256 "$DIST_ZIP" > "$DIST_ZIP.sha256"
echo "Simulator app: $DIST_APP"
echo "Simulator ZIP: $DIST_ZIP"
echo "Simulator checksum: $DIST_ZIP.sha256"
