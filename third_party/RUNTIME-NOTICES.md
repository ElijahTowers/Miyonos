# Target runtime notices

The OnionOS archive contains unmodified, stripped ARM shared libraries needed
by the selected Miyoo SDL port:

- SDL2 and SDL2_image: zlib license;
- Steward Fu's Miyoo SDL port integration: repository license (LGPL-2.1);
- SwiftShader `libEGL`/`libGLESv2`: Apache-2.0;
- json-c: MIT;
- libpng: libpng license;
- libjpeg: Independent JPEG Group license;
- zlib: zlib license;
- GCC `libstdc++` and `libgcc_s`: GPL-3.0-or-later with the GCC Runtime Library
  Exception 3.1.
- OpenSSL 1.1.1l: statically linked only for the optional verified Spotify-cover
  connection; the dual OpenSSL/SSLeay license is included in
  `licenses/OpenSSL-1.1.1l.txt`.

These components are not authored by Miyonos. The exact binaries come from
the pinned repositories/toolchain named in `docs/DEPENDENCIES.md`. Their
corresponding sources and license material are available from:

- <https://github.com/steward-fu/sdl2>
- <https://github.com/google/swiftshader>
- <https://github.com/json-c/json-c>
- <https://github.com/pnggroup/libpng>
- <https://www.ijg.org/>
- <https://github.com/madler/zlib>
- <https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html>
- <https://www.openssl.org/>

The release includes the complete license files provided alongside the SDL
port, SDL2, SDL2_image, and SwiftShader inputs. This notice does not replace
their terms.

This software is based in part on the work of the Independent JPEG Group.
This product includes software developed by the OpenSSL Project for use in the
OpenSSL Toolkit (http://www.openssl.org/). This product includes cryptographic
software written by Eric Young (eay@cryptsoft.com).
