# SPDX-License-Identifier: GPL-3.0-or-later
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
import urllib.error

spec = importlib.util.spec_from_file_location("ranges", Path(__file__).parents[1] / "tools/test-youtube-ranges.py")
ranges = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ranges)
SECRET_URL = "https://rr1.googlevideo.com/videoplayback?sig=PRIVATE_TEST_SIGNATURE"
TOTAL = 2 * ranges.CAPACITY + 100
PLAYER = {"streamingData": {"adaptiveFormats": [
    {"itag": 137, "height": 1080, "fps": 30, "mimeType": "video/mp4; codecs=avc1.640028",
     "contentLength": str(TOTAL), "url": SECRET_URL},
    {"itag": 140, "mimeType": "audio/mp4; codecs=mp4a.40.2", "bitrate": 128000,
     "contentLength": str(TOTAL), "url": SECRET_URL},
]}}


class Response:
    status = 206

    def __init__(self, request, bad_range):
        first, last = request.get_header("Range").removeprefix("bytes=").split("-")
        self.size = int(last) - int(first) + 1
        self.headers = {"Content-Range": f"bytes {first}-{last}/{TOTAL}" if not bad_range else "invalid"}

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def read(self, bound):
        assert bound == ranges.CAPACITY + 1 and self.size <= ranges.CAPACITY
        return b"x" * self.size


class Opener:
    def __init__(self, forbidden=False, bad_range=False):
        self.requests = []
        self.forbidden = forbidden
        self.bad_range = bad_range

    def open(self, request, timeout):
        assert timeout == 8
        self.requests.append(request)
        if self.forbidden and len(self.requests) == 1:
            raise urllib.error.HTTPError(SECRET_URL, 403, "Forbidden", {}, None)
        return Response(request, self.bad_range)


class RangeProbeTests(unittest.TestCase):
    def run_probe(self, opener):
        output = io.StringIO()
        with patch.object(ranges.adaptive, "extract", return_value=(PLAYER, "PRIVATE_CPN")), \
                patch.object(ranges.urllib.request, "build_opener", return_value=opener), \
                contextlib.redirect_stdout(output):
            failures = ranges.run("bHIhgxav9LY", 1)
        text = output.getvalue()
        for secret in (SECRET_URL, "PRIVATE_TEST_SIGNATURE", "PRIVATE_CPN"):
            self.assertNotIn(secret, text)
        return failures, [json.loads(line) for line in text.splitlines()]

    def test_all_variants_are_bounded_and_compare_identical_ranges(self):
        opener = Opener()
        failures, rows = self.run_probe(opener)
        self.assertEqual(failures, 0)
        self.assertEqual(rows[-1], {"sessions": 1, "requests": 48, "failures": 0})
        self.assertEqual(len(opener.requests), 48)
        self.assertEqual({r.get_method() for r in opener.requests}, {"GET", "POST"})
        for request in opener.requests:
            self.assertEqual(request.data, b"\x78\x00" if request.get_method() == "POST" else None)

    def test_http_error_does_not_leak_url(self):
        failures, rows = self.run_probe(Opener(forbidden=True))
        self.assertEqual(failures, 1)
        self.assertEqual(rows[0]["http"], 403)

    def test_wrong_content_range_is_not_a_pass(self):
        failures, _ = self.run_probe(Opener(bad_range=True))
        self.assertEqual(failures, 48)


if __name__ == "__main__":
    unittest.main()
