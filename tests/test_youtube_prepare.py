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

    def test_resolution_tests_require_native_height_30fps_and_one_minute(self):
        audio = dict(codec_type="audio", codec_name="aac", channels=2)
        for height, width in ((720, 1280), (1080, 1920)):
            video = dict(codec_type="video", codec_name="h264", pix_fmt="yuv420p",
                         width=width, height=height, r_frame_rate="30/1")
            probe = dict(streams=[video, audio], format=dict(duration="60.02"))
            self.assertEqual(prepare.validate_media(probe, height), 60.02)
            for change in ({"height": 360}, {"r_frame_rate": "60/1"}, {"codec_name": "av1"}):
                with self.assertRaises(ValueError):
                    prepare.validate_media(dict(probe, streams=[dict(video, **change), audio]), height)
            with self.assertRaises(ValueError):
                prepare.validate_media(dict(probe, format=dict(duration="1701")), height)
            self.assertIn(f"[height={height}]", prepare.format_selection(height))
            self.assertIn("[fps<=60]", prepare.format_selection(height))

    def test_extended_hd_duration_is_checked(self):
        video = dict(codec_type="video", codec_name="h264", pix_fmt="yuv420p",
                     width=1920, height=1080, r_frame_rate="30/1")
        audio = dict(codec_type="audio", codec_name="aac", channels=2)
        for seconds in (300, 600):
            probe = dict(streams=[video, audio], format=dict(duration=str(seconds + 0.02)))
            self.assertAlmostEqual(prepare.validate_media(probe, 1080, seconds), seconds + 0.02)
            for duration in (60, seconds - 2, seconds + 2):
                with self.assertRaises(ValueError):
                    prepare.validate_media(dict(probe, format=dict(duration=str(duration))), 1080, seconds)


if __name__ == "__main__":
    unittest.main()
