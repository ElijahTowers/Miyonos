# Changelog

## 0.1.29 — Queue track cover art

- Queue rows now show a compact cover-art thumbnail for each visible track.
- Queue cover retrieval is lazy and serialized: Miyonos reads the existing
  bounded disk cache first, requests only the nearby rows, and keeps only eight
  64-pixel textures in memory so the Miyoo remains responsive on long queues.

## 0.1.28 — Real Sonos playlist and queue routing

- Queue now reads Sonos' track container `Q:0`, rather than the `Q:` index
  that real players return as technical queue instances. It therefore shows
  the actual upcoming tracks.
- Queue's X view now lists Spotify and other playlist-shaped Sonos Favorites,
  which is where the tested household exposes its playlists. Selecting one
  replaces the active queue, opens Now Playing immediately, and retains the
  selected playlist name while Sonos reports its generic queue transport.
- Uses the transport URI from `GetMediaInfo` to distinguish the generic Sonos
  queue from a direct track URI, keeping playlist context stable for Spotify
  playback.

## 0.1.27 — Immediate playlist Now Playing

- Selecting a Saved Playlist now opens Now Playing immediately and shows the
  selected playlist name before the player finishes its first metadata poll.
- If the playlist cannot start, Miyonos restores the previous context and
  explains the failure instead of leaving a misleading playlist label.

## 0.1.26 — Queue matches Sonos

- Queue now always reads Sonos' active `Q:` queue and no longer substitutes a
  saved-playlist browser view while a Saved Playlist is playing.
- This prevents duplicate-looking entries and makes Queue match the list shown
  by Sonos itself. Saved Playlists remains a separate view under X.

## 0.1.25 — Reliable Now Playing playlist context

- Shows the active Saved Playlist on Now Playing even when a Sonos player
  reports only its `SQ:` playlist identifier instead of its display name.
- Looks up a missing title from Saved Playlists and retains the title for the
  queue session that Miyonos starts, without carrying it into another source.

## 0.1.24 — Radio stream-label cleanup

- Removes Sonos' trailing `-BB-AAC` (and equivalent codec) backend suffix
  from station names while preserving the readable station name before it.

## 0.1.23 — Radio and Favorites polish

- Hides raw UPnP class values and station backend identifiers from Now Playing.
  A station started from Favorites keeps its readable Favorite name while
  Sonos supplies incomplete radio metadata.
- Treats Previous/Next as unavailable for radio streams before sending a
  transport request, replacing a protocol error with a clear explanation.
- Makes volume feedback stable: the persistent target and volume bar provide
  immediate feedback, while delayed write acknowledgements no longer replace
  it with stale toast messages.
- Filters unusable Sonos Favorites navigation placeholders that have neither a
  container nor a playable URI, so an empty "Favorites" item is not shown.

## 0.1.22 — TuneIn certificate-chain repair

- Adds the official Google Trust Services GTS Root R4 trust anchor required by
  the observed TuneIn station-logo CDN chain. The target now verifies the same
  strict Sonos Radio proxy-to-TuneIn redirect chain as the desktop build.

## 0.1.21 — Sonos Radio live compatibility

- Fixes the misleading direct-station failure after audio has already begun.
  Sonos can rewrite only the stream session flags in the active URI; Miyonos
  now confirms the stable station identifier and playing state instead of
  requiring an exact, unstable URI match. The confirmation window is bounded
  to eight seconds.
- Adds the currently reported `sali.sonos.superhi.fi` Sonos Radio artwork
  proxy to the deliberately narrow allowlist. It can make the same single,
  certificate-verified redirect to the fixed TuneIn logo CDNs as the already
  supported Sonos Radio proxies; no general external HTTPS access is added.

## 0.1.20 — Sonos Radio start and artwork

- Treats a transient UPnP 701 response from an `x-sonosapi-stream` Favorite as
  a station-start transition, then performs bounded transport/media checks
  before reporting success or failure. This prevents a misleading error while
  a Sonos Radio stream is buffering.
- Renames the opt-in setting to **External cover art over HTTPS** and adds a
  strict Sonos Radio/TuneIn artwork chain: only the two observed Sonos radio
  proxies may make one HTTPS redirect to either fixed TuneIn logo CDN. Every
  request keeps TLS 1.2+, certificate and hostname verification, public-IP
  resolution, 16 KiB headers, an 8 MB response cap, and no credentials.

## 0.1.19 — Complete trusted Spotify cover certificate bundle

- Replaces the single-root target trust file with a three-root bundle for the
  fixed Spotify and Spotify-in-Sonos cover hosts: DigiCert Global Root G3,
  GlobalSign Root R3, and Starfield Services Root G2.
- Fixes cover downloads such as Pop Soul Mix, whose Spotify endpoint chains to
  GlobalSign rather than DigiCert, while preserving hostname verification, no
  redirects, bounded responses, and the fixed public-host allowlist.

## 0.1.18 — Precise trusted-cover TLS diagnostics

- Distinguishes a trusted-cover TLS handshake failure from certificate-chain or
  hostname rejection in the local diagnostic log, without recording a cover
  URL, account, cookie, playback data, or device identifier.

## 0.1.17 — External cover reliability diagnostics

- Gives trusted external Spotify-cover downloads a bounded 4-second connection
  allowance and 8-second read allowance. Downloads remain asynchronous, so
  input and rendering stay responsive.
- Records a privacy-preserving cover failure reason in Diagnostics and the
  local Miyonos log without recording the requested cover URL.

## 0.1.16 — Real Spotify Favorite cover endpoints

- Accepts the strict public Spotify and Spotify-in-Sonos cover endpoint forms
  that real Sonos Favorites expose: 40- or 64-hex image IDs, 640-pixel Spotify
  mosaics, seed-mix artwork, regional Spotify CDN images, and Sonos' Spotify
  folder artwork.
- Retains the explicit default-off switch, TLS 1.2+ certificate and hostname
  verification, fixed allowlist, no redirects, bounded responses, and zero
  account, cookie, playback, or device data disclosure.
- Fixes the over-strict 64-hex-only validation that left valid 40-hex Spotify
  Favorite artwork as **Cover unavailable**.

## 0.1.15 — Spotify cover request-path fix

- Fixes the OnionOS TLS client request target for optional Spotify Favorite
  artwork. The previous target build omitted `/image/`, so Spotify rejected the
  otherwise verified request and Miyonos showed **Cover unavailable**.
- Adds a regression check for the exact canonical Spotify image request path.

## 0.1.14 — Optional verified Spotify Favorite cover art

- Adds **Spotify cover art over HTTPS** in Settings. It is off by default, so
  Miyonos remains LAN-only until its owner explicitly enables it.
- When enabled, Miyonos accepts only canonical `https://i.scdn.co/image/...`
  cover URLs supplied by Sonos Favorites. It uses certificate and hostname
  verification, sends no Sonos login, cookies, playback data, or identifiers,
  follows no redirects, and keeps the existing 8 MB cache and response limits.
- Keeps ordinary Sonos `/getaa` artwork local. Unsupported services and an
  unavailable internet connection safely continue to show **Cover unavailable**.

## 0.1.13 — Favorite cover preview and group volume target

- Shows the selected Sonos Favorite's source-provided cover art beside the
  list. The artwork request is bounded, uses the existing on-card cache, and
  remains separate from Now Playing and Saved Playlists. Favorites without a
  usable cover URL clearly show **Cover unavailable**.
- Adds **Group** to the L1/R1 volume-target cycle. Up/Down now adjusts either
  the selected individual speaker or the whole group's Sonos volume, with a
  clear on-screen target label. Pending group and individual volume requests
  are coalesced independently.
- Adds fixture and controller coverage for Favorite artwork metadata and the
  Group → speaker → Group volume-target sequence.

## 0.1.12 — Saved playlist browser and clean queue replacement

- Keeps the Main Menu clean while adding **Saved Playlists** directly beside
  **Queue**. Press X in Queue to switch views; press X again to return. The
  selected saved playlist shows its source-provided cover art beside its name.
- Starting a saved playlist now explicitly clears the active Sonos queue,
  loads the selected saved playlist, opens the refreshed queue, selects its
  first returned track, and starts playback. Tracks from a previously selected
  playlist are no longer left behind in the active queue.
- Adds protocol, native-simulator, and Python-fixture checks for saved
  playlist cover metadata, browsing, and replacement-queue playback.

## 0.1.11 — Current playlist queue and per-speaker volume

- Removes the standalone Sonos Playlists item from the Main Menu. **Queue** now
  shows the actual tracks that Sonos will play: when a saved Sonos playlist is
  active, it reads that playlist's active container instead of the unrelated
  generic queue container.
- Adds a visible speaker focus on Now Playing. L1/R1 cycles through the
  controllable speakers in the selected group, and D-pad Up/Down changes only
  that speaker's volume through `RenderingControl`, without changing the
  group volume.
- Keeps Left/Right as previous/next track and keeps X as the existing group
  mute control. The default L1/R1 mapping migrates automatically for people
  who have not customized their controls; customized mappings are preserved.
- Adds fixture coverage for active playlist tracks and individual speaker
  volume reads, plus deterministic decoded-cover and fallback-cover screen
  captures.

## 0.1.10 — Cover art and device battery

- Uses `GetMediaInfo` metadata to fill only missing current-track fields,
  including a source-provided cover URL. This keeps the current song title and
  timing intact while recovering cover art that some Sonos sources report at
  the media level instead of the track level.
- Shows the Miyoo Mini Plus battery percentage and a coloured battery gauge in
  the Now Playing header. The value is read locally from the AXP223 fuel gauge
  through a read-only I2C transaction and is cached for five seconds.
- Clearly labels the artwork area as **Cover unavailable** when Sonos supplies
  no usable cover, such as many TV, line-in, and radio sources.

## 0.1.9 — Current playlist on Now Playing

- Shows the title of the active Sonos saved playlist on the Now Playing screen
  without replacing the title, artist, album, artwork, or queue position of
  the current track.
- Reads the active-container metadata returned by `GetMediaInfo` and only
  presents it as a playlist when Sonos identifies a saved playlist container.
  It retains the selected title when a Sonos queue omits that metadata after
  Miyonos starts a saved playlist.
- Added native and Python fixture metadata plus automated protocol and
  deterministic screenshot coverage for the playlist label.

## 0.1.8 — Custom safe button mapping

- Changed D-pad Left and Right on Now Playing to previous and next track while
  preserving their normal navigation behavior in lists, menus, and editors.
- Added a Button Mapping screen for all 15 physical Miyoo controls, with
  assignable navigation, playback, seek, library, refresh, menu, exit, and
  no-action commands.
- Stages mapping edits until they pass a lockout check, which always requires
  Up, Down, Confirm, Back, and Exit to remain assigned.
- Added a mapping-independent MENU + START three-second recovery chord that
  restores the complete default layout and returns to Settings.
- Persisted mappings in the local settings file with backward-compatible,
  corruption-safe defaults and added keyboard, gamepad, persistence, and
  recovery coverage.

## 0.1.7 — Real Sonos favorite playback

- Read the physical household's Favorites container without mutation and
  identified real collection favorites represented by
  `x-rincon-cpcontainer` URIs plus embedded Sonos metadata.
- Added Sonos' advertised `AddURIToQueue` flow for collection favorites, then
  opens the coordinator queue, seeks to the exact returned track number, and
  starts playback.
- Kept direct `SetAVTransportURI` playback for radio and other stream
  favorites, and request complete browse metadata for both paths.
- Added contextual action errors with the real UPnP code, making a rejected
  queue, selection, or playback step distinguishable on the device.
- Added fixture coverage for collection and stream favorites while retaining
  the sharp direct framebuffer output introduced in 0.1.6.

## 0.1.6 — Sharp native framebuffer output

- Captured both physical framebuffer pages from a running 0.1.5 app and
  confirmed that the Mini hardware scale path produced incomplete, stretched,
  and blurred interface fragments despite reporting no blitter error.
- Replaced physical texture scaling with a device-only direct 640 × 480 ARGB
  double-buffered framebuffer presenter, bypassing the unreliable Mini GFX
  blitter while preserving SDL input and lifecycle handling.
- Software-rotated each frame to the panel's native orientation and switched
  pages atomically, keeping the complete interface sharp at its authored
  resolution.
- Visually confirmed the new color-band probe on the physical Miyoo and
  independently captured a complete, sharp framebuffer page.

## 0.1.5 — Proven Miyoo presentation route

- Reproduced the black screen on physical hardware and traced it to repeated
  `MI_GFX_BitBlit` failures for the 640 × 480 rotated source frame.
- Ran the upstream Miyoo Mini 320 × 240 RGB565 example on the same device and
  visually confirmed its red, green, and blue output.
- Kept the complete interface authored and tested at 640 × 480, then added a
  device-only downsample to the proven 320 × 240 RGB565 texture route; the
  Mini driver scales that texture back to the 640 × 480 panel.
- Replaced the physical fullscreen window with the normal visible window used
  by the working upstream examples, while leaving desktop and simulator output
  pixel-identical.

## 0.1.4 — Physical display compatibility

- Changed physical OnionOS presentation to the RGB565 streaming-texture path
  used by the official Miyoo Mini SDL examples, fixing a running app that
  could otherwise leave the framebuffer completely black.
- Added a safe on-device display probe with unmistakable color bands; it
  returns to Onion MainUI automatically after eight seconds or any key press.
- Made two consecutive Menu presses an emergency exit, so MainUI remains
  recoverable even if the interface cannot be seen.
- Retained the real local Sonos integration, source cover artwork, universal
  browser installer, Wi-Fi updater, and macOS simulator from 0.1.3.

## 0.1.3 — Real Sonos and cover artwork

- Validated local discovery, mixed-model topology, coordinator routing,
  playback metadata, cover retrieval, and group-volume control against a real
  Sonos household without a cloud account.
- Bundled the missing target JPEG runtime so source-provided Sonos cover art
  can be decoded and shown on the Miyoo instead of always using fallback art.
- Hardened asynchronous artwork changes so stale covers cannot replace a newer
  track and failed downloads can be retried.
- Added target-runtime and universal-package checks for the image decoder and
  its complete license notice.
- Added a double-click simulator scenario chooser and a built-in macOS image
  decoder so safe fixture artwork and live Sonos JPEG covers can be tested
  without installing additional Mac software.
- Added an opt-out for SSH connection reuse, allowing staged Wi-Fi updates on
  Miyoo SSH servers that accept login but stall on multiplexed sessions.

## 0.1.2 — Miyoo simulator

- Added a double-clickable, self-contained `Miyonos Simulator.app` for macOS
  with a clickable Miyoo Mini Plus shell around the exact 640 × 480 app frame.
- Split startup into a reusable `AppRuntime` with Desktop, Simulator, and
  OnionOS profiles; every profile now uses the same controller, renderer,
  storage, network, and semantic `Action` input path.
- Added mouse, keyboard, and SDL game-controller mappings with protected held
  input and rate-limited D-pad repeat.
- Added an embedded native Sonos fixture with normal, multi-room, grouped,
  long-queue, missing-artwork, slow, offline, and coordinator-change scenarios,
  plus an explicit Live Sonos mode.
- Added isolated simulator SD-card storage, deterministic 640 × 480 captures,
  pixel references, fixture/input/restart/cleanup tests, and one-click build,
  run, and full-test scripts.
- Kept all simulator sources, fixtures, and development data out of the ARM
  binary and OnionOS packages.

## 0.1.1 — Device display hotfix

- Fixed a black screen on the Miyoo Mini Plus by drawing the complete interface
  into a software frame and presenting that frame through the Mini SDL texture
  path supported by the device.
- Added an automated device-frame presenter smoke check to the desktop build.

## 0.1.0 — Technical Preview

- Added asynchronous SSDP and manual-IP Sonos discovery.
- Added topology-aware room/group selection and coordinator routing.
- Added playback, safe volume, mute, seeking, queue, favorites, playlists,
  grouping, settings, diagnostics, and offline recovery.
- Added a 640 × 480 button-first SDL2 interface and OnionOS package.
- Added desktop protocol/UI tests and a configurable mock Sonos server.
- Added checksummed Wi-Fi installation and staged updates over OnionOS SSH,
  preserving user data with one-command rollback.
- Added a local macOS browser installer with a double-click launcher, guided
  setup, progress display, friendly errors, credential non-persistence, and
  protected rollback.
- Added the primary universal browser package for Windows, macOS, and Linux,
  using OnionOS's built-in HTTP file server with no computer-side installation
  or runtime.
