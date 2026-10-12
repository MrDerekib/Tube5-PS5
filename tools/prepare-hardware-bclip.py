#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate one owned AVC I/P/B fixture with packet times and picture checksums."""
import fractions
import json
import pathlib
import re
import struct
import subprocess
import tempfile
import zlib

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="tube5-bclip-") as temporary:
    raw = pathlib.Path(temporary) / "clip.h264"
    mp4 = pathlib.Path(temporary) / "clip.mp4"
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "testsrc2=size=1920x1080:rate=30", "-map", "0:v", "-frames:v", "150",
        "-vf", "drawtext=text='FRAME %{n}':x=70:y=80:fontsize=80:fontcolor=white:box=1:boxcolor=black",
        "-an", "-c:v", "libx264", "-threads", "2", "-preset", "veryfast", "-crf", "28",
        "-profile:v", "high", "-level:v", "4.0", "-pix_fmt", "yuv420p",
        "-flags", "+global_header", "-x264-params",
        "keyint=30:min-keyint=30:scenecut=0:bframes=2:b-adapt=0:b-pyramid=none:ref=1:slices=4:repeat-headers=1:aud=1",
        "-f", "tee", f"[f=h264]{raw}|[f=mp4]{mp4}",
    ], check=True)
    probe = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_packets", "-show_frames", "-show_streams",
        "-show_entries", "packet=pts,dts:frame=pict_type:stream=time_base", "-of", "json", str(mp4)
    ]))
    records = probe["packets_and_frames"]
    types = [record["pict_type"] for record in records if record["type"] == "frame"]
    packets = [record for record in records if record["type"] == "packet"]
    base = fractions.Fraction(probe["streams"][0]["time_base"])
    pts = [fractions.Fraction(packet["pts"]) * base * 30 for packet in packets]
    if len(types) != 150 or "B" not in types or len(packets) != 150 or \
            any(value.denominator != 1 for value in pts) or sorted(pts) != list(range(150)):
        raise RuntimeError("Expected 150 I/P/B frames and a complete presentation-index mapping")
    decoder = subprocess.Popen([
        "ffmpeg", "-v", "error", "-i", str(mp4), "-f", "rawvideo", "-pix_fmt", "yuv420p", "pipe:1"
    ], stdout=subprocess.PIPE)
    crc = []
    for _ in range(150):
        frame = bytearray()
        while len(frame) < 1920 * 1080 * 3 // 2:
            block = decoder.stdout.read(1920 * 1080 * 3 // 2 - len(frame))
            if not block:
                raise RuntimeError("Truncated software picture reference")
            frame.extend(block)
        samples = bytes(frame[y * 1920 + x] for y in range(0, 1080, 31) for x in range(0, 1920, 31))
        crc.append(zlib.crc32(samples))
    if decoder.stdout.read(1) or decoder.wait() != 0:
        raise RuntimeError("Unexpected reference picture count")
    data = raw.read_bytes()
    offsets = [match.start() for match in re.finditer(b"(?:\x00\x00\x00\x01|\x00\x00\x01)\x09", data)]
    if len(offsets) != 150:
        raise RuntimeError(f"Expected 150 AUD-delimited packets, got {len(offsets)}")
    offsets[0] = 0  # Keep the muxer's initial SPS/PPS prefix before AUD in access unit zero.
    offsets.append(len(data))
    destination = root / "assets" / "hw-bclip.t5hc"
    with destination.open("wb") as output:
        output.write(b"T5HC" + struct.pack("<6I", 2, 1920, 1080, 30, 150, 1))
        for i in range(150):
            packet = data[offsets[i]:offsets[i + 1]]
            if not packet or len(packet) >= 1024 * 1024:
                raise RuntimeError("Input slot capacity exceeded")
            output.write(struct.pack("<4I", len(packet), int(pts[i]), i, crc[int(pts[i])]))
            output.write(packet)
    print(f"{destination}: I={types.count('I')} P={types.count('P')} B={types.count('B')}, {destination.stat().st_size} bytes")
