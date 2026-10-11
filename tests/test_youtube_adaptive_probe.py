# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "adaptive_probe", Path(__file__).resolve().parents[1] / "tools/probe-youtube-adaptive.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class AdaptiveProbeTests(unittest.TestCase):
    def test_cdn_target(self):
        self.assertTrue(probe.media_url("https://rr1.googlevideo.com/videoplayback?expire=1"))

    def test_reject_other_targets(self):
        for target in (
            None, "", "http://rr1.googlevideo.com/videoplayback?x=1",
            "https://rr1.googlevideo.com.evil.test/videoplayback?x=1",
            "https://rr1.googlevideo.com:443/videoplayback?x=1",
            "https://u@rr1.googlevideo.com/videoplayback?x=1",
            "https://rr1.googlevideo.com/other?x=1",
            "https://rr1.googlevideo.com/videoplayback?x=1#fragment",
            "https://rr1.googlevideo.com/videoplayback?x=1\r\n",
            "https://rr1.googlevideo.com/videoplayback?x=" + "a" * 8192,
        ):
            with self.subTest(target_type=type(target).__name__):
                self.assertFalse(probe.media_url(target))

    def test_id_checked_before_network(self):
        for value in ("bad", "../anything", "bHIhgxav9LY&extra=1"):
            with self.assertRaises(ValueError):
                probe.probe(value)
