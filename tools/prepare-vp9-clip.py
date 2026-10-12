#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate only the needed 3-second visible-frame VP9 hardware fixture."""
import pathlib
import struct
import subprocess
import tempfile
import zlib
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--altref", action="store_true", help="generate the new hidden/superframe fixture only")
options = parser.parse_args()

root = pathlib.Path(__file__).resolve().parents[1]
w, h, count = 2560, 1440, 90
with tempfile.TemporaryDirectory(prefix="tube5-vp9-") as folder:
    ivf = pathlib.Path(folder) / "clip.ivf"
    command = [
        "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
        f"testsrc2=size={w}x{h}:rate=30", "-frames:v", str(count), "-an",
        "-c:v", "libvpx-vp9", "-pix_fmt", "yuv420p", "-profile:v", "0",
        "-deadline", "good" if options.altref else "realtime", "-cpu-used", "4" if options.altref else "8", "-threads", "4", "-row-mt", "1",
        "-lag-in-frames", "25" if options.altref else "0", "-auto-alt-ref", "1" if options.altref else "0", "-g", "90" if options.altref else "30", "-crf", "32",
        "-b:v", "4M" if options.altref else "0", "-colorspace", "bt709", "-color_primaries", "bt709",
        "-color_trc", "bt709", "-color_range", "tv", "-f", "ivf", str(ivf)
    ]
    if options.altref:
        # libvpx's alt-ref mode needs two-pass encoding; keep stats temporary.
        log = str(pathlib.Path(folder) / "pass")
        subprocess.run(command[:-3] + ["-pass", "1", "-passlogfile", log, "-f", "null", "/dev/null"], check=True)
        command = command[:-3] + ["-pass", "2", "-passlogfile", log] + command[-3:]
    subprocess.run(command, check=True)
    data = ivf.read_bytes()
    assert data[:4] == b"DKIF" and data[8:12] == b"VP90"
    packets, offset = [], 32
    hidden, superframes, existing = 0, 0, 0
    while offset < len(data):
        size, timestamp = struct.unpack_from("<IQ", data, offset)
        offset += 12
        packet = data[offset:offset + size]
        assert 0 < size < 1024 * 1024 and len(packet) == size
        # Profile 0, no show_existing_frame, show_frame=1: visible packets only.
        parts = [packet]
        marker = packet[-1]
        if marker & 0xE0 == 0xC0:
            n, magnitude = (marker & 7) + 1, ((marker >> 3) & 3) + 1
            index = len(packet) - 2 - n * magnitude
            assert index > 0 and packet[index] == marker
            parts, start = [], 0
            for j in range(n):
                length = int.from_bytes(packet[index + 1 + j * magnitude:index + 1 + (j + 1) * magnitude], "little")
                assert length > 0 and start + length <= index
                parts.append(packet[start:start + length])
                start += length
            assert start == index
            superframes += 1
        flags = []
        for part in parts:
            assert part[0] & 0xF0 == 0x80
            display = bool(part[0] & 8 or part[0] & 2)
            existing += bool(part[0] & 8)
            hidden += not display
            flags.append(display)
        assert sum(flags) == 1 and flags[-1]
        assert timestamp == len(packets)
        packets.append(packet)
        offset += size
    assert offset == len(data) and len(packets) == count
    if options.altref:
        assert hidden > 0 and superframes > 0, "Encoder did not cover the requested VP9 cases"
    else:
        assert hidden == 0
    decoder = subprocess.Popen([
        "ffmpeg", "-v", "error", "-i", str(ivf), "-f", "rawvideo", "-pix_fmt",
        "nv12", "pipe:1"
    ], stdout=subprocess.PIPE)
    encoded = bytearray(b"T5HC" + struct.pack("<6I", 4 if options.altref else 3, w, h, 30, count, 2))
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
    destination = root / "assets" / ("hw-vp9-altref.t5hc" if options.altref else "hw-vp9-clip.t5hc")
    destination.write_bytes(encoded)
    print(f"{destination}: {count} visible frames, {hidden} hidden, {superframes} superframes, {existing} show-existing; {len(encoded)} bytes; software CRC per frame")
