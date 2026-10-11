#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""LAN-only media server: explicit clips, optional bounded catalog, HTTP byte ranges."""
import argparse
import importlib.util
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import threading
import unicodedata
import signal


def library_entries(clip, local):
    metadata = {}
    sidecar = clip.parent / "metadata.json" if clip.name == "media.mp4" else clip.with_suffix(".json")
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
    return [dict(id="network-test", title=title, youtube=identifier, seconds=seconds, height=height, path=clip,
                 timeline_full=clip.name == "media.mp4", duration=duration),
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


def handler_for(clip, stall_headers=False, library=None, sponsor=None, console=None):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def do_HEAD(self):
            self.send_clip(False)

        def do_GET(self):
            if self.path.startswith("/tube5/"):
                if console:
                    console.handle(self, "GET")
                else:
                    self.send_error(404)
            else:
                self.send_clip(True)

        def do_POST(self):
            if console and self.path.startswith("/tube5/"):
                console.handle(self, "POST")
            else:
                self.send_error(404)

        def log_message(self, format, *args):
            if not self.path.startswith("/tube5/"):
                super().log_message(format, *args)

        def send_clip(self, body):
            entries = library.snapshot() if hasattr(library, "snapshot") else library
            catalog = ("TUBE5\t1\n" + "".join(
                f"{e['id']}\t{e['title']}\t{e['youtube']}\t{e['seconds']}\t{e['height']}\n"
                for e in entries)).encode("ascii") if entries else None
            if self.path == "/catalog-sb.tsv" and entries:
                decorated = sponsor.decorate(entries) if sponsor else [dict(e, sb_state="unavailable" if e["youtube"] != "-" else "none", sb_ranges=[]) for e in entries]
                catalog = ("TUBE5\t2\n" + "".join(
                    f"{e['id']}\t{e['title']}\t{e['youtube']}\t{e['seconds']}\t{e['height']}\t{e['sb_state']}\t" +
                    (",".join(f"{start}-{end}" for start, end in e["sb_ranges"]) or "-") + "\n"
                    for e in decorated)).encode("ascii")
            routes = {"/test-clip.mp4": clip}
            if entries:
                routes.update({f"/media/{e['id']}.mp4": e["path"] for e in entries})
            if self.path in ("/catalog.tsv", "/catalog-sb.tsv") and catalog is not None:
                self.send_response(200)
                self.send_header("Content-Type", "text/tab-separated-values; charset=us-ascii")
                self.send_header("Content-Length", str(len(catalog)))
                self.send_header("Cache-Control", "no-store")
                if self.path == "/catalog-sb.tsv":
                    self.send_header("X-SponsorBlock-Source", "https://sponsor.ajay.app/")
                    self.send_header("X-SponsorBlock-License", "CC-BY-NC-SA-4.0; https://creativecommons.org/licenses/by-nc-sa/4.0/")
                    self.send_header("X-SponsorBlock-Changes", "sponsor/skip filter; merged intervals; milliseconds")
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
    parser.add_argument("--youtube-control", action="store_true",
                        help="Enable local authenticated URL submission (also enables library)")
    parser.add_argument("--control-port", type=int, default=8081)
    parser.add_argument("--console-ip", help="Opt-in authenticated console search/control for this IPv4 only")
    parser.add_argument("--sponsorblock", action="store_true", help="Read-only sponsor category with private cache")
    parser.add_argument("--clip", type=Path,
                        default=Path(__file__).resolve().parents[1] / "assets/test-clip.mp4")
    args = parser.parse_args()
    if args.console_ip:
        import ipaddress
        try:
            ipaddress.IPv4Address(args.console_ip)
            if ipaddress.IPv4Address(args.bind).is_unspecified:
                raise ValueError("Wildcard bind unsupported for console control")
        except ValueError:
            parser.error("console-ip and bind must be concrete IPv4 addresses")
        if not args.youtube_control:
            parser.error("console-ip requires youtube-control")
    clip = args.clip.resolve(strict=True)
    if not clip.is_file() or clip.stat().st_size == 0:
        parser.error("clip must be a nonempty file")
    local = Path(__file__).resolve().parents[1] / "assets/test-clip.mp4"
    library = library_entries(clip, local) if args.library or args.youtube_control or args.sponsorblock else None
    bridge = control = control_thread = sponsor = console = None
    if args.youtube_control:
        spec = importlib.util.spec_from_file_location("tube5_bridge", Path(__file__).with_name("youtube-bridge.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        # Reserve the control socket before changing its private token. A second
        # instance must not invalidate the credentials of a running service.
        control = ThreadingHTTPServer(("127.0.0.1", args.control_port), BaseHTTPRequestHandler)
        try:
            bridge = module.Bridge(library)
        except Exception:
            control.server_close()
            raise
        library = bridge
    try:
        if args.console_ip:
            spec_search = importlib.util.spec_from_file_location("tube5_search", Path(__file__).with_name("youtube-search.py"))
            module_search = importlib.util.module_from_spec(spec_search)
            spec_search.loader.exec_module(module_search)
            console = module_search.ConsoleBridge(bridge, args.console_ip)
        if args.sponsorblock:
            spec = importlib.util.spec_from_file_location("tube5_sponsor", Path(__file__).with_name("sponsorblock-cache.py"))
            module_sb = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module_sb)
            sponsor = module_sb.SponsorCache()
            sponsor.decorate(library.snapshot() if bridge else library)
        server = ThreadingHTTPServer((args.bind, args.port), handler_for(clip, args.stall_headers, library, sponsor, console))
        if bridge:
            control.RequestHandlerClass = module.control_handler(bridge)
            control.daemon_threads = True
            control_thread = threading.Thread(target=control.serve_forever, daemon=True)
            control_thread.start()
    except OSError:
        if bridge:
            bridge.close()
        if control:
            control.server_close()
        if sponsor:
            sponsor.close()
        if console:
            console.close()
        if 'server' in locals():
            server.server_close()
        raise
    server.daemon_threads = True
    print(f"Tube5 HTTP: http://{args.bind}:{server.server_port}/test-clip.mp4", flush=True)
    print("Only explicitly configured clips are exposed. Ctrl+C stops the server.", flush=True)
    if library:
        print("Tube5 library: /catalog.tsv", flush=True)
    if bridge:
        print(f"YouTube control: loopback only, port {args.control_port}; use tools/add-youtube.py", flush=True)
    if args.stall_headers:
        print("FAULT TEST: connections accepted, HTTP headers withheld for 30 s. "
              "Video playback intentionally unavailable.", flush=True)
    def stop_server(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop_server)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if control:
            control.shutdown()
            control.server_close()
            control_thread.join()
        if bridge:
            bridge.close()
        if sponsor:
            sponsor.close()
        if console:
            console.close()


if __name__ == "__main__":
    main()
