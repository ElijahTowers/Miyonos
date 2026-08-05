#!/usr/bin/env python3
"""Local-only browser interface for the Miyonos Wi-Fi updater."""

from __future__ import annotations

import argparse
import hmac
import json
import os
import queue
import re
import secrets
import signal
import subprocess
import threading
import time
import webbrowser
from dataclasses import dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit


IPV4_RE = re.compile(
    r"^(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}"
    r"(?:25[0-5]|2[0-4]\d|1?\d?\d)$"
)
USER_RE = re.compile(r"^[A-Za-z0-9._-]{1,32}$")
MAX_BODY = 16 * 1024
MAX_LOG_LINES = 80
JOB_TIMEOUT_SECONDS = 10 * 60


@dataclass
class Job:
    action: str = ""
    state: str = "idle"
    phase: str = "ready"
    message: str = "Ready to connect."
    logs: list[str] = field(default_factory=list)
    started_at: float | None = None

    def public(self) -> dict[str, Any]:
        return {
            "action": self.action,
            "state": self.state,
            "phase": self.phase,
            "message": self.message,
            "logs": self.logs[-MAX_LOG_LINES:],
            "started_at": self.started_at,
        }


class InstallerState:
    def __init__(self, installer: Path, version: str) -> None:
        self.installer = installer
        self.version = version
        self.token = secrets.token_urlsafe(32)
        self.lock = threading.Lock()
        self.job = Job()

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return self.job.public()

    def start(
        self,
        action: str,
        host: str,
        user: str,
        password: str,
        keep_backup: bool,
    ) -> bool:
        with self.lock:
            if self.job.state == "running":
                return False
            self.job = Job(
                action=action,
                state="running",
                phase="connecting",
                message="Connecting to your Miyoo…",
                started_at=time.time(),
            )

        thread = threading.Thread(
            target=self._run,
            args=(action, host, user, password, keep_backup),
            daemon=True,
        )
        thread.start()
        return True

    def _update(
        self,
        *,
        state: str | None = None,
        phase: str | None = None,
        message: str | None = None,
        log: str | None = None,
    ) -> None:
        with self.lock:
            if state is not None:
                self.job.state = state
            if phase is not None:
                self.job.phase = phase
            if message is not None:
                self.job.message = message
            if log:
                self.job.logs.append(log)
                self.job.logs = self.job.logs[-MAX_LOG_LINES:]

    @staticmethod
    def _friendly_error(output: str) -> str:
        lowered = output.lower()
        if "permission denied" in lowered:
            return "Login failed. Check the Onion password."
        if "connection refused" in lowered:
            return "SSH is probably not enabled on the Miyoo."
        if any(
            phrase in lowered
            for phrase in ("timed out", "no route to host", "could not resolve")
        ):
            return "The Miyoo cannot be reached. Check Wi-Fi and the IP address."
        if "checksum does not match" in lowered:
            return "The update package is damaged and was not installed."
        if "no previous miyonos version" in lowered:
            return "There is no previous version to restore."
        return "The operation was not completed. Open the details for more information."

    def _run(
        self,
        action: str,
        host: str,
        user: str,
        password: str,
        keep_backup: bool,
    ) -> None:
        command = [str(self.installer)]
        if action == "rollback":
            command.append("--rollback")
        command.append(host)
        environment = os.environ.copy()
        environment["MIYOO_USER"] = user
        environment["MIYOO_KEEP_BACKUP"] = "1" if keep_backup else "0"
        if password:
            environment["MIYOO_PASSWORD"] = password
        else:
            environment.pop("MIYOO_PASSWORD", None)

        try:
            process = subprocess.Popen(
                command,
                cwd=self.installer.parent,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                start_new_session=True,
            )
        except OSError as exc:
            self._update(
                state="error",
                phase="error",
                message=f"The local updater could not start: {exc}",
            )
            return

        lines: queue.Queue[str | None] = queue.Queue()

        def read_output() -> None:
            assert process.stdout is not None
            for raw_line in process.stdout:
                lines.put(raw_line.rstrip())
            lines.put(None)

        reader = threading.Thread(target=read_output, daemon=True)
        reader.start()
        deadline = time.monotonic() + JOB_TIMEOUT_SECONDS
        complete_output: list[str] = []
        stream_done = False

        while process.poll() is None or not stream_done:
            if time.monotonic() >= deadline and process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                self._update(
                    state="error",
                    phase="error",
                    message="The update took too long and was stopped safely.",
                )
                return
            try:
                line = lines.get(timeout=0.2)
            except queue.Empty:
                continue
            if line is None:
                stream_done = True
                continue
            safe_line = line.replace(password, "••••") if password else line
            complete_output.append(safe_line)
            self._update(log=safe_line)
            lowered = safe_line.lower()
            if "uploading miyonos" in lowered:
                self._update(
                    phase="uploading",
                    message="Sending the new version to the Miyoo…",
                )
            elif "installed at" in lowered:
                self._update(
                    phase="finishing",
                    message="Finishing installation and checking user data…",
                )

        return_code = process.wait()
        output = "\n".join(complete_output)
        if return_code == 0:
            if action == "rollback":
                message = "The previous version was restored. Your data was preserved."
            else:
                message = (
                    f"Miyonos {self.version} is installed on your Miyoo. "
                    "Your settings and artwork were preserved."
                )
            self._update(
                state="success",
                phase="done",
                message=message,
            )
        else:
            self._update(
                state="error",
                phase="error",
                message=self._friendly_error(output),
            )


class MiyonosHTTPServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        address: tuple[str, int],
        handler: type[BaseHTTPRequestHandler],
        *,
        state: InstallerState,
        web_root: Path,
        icon: Path,
    ) -> None:
        super().__init__(address, handler)
        self.state = state
        self.web_root = web_root
        self.icon = icon
        self.origin = f"http://127.0.0.1:{self.server_port}"
        self.last_activity = time.monotonic()


class Handler(BaseHTTPRequestHandler):
    server: MiyonosHTTPServer

    def log_message(self, _format: str, *_args: Any) -> None:
        return

    def _headers(
        self,
        status: HTTPStatus,
        content_type: str,
        length: int,
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; script-src 'self'; style-src 'self'; "
            "img-src 'self'; connect-src 'self'; form-action 'none'; "
            "frame-ancestors 'none'",
        )
        self.end_headers()

    def _send_bytes(
        self,
        body: bytes,
        content_type: str,
        status: HTTPStatus = HTTPStatus.OK,
    ) -> None:
        self._headers(status, content_type, len(body))
        self.wfile.write(body)

    def _json(
        self,
        payload: dict[str, Any],
        status: HTTPStatus = HTTPStatus.OK,
    ) -> None:
        self._send_bytes(
            json.dumps(payload, ensure_ascii=False).encode("utf-8"),
            "application/json; charset=utf-8",
            status,
        )

    def _trusted_host(self) -> bool:
        host = self.headers.get("Host", "")
        return host in {
            f"127.0.0.1:{self.server.server_port}",
            f"localhost:{self.server.server_port}",
        }

    def _authorized_api(self, *, require_origin: bool = True) -> bool:
        token = self.headers.get("X-Miyonos-Token", "")
        origin = self.headers.get("Origin", "")
        return (
            self._trusted_host()
            and (not require_origin or origin == self.server.origin)
            and hmac.compare_digest(token, self.server.state.token)
        )

    def do_GET(self) -> None:
        self.server.last_activity = time.monotonic()
        if not self._trusted_host():
            self.send_error(HTTPStatus.BAD_REQUEST)
            return
        path = urlsplit(self.path).path
        if path == "/api/status":
            if not self._authorized_api(require_origin=False):
                self._json({"error": "Not authorized."}, HTTPStatus.FORBIDDEN)
                return
            self._json(self.server.state.snapshot())
            return

        assets = {
            "/styles.css": (self.server.web_root / "styles.css", "text/css"),
            "/app.js": (
                self.server.web_root / "app.js",
                "text/javascript; charset=utf-8",
            ),
            "/icon.png": (self.server.icon, "image/png"),
        }
        if path == "/":
            template = (self.server.web_root / "index.html").read_text(
                encoding="utf-8"
            )
            body = (
                template.replace("__MIYONOS_TOKEN__", self.server.state.token)
                .replace("__MIYONOS_VERSION__", self.server.state.version)
                .encode("utf-8")
            )
            self._send_bytes(body, "text/html; charset=utf-8")
            return
        if path in assets:
            file_path, content_type = assets[path]
            self._send_bytes(file_path.read_bytes(), content_type)
            return
        self.send_error(HTTPStatus.NOT_FOUND)

    def do_POST(self) -> None:
        self.server.last_activity = time.monotonic()
        if not self._authorized_api():
            self._json({"error": "Not authorized."}, HTTPStatus.FORBIDDEN)
            return
        path = urlsplit(self.path).path
        if path == "/api/shutdown":
            self._json({"ok": True})
            threading.Thread(
                target=self._shutdown_later,
                daemon=True,
            ).start()
            return
        if path != "/api/run":
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0 or length > MAX_BODY:
            self._json({"error": "Invalid request."}, HTTPStatus.BAD_REQUEST)
            return
        try:
            payload = json.loads(self.rfile.read(length))
        except (json.JSONDecodeError, UnicodeDecodeError):
            self._json({"error": "Invalid request."}, HTTPStatus.BAD_REQUEST)
            return

        action = str(payload.get("action", ""))
        host = str(payload.get("host", "")).strip()
        user = str(payload.get("user", "onion")).strip()
        password = str(payload.get("password", ""))
        keep_backup = bool(payload.get("keep_backup", True))
        if action not in {"install", "rollback"}:
            self._json({"error": "Unknown operation."}, HTTPStatus.BAD_REQUEST)
            return
        if not IPV4_RE.fullmatch(host):
            self._json(
                {"error": "Enter a valid IPv4 address, for example 192.168.1.50."},
                HTTPStatus.BAD_REQUEST,
            )
            return
        if not USER_RE.fullmatch(user) or len(password) > 256:
            self._json({"error": "Invalid login details."}, HTTPStatus.BAD_REQUEST)
            return
        if not self.server.state.start(
            action, host, user, password, keep_backup
        ):
            self._json(
                {"error": "An operation is already running."},
                HTTPStatus.CONFLICT,
            )
            return
        self._json(self.server.state.snapshot(), HTTPStatus.ACCEPTED)

    def _shutdown_later(self) -> None:
        time.sleep(0.2)
        self.server.shutdown()


def parse_version(installer: Path) -> str:
    project_version = installer.parent.parent / "VERSION"
    if project_version.is_file():
        return project_version.read_text(encoding="utf-8").strip()
    releases = list(installer.parent.glob("Miyonos-*-OnionOS.zip"))
    if len(releases) == 1:
        match = re.fullmatch(r"Miyonos-(.+)-OnionOS\.zip", releases[0].name)
        if match:
            return match.group(1)
    return "release"


def create_server(
    *,
    installer: Path,
    web_root: Path,
    icon: Path,
    port: int = 0,
    version: str | None = None,
) -> MiyonosHTTPServer:
    for path in (
        installer,
        web_root / "index.html",
        web_root / "styles.css",
        web_root / "app.js",
        icon,
    ):
        if not path.is_file():
            raise FileNotFoundError(path)
    state = InstallerState(installer.resolve(), version or parse_version(installer))
    return MiyonosHTTPServer(
        ("127.0.0.1", port),
        Handler,
        state=state,
        web_root=web_root.resolve(),
        icon=icon.resolve(),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--installer", required=True, type=Path)
    parser.add_argument("--web-root", required=True, type=Path)
    parser.add_argument("--icon", required=True, type=Path)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--open", action="store_true", dest="open_browser")
    parser.add_argument("--idle-timeout", type=int, default=30 * 60)
    args = parser.parse_args()

    try:
        server = create_server(
            installer=args.installer,
            web_root=args.web_root,
            icon=args.icon,
            port=args.port,
        )
    except FileNotFoundError as exc:
        parser.error(f"required file is missing: {exc}")

    print(f"Miyonos Wi-Fi Installer: {server.origin}", flush=True)
    if args.open_browser:
        threading.Timer(0.25, webbrowser.open, args=(server.origin,)).start()

    def stop_when_idle() -> None:
        while True:
            time.sleep(min(30, max(1, args.idle_timeout)))
            if (
                server.state.snapshot()["state"] != "running"
                and time.monotonic() - server.last_activity >= args.idle_timeout
            ):
                server.shutdown()
                return

    threading.Thread(target=stop_when_idle, daemon=True).start()
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
