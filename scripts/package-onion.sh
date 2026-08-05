#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BINARY="$ROOT/build/miyoo/miyonos"
RUNTIME="$ROOT/build/miyoo/runtime"
STAGE="$ROOT/build/onion-stage"
APP="$STAGE/App/Miyonos"
DIST="$ROOT/dist"
ZIP="$DIST/Miyonos-$VERSION-OnionOS.zip"
WEB_BUNDLE="$DIST/Miyonos-WiFi-Installer-macOS"
WEB_ZIP="$DIST/Miyonos-$VERSION-WiFi-Installer-macOS.zip"
UNIVERSAL_BUNDLE="$DIST/Miyonos-Universal-Browser-Installer"
UNIVERSAL_ZIP="$DIST/Miyonos-$VERSION-Universal-Browser-Installer.zip"

if [[ ! -x "$BINARY" ]]; then
  echo "Target binary is missing. Run ./scripts/build-miyoo.sh first." >&2
  exit 1
fi
rm -rf "$STAGE"
mkdir -p "$APP/assets" "$APP/libs" "$APP/licenses" "$APP/certificates" "$DIST"
cp "$ROOT/packaging/onion/App/Miyonos/config.json" "$APP/"
cp "$ROOT/packaging/onion/App/Miyonos/icon.png" "$APP/"
cp "$ROOT/packaging/onion/App/Miyonos/launch.sh" "$APP/"
cp "$ROOT/packaging/onion/App/Miyonos/certificates/trusted-spotify-artwork-roots.pem" \
  "$APP/certificates/"
cp "$BINARY" "$APP/miyonos"
cp "$ROOT/VERSION" "$APP/VERSION"
cp "$ROOT/assets/app-icon-source.svg" "$APP/assets/"
cp "$ROOT/LICENSE" "$APP/licenses/Miyonos-MIT.txt"
cp "$ROOT/NOTICE.md" "$APP/licenses/NOTICE.md"
cp "$ROOT/third_party/licenses/SDL2.txt" "$APP/licenses/"
cp "$ROOT/third_party/licenses/libjpeg-IJG.txt" "$APP/licenses/"
cp "$ROOT/third_party/licenses/OpenSSL-1.1.1l.txt" "$APP/licenses/"
cp "$ROOT/third_party/RUNTIME-NOTICES.md" "$APP/licenses/"

SDL_ROOT="$ROOT/build/miyoo-deps/sdl2"
cp "$SDL_ROOT/LICENSE" "$APP/licenses/Miyoo-SDL-port-LGPL-2.1.txt"
cp "$SDL_ROOT/sdl2/LICENSE.txt" "$APP/licenses/SDL2-zlib.txt"
cp "$SDL_ROOT/android/app/jni/SDL2_image/LICENSE.txt" \
  "$APP/licenses/SDL2-image-zlib.txt"
cp "$SDL_ROOT/swiftshader/LICENSE.txt" \
  "$APP/licenses/SwiftShader-Apache-2.0.txt"

for library in "$RUNTIME"/*; do
  [[ -f "$library" ]] && cp "$library" "$APP/libs/"
done

if [[ ! -f "$APP/libs/libSDL2-2.0.so.0" ||
      ! -f "$APP/libs/libSDL2_image-2.0.so.0" ||
      ! -f "$APP/libs/libjpeg.so.9" ||
      ! -f "$APP/libs/libstdc++.so.6" ||
      ! -f "$APP/libs/libgcc_s.so.1" ]]; then
  echo "Target runtime libraries are missing. Run ./scripts/build-miyoo.sh." >&2
  exit 1
fi
chmod +x "$APP/launch.sh" "$APP/miyonos"
cp "$ROOT/packaging/onion/README.txt" "$STAGE/"

rm -f "$ZIP" "$ZIP.sha256"
(cd "$STAGE" && zip -q -r "$ZIP" App README.txt)
shasum -a 256 "$ZIP" > "$ZIP.sha256"
cp "$ROOT/build/miyoo/miyonos.debug" "$DIST/Miyonos-$VERSION-symbols"
cp "$ROOT/scripts/wifi-install.sh" "$DIST/Miyonos-wifi-install.sh"
chmod +x "$DIST/Miyonos-wifi-install.sh"

rm -rf "$WEB_BUNDLE"
rm -f "$WEB_ZIP" "$WEB_ZIP.sha256"
mkdir -p "$WEB_BUNDLE/web"
cp "$ROOT/scripts/wifi-web.py" "$WEB_BUNDLE/"
cp "$ROOT/scripts/wifi-install.sh" "$WEB_BUNDLE/Miyonos-wifi-install.sh"
cp "$ROOT/scripts/start-wifi-web.command" \
  "$WEB_BUNDLE/Open Miyonos Installer.command"
cp "$ROOT/web/wifi-installer/"* "$WEB_BUNDLE/web/"
cp "$ROOT/packaging/onion/App/Miyonos/icon.png" "$WEB_BUNDLE/icon.png"
cp "$ROOT/packaging/wifi/README.txt" "$WEB_BUNDLE/README.txt"
cp "$ZIP" "$WEB_BUNDLE/"
(cd "$WEB_BUNDLE" && \
  shasum -a 256 "$(basename "$ZIP")" > "$(basename "$ZIP").sha256")
chmod +x \
  "$WEB_BUNDLE/Open Miyonos Installer.command" \
  "$WEB_BUNDLE/Miyonos-wifi-install.sh" \
  "$WEB_BUNDLE/wifi-web.py"
(cd "$DIST" && zip -qry "$WEB_ZIP" "$(basename "$WEB_BUNDLE")")
shasum -a 256 "$WEB_ZIP" > "$WEB_ZIP.sha256"

rm -rf "$UNIVERSAL_BUNDLE"
rm -f "$UNIVERSAL_ZIP" "$UNIVERSAL_ZIP.sha256"
mkdir -p "$UNIVERSAL_BUNDLE"
cp -R "$APP" "$UNIVERSAL_BUNDLE/Miyonos"
sed "s/__MIYONOS_VERSION__/$VERSION/g" \
  "$ROOT/packaging/universal/Open Miyonos Installer.html" \
  > "$UNIVERSAL_BUNDLE/Open Miyonos Installer.html"
cp "$ROOT/packaging/universal/README.txt" "$UNIVERSAL_BUNDLE/README.txt"
(cd "$DIST" && zip -qry "$UNIVERSAL_ZIP" "$(basename "$UNIVERSAL_BUNDLE")")
shasum -a 256 "$UNIVERSAL_ZIP" > "$UNIVERSAL_ZIP.sha256"

echo "Release: $ZIP"
echo "Checksum: $ZIP.sha256"
echo "Universal browser installer: $UNIVERSAL_ZIP"
echo "Universal browser installer checksum: $UNIVERSAL_ZIP.sha256"
echo "Mac browser installer: $WEB_ZIP"
echo "Mac browser installer checksum: $WEB_ZIP.sha256"
