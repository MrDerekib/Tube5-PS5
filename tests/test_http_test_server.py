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


class RangeTests(unittest.TestCase):
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
