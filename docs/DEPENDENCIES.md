# Dependencies

Selections were rechecked on 2026-08-02. Target artifacts are pinned; no source
code was copied from the research projects.

| Component | Selected version | Purpose |
|---|---|---|
| OnionOS | release `v4.3.1-1`; source commit `7dfc008b851398dcfe57819519efe5f958c77f65` | App schema, paths, current software key mapping, and stable compatibility target |
| Miyoo SDL2 port | `steward-fu/sdl2` commit `0631abc8e8916db6f9bc7e2afd0c22913d092a29` | `Mini` video driver, SDL2, SDL2_image, and target graphics/runtime libraries |
| Miyoo toolchain | `mini_toolchain-v1.0.tar.gz` | GCC 8.2.1 ARM hard-float cross-compiler |
| IJG libjpeg | release `9d`; toolchain ABI `9.4.0` | JPEG decoding for Sonos cover artwork through SDL2_image |
| OpenSSL | toolchain `1.1.1l` | Statically linked TLS 1.2 and hostname verification for the opt-in Spotify-cover path only |
| Toolchain SHA-256 | `8addff71be4b015a4e1aef51e43635e50978d558a1675f5b1664124e8437d071` | Verified before extraction |
| Sonos API docs | tag `v1.4.1`, commit `ece20b10b007af68df6f10087f855b5d13773720` | Cross-check of local UPnP action/service behavior |
| Docker base | `ubuntu:22.04`, observed digest `sha256:0e0a0fc6d18feda9db1590da249ac93e8d5abfea8f4c3c0c849ce512b5ef8982` | Reproducible amd64 host for the Linux-only toolchain |

Primary upstream locations:

- <https://github.com/OnionUI/Onion>
- <https://onionui.github.io/docs/apps/package-manager>
- <https://github.com/steward-fu/sdl2>
- <https://github.com/svrooij/sonos-api-docs>
- <https://www.ijg.org/>
- toolchain asset:
  <https://github.com/steward-fu/website/releases/download/miyoo-mini/mini_toolchain-v1.0.tar.gz>

The current Onion package examples use `App/<name>/config.json` with `label`,
`icon`, `launch`, and `description`. The selected SDL port states Onion
4.3.1-1 support and uses `SDL_VIDEODRIVER=Mini`. Onion source maps A to Space,
B to Left Ctrl, X to Left Shift, Y to Left Alt, L1/R1 to E/T, L2/R2 to
Tab/Backspace, Select to Right Ctrl, Start to Return, and Menu to Escape.

The previously referenced community Spotify and Wthr examples were not treated
as authoritative inputs because their old links are not present in the current
official repository. Packaging and key assumptions came from current OnionOS
source/docs and the selected SDL port.

Target packages include SDL2's zlib-licensed binaries and license. The app
uses a code-native bitmap font and XML parser. Desktop development uses the
host SDL2 and the C++/Python standard libraries.
