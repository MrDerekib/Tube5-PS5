# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from http.client import HTTPConnection
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("youtube_search", ROOT / "tools/youtube-search.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
server_spec = importlib.util.spec_from_file_location("tube5_http_server", ROOT / "tools/http-test-server.py")
server_module = importlib.util.module_from_spec(server_spec)
server_spec.loader.exec_module(server_module)
ENTRY = dict(id="bHIhgxav9LY", title="Electricité\n", channel="Veritasium", duration=887)


class SearchTests(unittest.TestCase):
    def test_validation_and_filter(self):
        for query in ("", " " * 48, "A" * 49, "https://youtu.be/x", "X; touch /tmp/x", "á", None):
            with self.assertRaises(ValueError):
                module.query_text(query)
        self.assertEqual(module.query_text(" TEST "), "TEST")
        entries = [ENTRY, ENTRY, dict(ENTRY, id="bad"), dict(ENTRY, duration=float("nan")),
                   dict(ENTRY, id="qf31PkeESoU", is_live=True),
                   dict(ENTRY, id="C9sSIz5VSo4", duration=3601)]
        result = module.results(dict(entries=entries))
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["title"], "Electricite")
        self.assertEqual(module.results(dict(entries=[dict(ENTRY, title="漢字", channel="漢字")]))[0]["title"], ENTRY["id"])
        with self.assertRaises(ValueError):
            module.results({})

    def test_async_cache_and_busy(self):
        gate = threading.Event()
        calls = []
        def run(query):
            calls.append(query)
            gate.wait(2)
            return dict(entries=[ENTRY])
        provider = module.SearchProvider(run)
        try:
            self.assertEqual(provider.request("TEST")["state"], "searching")
            with self.assertRaises(RuntimeError):
                provider.request("OTHER")
            gate.set()
            deadline = time.monotonic() + 3
            while provider.request("TEST")["state"] == "searching":
                self.assertLess(time.monotonic(), deadline)
                time.sleep(.01)
            self.assertEqual(provider.request("TEST")["state"], "ready")
            self.assertEqual(calls, ["TEST"])
        finally:
            gate.set()
            provider.close()

    def test_failure_is_generic(self):
        def fail(query):
            raise RuntimeError("PRIVATE TOKEN")
        provider = module.SearchProvider(fail)
        try:
            provider.request("TEST")
            deadline = time.monotonic() + 3
            while provider.request("TEST")["state"] == "searching":
                self.assertLess(time.monotonic(), deadline)
                time.sleep(.01)
            self.assertEqual(provider.request("TEST"), dict(state="failed", entries=[]))
        finally:
            provider.close()


class ConsoleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        class Bridge:
            state = self.temp.name
            calls = []
            def submit(inner, value):
                inner.calls.append(value)
                return dict(state="ready", id="yt-bHIhgxav9LY-360-60")
            def status(inner):
                return dict(state="idle")
        self.bridge = Bridge()
        self.console = module.ConsoleBridge(self.bridge, "127.0.0.1", module.SearchProvider(lambda q: dict(entries=[ENTRY])))
        Handler = server_module.handler_for(ROOT / "assets/test-clip.mp4", console=self.console)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()
        self.console.close()
        self.temp.cleanup()

    def request(self, path, method="GET", headers=None):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        auth = {"Authorization": f"Bearer {self.console.token}"}
        auth.update(headers or {})
        connection.request(method, path, headers=auth)
        response = connection.getresponse()
        result = response.status, response.read().decode()
        connection.close()
        return result

    def test_auth_host_origin_and_ip(self):
        for headers in ({"Authorization": "Bearer wrong"}, {"Origin": "https://evil.test"}, {"Host": "evil.test"}):
            self.assertEqual(self.request("/tube5/job", headers=headers)[0], 403)
        self.console.address = "192.168.8.200"
        self.assertEqual(self.request("/tube5/job")[0], 403)
        self.assertEqual(self.bridge.calls, [])

    def test_token_private_and_persistent(self):
        path = Path(self.temp.name) / "console-token"
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        restored = module.ConsoleBridge(self.bridge, "127.0.0.1", module.SearchProvider())
        try:
            self.assertEqual(restored.token, self.console.token)
        finally:
            restored.close()

    def test_search_prepare_status_and_bad_inputs(self):
        self.assertEqual(self.request("/tube5/job"), (200, "TUBE5_JOB\t1\tidle\t-\n"))
        deadline = time.monotonic() + 3
        while True:
            status, body = self.request("/tube5/search?q=VERITASIUM%20ELECTRICITY")
            self.assertEqual(status, 200)
            if "\tready\n" in body: break
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        self.assertIn("bHIhgxav9LY\tElectricite\tVeritasium\t887\n", body)
        self.assertEqual(self.request("/tube5/prepare?id=bHIhgxav9LY", "POST"),
                         (200, "TUBE5_JOB\t1\tready\tyt-bHIhgxav9LY-360-60\n"))
        self.assertEqual(self.bridge.calls, [dict(url="https://www.youtube.com/watch?v=bHIhgxav9LY")])
        for path in ("/tube5/search?q=X&q=Y", "/tube5/search?q=http%3A%2F%2Fevil", "/tube5/search?q=%0A"):
            self.assertEqual(self.request(path)[0], 400)
        self.assertEqual(self.request("/tube5/prepare?id=bad", "POST")[0], 400)
        self.assertEqual(self.request("/tube5/prepare?id=bHIhgxav9LY", "POST", {"Content-Length": "1"})[0], 400)

    def test_native_client_get_and_empty_post(self):
        binary = ROOT / "build/youtube-search-test"
        if not binary.exists(): self.skipTest("Build native-client fixture first")
        # Fixture token only, not the developer's persistent console token.
        self.console.token = "a" * 64
        self.console.provider.request("VERITASIUM ELECTRICITY")
        deadline = time.monotonic() + 3
        while self.console.provider.request("VERITASIUM ELECTRICITY")["state"] == "searching":
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        url = f"http://127.0.0.1:{self.server.server_port}/test.mp4"
        for mode in ("search", "prepare"):
            subprocess.run([str(binary), url, mode], check=True, timeout=12, capture_output=True)
        self.assertEqual(len(self.bridge.calls), 1)
        self.console.token = "b" * 64
        subprocess.run([str(binary), url, "failure"], check=True, timeout=12, capture_output=True)

    def test_native_client_cancel_timeout_and_oversize(self):
        binary = ROOT / "build/youtube-search-test"
        if not binary.exists(): self.skipTest("Build native-client fixture first")
        release = threading.Event()
        class Handler(BaseHTTPRequestHandler):
            oversized = False
            def do_GET(inner):
                if not inner.oversized:
                    release.wait(10)
                    return
                body = b"x" * 2049
                inner.send_response(200)
                inner.send_header("Content-Length", str(len(body)))
                inner.end_headers()
                inner.wfile.write(body)
            def log_message(inner, *args): pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        url = f"http://127.0.0.1:{server.server_port}/test.mp4"
        try:
            for mode in ("cancel", "failure"):
                subprocess.run([str(binary), url, mode], check=True, timeout=12, capture_output=True)
            Handler.oversized = True
            subprocess.run([str(binary), url, "failure"], check=True, timeout=12, capture_output=True)
        finally:
            release.set()
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__": unittest.main()
