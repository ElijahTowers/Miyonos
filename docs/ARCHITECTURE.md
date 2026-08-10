# Architecture

Miyonos is deliberately split into six layers:

- `platform`: SDL input, monotonic time, local battery-gauge reads, logging,
  and Onion launch behavior;
- `network`: bounded IPv4 LAN HTTP/SSDP transport plus a deliberately narrow,
  certificate-verified external cover client;
- `sonos`: XML, SOAP, service discovery, topology, media, and typed actions;
- `domain` and `app`: player/group/playback models and the navigation/state
  controller;
- `ui`: bitmap typography and pixel-aligned SDL rendering;
- `simulator`: the desktop-only Miyoo shell and native safe Sonos fixture.

The UI never creates SOAP and never waits on a socket.

## Runtime profiles and frame flow

`RuntimeMode` selects `Desktop`, `Simulator`, or `OnionOS`. `AppRuntime` owns
the shared controller, semantic input, update, rendering, sleep, capture, and
shutdown loop. Raw SDL keys, mouse positions, controller buttons, and physical
Mini keycodes are converted to `Action` in the platform layer; the application
controller never sees device-specific codes.

Every mode renders through the same 640 × 480 ARGB software surface. Desktop
and `--screen-only` present it directly. Simulator uploads the frame as one
texture and places it in the clickable Miyoo shell. OnionOS uploads that same
frame through the pinned Mini SDL streaming-texture path. Simulator shell,
fixture, and development data are excluded at compile time from the ARM build.

## Controller flow

The main thread polls SDL, translates raw keys into semantic actions, updates
the controller, and renders at about 30 FPS. A single network worker consumes a
bounded 32-command queue and publishes into a bounded 32-result queue.
Playback, browse, topology, discovery, and artwork work all use that worker.

Individual speaker-volume updates are optimistic. Now Playing keeps a focused
visible speaker in the active group; R1 changes that focus while L1 opens the
Speaker Volumes overview, and Up/Down sends `RenderingControl` volume only to
that speaker. Pending volume commands are
replaced only by a newer value for the same speaker, so rapid changes to one
room cannot discard another room's request. Other full queues give visible
“please wait” feedback. Socket waits are bounded and observe the shutdown
cancellation flag.

Playback is polled at an intensity selected in Settings. Between responses,
elapsed time is extrapolated locally only while Playing. After repeated poll
failure the topology is refreshed. Failed discovery retries in the background
with bounded exponential backoff.

## Sonos adapter

`SonosAdapter` is the protocol boundary. It discovers device-description
service URLs instead of assuming fixed control paths. The adapter parses
ZoneGroupTopology, routes group transport to its coordinator, uses
GroupRenderingControl for existing group-level state where advertised, and
uses per-player RenderingControl for the focused speaker's volume. UI and
settings code only see typed domain objects and results.

## Persistence and caching

`settings.ini` is schema-versioned, preserves unknown fields, validates ranges,
and is written through a temporary file plus atomic rename. Cached player
addresses and the last selection are included. `topology.snapshot` contains
only bounded room/group labels and counts for non-sensitive local recovery
context.

Artwork uses deterministic FNV-derived file names, validates PNG/JPEG
signatures and dimensions, writes atomically, touches hits, and evicts oldest
entries above the configured cap. Current-track, selected Favorite, and
selected Saved Playlist artwork use separate bounded requests while sharing
that on-card cache. The renderer holds at most one decoded artwork texture.
Local Sonos artwork remains plain IPv4 HTTP. The separate external-cover path
is on by default and accepts only a fixed allowlist of strict Spotify,
Spotify-in-Sonos, and observed Sonos Radio/TuneIn public cover endpoints:
40- or 64-hex image IDs, 640-pixel mosaics, seed-mix artwork, regional Spotify
CDN images, the fixed Sonos Spotify folder image, and three Sonos Radio artwork
proxies. A radio proxy may make exactly one TLS-verified redirect to either
fixed TuneIn logo CDN; every other redirect is rejected. It uses a bundled
five-root trust bundle with TLS hostname verification and the same
response/cache limits. It sends no account, cookie, playback, or device data.
It is not a general web client. The same worker also supports the separate,
default-on **Official Sonos product photos** setting: it may fetch only four
exact, direct 480-pixel PNG URLs from `media.sonos.com`, selected from the
locally discovered Beam, Roam, Era 100, or Arc model. Product photos are not
bundled or proxied; no redirects or dynamically supplied URLs are accepted.
Logs rotate at 256 KiB into one previous file.

OnionOS reads the Miyoo Mini Plus battery percentage from the AXP223 fuel
gauge through a read-only I2C transaction. The renderer requests it at most
once every five seconds, so rendering never repeatedly touches the device bus.

## Shutdown

Exit first sets a cancellation flag, closes the command queue, joins the
worker, saves settings, destroys SDL resources, and removes the Onion
stay-awake marker. `launch.sh` also traps normal exit and signals, removes the
marker, and resumes Onion MainUI even after an application failure.

In Simulator mode the owned native Sonos fixture is stopped and joined after
the controller worker; clean app exit then renders a brief OnionOS Apps view.
The macOS launcher keeps all simulator settings, logs, and artwork in its own
Application Support SD-card tree.
