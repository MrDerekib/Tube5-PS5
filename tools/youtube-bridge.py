#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private single-job YouTube preparation bridge; never a public proxy."""
import importlib.util
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("tube5_prepare", ROOT / "tools/prepare-youtube-test.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def request_profile(value):
    if not isinstance(value, dict) or set(value) - {"url", "height", "seconds"}:
        raise ValueError("Expected url, optional height and seconds")
    url = value.get("url")
    if not isinstance(url, str) or len(url) > 512:
        raise ValueError("Expected a YouTube URL")
    identifier = prepare.youtube_id(url)
    height, seconds = value.get("height", 360), value.get("seconds", 60)
    if type(height) is not int or height not in (360, 720, 1080):
        raise ValueError("Height must be 360, 720 or 1080")
    if type(seconds) is not int or seconds not in (60, 300, 600):
        raise ValueError("HD length must be 60, 300 or 600 seconds")
    return identifier, height, 60 if height == 360 else seconds


class Bridge:
    def __init__(self, entries, cache=None, state=None, runner=None):
        self.cache = (cache or ROOT / ".local/youtube").resolve()
        self.state = state or ROOT / ".local/bridge"
        self.state.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.token = secrets.token_hex(32)
        token_file = self.state / "control-token"
        descriptor = os.open(token_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w") as stream:
            stream.write(self.token)
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.entries = [dict(entry) for entry in entries]
        self.profiles = []
        self.job = {"state": "idle"}
        self.worker = None
        self.runner = runner or self.prepare_source
        manifest = self.state / "library.json"
        if manifest.is_file():
            if manifest.stat().st_size > 8192:
                raise ValueError("Private library manifest exceeds limit")
            profiles = json.loads(manifest.read_text())
            if not isinstance(profiles, list) or len(profiles) > 6:
                raise ValueError("Invalid private library manifest")
            for profile in profiles:
                self.publish(*request_profile(profile), persist=False)

    def snapshot(self):
        with self.lock:
            return [dict(entry) for entry in self.entries]

    def status(self):
        with self.lock:
            return dict(self.job)

    def path(self, identifier, height, seconds):
        name = "media.mp4" if height == 360 else f"resolution-{height}p-{seconds}s.mp4"
        candidate = self.cache / identifier / name
        # Do not serve symlinks escaping the private cache, even for reused files.
        if not candidate.resolve().is_relative_to(self.cache):
            raise ValueError("Prepared media outside cache")
        return candidate

    def publish(self, identifier, height, seconds, persist=True):
        path = self.path(identifier, height, seconds)
        limit = (256 if height == 360 else 64 * (seconds // 60)) * 1024 * 1024
        if not path.is_file() or not 0 < path.stat().st_size <= limit:
            raise ValueError("Prepared media missing or oversized")
        sidecar = path.parent / "metadata.json" if height == 360 else path.with_suffix(".json")
        if not sidecar.resolve().is_relative_to(self.cache) or sidecar.stat().st_size > 16384:
            raise ValueError("Invalid metadata file")
        metadata = json.loads(sidecar.read_text())
        if not isinstance(metadata, dict) or metadata.get("id") != identifier:
            raise ValueError("Media ID mismatch")
        probe = json.loads(subprocess.check_output(
            ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", str(path)],
            text=True, timeout=30))
        duration = prepare.validate_media(probe, height, seconds)
        video = next(stream for stream in probe["streams"] if stream["codec_type"] == "video")
        import unicodedata
        title = unicodedata.normalize("NFKD", str(metadata.get("title", identifier))).encode("ascii", "ignore").decode()
        title = "".join(c if 32 <= ord(c) <= 126 else " " for c in title)[:80] or identifier
        entry_id = f"yt-{identifier}-{height}-{seconds}"
        entry = dict(id=entry_id, title=title, youtube=identifier, seconds=int(duration),
                     height=video["height"] if video["height"] in (360, 720, 1080) else 0, path=path)
        # Manifest contains only canonical URLs/profiles, not arbitrary file paths.
        profile = dict(url=f"https://www.youtube.com/watch?v={identifier}", height=height, seconds=seconds)
        with self.lock:
            if any(item["id"] == entry_id for item in self.entries):
                return entry_id
            if len(self.entries) >= 8:
                raise ValueError("Library full (eight entries)")
            profiles = self.profiles + [profile]
            if persist:
                temporary = self.state / "library.json.tmp"
                temporary.write_text(json.dumps(profiles) + "\n")
                temporary.replace(self.state / "library.json")
            self.profiles = profiles
            self.entries.append(entry)
        return entry_id

    def submit(self, value):
        identifier, height, seconds = request_profile(value)
        with self.lock:
            if self.job["state"] == "preparing":
                raise RuntimeError("Another video is being prepared")
            entry_id = f"yt-{identifier}-{height}-{seconds}"
            if any(entry["id"] == entry_id for entry in self.entries):
                self.job = dict(state="ready", id=entry_id, reused=True)
                return dict(self.job)
            if len(self.entries) >= 8:
                raise RuntimeError("Library full (eight entries)")
            self.job = dict(state="preparing", id=entry_id)
            self.worker = threading.Thread(target=self.run, args=(identifier, height, seconds), daemon=True)
            self.worker.start()
            return dict(self.job)

    def run(self, identifier, height, seconds):
        try:
            reused = self.path(identifier, height, seconds).exists()
            if not reused:
                self.runner(identifier, height, seconds)
            if self.stop.is_set():
                raise RuntimeError("Bridge stopped")
            entry_id = self.publish(identifier, height, seconds)
            result = dict(state="ready", id=entry_id, reused=reused)
        except Exception:
            # Do not expose yt-dlp stderr, signed CDN URLs or local paths.
            result = dict(state="failed", error="Preparation or validation failed; nothing published")
        with self.lock:
            self.job = result

    def prepare_source(self, identifier, height, seconds):
        command = [sys.executable, str(ROOT / "tools/prepare-youtube-test.py"),
                   f"https://www.youtube.com/watch?v={identifier}", "--height", str(height),
                   "--seconds", str(seconds)]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   start_new_session=True)
        deadline = time.monotonic() + 2500
        try:
            while process.poll() is None:
                if self.stop.wait(0.2) or time.monotonic() >= deadline:
                    raise RuntimeError("Preparation interrupted")
            if process.returncode:
                raise RuntimeError("Preparation failed")
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()

    def close(self):
        self.stop.set()
        if self.worker:
            self.worker.join(timeout=35)


def control_handler(bridge):
    class Control(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(3)

        def log_message(self, *args):
            pass

        def reply(self, code, value):
            body = json.dumps(value).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def authorized(self):
            self.connection.settimeout(3)
            host = f"127.0.0.1:{self.server.server_port}"
            token = self.headers.get("Authorization", "")
            if (self.headers.get("Host") != host or self.headers.get("Origin") is not None or
                    not secrets.compare_digest(token.encode("utf-8"), f"Bearer {bridge.token}".encode())):
                self.reply(403, {"error": "Local authenticated client required"})
                return False
            return True

        def do_GET(self):
            if self.authorized():
                self.reply(200, bridge.status()) if self.path == "/status" else self.reply(404, {})

        def do_POST(self):
            if not self.authorized():
                return
            if self.path != "/jobs":
                self.reply(404, {})
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 1024 or self.headers.get("Transfer-Encoding"):
                    raise ValueError("Request exceeds limit or has invalid framing")
                self.reply(202, bridge.submit(json.loads(self.rfile.read(size))))
            except (ValueError, TypeError, UnicodeError) as error:
                self.reply(400, {"error": str(error)})
            except RuntimeError as error:
                self.reply(409, {"error": str(error)})
    return Control
