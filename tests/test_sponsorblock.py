# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sponsor_cache", ROOT / "tools/sponsorblock-cache.py")
sb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sb)

def segment(start, end, **kwargs):
    return dict(category="sponsor", actionType="skip", segment=[start, end], videoDuration=20,
                votes=3, **kwargs)

class SponsorTests(unittest.TestCase):
    def test_ranges_merge_and_reject_stale_duration(self):
        records = [segment(5, 9), segment(0, 4), segment(3, 6)]
        self.assertEqual(sb.ranges_for(records, 20), ("ready", [[0, 9000]]))
        self.assertEqual(sb.ranges_for(records, 30), ("unaligned", []))
        self.assertEqual(sb.ranges_for([], 20), ("none", []))
        for duration in (0, -1, float("nan"), float("inf"), "20"):
            with self.assertRaises(ValueError): sb.ranges_for(records, duration)
        for pair in ([0, float("nan")], [-1, 5], [5, 4], [0, 3601], [0, 30]):
            bad = segment(0, 1)
            bad["segment"] = pair
            with self.assertRaises(ValueError): sb.ranges_for([bad], 20)
        ignored = segment(0, 5)
        ignored["category"] = "intro"
        self.assertEqual(sb.ranges_for([ignored], 20), ("none", []))
        ignored["category"] = "sponsor"
        ignored["actionType"] = "mute"
        self.assertEqual(sb.ranges_for([ignored], 20), ("none", []))
        ignored["actionType"] = "skip"
        ignored["votes"] = -1
        self.assertEqual(sb.ranges_for([ignored], 20), ("none", []))

    def wait(self, cache, entries):
        until = time.monotonic() + 3
        while True:
            result = cache.decorate(entries)
            if result[0]["sb_state"] != "pending": return result
            self.assertLess(time.monotonic(), until)
            time.sleep(.01)

    def test_async_cache_persistence_and_failure(self):
        entry = dict(id="network-test", title="TEST", youtube="bHIhgxav9LY", seconds=20, height=360,
                     timeline_full=True, path=ROOT / "assets/test-clip.mp4")
        calls = []
        def fetch(identifier):
            calls.append(identifier)
            return [segment(0, 4)]
        with tempfile.TemporaryDirectory() as directory:
            cache = sb.SponsorCache(Path(directory), fetch)
            try:
                self.assertEqual(self.wait(cache, [entry])[0]["sb_ranges"], [[0, 4000]])
                self.assertEqual(len(calls), 1)
                self.assertEqual(cache.decorate([dict(entry, timeline_full=False)])[0]["sb_state"], "unaligned")
                self.assertEqual(cache.decorate([dict(entry, youtube="-")])[0]["sb_state"], "none")
            finally: cache.close()
            def fail(identifier): raise OSError("private network error")
            restored = sb.SponsorCache(Path(directory), fail)
            try: self.assertEqual(restored.decorate([entry])[0]["sb_state"], "ready")
            finally: restored.close()
            (Path(directory) / "bHIhgxav9LY.json").unlink()
            failed = sb.SponsorCache(Path(directory), fail)
            try:
                self.assertEqual(self.wait(failed, [entry])[0]["sb_state"], "unavailable")
            finally: failed.close()

    def test_server_v1_v2_and_native_client(self):
        spec = importlib.util.spec_from_file_location("sb_http", ROOT / "tools/http-test-server.py")
        http = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(http)
        clip = ROOT / "assets/test-clip.mp4"
        entries = http.library_entries(clip, clip)
        entries[0].update(youtube="bHIhgxav9LY", seconds=20, duration=20, timeline_full=True)
        with tempfile.TemporaryDirectory() as directory:
            cache = sb.SponsorCache(Path(directory), lambda identifier: [segment(0, 4)])
            self.wait(cache, entries)
            server = ThreadingHTTPServer(("127.0.0.1", 0), http.handler_for(clip, library=entries, sponsor=cache))
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            connection = HTTPConnection("127.0.0.1", server.server_port, timeout=3)
            try:
                connection.request("GET", "/catalog.tsv")
                response = connection.getresponse()
                self.assertTrue(response.read().startswith(b"TUBE5\t1\n"))
                connection.request("GET", "/catalog-sb.tsv")
                response = connection.getresponse()
                body = response.read()
                self.assertEqual(response.getheader("X-SponsorBlock-Source"), "https://sponsor.ajay.app/")
                self.assertIn("CC-BY-NC-SA-4.0", response.getheader("X-SponsorBlock-License"))
                self.assertTrue(body.startswith(b"TUBE5\t2\n"))
                self.assertIn(b"ready\t0-4000", body)
                if (ROOT / "build/lan-catalog-test").is_file():
                    import subprocess
                    result = subprocess.run([str(ROOT / "build/lan-catalog-test"),
                        f"http://127.0.0.1:{server.server_port}/test-clip.mp4"], capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            finally:
                connection.close()
                server.shutdown()
                server.server_close()
                thread.join()
                cache.close()

if __name__ == "__main__": unittest.main()
