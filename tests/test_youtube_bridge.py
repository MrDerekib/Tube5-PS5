# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
import json
from pathlib import Path
import shutil
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("youtube_bridge", ROOT / "tools/youtube-bridge.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.calls = []
        self.clip = ROOT / "assets/test-clip.mp4"
        self.base = [dict(id="network-test", title="NETWORK", youtube="-", seconds=20, height=360, path=self.clip),
                     dict(id="local-test", title="LOCAL", youtube="-", seconds=20, height=360, path=self.clip)]
        self.bridge = module.Bridge(self.base, self.root / "cache", self.root / "state", self.prepare)

    def tearDown(self):
        self.bridge.close()
        self.temporary.cleanup()

    def prepare(self, identifier, height, seconds):
        self.calls.append((identifier, height, seconds))
        self.assertEqual(height, 360)
        destination = self.bridge.path(identifier, height, seconds)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.clip, destination)
        (destination.parent / "metadata.json").write_text(json.dumps(
            dict(id=identifier, title="Un título\ncon minúsculas " + "A" * 100)))

    def wait(self):
        deadline = time.monotonic() + 5
        while self.bridge.status()["state"] == "preparing":
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        return self.bridge.status()

    def test_profile_limits_and_url_allowlist(self):
        for value in ({"url": "http://youtu.be/qf31PkeESoU"},
                      {"url": "https://evil.test/qf31PkeESoU"},
                      {"url": "https://youtu.be/qf31PkeESoU", "height": 2160},
                      {"url": "https://youtu.be/qf31PkeESoU", "seconds": True},
                      {"url": "https://youtu.be/qf31PkeESoU", "path": "/etc/passwd"}):
            with self.assertRaises(ValueError):
                self.bridge.submit(value)
        self.assertEqual(self.bridge.status()["state"], "idle")

    def test_publish_reuse_and_persistence(self):
        value = {"url": "https://youtu.be/qf31PkeESoU?si=tracking"}
        self.bridge.submit(value)
        self.assertEqual(self.wait()["state"], "ready")
        entry = self.bridge.snapshot()[-1]
        self.assertEqual(entry["height"], 360)
        self.assertLessEqual(len(entry["title"]), 80)
        self.assertNotIn("\n", entry["title"])
        self.assertTrue(self.bridge.submit(value)["reused"])
        self.assertEqual(len(self.calls), 1)
        restored = module.Bridge(self.base, self.root / "cache", self.root / "state", self.prepare)
        try:
            self.assertEqual(len(restored.snapshot()), 3)
            self.assertTrue(restored.submit(value)["reused"])
            self.assertEqual((self.root / "state/control-token").stat().st_mode & 0o777, 0o600)
        finally:
            restored.close()

    def test_one_job_and_failure_never_publish(self):
        release = threading.Event()
        def fail(*args):
            release.wait(3)
            raise RuntimeError("private CDN URL must not escape")
        self.bridge.runner = fail
        self.bridge.submit({"url": "https://youtu.be/qf31PkeESoU"})
        try:
            with self.assertRaises(RuntimeError):
                self.bridge.submit({"url": "https://youtu.be/qf31PkeESoU"})
            self.assertEqual(len(self.bridge.snapshot()), 2)
        finally:
            release.set()
        status = self.wait()
        self.assertEqual(status["state"], "failed")
        self.assertNotIn("CDN", status["error"])
        self.assertEqual(len(self.bridge.snapshot()), 2)

    def test_cache_escape_rejected(self):
        self.bridge.cache.mkdir()
        (self.bridge.cache / "qf31PkeESoU").symlink_to(self.root)
        with self.assertRaises(ValueError):
            self.bridge.path("qf31PkeESoU", 360, 60)

    def test_full_library_rejects_before_download(self):
        self.bridge.entries += [dict(self.base[0], id=f"extra-{i}") for i in range(6)]
        with self.assertRaises(RuntimeError):
            self.bridge.submit({"url": "https://youtu.be/qf31PkeESoU"})
        self.assertEqual(self.calls, [])

    def test_control_requires_token_and_blocks_browser_origin(self):
        server = ThreadingHTTPServer(("127.0.0.1", 0), module.control_handler(self.bridge))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
        try:
            for headers, expected in (({}, 403),
                    ({"Authorization": f"Bearer {self.bridge.token}", "Origin": "https://evil.test"}, 403),
                    ({"Authorization": f"Bearer {self.bridge.token}", "Host": "evil.test"}, 403),
                    ({"Authorization": f"Bearer {self.bridge.token}"}, 200)):
                connection.request("GET", "/status", headers=headers)
                response = connection.getresponse()
                self.assertEqual(response.status, expected)
                response.read()
            connection.request("POST", "/jobs", json.dumps({"url": "https://youtu.be/qf31PkeESoU"}),
                               headers={"Authorization": f"Bearer {self.bridge.token}"})
            response = connection.getresponse()
            self.assertEqual(response.status, 202)
            response.read()
            self.assertEqual(self.wait()["state"], "ready")
        finally:
            connection.close()
            server.shutdown()
            server.server_close()
            thread.join()

    def test_media_catalog_updates_without_server_restart(self):
        spec = importlib.util.spec_from_file_location("bridge_media", ROOT / "tools/http-test-server.py")
        http = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(http)
        server = ThreadingHTTPServer(("127.0.0.1", 0), http.handler_for(self.clip, library=self.bridge))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
        try:
            connection.request("GET", "/catalog.tsv")
            response = connection.getresponse()
            before = response.read().decode()
            self.assertEqual(len(before.splitlines()), 3)
            self.bridge.submit({"url": "https://youtu.be/qf31PkeESoU"})
            self.assertEqual(self.wait()["state"], "ready")
            connection.request("GET", "/catalog.tsv")
            response = connection.getresponse()
            after = response.read().decode()
            self.assertEqual(len(after.splitlines()), 4)
            self.assertNotIn(str(self.root), after)
            self.assertIn("yt-qf31PkeESoU-360-60", after)
            connection.request("GET", "/media/yt-qf31PkeESoU-360-60.mp4", headers={"Range": "bytes=0-9"})
            response = connection.getresponse()
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), self.clip.read_bytes()[:10])
            connection.request("GET", "/status")
            response = connection.getresponse()
            self.assertEqual(response.status, 404)
            response.read()
        finally:
            connection.close()
            server.shutdown()
            server.server_close()
            thread.join()


if __name__ == "__main__":
    unittest.main()
