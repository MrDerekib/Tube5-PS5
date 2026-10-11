#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LAN-only media server: explicit clips, optional bounded catalog, HTTP byte ranges."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import threading
import unicodedata


def library_entries(clip, local):
    metadata = {}
    sidecar = clip.with_suffix(".json")
    if sidecar.is_file() and sidecar.stat().st_size <= 16384:
        metadata = json.loads(sidecar.read_text(encoding="utf-8"))
    title = unicodedata.normalize("NFKD", str(metadata.get("title", "VIDEO HTTP"))).encode("ascii", "ignore").decode()
    title = "".join(c if 32 <= ord(c) <= 126 else " " for c in title)[:80] or "VIDEO HTTP"
    identifier = metadata.get("id", "-")
    if not isinstance(identifier, str) or not re.fullmatch(r"[A-Za-z0-9_-]{11}", identifier):
        identifier = "-"
    duration = metadata.get("duration", 0)
    seconds = int(duration) if isinstance(duration, (int, float)) and 0 <= duration <= 3600 else 0
    height = metadata.get("height", 0)
    if height not in (360, 720, 1080):
        height = 0
    return [dict(id="network-test", title=title, youtube=identifier, seconds=seconds, height=height, path=clip),
            dict(id="local-test", title="CLIP SINTETICO - 20 SEG", youtube="-", seconds=20, height=360, path=local)]


def byte_range(header, size):
    if header is None:
        return 0, size - 1
    match = re.fullmatch(r"bytes=(\d*)-(\d*)", header)
    if not match or not any(match.groups()):
        raise ValueError("Only a single byte range is supported")
    first, last = match.groups()
    if not first:
        count = int(last)
        if count == 0:
            raise ValueError("Empty suffix")
        start, end = max(0, size - count), size - 1
    else:
        start, end = int(first), int(last) if last else size - 1
    if size <= 0 or start >= size or start > end:
        raise ValueError("Unsatisfiable range")
    return start, min(end, size - 1)


def handler_for(clip, stall_headers=False, library=None):
    catalog = ("TUBE5\t1\n" + "".join(
        f"{e['id']}\t{e['title']}\t{e['youtube']}\t{e['seconds']}\t{e['height']}\n"
        for e in library)).encode("ascii") if library else None
    routes = {"/test-clip.mp4": clip}
    if library:
        routes.update({f"/media/{e['id']}.mp4": e["path"] for e in library})
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def do_HEAD(self):
            self.send_clip(False)

        def do_GET(self):
            self.send_clip(True)

        def send_clip(self, body):
            if self.path == "/catalog.tsv" and catalog is not None:
                self.send_response(200)
                self.send_header("Content-Type", "text/tab-separated-values; charset=us-ascii")
                self.send_header("Content-Length", str(len(catalog)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                if body:
                    self.wfile.write(catalog)
                return
            clip_path = routes.get(self.path)
            if clip_path is None:
                self.send_error(404, "Only explicitly configured media are served")
                return
            if stall_headers:
                print(f"Tube5 STALL: {self.client_address[0]} {self.command} {self.path}; "
                      "connection accepted, withholding headers", flush=True)
                # Explicit fault injection. No file bytes or HTTP response are
                # sent; FFmpeg must interrupt or time out before this guard ends.
                threading.Event().wait(30)
                self.close_connection = True
                return
            try:
                source = clip_path.open("rb")
            except OSError:
                self.send_error(503, "Test clip unavailable")
                return
            with source:
                source.seek(0, 2)
                size = source.tell()
                # RFC 9110: Range applies to GET, not HEAD.
                range_header = self.headers.get("Range") if body else None
                try:
                    start, end = byte_range(range_header, size)
                except ValueError:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(206 if range_header else 200)
                self.send_header("Content-Type", "video/mp4")
                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-Length", str(end - start + 1))
                self.send_header("Cache-Control", "no-store")
                if range_header:
                    self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
                self.end_headers()
                if not body:
                    return
                source.seek(start)
                remaining = end - start + 1
                try:
                    while remaining:
                        data = source.read(min(remaining, 65536))
                        if not data:
                            self.close_connection = True
                            break
                        self.wfile.write(data)
                        remaining -= len(data)
                except (BrokenPipeError, ConnectionResetError, TimeoutError):
                    # FFmpeg closes/reopens requests during probing and seeks.
                    self.close_connection = True

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--stall-headers", action="store_true",
                        help="Timeout/cancel test: accept requests but send no response for 30 s")
    parser.add_argument("--library", action="store_true", help="Expose bounded catalog and two explicit media entries")
    parser.add_argument("--clip", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets/test-clip.mp4")
    args = parser.parse_args()
    clip = args.clip.resolve(strict=True)
    if not clip.is_file() or clip.stat().st_size == 0:
        parser.error("clip must be a nonempty file")
    local = Path(__file__).resolve().parents[1] / "assets/test-clip.mp4"
    library = library_entries(clip, local) if args.library else None
    server = ThreadingHTTPServer((args.bind, args.port), handler_for(clip, args.stall_headers, library))
    server.daemon_threads = True
    print(f"Tube5 HTTP: http://{args.bind}:{server.server_port}/test-clip.mp4", flush=True)
    print("Only explicitly configured clips are exposed. Ctrl+C stops the server.", flush=True)
    if library:
        print("Tube5 library: /catalog.tsv (2 entries)", flush=True)
    if args.stall_headers:
        print("FAULT TEST: connections accepted, HTTP headers withheld for 30 s. "
              "Video playback intentionally unavailable.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
