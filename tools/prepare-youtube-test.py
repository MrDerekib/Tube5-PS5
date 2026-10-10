#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare one private YouTube test MP4; not a streaming proxy or public service."""
import argparse
from fractions import Fraction
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
FORMAT = ("bv[ext=mp4][vcodec^=avc1][height<=360][width<=640][fps<=30][protocol=https]"
          "+ba[ext=m4a][protocol=https]/"
          "b[ext=mp4][vcodec^=avc1][height<=360][width<=640][fps<=30][protocol=https]")


def youtube_id(value):
    url = urlsplit(value)
    if url.scheme != "https" or url.username or url.password or url.port:
        raise ValueError("Use a normal HTTPS YouTube URL")
    if url.hostname == "youtu.be":
        identifier = url.path.removeprefix("/")
    elif url.hostname in ("youtube.com", "www.youtube.com", "m.youtube.com") and url.path == "/watch":
        values = parse_qs(url.query).get("v", [])
        identifier = values[0] if len(values) == 1 else ""
    else:
        raise ValueError("Only YouTube watch URLs and youtu.be links are accepted")
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", identifier):
        raise ValueError("Invalid YouTube video ID")
    return identifier


def validate_media(probe):
    video = [s for s in probe["streams"] if s["codec_type"] == "video"]
    audio = [s for s in probe["streams"] if s["codec_type"] == "audio"]
    if len(video) != 1 or len(audio) != 1:
        raise ValueError("Expected one video stream and one audio stream")
    v, a = video[0], audio[0]
    if (v["codec_name"] != "h264" or v.get("pix_fmt") != "yuv420p" or
            not 0 < v["width"] <= 640 or not 0 < v["height"] <= 360 or
            not 0 < Fraction(v["r_frame_rate"]) <= 30):
        raise ValueError("Expected H.264 YUV420, at most 640x360 and 30 fps")
    if a["codec_name"] != "aac" or not 0 < a["channels"] <= 2:
        raise ValueError("Expected mono/stereo AAC audio")
    duration = float(probe["format"]["duration"])
    if not 20 <= duration <= 3600:
        raise ValueError("Test media must be between 20 seconds and one hour")
    return duration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    args = parser.parse_args()
    try:
        identifier = youtube_id(args.url)
        for tool in ("yt-dlp", "ffmpeg", "ffprobe"):
            if not shutil.which(tool):
                raise ValueError(f"Missing developer tool: {tool}")
        canonical = f"https://www.youtube.com/watch?v={identifier}"
        base = ["yt-dlp", "--ignore-config", "--no-playlist", "--no-cache-dir",
                "--socket-timeout", "15", "--retries", "2", "--fragment-retries", "2"]
        # Never load browser cookies/accounts or persist signed CDN URLs.
        info = json.loads(subprocess.check_output(
            base + ["--skip-download", "--dump-single-json", canonical], text=True, timeout=90))
        duration = info.get("duration")
        if (info.get("id") != identifier or info.get("is_live") or
                info.get("live_status") not in (None, "not_live", "was_live") or
                not isinstance(duration, (int, float)) or not 20 <= duration <= 3600):
            raise ValueError("Expected a non-live video between 20 seconds and one hour")
        cache = ROOT / ".local" / "youtube" / identifier
        cache.mkdir(parents=True, exist_ok=True)
        final = cache / "media.mp4"
        if final.exists():
            raise ValueError(f"Prepared media already exists; use it without downloading again: {final}")
        print(f"Preparing: {info.get('title', identifier)} ({duration} s)", flush=True)
        with tempfile.TemporaryDirectory(prefix="prepare-", dir=cache) as temporary:
            work = Path(temporary)
            downloaded = work / "download.mp4"
            subprocess.run(base + ["--no-progress", "--max-filesize", "128M", "-S", "lang",
                                   "-f", FORMAT, "--merge-output-format", "mp4", "-o",
                                   str(work / "download.%(ext)s"), canonical], check=True, timeout=600)
            if not downloaded.is_file() or downloaded.stat().st_size > 256 * 1024 * 1024:
                raise ValueError("Download missing or exceeds the 256 MiB pilot limit")
            prepared = work / "media.mp4"
            # Remux only: retain YouTube's H.264/AAC, put metadata before media
            # so the PS5 can probe quickly and perform HTTP range seeks.
            subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-n", "-i", str(downloaded),
                            "-map", "0:v:0", "-map", "0:a:0", "-c", "copy", "-movflags",
                            "+faststart", str(prepared)], check=True, timeout=120)
            probe = json.loads(subprocess.check_output(
                ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json",
                 str(prepared)], text=True, timeout=30))
            validate_media(probe)
            metadata = {"id": identifier, "title": info.get("title", identifier),
                        "url": canonical, "duration": float(probe["format"]["duration"]),
                        "bytes": prepared.stat().st_size, "mode": "downloaded-remuxed-pilot"}
            # No video or extraction metadata goes into tracked assets/dist.
            # Same filesystem: publish without ever overwriting another run's
            # prepared media. TemporaryDirectory removes only its own link.
            final.hardlink_to(prepared)
            (cache / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2)
                                                + "\n", encoding="utf-8")
        print(f"Prepared MP4: {final}", flush=True)
        print("Start the existing HTTP server with --clip pointing to this MP4. "
              "The PS5 URL stays /test-clip.mp4.", flush=True)
    except (ValueError, KeyError, subprocess.SubprocessError, OSError) as error:
        parser.exit(1, f"Preparation failed: {error}\n")


if __name__ == "__main__":
    main()
