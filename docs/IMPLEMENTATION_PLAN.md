# Implementation plan

The 0.1.0 technical preview was delivered in eight continuously buildable
phases:

1. Establish C++17/SDL2 builds, input abstraction, settings, logging, renderer,
   test harness, and mock speaker.
2. Add bounded SSDP, HTTP, XML, device descriptions, cached/manual fallbacks,
   and discovery UI.
3. Parse Sonos topology into logical players and coordinator-led groups while
   filtering invisible bonded components.
4. Add typed transport/rendering actions, polling, optimistic volume, current
   media, progress, and artwork.
5. Add bounded paging for Queue, Favorites, and saved-playlist containers,
   including unsupported-item feedback.
6. Add join/leave group actions, topology reconciliation, confirmations, and
   error recovery.
7. Pin and verify target dependencies, cross-compile ARM, stage OnionOS,
   generate the icon, and produce a checksummed release.
8. Exercise protocol, storage, state, timeout, paging, and UI paths; audit
   package contents; document real-device checks that remain.

The release gate is: passing desktop tests, a rendered 640 × 480 preview, a
stripped ARM hard-float executable, a self-contained ZIP under 20 MB, bounded
resources, no required placeholder paths, and no false physical-device claim.
