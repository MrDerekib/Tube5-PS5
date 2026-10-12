#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the owned five-second AVC fixture with bounded, length-prefixed AUs."""
import pathlib
import json
import re
import struct
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tube5-hwclip-") as temporary:
    raw = pathlib.Path(temporary) / "clip.h264"
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "testsrc2=size=1920x1080:rate=30", "-frames:v", "150", "-an",
        "-c:v", "libx264", "-threads", "2", "-preset", "veryfast", "-crf", "28",
        "-profile:v", "high", "-level:v", "4.0", "-pix_fmt", "yuv420p",
        "-x264-params", "keyint=30:min-keyint=30:scenecut=0:bframes=0:ref=1:slices=4:repeat-headers=1:aud=1",
        "-f", "h264", str(raw),
    ], check=True)
    data = raw.read_bytes()
    decoded = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_entries", "frame=pict_type", "-of", "json", str(raw)
    ]))
    types = [frame["pict_type"] for frame in decoded["frames"]]
    if len(types) != 150 or types.count("I") != 5 or types.count("P") != 145:
        raise RuntimeError("Expected five keyframes and 145 dependent P frames, without B frames")
    offsets = [match.start() for match in re.finditer(b"(?:\x00\x00\x00\x01|\x00\x00\x01)\x09", data)]
    if len(offsets) != 150 or offsets[0] != 0:
        raise RuntimeError("Expected 150 Annex B access units starting with AUD")
    offsets.append(len(data))
    packets = [data[offsets[i]:offsets[i + 1]] for i in range(150)]
    if any(not packet or len(packet) >= 1024 * 1024 for packet in packets):
        raise RuntimeError("Access unit exceeds the native input slot")
    destination = root / "assets" / "hw-clip.t5hc"
    with destination.open("wb") as output:
        output.write(b"T5HC" + struct.pack("<6I", 1, 1920, 1080, 30, 150, 0))
        for packet in packets:
            output.write(struct.pack("<I", len(packet)))
            output.write(packet)
    print(f"{destination}: 150 frames, 30 fps, 5 seconds, {destination.stat().st_size} bytes")
