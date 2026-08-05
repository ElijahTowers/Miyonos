#!/usr/bin/env python3
"""Checks for the no-install, cross-platform browser package."""

from __future__ import annotations

import re
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
SOURCE_HTML = (
    ROOT / "packaging" / "universal" / "Open Miyonos Installer.html"
)
PACKAGE = ROOT / "dist" / f"Miyonos-{VERSION}-Universal-Browser-Installer.zip"
PREFIX = "Miyonos-Universal-Browser-Installer/"


def check_html(html: str, *, packaged: bool) -> None:
    assert '<html lang="en">' in html
    assert "Install from any computer with a browser" in html
    assert "Windows" in html and "macOS" in html and "Linux" in html
    assert "HTTP: Web-based file sync" in html
    assert "Open Miyoo file server" in html
    assert "admin" in html
    assert "Miyonos" in html
    assert "default-src 'none'" in html
    assert "<script src=" not in html
    assert "<link rel=" not in html
    assert re.search(r"window\.open\(target, \"_blank\"", html)
    if packaged:
        assert "__MIYONOS_VERSION__" not in html
        assert f'const packagedVersion = "{VERSION}"' in html


def main() -> int:
    check_html(SOURCE_HTML.read_text(encoding="utf-8"), packaged=False)

    if not PACKAGE.is_file():
        print("Universal browser package check skipped until packaging.")
        return 0

    with zipfile.ZipFile(PACKAGE) as archive:
        bad_file = archive.testzip()
        assert bad_file is None
        names = set(archive.namelist())
        assert PREFIX + "Open Miyonos Installer.html" in names
        assert PREFIX + "README.txt" in names
        assert PREFIX + "Miyonos/VERSION" in names
        assert PREFIX + "Miyonos/config.json" in names
        icon = PREFIX + "Miyonos/icon.png"
        assert icon in names
        assert PREFIX + "Miyonos/launch.sh" in names
        assert PREFIX + "Miyonos/miyonos" in names
        trust_bundle = (
            PREFIX + "Miyonos/certificates/trusted-spotify-artwork-roots.pem"
        )
        assert trust_bundle in names
        assert PREFIX + "Miyonos/libs/libSDL2_image-2.0.so.0" in names
        assert PREFIX + "Miyonos/libs/libjpeg.so.9" in names
        assert PREFIX + "Miyonos/licenses/libjpeg-IJG.txt" in names
        assert not any(name.startswith(PREFIX + "Miyonos/data/") for name in names)
        html = archive.read(PREFIX + "Open Miyonos Installer.html").decode("utf-8")
        check_html(html, packaged=True)
        assert (
            archive.read(PREFIX + "Miyonos/VERSION")
            .decode("utf-8")
            .strip()
            == VERSION
        )
        # The bounded external-artwork allowlist needs the three original
        # service roots plus Google Trust Services Root R4 for the current
        # TuneIn logo CDN chain.
        assert archive.read(trust_bundle).count(b"-----BEGIN CERTIFICATE-----") == 4
        icon_data = archive.read(icon)
        assert icon_data[:8] == b"\x89PNG\r\n\x1a\n"
        assert int.from_bytes(icon_data[16:20], "big") == 74
        assert int.from_bytes(icon_data[20:24], "big") == 74

    print("Universal no-install browser package checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
