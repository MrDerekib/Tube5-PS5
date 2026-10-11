# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import os
import subprocess
import threading
import unittest
from http.client import HTTPConnection
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

spec = importlib.util.spec_from_file_location(
    "tube5_http", Path(__file__).resolve().parents[1] / "tools/http-test-server.py")
http = importlib.util.module_from_spec(spec)
spec.loader.exec_module(http)


class CatalogTests(unittest.TestCase):
    def run_client(self, handler, mode=None):
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            command = ["build/lan-catalog-test", f"http://127.0.0.1:{server.server_port}/test-clip.mp4"]
            if mode:
                command.append(mode)
            result = subprocess.run(command, capture_output=True, text=True, timeout=12)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    @unittest.skipUnless(Path("build/lan-catalog-test").is_file(), "Build catalogue tests first")
    def test_catalog_client_and_allowed_media(self):
        clip = Path("assets/test-clip.mp4")
        entries = http.library_entries(clip, clip)
        handler = http.handler_for(clip, library=entries)
        self.run_client(handler)
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
        try:
            for path in ("/media/network-test.mp4", "/media/local-test.mp4"):
                connection.request("GET", path, headers={"Range": "bytes=0-9"})
                response = connection.getresponse()
                self.assertEqual(response.status, 206)
                self.assertEqual(response.read(), clip.read_bytes()[:10])
            connection.request("GET", "/assets/test-clip.mp4")
            response = connection.getresponse()
            self.assertEqual(response.status, 404)
            response.read()
        finally:
            connection.close()
            server.shutdown()
            server.server_close()
            thread.join()

    @unittest.skipUnless(Path("build/lan-catalog-test").is_file(), "Build catalogue tests first")
    def test_catalog_rejects_invalid_and_oversized_responses(self):
        for body in (b"TUBE5\t2\n", b"x" * 4097):
            class Invalid(BaseHTTPRequestHandler):
                def do_GET(self):
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
            self.run_client(Invalid, "--failure")

    @unittest.skipUnless(Path("build/lan-catalog-test").is_file(), "Build catalogue tests first")
    def test_catalog_timeout_and_cancel(self):
        release = threading.Event()
        class Stalled(BaseHTTPRequestHandler):
            def do_GET(self):
                release.wait(12)
        try:
            self.run_client(Stalled, "--cancel")
            self.run_client(Stalled, "--failure")
        finally:
            release.set()


class RangeTests(unittest.TestCase):
    @unittest.skipUnless(Path("build/media-smoke").is_file(), "Build host media smoke first")
    def test_player_blocks_retries_after_read_oom(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), http.handler_for(Path("assets/test-clip.mp4")))
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            for mode in ["--expect-read-oom", "--expect-allocator-oom"]:
                with self.subTest(mode=mode):
                    result = subprocess.run(
                        ["build/media-smoke", f"http://127.0.0.1:{server.server_port}/test-clip.mp4",
                         mode], env=env, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("HTTP OOM passed", result.stdout)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    @unittest.skipUnless(Path("build/media-smoke").is_file(), "Build host media smoke first")
    def test_player_recovers_from_midstream_stall(self):
        release = threading.Event()
        with tempfile.TemporaryDirectory() as directory:
            clip = Path(directory) / "faststart.mp4"
            subprocess.run(["ffmpeg", "-v", "error", "-i", "assets/test-clip.mp4",
                            "-c", "copy", "-movflags", "+faststart", str(clip)], check=True)
            data = clip.read_bytes()

            class Partial(BaseHTTPRequestHandler):
                def do_GET(self):
                    start, end = http.byte_range(self.headers.get("Range"), len(data))
                    self.send_response(206)
                    self.send_header("Content-Type", "video/mp4")
                    self.send_header("Accept-Ranges", "bytes")
                    self.send_header("Content-Length", str(end - start + 1))
                    self.send_header("Content-Range", f"bytes {start}-{end}/{len(data)}")
                    self.end_headers()
                    try:
                        # A valid header and enough media to start, then no more bytes.
                        self.wfile.write(data[start:min(end + 1, 1000000)])
                        self.wfile.flush()
                        release.wait(25)
                    except (BrokenPipeError, ConnectionResetError):
                        pass

            server = ThreadingHTTPServer(("127.0.0.1", 0), Partial)
            server.daemon_threads = True
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
                result = subprocess.run(
                    ["build/media-smoke", f"http://127.0.0.1:{server.server_port}/test-clip.mp4",
                     "--expect-read-failure"], env=env, capture_output=True, text=True, timeout=26)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("HTTP cut passed", result.stdout)
            finally:
                release.set()
                server.shutdown()
                server.server_close()
                thread.join()

    @unittest.skipUnless(Path("build/media-smoke").is_file(), "Build host media smoke first")
    def test_player_cancels_stalled_http(self):
        release = threading.Event()

        class Stalled(BaseHTTPRequestHandler):
            def do_GET(self):
                release.wait(15)

        server = ThreadingHTTPServer(("127.0.0.1", 0), Stalled)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            result = subprocess.run(
                ["build/media-smoke", f"http://127.0.0.1:{server.server_port}/test-clip.mp4",
                 "--expect-cancel"], env=env, capture_output=True, text=True, timeout=13)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("HTTP cancel passed", result.stdout)
        finally:
            release.set()
            server.shutdown()
            server.server_close()
            thread.join()

    @unittest.skipUnless(Path("build/media-smoke").is_file(), "Build host media smoke first")
    def test_player_recovers_from_stalled_http(self):
        release = threading.Event()

        class Stalled(BaseHTTPRequestHandler):
            def do_GET(self):
                release.wait(15)  # Deliberately never send headers during open.

        server = ThreadingHTTPServer(("127.0.0.1", 0), Stalled)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
            result = subprocess.run(
                ["build/media-smoke", f"http://127.0.0.1:{server.server_port}/test-clip.mp4",
                 "--expect-open-failure"], env=env, capture_output=True, text=True, timeout=13)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("HTTP stall passed", result.stdout)
        finally:
            release.set()
            server.shutdown()
            server.server_close()
            thread.join()

    def test_ranges(self):
        for value, expected in [(None, (0, 99)), ("bytes=0-9", (0, 9)),
                                ("bytes=50-", (50, 99)), ("bytes=-10", (90, 99)),
                                ("bytes=90-999", (90, 99)), ("bytes=-999", (0, 99))]:
            self.assertEqual(http.byte_range(value, 100), expected)
        for value in ["bytes=100-", "bytes=10-9", "bytes=-0", "bytes=-",
                      "bytes=0-9,20-29", "not-a-range"]:
            with self.assertRaises(ValueError):
                http.byte_range(value, 100)

    def test_server_exposes_only_clip(self):
        with tempfile.TemporaryDirectory() as directory:
            clip = Path(directory) / "clip.mp4"
            clip.write_bytes(bytes(range(100)))
            server = ThreadingHTTPServer(("127.0.0.1", 0), http.handler_for(clip))
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            try:
                for method, path, headers, status, body in [
                    ("GET", "/test-clip.mp4", {}, 200, bytes(range(100))),
                    ("GET", "/test-clip.mp4", {"Range": "bytes=10-19"}, 206, bytes(range(10, 20))),
                    ("GET", "/test-clip.mp4", {"Range": "bytes=-3"}, 206, bytes(range(97, 100))),
                    ("HEAD", "/test-clip.mp4", {"Range": "bytes=10-19"}, 200, b""),
                    ("GET", "/test-clip.mp4", {"Range": "bytes=100-"}, 416, b""),
                    ("GET", "/", {}, 404, None),
                    ("GET", "/../IMG_4280.MOV", {}, 404, None),
                ]:
                    connection.request(method, path, headers=headers)
                    response = connection.getresponse()
                    self.assertEqual(response.status, status)
                    if status in (200, 206):
                        self.assertEqual(response.getheader("Accept-Ranges"), "bytes")
                    if status == 206:
                        self.assertTrue(response.getheader("Content-Range").endswith("/100"))
                    result = response.read()
                    if body is not None:
                        self.assertEqual(result, body)
            finally:
                connection.close()
                server.shutdown()
                server.server_close()
                thread.join()


if __name__ == "__main__":
    unittest.main()
