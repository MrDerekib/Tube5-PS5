#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded metadata provider and opt-in console bridge, separate from the UI."""
import json
import math
import os
from pathlib import Path
import re
import secrets
import signal
import subprocess
import tempfile
import threading
import time
import unicodedata
from urllib.parse import parse_qs, urlsplit


def text(value, limit):
    value = unicodedata.normalize("NFKD", str(value)).encode("ascii", "ignore").decode()
    return "".join(c if 32 <= ord(c) < 127 else " " for c in value)[:limit]


def query_text(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 48:
        raise ValueError("Query must contain 1-48 characters")
    if not all(c.isascii() and (c.isalnum() or c in " .-_") for c in value) or not value.strip():
        raise ValueError("Unsupported query")
    return value.strip()


def results(document):
    if not isinstance(document, dict) or not isinstance(document.get("entries"), list):
        raise ValueError("Invalid search response")
    output, seen = [], set()
    for entry in document["entries"][:12]:
        if not isinstance(entry, dict):
            continue
        identifier, duration = entry.get("id"), entry.get("duration")
        if (not isinstance(identifier, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", identifier)
                or identifier in seen or type(duration) not in (int, float)
                or not math.isfinite(duration) or not 20 <= duration <= 3600
                or entry.get("is_live") or entry.get("live_status") not in (None, "not_live", "was_live")):
            continue
        seen.add(identifier)
        output.append(dict(id=identifier, title=text(entry.get("title") or identifier, 80).strip() or identifier,
                           channel=text(entry.get("channel") or entry.get("uploader") or "CANAL DESCONOCIDO", 48).strip() or "CANAL DESCONOCIDO",
                           seconds=int(duration)))
        if len(output) == 8:
            break
    return output


class SearchProvider:
    def __init__(self, runner=None):
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.worker = None
        self.runner = runner or self.fetch
        self.query = ""
        self.updated = 0.0
        self.value = dict(state="idle", entries=[])

    def request(self, query):
        query = query_text(query)
        with self.lock:
            if self.value["state"] == "searching":
                if query != self.query:
                    raise RuntimeError("Another search is running")
            elif query != self.query or time.monotonic() - self.updated > (5 if self.value["state"] == "failed" else 300):
                self.query = query
                self.value = dict(state="searching", entries=[])
                self.worker = threading.Thread(target=self.run, args=(query,), daemon=True)
                self.worker.start()
            return dict(self.value)

    def run(self, query):
        try:
            value = dict(state="ready", entries=results(self.runner(query)))
        except Exception:
            value = dict(state="failed", entries=[])
        with self.lock:
            self.value = value
            self.updated = time.monotonic()

    def fetch(self, query):
        # Flat metadata only: no media downloads, cookies, accounts or shell.
        command = ["yt-dlp", "--ignore-config", "--no-cache-dir", "--flat-playlist",
                   "--skip-download", "--dump-single-json", "--socket-timeout", "10",
                   "--retries", "0", "--playlist-end", "12", "--", f"ytsearch12:{query}"]
        with tempfile.TemporaryFile() as output:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.DEVNULL,
                                       start_new_session=True)
            deadline = time.monotonic() + 45
            try:
                while process.poll() is None:
                    if (self.stop.wait(.1) or time.monotonic() > deadline
                            or os.fstat(output.fileno()).st_size > 128 * 1024):
                        raise RuntimeError("Search interrupted or oversized")
                if process.returncode or os.fstat(output.fileno()).st_size > 128 * 1024:
                    raise RuntimeError("Search failed")
                output.seek(0)
                return json.loads(output.read(128 * 1024))
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
            self.worker.join(timeout=50)


class ConsoleBridge:
    def __init__(self, bridge, address, provider=None):
        self.bridge, self.address = bridge, address
        self.provider = provider or SearchProvider()
        path = Path(bridge.state) / "console-token"
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        except FileExistsError:
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            with os.fdopen(fd) as stream:
                self.token = stream.read(65)
            if not re.fullmatch(r"[0-9a-f]{64}", self.token):
                raise ValueError("Invalid console token")
        else:
            self.token = secrets.token_hex(32)
            with os.fdopen(fd, "w") as stream:
                stream.write(self.token)
        os.chmod(path, 0o600)

    def reply(self, handler, status, body):
        encoded = body.encode("ascii")
        handler.send_response(status)
        handler.send_header("Content-Type", "text/tab-separated-values; charset=us-ascii")
        handler.send_header("Content-Length", str(len(encoded)))
        handler.send_header("Cache-Control", "no-store")
        handler.end_headers()
        handler.wfile.write(encoded)

    def handle(self, handler, method):
        token = handler.headers.get("Authorization", "")
        host = f"{handler.server.server_address[0]}:{handler.server.server_port}"
        if (handler.client_address[0] != self.address or handler.headers.get("Host") != host
                or handler.headers.get("Origin") is not None
                or not secrets.compare_digest(token.encode(), f"Bearer {self.token}".encode())):
            self.reply(handler, 403, "TUBE5_ERROR\tAUTH\n")
            return
        try:
            if len(handler.path) > 256:
                raise ValueError("URL too long")
            route = urlsplit(handler.path)
            fields = parse_qs(route.query, strict_parsing=True, max_num_fields=2)
            if method == "GET" and route.path == "/tube5/search":
                if set(fields) != {"q"} or len(fields["q"]) != 1:
                    raise ValueError("Invalid query")
                value = self.provider.request(fields["q"][0])
                body = f"TUBE5_SEARCH\t1\t{value['state']}\n" + "".join(
                    f"{e['id']}\t{e['title']}\t{e['channel']}\t{e['seconds']}\n"
                    for e in value["entries"])
                self.reply(handler, 200, body)
            elif method == "POST" and route.path == "/tube5/prepare":
                if (set(fields) != {"id"} or len(fields["id"]) != 1
                        or not re.fullmatch(r"[A-Za-z0-9_-]{11}", fields["id"][0])
                        or handler.headers.get("Transfer-Encoding")
                        or handler.headers.get("Content-Length", "0") != "0"):
                    raise ValueError("Invalid preparation request")
                value = self.bridge.submit(dict(url=f"https://www.youtube.com/watch?v={fields['id'][0]}"))
                self.reply(handler, 200, self.job_body(value))
            elif method == "GET" and route.path == "/tube5/job" and not fields:
                self.reply(handler, 200, self.job_body(self.bridge.status()))
            else:
                self.reply(handler, 405, "TUBE5_ERROR\tROUTE\n")
        except (ValueError, TypeError, UnicodeError):
            self.reply(handler, 400, "TUBE5_ERROR\tREQUEST\n")
        except RuntimeError:
            self.reply(handler, 409, "TUBE5_ERROR\tBUSY_OR_FULL\n")

    @staticmethod
    def job_body(value):
        return f"TUBE5_JOB\t1\t{value['state']}\t{value.get('id', '-')}\n"

    def close(self):
        self.provider.close()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Search bounded YouTube metadata; does not download videos")
    parser.add_argument("query")
    args = parser.parse_args()
    provider = SearchProvider()
    try:
        print(json.dumps(results(provider.fetch(query_text(args.query))), ensure_ascii=True))
    finally:
        provider.close()
