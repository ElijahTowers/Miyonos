# Building

## Desktop

Requirements are a C++17 compiler and SDL2 development files (`sdl2-config`).
Python 3 and `curl` are used by the legacy protocol integration fixture, but
the simulator app itself has no Python dependency. Linux and macOS desktop
builds are supported.

```sh
./scripts/build-desktop.sh
./scripts/run-tests.sh
./scripts/render-preview.sh
```

The executable is `build/desktop/miyonos`. Use `--scale 2` for a 1280 × 960
window, `--data-dir PATH` for isolated settings, or `--player-ip 192.168.1.20`
to use a real LAN player fallback. On Apple Silicon, the script detects an
Intel-only Homebrew SDL2 and builds an x86_64/Rosetta development binary.

## macOS simulator

Build and start the clickable Miyoo simulator with:

```sh
./scripts/run-simulator.sh --scenario grouped
./scripts/build-simulator-app.sh
open "dist/Miyonos Simulator.app"
```

Supported safe scenarios are `normal`, `multi-room`, `grouped`, `long-queue`,
`no-artwork`, `slow`, `offline`, and `coordinator-change`. Add `--screen-only`
for an undecorated 640 × 480 window. Add `--live-sonos` only when the simulator
may discover and control real speakers on the local LAN.

The app bundle contains the native simulator fixture and its linked SDL2
library. On macOS it uses the system ImageIO decoder for JPEG, PNG, and the
code-generated fixture cover, so no separate SDL2_image installation is
required. Double-clicking opens a scenario chooser; Live Sonos requires an
additional warning confirmation. Its default storage is a separate
reconstructed SD-card tree under `~/Library/Application Support/Miyonos
Simulator`. Outputs are:

- `dist/Miyonos Simulator.app`;
- `dist/Miyonos-Simulator-0.1.21-macOS.zip`;
- `dist/Miyonos-Simulator-0.1.21-macOS.zip.sha256`.

For a manual CMake build:

```sh
cmake -S . -B build/cmake -DMIYONOS_BUILD_TESTS=ON
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

## Miyoo Mini Plus

Requirements are Docker Desktop, Git, `curl`, `shasum`, and `zip`. Docker must
be running. The first build downloads the verified toolchain archive (about
716 MB), clones the pinned SDL port, and builds an amd64 Linux container:

```sh
./scripts/build-miyoo.sh
```

Outputs:

- `build/miyoo/miyonos`: stripped ARM release binary;
- `dist/Miyonos-0.1.21-symbols`: unstripped debug binary;
- `dist/Miyonos-0.1.21-OnionOS.zip`: SD-card package;
- `dist/Miyonos-0.1.21-OnionOS.zip.sha256`: checksum;
- `dist/Miyonos-0.1.21-Universal-Browser-Installer.zip`: no-install package for
  Windows, macOS, and Linux;
- `dist/Miyonos-0.1.21-Universal-Browser-Installer.zip.sha256`: universal
  package checksum;
- `dist/Miyonos-0.1.21-WiFi-Installer-macOS.zip`: local browser installer;
- `dist/Miyonos-0.1.21-WiFi-Installer-macOS.zip.sha256`: browser bundle
  checksum.

`./scripts/package-onion.sh` can repackage an already successful target build.
The target is C++17, ARMv7-A/Cortex-A7, hard-float, NEON/VFPv4. Runtime
libraries are stripped copies from the same pinned target inputs.

The macOS archive contains the checksummed OnionOS release plus a local
browser interface and double-click launcher. It has no third-party Python
dependencies.

The universal archive is the primary end-user package. It contains a
self-contained HTML guide and an upload-ready `Miyonos` application folder. It
uses OnionOS's built-in HTTP file server and has no computer-side runtime.

## Wi-Fi install and update

Enable SSH in OnionOS and run:

```sh
./scripts/wifi-install.sh 192.168.1.50
```

Optional variables are `MIYOO_USER` (default `onion`),
`MIYOO_APP_PATH` (default `/mnt/SDCARD/App/Miyonos`), and
`MIYOO_KEEP_BACKUP=0` to remove the previous app binary after updating. The
updater preserves the complete data/cache directory and supports
`--rollback`. See `WIFI_INSTALL.md` for the one-time OnionOS setup.

To collect read-only target facts before a port investigation:

```sh
MIYOO_HOST=192.168.1.50 ./scripts/collect-device-info.sh
```

For SD-card installation, extract the release ZIP at the card root and verify
`App/Miyonos/launch.sh`. A copy method that loses executable bits may require:

```sh
chmod +x /mnt/SDCARD/App/Miyonos/launch.sh \
  /mnt/SDCARD/App/Miyonos/miyonos
```
