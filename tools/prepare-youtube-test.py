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
WIDTHS = {360: 640, 720: 1280, 1080: 1920}


def format_selection(height):
    if height == 360:
        return FORMAT
    width = WIDTHS[height]
    return (f"bv[ext=mp4][vcodec^=avc1][height={height}][width<={width}]"
            "[fps<=60][protocol=https]+ba[ext=m4a][protocol=https]")


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


def validate_media(probe, height=360, seconds=60):
    video = [s for s in probe["streams"] if s["codec_type"] == "video"]
    audio = [s for s in probe["streams"] if s["codec_type"] == "audio"]
    if len(video) != 1 or len(audio) != 1:
        raise ValueError("Expected one video stream and one audio stream")
    v, a = video[0], audio[0]
    if (v["codec_name"] != "h264" or v.get("pix_fmt") != "yuv420p" or
            not 0 < v["width"] <= WIDTHS[height] or not 0 < v["height"] <= height or
            not 0 < Fraction(v["r_frame_rate"]) <= 30):
        raise ValueError(f"Expected H.264 YUV420, at most {WIDTHS[height]}x{height} and 30 fps")
    if height != 360 and v["height"] != height:
        raise ValueError("Resolution test must have the requested native height")
    if a["codec_name"] != "aac" or not 0 < a["channels"] <= 2:
        raise ValueError("Expected mono/stereo AAC audio")
    duration = float(probe["format"]["duration"])
    if not 20 <= duration <= 3600:
        raise ValueError("Test media must be between 20 seconds and one hour")
    if height != 360 and not seconds - 1 <= duration <= seconds + 1:
        raise ValueError(f"Resolution test must last approximately {seconds} seconds")
    return duration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--height", type=int, choices=tuple(WIDTHS), default=360,
                        help="720/1080 prepare an H.264 Main 30-fps resolution test")
    parser.add_argument("--seconds", type=int, choices=(60, 300, 600), default=60,
                        help="HD test length; ignored for the full 360p pilot")
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
        resolution_test = args.height != 360
        if resolution_test and duration < args.seconds:
            raise ValueError("Source is shorter than the requested HD test")
        final = cache / (f"resolution-{args.height}p-{args.seconds}s.mp4" if resolution_test else "media.mp4")
        if final.exists():
            raise ValueError(f"Prepared media already exists; use it without downloading again: {final}")
        print(f"Preparing: {info.get('title', identifier)} ({duration} s)", flush=True)
        with tempfile.TemporaryDirectory(prefix="prepare-", dir=cache) as temporary:
            work = Path(temporary)
            downloaded = work / "download.mp4"
            # For HD tests request only the selected section. Full-source filesize
            # estimates must not reject a short section of a much larger track.
            limits = (["--download-sections", f"*0-{args.seconds}"] if resolution_test
                      else ["--max-filesize", "128M"])
            subprocess.run(base + ["--no-progress"] + limits + ["-S", "lang",
                                   "-f", format_selection(args.height), "--merge-output-format", "mp4", "-o",
                                   str(work / "download.%(ext)s"), canonical], check=True, timeout=1200)
            byte_limit = (64 * (args.seconds // 60) if resolution_test else 256) * 1024 * 1024
            if not downloaded.is_file() or downloaded.stat().st_size > byte_limit:
                raise ValueError(f"Download missing or exceeds the {byte_limit // (1024 * 1024)} MiB pilot limit")
            prepared = work / "media.mp4"
            # Default pilot retains YouTube's codecs. HD tests normalize video
            # to Main/30fps without scaling; the audio track is copied intact.
            video_options = (["-t", str(args.seconds), "-vf", "fps=30", "-c:v", "libx264",
                              "-threads", "2", "-preset", "fast", "-crf", "23",
                              "-profile:v", "main", "-level:v", "3.1" if args.height == 720 else "4.0",
                              "-pix_fmt", "yuv420p", "-c:a", "copy"]
                             if resolution_test else ["-c", "copy"])
            subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-n", "-i", str(downloaded),
                            "-map", "0:v:0", "-map", "0:a:0"] + video_options + ["-movflags",
                            "+faststart", str(prepared)], check=True, timeout=1200)
            probe = json.loads(subprocess.check_output(
                ["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json",
                 str(prepared)], text=True, timeout=30))
            validate_media(probe, args.height, args.seconds)
            if prepared.stat().st_size > byte_limit:
                raise ValueError("Prepared file exceeds the pilot size limit")
            metadata = {"id": identifier, "title": info.get("title", identifier),
                        "url": canonical, "duration": float(probe["format"]["duration"]),
                        "bytes": prepared.stat().st_size,
                        "mode": "native-hd-reencoded-30fps-pilot" if resolution_test else "downloaded-remuxed-pilot",
                        "height": args.height}
            # No video or extraction metadata goes into tracked assets/dist.
            # Same filesystem: publish without ever overwriting another run's
            # prepared media. TemporaryDirectory removes only its own link.
            final.hardlink_to(prepared)
            metadata_file = final.with_suffix(".json") if resolution_test else cache / "metadata.json"
            metadata_file.write_text(json.dumps(metadata, ensure_ascii=False, indent=2)
                                                + "\n", encoding="utf-8")
        print(f"Prepared MP4: {final}", flush=True)
        print("Start the existing HTTP server with --clip pointing to this MP4. "
              "The PS5 URL stays /test-clip.mp4.", flush=True)
    except (ValueError, KeyError, subprocess.SubprocessError, OSError) as error:
        parser.exit(1, f"Preparation failed: {error}\n")


if __name__ == "__main__":
    main()
