#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded live transport A/B. No signed URLs/tokens in output or on disk."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import time
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location("adaptive", Path(__file__).with_name("probe-youtube-adaptive.py"))
adaptive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adaptive)
ANDROID = "com.google.android.youtube/21.26.364 (Linux; U; Android 11) gzip"
CAPACITY = 512 * 1024
VARIANTS = (("get-android", "GET", ANDROID, False),
            ("get-visionos", "GET", adaptive.AGENT, False),
            ("post-visionos", "POST", adaptive.AGENT, False),
            ("post-visionos-rn", "POST", adaptive.AGENT, True))


def run(video_id, sessions):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), adaptive.NoRedirect())
    failures = 0
    for session in range(sessions):
        player, cpn = adaptive.extract(opener, video_id)
        formats = player.get("streamingData", {}).get("adaptiveFormats", [])
        video = [f for f in formats if f.get("height") == 1080 and 0 < f.get("fps", 0) <= 30
                 and "avc1." in f.get("mimeType", "") and adaptive.media_url(f.get("url"))]
        audio = [f for f in formats if "mp4a." in f.get("mimeType", "")
                 and 0 < f.get("bitrate", 0) <= 192000 and adaptive.media_url(f.get("url"))]
        audio.sort(key=lambda f: (f.get("audioTrack", {}).get("audioIsDefault", True), f.get("bitrate", 0)), reverse=True)
        if not video or not audio:
            raise ValueError("no compatible pair")
        for kind, track in (("video", video[0]), ("audio", audio[0])):
            total = int(track["contentLength"])
            if not 0 < total <= (1 << 63) - 1:
                raise ValueError("invalid track length")
            url = track["url"] + "&cpn=" + cpn
            # Non-monotonic order simulates distant seeks and backtracking.
            offsets = [0, total // 3, 2 * total // 3, total // 10, max(0, total - CAPACITY), 0]
            for index, start in enumerate(offsets):
                end = min(total - 1, start + CAPACITY - 1)
                expected = f"bytes {start}-{end}/{total}"
                reference = None
                for step in range(len(VARIANTS)):
                    name, method, agent, rn = VARIANTS[(step + index + session) % len(VARIANTS)]
                    target = url + (f"&rn={index}" if rn else "")
                    request = urllib.request.Request(target, data=b"\x78\x00" if method == "POST" else None,
                        headers={"User-Agent": agent, "Range": f"bytes={start}-{end}",
                                 "Accept-Encoding": "identity"}, method=method)
                    http, size, valid, same, error = 0, 0, False, None, None
                    begun = time.monotonic()
                    try:
                        with opener.open(request, timeout=8) as response:
                            http = response.status
                            data = response.read(CAPACITY + 1)
                            size = len(data)
                            valid = http == 206 and response.headers.get("Content-Range") == expected and size == end - start + 1
                            if valid:
                                digest = hashlib.sha256(data).digest()
                                if reference is None:
                                    reference = digest
                                same = digest == reference
                    except urllib.error.HTTPError as exc:
                        http = exc.code
                        exc.close()
                    except (urllib.error.URLError, TimeoutError, OSError) as exc:
                        error = type(exc).__name__ # Never stringify exceptions containing URLs.
                    ok = valid and same
                    failures += not ok
                    print(json.dumps({"session": session + 1, "track": kind, "itag": track["itag"],
                        "variant": name, "start": start, "end": end, "http": http, "bytes": size,
                        "range_ok": valid, "same_bytes": same, "error": error,
                        "ms": round((time.monotonic() - begun) * 1000)}), flush=True)
    print(json.dumps({"sessions": sessions, "requests": sessions * 48, "failures": failures}), flush=True)
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video_id", nargs="?", default="bHIhgxav9LY")
    parser.add_argument("--sessions", type=int, choices=(1, 2, 3), default=2)
    args = parser.parse_args()
    try:
        raise SystemExit(bool(run(args.video_id, args.sessions)))
    except Exception as exc:
        raise SystemExit(f"Range probe failed: {type(exc).__name__} (details withheld)") from None
