# Testing

Run all desktop protocol, integration, storage, queue, and state tests with:

```sh
./scripts/run-tests.sh
```

The script builds with warnings enabled, starts the Python Sonos-compatible
fixture on `127.0.0.1:1400`, runs the tests, and always stops the server.
Current coverage includes SSDP/header/deduplication, HTTP Content-Length and
chunking, timeout, URL rules, XML limits/entities/escaping, SOAP envelopes and
faults, device services, bonded/invisible topology, coordinator selection,
transport, volume, durations, seek, DIDL, settings migration/atomic writes,
cache validation/eviction, bounded command coalescing, two-page queue browse,
live adapter actions, active saved-playlist tracks and cover metadata,
replacement-queue saved-playlist playback, Favorite artwork metadata,
individual/group volume-target cycling, controller navigation/modal safety, and
strict default-off external HTTPS cover URL acceptance/rejection, including
the Sonos Radio/TuneIn proxy and redirect destination boundary.

The legacy Python mock server supports `one`, `normal`, `multi-room`, `grouped`,
`long-queue`, `no-artwork`, `stereo`, `home-theater`, `delay`, `slow`, `fault`,
`malformed`, and `coordinator-change` scenarios. The bundled native simulator
fixture exposes the eight end-user scenario names documented above. Both
provide paged Queue data, favorite containers, playlists, controllable
playback, artwork, and deliberate error paths.

When a packaged release is present, run the isolated Wi-Fi lifecycle test with:

```sh
./tests/integration/test_wifi_installer.sh
```

It replaces SSH with a local test double and verifies first installation,
update staging, checksum use, preservation of settings/data, backup isolation,
and rollback without touching a real device.

The full runner also checks the local browser installer: loopback-only access,
origin and token protection, IPv4 validation, password redaction, install
completion, status reporting, and rollback.

The universal package test verifies that the installer is self-contained,
English-language, browser-safe, free of computer-side dependencies, correctly
versioned, and contains an upload-ready app folder without user data.

## Simulator suite

Run the complete local release pipeline with:

```sh
./scripts/test-all.sh
```

The simulator-specific parts can be run independently:

```sh
./scripts/test-simulator-input.sh
./scripts/test-simulator-fixture.sh
./scripts/test-simulator-screenshots.sh
```

The input suite checks all 15 Miyoo controls through clickable hit areas and
their keyboard/gamepad equivalents, including short press, D-pad hold delay,
repeat rate, release outside a button, and the absence of unsafe repeats for
volume-independent function buttons. The native fixture suite validates normal
playback, separate rooms, grouping, 360 queue entries, missing artwork, slow
responses, coordinator changes, and cleanup/offline refusal.

The screenshot suite uses `--screen-only`, a fixed visual clock, and a fixed
simulated battery reading to compare a deterministic 640 × 480 BMP against its
approved SHA-256 reference. It also restarts against the same isolated SD-card
tree, verifies settings persistence, captures offline behavior, and confirms
that the local fixture leaves no listener behind. The current 170-check
controller suite automatically visits
Now Playing, Rooms, Group Editor, Queue, Favorites, Settings, Help, About,
Diagnostics, IP Editor, action confirmation, and exit confirmation.

Render a deterministic UI preview without a display:

```sh
./scripts/render-preview.sh
```

## Remaining physical smoke test

- [ ] Cold start reaches a visible discovery screen in under one second.
- [ ] Wi-Fi off shows the offline actions; retry does not freeze.
- [ ] One room is discovered, selected, and controlled.
- [ ] Multiple rooms and current groups have correct names/member counts.
- [ ] Stereo pair and home theater hide satellites, surrounds, and Sub.
- [ ] Play/pause, previous/next, group mute, and safe repeated volume changes
      work. L1/R1 must cycle Group and each controllable member of a group.
- [ ] Music, radio, TV audio, podcast, AirPlay/Spotify Connect metadata degrade
      cleanly when fields or actions are unavailable.
- [ ] Seekable media seeks; non-seekable media explains without track skipping.
- [ ] Queue pages past 60, page jumps, selection, refresh, and play work. It
      must match the active Sonos Queue (`Q:`), including after a saved playlist
      was started. X must switch to Saved Playlists, show the selected cover
      when supplied, and A must replace—not append to—the active Sonos queue.
- [ ] Favorite and nested favorite container open/play. The selected Favorite
      shows source-provided cover art, or **Cover unavailable** when absent.
- [ ] With internet access and **External cover art over HTTPS** explicitly on,
      a Spotify Favorite with a canonical public cover URL and a supported
      Sonos Radio/TuneIn station both show their selected cover. Starting the
      radio station must show a progress message, not a transient UPnP error.
      Turn the setting off again and confirm no internet request is made.
- [ ] Unsupported favorite shows the explicit unsupported message.
- [ ] Join and remove a room refreshes topology and retains a coordinator.
- [ ] Speaker reboot and router reconnect recover without restarting Miyonos.
- [ ] Sleep/dim settings and `/tmp/stay_awake` behavior are correct.
- [ ] Menu exit, signal exit, and an induced crash all resume Onion MainUI.
- [ ] Settings persist; updating the app keeps the `data` directory.
- [ ] One-hour session shows no increasing memory, cache, queue, or log growth.

Physical checks must be recorded with Onion version, speaker models/firmware,
scenario, and log attachment; a mock result is never marked as device proof.
