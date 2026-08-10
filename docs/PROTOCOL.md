# Local Sonos protocol

Miyonos uses the speakers' unofficial local UPnP interface. It is not the
Sonos cloud Control API and is not guaranteed by Sonos to remain compatible.
All behavior is isolated in `src/sonos`.

## Discovery and descriptions

The worker sends M-SEARCH for
`urn:schemas-upnp-org:device:ZonePlayer:1` to `239.255.255.250:1900`, listens
for multiple bounded responses, and also attempts local UDP broadcast. Header
names are case-insensitive; LOCATION/USN/ST/cache lifetime are parsed and
deduplicated. Cached and manually entered IPv4 addresses are direct fallbacks.

Every candidate's device description is retrieved. Room/model/software/UDN
data and its advertised service control URLs are parsed relative to the
description base URL. Requests remain IPv4 plain HTTP on the LAN and never
resolve internet DNS.

## Topology and routing

`ZoneGroupTopology:GetZoneGroupState` supplies groups, coordinators, visible
members, and bonded/invisible membership. Satellites, surrounds, and Subs are
retained as topology information but are not offered as independent rooms
when invisible. Transport and ContentDirectory actions go to the active group
coordinator.

Group changes use `SetAVTransportURI` with the coordinator's `x-rincon` URI to
join and `BecomeCoordinatorOfStandaloneGroup` to leave. Topology is refreshed
afterward and the UI changes only from the confirmed response.

## Services and polling

- `AVTransport`: state, position/media info, play, pause, previous, next,
  relative-time and track-number seek, URI playback, join, and leave.
- `RenderingControl`: player volume and mute.
- `GroupRenderingControl`: group volume and mute when the service is present.
- `ContentDirectory`: Queue tracks (`Q:0`) and Favorites (`FV:2`), 60 items
  per page and at most 240 loaded per list. Real players can return technical
  queue-instance entries from `Q:`, so the Queue screen reads `Q:0` to show
  actual upcoming tracks. The Queue X view filters playlist-shaped Favorites,
  including Spotify playlists; browse metadata includes `albumArtURI` when
  Sonos provides one. Queue artwork is fetched lazily for only the visible
  rows plus a one-row buffer; at most one uncached cover is in flight, and the
  renderer retains no more than eight 64-pixel cover textures.
- HTTP GET: bounded artwork retrieval, including Sonos-relative `/getaa` URLs.
  JPEG and PNG covers are cached locally and decoded by the bundled target
  image runtime. **External cover art over HTTPS** is an optional, default-off
  exception for the strict public Spotify, Spotify-in-Sonos, and Sonos
  Radio/TuneIn cover endpoint forms exposed by Sonos Favorites. It uses a fixed
  host/path allowlist, TLS 1.2+, a bundled offline trust bundle, hostname
  verification, and no credentials, cookies, or telemetry. Spotify redirects
  remain forbidden. A Sonos Radio proxy may make exactly one verified redirect
  to either fixed TuneIn logo CDN; all other redirects are rejected.

Current transport metadata is decoded from escaped DIDL-Lite. Missing and
unknown fields remain empty; no raw XML is shown. The active playlist container
identifier is retained separately from current-track metadata. Position polls
are roughly 1–3 seconds while playing depending on settings and slower while
paused.

Starting a playlist-shaped Favorite is intentionally a replacement operation:
Miyonos calls `RemoveAllTracksFromQueue`, adds the selected Favorite URI,
opens the coordinator queue, seeks to the first returned position, and starts
playback. This prevents songs from an earlier playlist remaining in the active
queue.
Sonos can report UPnP 804 for an already-empty queue; Miyonos treats that as
the required clean state and continues loading the selected playlist.

Many players then report only a generic `x-rincon-queue` transport URI for
each following track, with no source playlist label. Miyonos keeps the title
of the playlist it started for that active group queue. A routine refresh of
the same group preserves this context; selecting another group or starting a
different source clears it. When Miyonos starts a playlist, it also retains
that Favorite's artwork URL for the dedicated Now Playing playlist block;
this remains separate from the changing current-track artwork.

## Defensive limits

HTTP has bounded connect/read waits, 16 KiB headers, per-request bodies,
Content-Length and chunked decoding, cancellation, and IP/port validation.
XML parsing limits input, node count, and nesting. SOAP arguments are escaped,
faults are decoded, numeric values are range checked, and response strings are
bounded. Artwork is limited to 8 MB compressed, 2048 per dimension, and four
million decoded pixels.

Known limitations include firmware/model differences, media-service-specific
favorite URIs, no global search, no event-subscription server (polling is used),
and no guarantee that a future Sonos firmware will retain these local actions.
