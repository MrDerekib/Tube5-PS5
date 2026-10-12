#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Remux the already validated VP9 fixture, without re-encoding its video."""
import pathlib
import struct
import subprocess
import tempfile
import json
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--4k", dest="four_k", action="store_true")
parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
parser.add_argument("--tile-columns", type=int, choices=(0, 1, 2, 3), default=None)
options = parser.parse_args()

root = pathlib.Path(__file__).resolve().parents[1]
if options.fps == 60 and not options.four_k:
    parser.error("The current 60fps control is 4K only")
name = "hw-vp9-4k60.t5hc" if options.fps == 60 else "hw-vp9-4k.t5hc" if options.four_k else "hw-vp9-altref.t5hc"
if options.tile_columns is not None:
    if not options.four_k or options.fps != 60:
        parser.error("The tile-layout control is currently 4K60 only")
    name = pathlib.Path(name).stem + f"-tc{options.tile_columns}.t5hc"
data = (root / "assets" / name).read_bytes()
assert data[:4] == b"T5HC"
version, width, height, fps, count, flags = struct.unpack_from("<6I", data, 4)
expected = (6, 3840, 2160, 60, 180, 2) if options.fps == 60 else (5, 3840, 2160, 30, 90, 2) if options.four_k else (4, 2560, 1440, 30, 90, 2)
assert (version, width, height, fps, count, flags) == expected
with tempfile.TemporaryDirectory(prefix="tube5-vp9-player-") as folder:
    ivf = pathlib.Path(folder) / "clip.ivf"
    encoded = bytearray(struct.pack("<4sHH4sHHIIII", b"DKIF", 0, 32, b"VP90", width, height, fps, 1, count, 0))
    offset = 28
    for index in range(count):
        size, pts, dts, crc = struct.unpack_from("<4I", data, offset)
        offset += 16
        assert pts == dts == index and size > 0 and offset + size <= len(data)
        encoded.extend(struct.pack("<IQ", size, index))
        encoded.extend(data[offset:offset + size])
        offset += size
    assert offset == len(data)
    ivf.write_bytes(encoded)
    destination = root / "assets" / ("hw-vp9-4k60-player.mp4" if options.fps == 60 else "hw-vp9-4k-player.mp4" if options.four_k else "hw-vp9-player.mp4")
    if options.tile_columns is not None:
        destination = destination.with_name(destination.stem + f"-tc{options.tile_columns}" + destination.suffix)
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-stream_loop", "3", "-i", str(ivf), "-f", "lavfi", "-i",
        "sine=frequency=440:sample_rate=48000", "-map", "0:v:0", "-map", "1:a:0",
        "-c:v", "copy", "-c:a", "aac", "-b:a", "96k", "-ac", "2",
        "-af", "volume=0.15", "-t", "12", "-movflags", "+faststart", str(destination)
    ], check=True)
    def hashes(path):
        return json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_packets",
            "-show_data_hash", "sha256", "-show_entries", "packet=data_hash",
            "-of", "json", str(path)]))["packets"]
    assert hashes(ivf) * 4 == hashes(destination)
    print(f"{destination}: same {count} compressed VP9 packets repeated 4 times (12s), {fps}fps; AAC audio added")
