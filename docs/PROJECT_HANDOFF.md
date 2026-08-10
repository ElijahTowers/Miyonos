# Miyonos project handoff

This document is the starting point for a person or a new chat that receives
this repository without the conversation that created it. It explains the
project, current release, physical Miyoo state, safe update paths, and test
sequence.

All product text, scripts, code comments, and documentation in this project
must remain in English. Conversation with an operator may use another
language.

## What Miyonos is

Miyonos is a C++17/SDL2 application that turns a Miyoo Mini Plus running
OnionOS into a button-first Sonos remote. It talks directly to Sonos speakers
on the local IPv4 network through their UPnP/SOAP interface. It has no
Miyonos account, cloud backend, analytics, API key, advertising, or Sonos
cloud-login requirement.

It discovers speakers, chooses rooms and groups, shows now-playing metadata,
cover art, and the Miyoo's local battery percentage, controls
playback/volume/group mute, browses Queue and Favorites, and persists its
local settings on the SD card. Queue reads Sonos' `Q:0` track container; X in
Queue opens Favorite Playlists, which filters playlist-shaped Favorites such
as Spotify playlists and shows the selected source-provided cover art.
Starting one replaces the active Sonos queue before playback begins. Individual music
services can still impose their own source-specific restrictions; Miyonos does
not bypass those restrictions.

The Main Menu also includes **Speaker Volumes**, a local visual overview of
every available speaker in the selected group. By default it uses built-in
illustrations matched to the discovered Sonos model. The optional, default-off
**Official Sonos product photos** setting permits only a fixed catalog of four
direct `media.sonos.com` images for Beam, Roam, Era 100, and Arc; they are
TLS-verified, cached locally, and never bundled or proxied. It shows each
speaker's individual volume and mute state; Left/Right or L1/R1 selects a
speaker, Up/Down adjusts only that speaker, and A toggles its mute.

Favorites uses the same source-provided artwork policy: its selected entry
shows a bounded cached cover when Sonos supplies one, otherwise **Cover
unavailable**. Spotify Favorites and Sonos Radio stations can provide a public
HTTPS cover instead of a local Sonos image. **External cover art over HTTPS**
is therefore a default-off setting that permits only an explicit fixed
allowlist of observed Spotify and Sonos Radio/TuneIn endpoint forms with TLS
certificate and hostname verification against the bundled offline trust roots.
A radio proxy may make one verified redirect to either fixed TuneIn logo CDN;
every other redirect is refused. It sends no Sonos login, cookies, playback
data, or device identifiers and is not a general web client. L1/R1 cycles the
volume target through Group and all visible individual speakers; Up/Down
changes the selected target only.

## Current snapshot

| Item | Current state |
| --- | --- |
| Release version | `0.1.31` |
| End-user download | `dist/Miyonos-App-0.1.31.zip` — a single `Miyonos` folder to put in `App` |
| macOS simulator | `dist/Miyonos Simulator.app` |
| Last verified device | Miyoo Mini Plus on OnionOS 4.3.x |
| Last observed device IP | Not recorded in the public repository; check **Apps → Tweaks → Network** on the device |
| Last verified device state | Version 0.1.23 is installed. Version 0.1.28 validated the live playlist-start flow. Version 0.1.29 additionally read the live `Q:0` queue: all 50 returned tracks included an artwork reference. Version 0.1.31 retains the selected playlist name and artwork in Now Playing through generic queue metadata. Its physical retest is pending. |
| Current 0.1.31 package ARM binary SHA-256 | `8f7f9ac687fac7acd27f68125996547b23d8280e5f431d86d5f561bdabac1cc1` |
| Automated verification | 243 core checks, 131 simulator-input checks, 73 simulator-fixture checks, simulator screenshot/storage checks, and package integrity checks passed |

The device address is only a local handoff record. Check **Apps → Tweaks →
Network** on the Miyoo when a connection fails rather than assuming that this
address is still valid.

Before publishing this repository outside the owner's trusted circle, remove
the local device IP from this snapshot and never add a non-default password,
speaker identifier, or listening history to the project.

The physical build uses a direct, double-buffered 640 × 480 framebuffer path.
Earlier Mini SDL texture/blitter paths caused black, incomplete, or blurred
output. Do not replace this route casually; inspect
`src/platform/frame_presenter.*` and [FINAL_STATUS.md](FINAL_STATUS.md) first.

## Start here

1. Read [README.md](../README.md) for the end-user overview.
2. Read this handoff document for the current operational state.
3. Read [ARCHITECTURE.md](ARCHITECTURE.md) before changing code.
4. Read [TESTING.md](TESTING.md) before claiming a feature is complete.
5. Read [FINAL_STATUS.md](FINAL_STATUS.md) and
   [LIVE_SONOS_VALIDATION.md](LIVE_SONOS_VALIDATION.md) for the exact real
   Sonos and physical-device evidence boundary.

## Project map

| Path | Purpose |
| --- | --- |
| `src/app` | Shared runtime, controller, navigation, settings interactions, worker queue |
| `src/platform` | SDL input, button mapping, local battery gauge, logging, clocks, direct framebuffer presenter |
| `src/network` | Bounded IPv4 LAN HTTP/SSDP plus the narrow opt-in Spotify HTTPS artwork client |
| `src/sonos` | XML/SOAP parsing and Sonos protocol adapter |
| `src/domain` | Typed room, group, player, playback, and media data |
| `src/storage` | Settings, artwork cache, topology snapshot, and local logs |
| `src/ui` | 640 × 480 renderer, bitmap font, and English interface strings |
| `src/simulator` | Desktop Miyoo shell and safe native Sonos fixture; excluded from ARM package |
| `tests` | Unit, integration, mock Sonos, simulator input/fixture/screenshot checks |
| `scripts` | Build, package, simulator, test, Wi-Fi update, and diagnostics commands |
| `packaging` | OnionOS launcher/app metadata and browser-installer templates |
| `assets` | Source artwork and static app assets |
| `web` | Local browser-installer frontend copied into the macOS installer bundle |
| `third_party` | Required runtime notices and bundled third-party licenses |
| `cmake` | Optional CMake build configuration |
| `build` | Reproducible local build output; regenerate rather than hand-edit it |
| `dist` | Current release packages; never add user data here |
| `docs` | Architecture, installation, test, release, troubleshooting, and handoff documentation |
| `VERSION`, `CMakeLists.txt`, `CHANGELOG.md` | Release identity, build version, and user-visible release history |

## Connect to the Miyoo

Both the Miyoo and computer must be on the same trusted Wi-Fi network. The
device IP is shown at the top of **Apps → Tweaks → Network**.

### Recommended universal browser route

This is the simplest path for Windows, macOS, and Linux. It installs no helper
on the computer.

1. Extract `dist/Miyonos-App-0.1.31.zip`.
2. On the Miyoo, enable **Apps → Tweaks → Network → HTTP: Web-based file sync**.
3. Open the device page in a browser, then open `App`.
4. Upload the supplied `Miyonos` folder. Do not upload a nested folder and do
   not upload a `data` folder.
5. Wait for all files to finish, then open **Apps → Miyonos** on the device.

The browser login is normally `admin` / `admin` on an untouched OnionOS setup.
Use the service only on a trusted network and disable it after use. Full detail
is in [UNIVERSAL_INSTALL.md](UNIVERSAL_INSTALL.md).

### Managed SSH update route

This is the best route for maintainers because it validates the package, stages
the new app, preserves `App/Miyonos/data`, keeps a rollback copy, and switches
only after the new directory is complete.

1. On the Miyoo, enable **Apps → Tweaks → Network → SSH: Secure shell**.
2. From the project root, use the displayed address. For the last known device:

   ```sh
   MIYOO_SSH_MULTIPLEX=0 ./scripts/wifi-install.sh MIYOO_IP_ADDRESS
   ```

3. Enter the device password only when the SSH client requests it. The default
   OnionOS SSH account is normally `onion` with password `onion`; it may have
   been changed by the owner.
4. Open **Apps → Miyonos** when the installer finishes.

`MIYOO_SSH_MULTIPLEX=0` is intentionally shown because the last physical
device accepted SSH login but was less reliable with reused SSH connections.
For a normal device, `./scripts/wifi-install.sh DEVICE_IP` is sufficient.

To undo the last staged update:

```sh
MIYOO_SSH_MULTIPLEX=0 ./scripts/wifi-install.sh --rollback MIYOO_IP_ADDRESS
```

Do not use a raw file copy while the app is running unless diagnosing a broken
updater. It can leave an old in-memory process holding the framebuffer. The
managed updater is the authoritative day-to-day path. See
[WIFI_INSTALL.md](WIFI_INSTALL.md) for all options.

### Read-only connection check

Before a hardware investigation, collect facts without modifying the device:

```sh
MIYOO_HOST=MIYOO_IP_ADDRESS ./scripts/collect-device-info.sh
```

If the address is unavailable, use the Miyoo Network screen rather than
guessing an address.

## Build a new version and install it

For an ordinary source change, use this sequence from the project root:

```sh
./scripts/run-tests.sh
./scripts/test-simulator-input.sh
./scripts/test-simulator-fixture.sh
./scripts/build-desktop.sh
./scripts/test-simulator-screenshots.sh
./scripts/build-miyoo.sh
MIYOO_SSH_MULTIPLEX=0 ./scripts/wifi-install.sh MIYOO_IP_ADDRESS
```

For a complete release pass, run the following. It runs the core tests,
simulator suites, desktop build, simulator bundle, and ARM build when Docker
is available:

```sh
./scripts/test-all.sh
```

Before publishing a changed build, update all of these together:

- `VERSION`;
- `CMakeLists.txt` project version;
- `packaging/simulator/Info.plist`;
- `CHANGELOG.md`;
- user-facing version references and hashes in the relevant documentation.

`./scripts/build-miyoo.sh` packages the OnionOS ZIP, checksums, universal
browser installer, macOS browser installer, and debug symbols. It requires
Docker Desktop. `./scripts/build-simulator-app.sh` creates the double-clickable
macOS simulator and its ZIP.

## Test before hardware

### Fast local checks

```sh
./scripts/run-tests.sh
./scripts/test-simulator-input.sh
./scripts/test-simulator-fixture.sh
./scripts/test-simulator-screenshots.sh
```

These must pass before a device update. They cover protocol parsing, settings
persistence, staged Wi-Fi updates, universal-browser packaging, all 15 button
inputs, custom button mapping, the fixed recovery chord, native fixture
behavior, and deterministic UI output.

### Test interactively in the macOS simulator

```sh
./scripts/build-simulator-app.sh
open "dist/Miyonos Simulator.app"
```

Choose a safe scenario first. Only choose **Live Sonos** after the owner has
explicitly agreed that the simulator may discover and control speakers on the
local network. The simulator uses a separate Application Support data folder,
so it cannot overwrite the physical Miyoo settings.

### Minimal physical smoke test

After a successful update, verify these manually before calling a change
complete:

1. Open **Apps → Miyonos** and confirm a visible, sharp UI appears.
2. Confirm discovery or enter a known player under **Settings → Manual player
   IP**.
3. On Now Playing, test D-pad Left/Right for previous/next track only with the
   owner's approval because it changes playback. In a multi-room group, L1/R1
   must cycle Group and every speaker; Up/Down must change the displayed
   target only.
4. Confirm the battery percentage appears in the Now Playing header. Play a
   music track with known cover art, then confirm that a TV or line-in source
   shows **Cover unavailable** instead of a misleading cover.
5. Open **Settings → Button Mapping**. Verify that a mapping change is staged,
   **Back** saves it, and an invalid layout is refused.
6. Hold **Menu + Start** for three seconds and confirm default controls return.
7. Confirm **Menu** can still exit back to OnionOS.
8. In Favorites, confirm that a selected item with supplied artwork shows a
   cover and that an item without artwork shows **Cover unavailable**.
9. Test any Sonos action that changes playback, volume, groups, or a queue only
   with the owner's approval.

The full device checklist and evidence rules are in [TESTING.md](TESTING.md).

## Controls and recovery

The default Now Playing layout is: L1/R1 previous/next volume target (Group
and each speaker), Up/Down the focused target's volume, Left/Right previous/
next track, A play/pause, X group mute, Y rooms, L2 queue, R2 favorites, Start
menu, Select controls, Menu exit, and B back.

Every physical control is configurable in **Settings → Button Mapping**.
Changes are staged until saving and must retain actions for Up, Down, Confirm,
Back, and Exit. The fixed **Menu + Start for three seconds** recovery chord is
not configurable and always restores the defaults. This prevents a custom
layout from trapping the user.

See [CONTROLS.md](CONTROLS.md) for the full default layout and desktop keys.

## Real Sonos status and boundaries

The local adapter, room/group topology, playback metadata, cover-art download,
and an idempotent group-volume write have been exercised against a real Sonos
household without cloud authorization. The implementation also has fixture
coverage for direct stream favorites and `x-rincon-cpcontainer` collection
favorites.

Do not claim that every physical favorite type is proven yet. Version 0.1.21
has automated coverage for a direct radio station whose first `Play` call
returns transient UPnP 701 and whose active URI has rewritten session flags.
The matching new-logo proxy has been TLS-validated against the packaged trust
bundle, but final physical confirmation is still needed. Read
[LIVE_SONOS_VALIDATION.md](LIVE_SONOS_VALIDATION.md) and the unverified section
of [FINAL_STATUS.md](FINAL_STATUS.md) before changing or claiming Sonos
behavior.

## Troubleshooting rules

- Preserve `App/Miyonos/data`; it contains settings, logs, cached artwork, and
  diagnostics. Release packages deliberately do not include it.
- Read `App/Miyonos/data/logs/launcher.log` and `miyonos.log` before changing
  display or network code.
- If the UI is invisible, use the fixed recovery/exit controls and consult
  [TROUBLESHOOTING.md](TROUBLESHOOTING.md). Do not blindly replace framebuffer
  code that has already been validated on hardware.
- Treat a Sonos write as a real-world action. Ask before play/pause, changing a
  track, muting, changing volume, editing a group, or modifying a queue.
- Keep all release artifacts, source text, UI labels, documentation, and test
  messages in English.

## Documentation index

| Document | Use it for |
| --- | --- |
| [README.md](../README.md) | Product overview and end-user installation |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Code boundaries and runtime design |
| [CONTROLS.md](CONTROLS.md) | Default button layout and custom mapping safety |
| [BUILDING.md](BUILDING.md) | Desktop, simulator, ARM, and package builds |
| [TESTING.md](TESTING.md) | Automated suites and physical hardware checklist |
| [WIFI_INSTALL.md](WIFI_INSTALL.md) | Managed SSH install, update, and rollback |
| [UNIVERSAL_INSTALL.md](UNIVERSAL_INSTALL.md) | No-install browser upload for any computer |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Recovery and diagnosis |
| [PROTOCOL.md](PROTOCOL.md) | Sonos/UPnP protocol decisions |
| [LIVE_SONOS_VALIDATION.md](LIVE_SONOS_VALIDATION.md) | Privacy-sanitized real Sonos evidence |
| [FINAL_STATUS.md](FINAL_STATUS.md) | Current release artifacts and unverified boundary |
| [RELEASE.md](RELEASE.md) | Maintainer release checklist |

## Suggested first prompt for a new chat

> Read `docs/PROJECT_HANDOFF.md`, `README.md`, `docs/ARCHITECTURE.md`,
> `docs/TESTING.md`, and `docs/FINAL_STATUS.md` before changing Miyonos. Keep
> all project text in English. Preserve `App/Miyonos/data`, use the managed
> Wi-Fi updater for device changes, test in the simulator first, and ask before
> sending a Sonos command that changes real playback or volume.
