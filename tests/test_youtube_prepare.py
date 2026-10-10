# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "youtube_prepare", Path(__file__).resolve().parents[1] / "tools/prepare-youtube-test.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PrepareTests(unittest.TestCase):
    def test_normalizes_only_youtube_links(self):
        for url in ("https://youtu.be/qf31PkeESoU?si=tracking",
                    "https://www.youtube.com/watch?v=qf31PkeESoU&list=ignored",
                    "https://m.youtube.com/watch?v=qf31PkeESoU"):
            self.assertEqual(prepare.youtube_id(url), "qf31PkeESoU")
        for url in ("http://youtu.be/qf31PkeESoU", "https://example.com/watch?v=qf31PkeESoU",
                    "https://youtube.com.evil.test/watch?v=qf31PkeESoU",
                    "https://user@youtu.be/qf31PkeESoU", "https://youtu.be:8080/qf31PkeESoU",
                    "https://youtu.be/../qf31PkeESoU", "https://youtu.be/not-an-id",
                    "https://youtube.com/watch?v=qf31PkeESoU&v=qf31PkeESoU"):
            with self.assertRaises(ValueError):
                prepare.youtube_id(url)

    def test_checks_actual_codec_and_limits(self):
        video = dict(codec_type="video", codec_name="h264", pix_fmt="yuv420p",
                     width=640, height=360, r_frame_rate="30/1")
        audio = dict(codec_type="audio", codec_name="aac", channels=2)
        probe = dict(streams=[video, audio], format=dict(duration="1701"))
        self.assertEqual(prepare.validate_media(probe), 1701)
        for key, value in (("codec_name", "av1"), ("pix_fmt", "yuv420p10le"),
                           ("width", 1920), ("r_frame_rate", "60/1")):
            changed = dict(video, **{key: value})
            with self.assertRaises(ValueError):
                prepare.validate_media(dict(probe, streams=[changed, audio]))
        with self.assertRaises(ValueError):
            prepare.validate_media(dict(probe, streams=[video]))
        with self.assertRaises(ValueError):
            prepare.validate_media(dict(probe, format=dict(duration="7200")))


if __name__ == "__main__":
    unittest.main()
