# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import threading
import unittest
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer

spec = importlib.util.spec_from_file_location(
    "tube5_http", Path(__file__).resolve().parents[1] / "tools/http-test-server.py")
http = importlib.util.module_from_spec(spec)
spec.loader.exec_module(http)


class RangeTests(unittest.TestCase):
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
