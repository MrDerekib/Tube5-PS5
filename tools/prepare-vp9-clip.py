#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate only the needed 3-second visible-frame VP9 hardware fixture."""
import pathlib
import struct
import subprocess
import tempfile
import zlib

root = pathlib.Path(__file__).resolve().parents[1]
w, h, count = 2560, 1440, 90
with tempfile.TemporaryDirectory(prefix="tube5-vp9-") as folder:
    ivf = pathlib.Path(folder) / "clip.ivf"
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
        f"testsrc2=size={w}x{h}:rate=30", "-frames:v", str(count), "-an",
        "-c:v", "libvpx-vp9", "-pix_fmt", "yuv420p", "-profile:v", "0",
        "-deadline", "realtime", "-cpu-used", "8", "-threads", "4", "-row-mt", "1",
        "-lag-in-frames", "0", "-auto-alt-ref", "0", "-g", "30", "-crf", "32",
        "-b:v", "0", "-colorspace", "bt709", "-color_primaries", "bt709",
        "-color_trc", "bt709", "-color_range", "tv", "-f", "ivf", str(ivf)
    ], check=True)
    data = ivf.read_bytes()
    assert data[:4] == b"DKIF" and data[8:12] == b"VP90"
    packets, offset = [], 32
    while offset < len(data):
        size, timestamp = struct.unpack_from("<IQ", data, offset)
        offset += 12
        packet = data[offset:offset + size]
        assert 0 < size < 1024 * 1024 and len(packet) == size
        # Profile 0, no show_existing_frame, show_frame=1: visible packets only.
        assert packet[0] & 0xFA == 0x82
        assert timestamp == len(packets)
        packets.append(packet)
        offset += size
    assert offset == len(data) and len(packets) == count
    decoder = subprocess.Popen([
        "ffmpeg", "-v", "error", "-i", str(ivf), "-f", "rawvideo", "-pix_fmt",
        "nv12", "pipe:1"
    ], stdout=subprocess.PIPE)
    encoded = bytearray(b"T5HC" + struct.pack("<6I", 3, w, h, 30, count, 2))
    for index, packet in enumerate(packets):
        frame = bytearray()
        needed = w * h * 3 // 2
        while len(frame) < needed:
            block = decoder.stdout.read(needed - len(frame))
            if not block:
                raise RuntimeError("Truncated reference frame")
            frame.extend(block)
        samples = bytes(frame[y * w + x] for y in range(0, h, 31) for x in range(0, w, 31))
        encoded.extend(struct.pack("<4I", len(packet), index, index, zlib.crc32(samples)))
        encoded.extend(packet)
    assert not decoder.stdout.read(1) and decoder.wait() == 0
    assert len(encoded) < 16 * 1024 * 1024
    destination = root / "assets" / "hw-vp9-clip.t5hc"
    destination.write_bytes(encoded)
    print(f"{destination}: {count} visible frames, {len(encoded)} bytes; software CRC per frame")
