# Miyonos 0.1.21 final status

Status: **real Sonos LAN integration, cover retrieval, simulator, and release
packages complete; sharp direct framebuffer output is visually and digitally
verified. Version 0.1.21 reconciles direct Sonos Radio buffering using the
stable station identity and adds the tightly bounded current Sonos
Radio/TuneIn logo path. Its physical radio visual smoke test awaits final
confirmation.**

## Delivered

Miyonos is a C++17/SDL2 local Sonos remote for OnionOS. It discovers speakers
over SSDP, reads device descriptions and grouped topology, routes transport
actions to the coordinator, controls playback/volume/mute/seeking, browses the
Queue/Favorites, presents current metadata and selected cover art, persists
settings, recovers from offline speakers, and records bounded diagnostics and
logs. When a saved Sonos playlist is playing, Queue reads the active playlist
container and lists its tracks. X in Queue opens Saved Playlists, whose
selected entry shows source-provided cover art; A replaces the active queue
with that saved playlist before starting it. It uses no Miyonos account, cloud
service, API key, analytics, or Sonos cloud authorization.

Version 0.1.21 retains the adapter validated against a real Beam/Play:1/Sub
Mini/Roam household and the target cover-art path fixed in 0.1.3. Physical
testing showed that both the rejected 640 × 480 Mini GFX route and its accepted
320 × 240 scale route were unsuitable for a complete sharp interface. Miyonos
now software-rotates its authored 640 × 480 frame directly into the device's
double-buffered ARGB framebuffer, bypassing the unreliable hardware blitter.
It retains the safe display probe and two-press Menu emergency exit. It also
distinguishes direct stream favorites from collection favorites, adding the
latter through Sonos' advertised queue action before selecting and playing the
returned position. Detailed, privacy-sanitized Sonos evidence is in
[LIVE_SONOS_VALIDATION.md](LIVE_SONOS_VALIDATION.md).

The Now Playing D-pad Left/Right actions select the previous/next track. L1/R1
now cycles the volume target through Group and every controllable speaker in
the active group; D-pad Up/Down changes only that target. Group uses Sonos'
group-volume service when available, while an individual target uses its own
`RenderingControl` volume. X remains the existing group mute control.
All 15 physical Miyoo controls can be reassigned from Settings, including
separate seek actions and No action. Edits remain staged until a safety check
confirms that Up, Down, Confirm, Back, and Exit are still available. A fixed
MENU + START three-second chord restores the defaults independently of the
saved mapping.

Version 0.1.12 adds the saved-playlist browser as a companion view to Queue,
not a Main Menu entry. It parses each playlist's `albumArtURI`, downloads only
the selected cover through the existing bounded cache, and displays it beside
the playlist names. Choosing a playlist clears the active Sonos queue, adds
the selected saved queue, opens the refreshed queue, seeks to its first track,
and starts playback. This makes playlist selection a predictable replacement
instead of leaving tracks from a previous playlist behind.

Now Playing also shows the name of the active Sonos saved playlist when the
speaker's active-container metadata identifies a playlist. The current track
metadata remains separate, so album, artist, artwork, elapsed time, and queue
position continue to describe the playing item.

Version 0.1.13 gives Favorites the same selected-item cover treatment as Saved
Playlists. It requests only the focused Favorite's `albumArtURI`, shares the
existing bounded cache, and keeps a separate asynchronous result target so a
late Favorite, Playlist, or Now Playing download can never overwrite another
screen's artwork. The Favorites view shows **Cover unavailable** when Sonos
does not expose usable artwork.

Versions 0.1.14 through 0.1.21 add a narrow, explicit solution for Favorites
whose Sonos metadata exposes public artwork instead of a local Sonos image.
**External cover art over HTTPS** remains off by default. When enabled, it
accepts only an explicit fixed allowlist of observed Spotify,
Spotify-in-Sonos, and Sonos Radio/TuneIn endpoint forms: 40- or 64-hex
`i.scdn.co` images, regional Spotify CDN images, 640-pixel mosaics, seed
mixes, the fixed Sonos Spotify folder icon, and three fixed Sonos Radio proxies.
It rejects local/private addresses and every redirect except one verified
radio-proxy redirect to either fixed TuneIn logo CDN. It verifies TLS 1.2+ and
the hostname, and uses only the three offline roots needed for these fixed
hosts: DigiCert Global Root G3, GlobalSign Root CA - R3, and Starfield Services
Root Certificate Authority - G2. It has the same 16 KiB-header, 8 MB-body, and
cache limits as local artwork. It sends no Sonos-cloud login, cookies, playback
state, or device identifier. A failed or disabled request simply leaves
**Cover unavailable**; playback remains local and independent.

Version 0.1.15 corrected the target HTTPS request path. Version 0.1.16 added
the observed endpoint forms. Version 0.1.17 increased bounded timeouts and
records a local diagnostic, while 0.1.18 distinguishes a TLS handshake failure
from a post-handshake verification failure. A real Miyoo diagnostic then
identified certificate-chain validation as the remaining failure. Version
0.1.19 packages the full required trust bundle and updates the launcher to use
it. The bundle validates every observed cover endpoint locally; the owner has
since confirmed the Spotify cover result on the Miyoo.

Version 0.1.20 added the first Sonos Radio behavior. The observed
station Favorites are direct `x-sonosapi-stream` items. A station can answer
the immediate `Play` request with transient UPnP 701 while buffering, then
begin playing shortly afterward. A real-device retest showed that Sonos also
rewrites the session-only query flags in the active URI, so an exact URI
comparison falsely reported failure even when audio was playing. Version
0.1.21 now waits up to eight seconds for the stable station identity and
playing state. It also accepts the new observed `sali.sonos.superhi.fi` radio
proxy, alongside the previous two fixed proxies; each may redirect once to a
fixed TuneIn logo CDN. The optional external-artwork path permits precisely
that single verified redirect and no other redirect. The three-root bundle
validates every endpoint in this observed chain.

Version 0.1.11 fills missing current-track fields from Sonos `GetMediaInfo`
metadata, including a source-provided `albumArtURI`, without replacing the
title, timing, or queue position from `GetPositionInfo`. It therefore recovers
cover art for sources that report it at the active-media level. When Sonos
supplies no usable cover URL, Now Playing explicitly shows **Cover
unavailable** instead of inventing cover art for TV, line-in, or radio playback.

Now Playing also shows the Miyoo Mini Plus battery percentage and a coloured
gauge. It reads the AXP223 fuel gauge through a local read-only I2C transaction
and caches readings for five seconds. If the gauge is unavailable, Miyonos
hides the indicator rather than showing a guess.

The release includes the no-install universal browser package for Windows,
macOS, and Linux, an optional staged SSH installer with update/rollback, and a
double-clickable macOS Miyoo simulator. All authored application text,
packaging copy, scripts, and documentation are English.

The simulator's double-click launcher now offers every safe scenario in a
native chooser and gates Live Sonos behind a second warning. Its built-in
macOS ImageIO path displays fixture PNGs and real Sonos JPEG covers without a
separate SDL2_image installation.

## Release artifacts

```text
dist/Miyonos-Simulator-0.1.21-macOS.zip
SHA-256 75d76b9df818f3f2857429e55a5d18f1130a4dcf4ccfa7ed6d68a89fb7bb2b10
Size 1,147,530 bytes

dist/Miyonos-0.1.21-OnionOS.zip
SHA-256 68a4c73c5676f8dd93a80e9d3b8d25c03ce2aa23a5df65e84ccca2a3ddbabb97
Size 12,598,660 bytes

dist/Miyonos-0.1.21-Universal-Browser-Installer.zip
SHA-256 5f505ab5c65a5ae0b9e063adba78572fa22dba5fbe5c81118d266bf1b8aa700d
Size 12,605,604 bytes

dist/Miyonos-0.1.21-WiFi-Installer-macOS.zip
SHA-256 ecf8c42347e0aeb49cf189e3034ec0838c203f5c29c0d5150e3553adfedd96b0
Size 12,618,060 bytes
```

All ZIP files passed CRC and SHA-256 verification. The universal ZIP contains
the same upload-ready `Miyonos` application folder as the OnionOS package,
including `libSDL2_image-2.0.so.0`, `libjpeg.so.9`, and their notices. It
contains no user `data` directory. The simulator app is ad-hoc signed, bundles
its linked SDL2 library, embeds the Spotify trust certificate resource, and
reports version 0.1.21. Debug symbols remain separate at
`dist/Miyonos-0.1.21-symbols`.

## Verification completed

- Desktop protocol/integration/state suite: **205 checks, 0 failures**.
- Simulator input suite: **131 checks, 0 failures** across mouse, keyboard,
  gamepad, custom mappings, the fixed recovery chord, protected held input,
  and D-pad repeat.
- Native simulator fixture suite: **68 checks, 0 failures** across grouped and
  separate rooms, long queues, saved-playlist replacement, Favorite artwork,
  missing artwork, slow/offline behavior, and coordinator changes.
- Simulator screenshot/restart suite: deterministic 640 × 480 frame,
  preserved settings, offline rendering, isolated SD tree, and clean fixture
  shutdown passed.
- Wi-Fi updater and browser installers: checksum gate, staged first install,
  update, complete data preservation, previous-version backup, rollback,
  local-only browser security, and universal-package layout passed.
- Physical Wi-Fi update: an OnionOS Miyoo was upgraded from 0.1.0 through
  0.1.7 to 0.1.8 over SSH. Device data was preserved, the 0.1.8 ARM binary was
  checksum-verified before an atomic switch, its command-line smoke returned
  `Miyonos 0.1.8`, and the previous binary was removed.
- Physical Wi-Fi update: the same Miyoo was atomically updated from 0.1.8 to
  0.1.10 over SSH. Its installed ARM binary matches SHA-256
  `d4e4a8141c730b1dbaa7153935926121260c9f1cf125d1376e09090eeb0016cc`,
  `miyonos --version` returned `Miyonos 0.1.10`, all required artwork runtime
  libraries are present, user data was retained, and the updater kept a 0.1.8
  rollback directory.
- Physical Wi-Fi update: the same Miyoo was updated from 0.1.10 to 0.1.11 on
  2026-08-04. Its Wi-Fi link stalled on the full package stream, so the update
  used the separately checksum-verified delta: the only changed package files,
  `miyonos` and `VERSION`, while re-verifying the identical installed runtime
  libraries first. The installed binary matches SHA-256
  `4e3c81be93996a1481f9d967cd2a95ebfd79126ff6931aa700f3b66bbb429500`,
  `miyonos --version` returned `Miyonos 0.1.11`, user data was retained, and
  the device retains a 0.1.10 binary/version delta backup plus the earlier
  0.1.8 full rollback directory.
- Physical HTTP update: the same Miyoo was updated from 0.1.11 to 0.1.12 on
  2026-08-05 through OnionOS's local HTTP file sync. The only changed package
  files, `miyonos` and `VERSION`, were uploaded to temporary names, checksum-
  verified, copied to a retained 0.1.11 HTTP backup, and then renamed into
  place. The active binary matches SHA-256
  `6287715f1fb41f40c0c5327671c517f15792cf0991738f3432f1a7cf5190a012`,
  `VERSION` is `0.1.12`, the binary is executable, and user data is present.
  The 0.1.12 UI still needs a manual visual smoke test.
- Physical HTTP update: the same Miyoo was updated from 0.1.12 to 0.1.13 on
  2026-08-05 through OnionOS's local HTTP file sync. Before writing, the
  updater checksum-verified the target ZIP, confirmed the installed 0.1.12
  binary and every unchanged runtime file against its trusted package, and
  proved that only `miyonos` and `VERSION` differed. It staged, verified, and
  switched only those files. The active binary matches SHA-256
  `66c9cd46e426f7691143e0ef6cd67b9ba82072c310069e51bcd59dde23857657`,
  `VERSION` is `0.1.13`, the binary is executable, and user data is present
  and untouched. It retained `.miyonos-0.1.12.http-backup-20260805T055214Z`
  and `.VERSION-0.1.12.http-backup-20260805T055214Z`; visual confirmation of
  the 0.1.13 UI remains pending.
- Physical HTTP update: the same Miyoo was updated from 0.1.14 to 0.1.15 on
  2026-08-05 through OnionOS's local HTTP file sync. Before writing, the
  updater checksum-verified both release packages, matched every unchanged
  runtime file to the trusted 0.1.14 package, and proved that only `miyonos`
  and `VERSION` differed. It staged, verified, backed up, and switched only
  those files. The active binary matches SHA-256
  `fdf90a26b8029e5c2cfcf092fd609ad1f6748a099a9a191e0276fd5c1cd3401f`,
  `VERSION` is `0.1.15`, the binary is executable, and user data is present
  and untouched. It retained `.miyonos-0.1.14.http-backup-20260805T075653Z`
  and `.VERSION-0.1.14.http-backup-20260805T075653Z`; a visual Spotify-
  Favorite cover smoke test remains pending.
- Physical diagnostic: the most recent readable device report was version
  0.1.18 with ARM SHA-256
  `1545641c0dda29c10bbc4ab92064e03c09e49d12ad8a6f0c795fa99ecefc5fcf`.
  Its local log reported `TLS handshake failed (OpenSSL 1): ... certificate
  verify failed` for an enabled Spotify Favorite cover. This proved that the
  image request path, time, cache, and decoder were no longer the blocker.
  Version 0.1.19 required a complete browser-folder update because the
  certificate bundle and launcher changed. The owner subsequently confirmed
  that the Spotify Favorite covers display correctly on the physical Miyoo.
- Radio release boundary: version 0.1.21's stable-stream-identity
  reconciliation and strict three-proxy Sonos Radio/TuneIn logo route are
  covered by deterministic tests, live read-only Sonos metadata inspection,
  TLS validation with the packaged roots, and target packaging. A physical
  station-start and station-logo confirmation is still required before
  claiming hardware proof.
- Physical root cause: 0.1.4 generated repeated `MI_GFX_BitBlit` failures for
  every 640 × 480 frame. The upstream 320 × 240 RGB565 example was then run
  against the same bundled SDL library and device, and its red, green, and blue
  output was visually confirmed.
- Physical 0.1.5 diagnostic: both framebuffer pages were captured while the
  full app ran. They proved the scaled hardware output contained incomplete,
  stretched, and blurred interface fragments even without a reported error.
- Physical 0.1.6 diagnostic: the direct 640 × 480 ARGB display probe presented
  sharp complete color bands for eight seconds, exited with status 0, resumed
  MainUI, and was visually confirmed by the user. A concurrent raw framebuffer
  capture independently showed one complete sharp frame and the untouched
  alternate page, proving deterministic page selection without GFX scaling.
- Real Favorites read path: all household favorites were browsed without
  mutation. The result contained direct `x-sonosapi-stream` items and multiple
  `x-rincon-cpcontainer` collections with complete embedded metadata. Version
  0.1.10 retains separate fixture-verified playback paths for both types.
- Live Sonos read path: real discovery, descriptions, five topology members,
  two selectable groups, coordinator routing, transport state, live position,
  title, artist, album, duration, and source metadata passed.
- Live cover path: a relative Sonos `/getaa` URL was resolved, downloaded,
  bounded, cached, and independently identified as a valid 640 × 640 JPEG in
  both the initial probe and the final Sonos-enabled build. The macOS simulator
  rendered live covers at the correct orientation in the Now Playing frame.
- Live Sonos write path: an idempotent group-volume write sent the already
  active value, received HTTP 200 and a valid SOAP response, and read back the
  unchanged volume without audible playback mutation.
- Target: reproducible Docker cross-build produced a stripped ELF32 ARM EABI5
  hard-float binary with `$ORIGIN/libs` RPATH. The target image library exposes
  `IMG_LoadJPG_RW`, requests `libjpeg.so.9`, and the package contains an ARM
  library with that exact SONAME.
- Package audit: executable modes, JSON/shell/Python syntax, archive structure,
  version injection, dependency files, licenses, size, and absence of bundled
  user data passed.

## Not yet verified

Version 0.1.3 ran continuously on a physical Miyoo Mini Plus but remained
black. Version 0.1.4 proved that RGB565 alone was insufficient, and the 0.1.5
scale route made the app visible but blurred and incomplete. Version 0.1.6's
direct full-resolution probe is visibly sharp and its framebuffer capture is
complete. Until the user confirms the complete app after installation, the
following remain explicitly unclaimed:

- real `AddURIToQueue` collection-favorite playback on 0.1.8;
- the new 0.1.8 physical Left/Right track controls, custom mapping screen, and
  MENU + START recovery chord;
- visual rendering of the 0.1.21 Saved Playlists and Favorites cover views,
  replacement-queue playback, current-playlist Queue, Group/speaker focus,
  battery gauge, and media-level cover-art fallback on a physical Miyoo using
  real Sonos metadata, despite native and Python fixture coverage;
- complete physical button behavior and sleep-marker behavior, although MainUI
  resume after the automated diagnostic was verified and Menu twice is now a
  deterministic emergency exit;
- on-device JPEG decoding/display, although the real JPEG payload and complete
  ARM decoder dependency chain were independently verified;
- real-device actions that intentionally change playback, grouping, mute, or
  audible volume;
- a physical direct Sonos Radio start and its supported Sonos Radio/TuneIn
  station logo on 0.1.21; TV, line-in, AirPlay, unusual favorites, and
  providers that omit cover metadata;
- a physical Spotify-Favorite cover download with the explicitly enabled
  setting. The corrected request path, target ARM package, system clock, and
  complete trusted root bundle are verified, but the resulting cover has not
  yet been seen on the device;
- measured Miyoo RAM/CPU/battery use and the one-hour stability checklist.

The exact hardware matrix is in [TESTING.md](TESTING.md).

## Known limitations

- The local Sonos interface is unofficial and Miyonos polls rather than
  hosting UPnP event subscriptions.
- Cover art appears only when the current source exposes a reachable supported
  image in either current-track or active-media metadata; otherwise Miyonos
  deliberately shows **Cover unavailable** and fallback artwork.
- At most 240 entries from the active media container are held at once.
- Directly playable favorites and saved playlists are supported; global music
  service search/authentication and unusual provider-specific item types are
  intentionally unsupported with explicit feedback.
- Queue editing, alarms, sleep timers, EQ, and other version-1 out-of-scope
  controls are not included.

No known critical crash or silent required-feature placeholder remains in the
tested paths.

## Next priorities

1. Confirm collection and stream Favorites, Favorite cover preview, the new
   opt-in Spotify cover path, group volume targeting, and saved-playlist
   replacement on 0.1.21, then run the
   battery, key, MainUI, sleep/resume, Wi-Fi, and one-hour stability checklist.
2. Validate controlled playback/group changes and additional real media
   sources while preserving explicit user confirmation for disruptive actions.
3. Add optional UPnP event subscriptions and a sliding browse window to reduce
   polling and allow navigation beyond the current 240-item memory cap.
