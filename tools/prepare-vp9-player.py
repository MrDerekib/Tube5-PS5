#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Remux the already validated VP9 fixture, without re-encoding its video."""
import pathlib
import struct
import subprocess
import tempfile
import json

root = pathlib.Path(__file__).resolve().parents[1]
data = (root / "assets/hw-vp9-altref.t5hc").read_bytes()
assert data[:4] == b"T5HC"
version, width, height, fps, count, flags = struct.unpack_from("<6I", data, 4)
assert (version, width, height, fps, count, flags) == (4, 2560, 1440, 30, 90, 2)
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
    destination = root / "assets/hw-vp9-player.mp4"
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
    print(f"{destination}: same 90 compressed VP9 packets repeated 4 times (12s); AAC audio added")
