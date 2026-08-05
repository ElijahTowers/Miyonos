# Live Sonos validation

Miyonos 0.1.3 was exercised on 2026-08-02 from a Mac connected to the same
trusted IPv4 LAN as a mixed-model Sonos household. This validation used the
same controller, Sonos adapter, HTTP client, XML parser, storage, and renderer
logic as the OnionOS build. No cloud account or Sonos cloud API was used.

## Household coverage

The discovered topology contained a Sonos Beam group with Play:1 surrounds
and a Sub Mini, plus a standalone Sonos Roam. Bonded satellites remained part
of topology but were correctly hidden as independent selectable rooms. The
active transport was routed through the group coordinator.

## Verified behavior

- SSDP discovery and direct device-description hydration completed for real
  speakers.
- `ZoneGroupTopology:GetZoneGroupState` produced two selectable groups from
  five topology members.
- `AVTransport:GetTransportInfo`, `GetPositionInfo`, and `GetMediaInfo`
  returned the live playback state, duration, position, title, artist, album,
  and source metadata.
- A Spotify item exposed a relative Sonos `/getaa` URI. Miyonos resolved it
  against the coordinator, downloaded it through the bounded HTTP client, and
  stored a valid 640 × 640 JPEG in the artwork cache.
- The final macOS simulator decoder rendered real 640 × 640 Sonos JPEG covers
  in the Now Playing frame with the correct orientation.
- `GroupRenderingControl:GetGroupVolume` returned the live group volume.
- An idempotent `SetGroupVolume` request wrote the same value already present,
  received HTTP 200 with a valid response, and read back the unchanged value.
  This proved the real write path without changing audible playback.

No play, pause, seek, track, mute, grouping, or audible volume mutation was
performed during this safe validation. Speaker addresses and identifiers are
intentionally excluded from this document.

## Remaining boundary

The local Sonos adapter and actual cover payload were validated in 0.1.3.
Version 0.1.13 retains the visually verified direct 640 × 480 double-buffered
framebuffer route. A subsequent read-only Favorites browse against the same
household identified both direct stream favorites and collection favorites
with `x-rincon-cpcontainer` URIs and embedded Sonos metadata. Fixture coverage
now verifies the separate direct-stream and `AddURIToQueue` collection paths;
the collection write awaits explicit physical confirmation. The bundled target
JPEG decoder, complete physical buttons, sleep/resume, and long-running device
resource use still require combined physical-device verification.
