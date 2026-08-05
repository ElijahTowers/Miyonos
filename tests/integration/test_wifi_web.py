#!/usr/bin/env python3
"""Integration checks for the local Miyonos browser installer."""

from __future__ import annotations

import importlib.util
import json
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from types import ModuleType
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()


def load_server_module() -> ModuleType:
    source = ROOT / "scripts" / "wifi-web.py"
    spec = importlib.util.spec_from_file_location("miyonos_wifi_web", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("Could not load Wi-Fi web server")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def request_json(
    url: str,
    token: str,
    *,
    method: str = "GET",
    payload: dict[str, Any] | None = None,
    origin: str | None = None,
) -> tuple[int, dict[str, Any]]:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers={
            "Content-Type": "application/json",
            "Origin": origin or url.rsplit("/", 2)[0],
            "X-Miyonos-Token": token,
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def wait_for_finish(origin: str, token: str) -> dict[str, Any]:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        status_code, payload = request_json(
            f"{origin}/api/status",
            token,
            origin=origin,
        )
        assert status_code == 200
        if payload["state"] != "running":
            return payload
        time.sleep(0.05)
    raise AssertionError("Web installer job did not finish")


def main() -> int:
    module = load_server_module()
    server = module.create_server(
        installer=ROOT / "tests" / "integration" / "fake_web_updater.sh",
        web_root=ROOT / "web" / "wifi-installer",
        icon=ROOT / "packaging" / "onion" / "App" / "Miyonos" / "icon.png",
        version=VERSION,
    )
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    origin = server.origin
    token = server.state.token
    try:
        with urllib.request.urlopen(origin, timeout=3) as response:
            html = response.read().decode("utf-8")
            assert response.status == 200
            assert "Install Miyonos on your Miyoo over Wi-Fi" in html
            assert "Install or update" in html
            assert '<html lang="en">' in html
            assert token in html
            assert f"v{VERSION}" in html
            assert response.headers["X-Frame-Options"] == "DENY"

        denied, _ = request_json(
            f"{origin}/api/run",
            token,
            method="POST",
            origin="https://example.invalid",
            payload={
                "action": "install",
                "host": "192.168.1.50",
                "user": "onion",
                "password": "onion",
                "keep_backup": True,
            },
        )
        assert denied == 403

        invalid, payload = request_json(
            f"{origin}/api/run",
            token,
            method="POST",
            origin=origin,
            payload={
                "action": "install",
                "host": "not-an-ip",
                "user": "onion",
                "password": "onion",
                "keep_backup": True,
            },
        )
        assert invalid == 400
        assert "IPv4" in payload["error"]

        accepted, payload = request_json(
            f"{origin}/api/run",
            token,
            method="POST",
            origin=origin,
            payload={
                "action": "install",
                "host": "192.168.1.50",
                "user": "onion",
                "password": "onion",
                "keep_backup": True,
            },
        )
        assert accepted == 202
        assert payload["state"] == "running"
        finished = wait_for_finish(origin, token)
        assert finished["state"] == "success"
        assert "installed on your Miyoo" in finished["message"]
        assert "onion\n" not in "\n".join(finished["logs"])

        accepted, _ = request_json(
            f"{origin}/api/run",
            token,
            method="POST",
            origin=origin,
            payload={
                "action": "rollback",
                "host": "192.168.1.50",
                "user": "onion",
                "password": "onion",
                "keep_backup": True,
            },
        )
        assert accepted == 202
        finished = wait_for_finish(origin, token)
        assert finished["state"] == "success"
        assert "restored" in finished["message"]
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)

    print("Local browser installer security, install, and rollback passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
