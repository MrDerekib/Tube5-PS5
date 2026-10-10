#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LAN-only test server: one MP4, no directory listing, single HTTP byte ranges."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import threading


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


def handler_for(clip, stall_headers=False):
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
            if self.path != "/test-clip.mp4":
                self.send_error(404, "Only /test-clip.mp4 is served")
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
                source = clip.open("rb")
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
    parser.add_argument("--clip", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets/test-clip.mp4")
    args = parser.parse_args()
    clip = args.clip.resolve(strict=True)
    if not clip.is_file() or clip.stat().st_size == 0:
        parser.error("clip must be a nonempty file")
    server = ThreadingHTTPServer((args.bind, args.port), handler_for(clip, args.stall_headers))
    server.daemon_threads = True
    print(f"Tube5 HTTP: http://{args.bind}:{server.server_port}/test-clip.mp4", flush=True)
    print("Only the test clip is exposed. Ctrl+C stops the server.", flush=True)
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
