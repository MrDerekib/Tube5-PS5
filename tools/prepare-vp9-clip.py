#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate only the needed 3-second visible-frame VP9 hardware fixture."""
import pathlib
import struct
import subprocess
import tempfile
import zlib
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument("--altref", action="store_true", help="generate the new hidden/superframe fixture only")
parser.add_argument("--4k", dest="four_k", action="store_true", help="generate only the new 4K visible-frame fixture")
parser.add_argument("--fps", type=int, choices=(30, 60), default=30)
parser.add_argument("--tile-columns", type=int, choices=(0, 1, 2, 3), default=None, help="log2 of tile-column count; 2 means four columns")
options = parser.parse_args()
if options.four_k and options.altref:
    parser.error("The first 4K proof is visible-frame only")
if options.fps == 60 and not options.four_k:
    parser.error("The current 60fps proof is 4K only")
if options.tile_columns is not None and (not options.four_k or options.fps != 60):
    parser.error("The tile-layout control is currently 4K60 only")

root = pathlib.Path(__file__).resolve().parents[1]
w, h = (3840, 2160) if options.four_k else (2560, 1440)
fps, count = options.fps, options.fps * 3
with tempfile.TemporaryDirectory(prefix="tube5-vp9-") as folder:
    ivf = pathlib.Path(folder) / "clip.ivf"
    command = [
        "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
        f"testsrc2=size={w}x{h}:rate={fps}", "-frames:v", str(count), "-an",
        "-c:v", "libvpx-vp9", "-pix_fmt", "yuv420p", "-profile:v", "0",
        "-deadline", "good" if options.altref else "realtime", "-cpu-used", "4" if options.altref else "8", "-threads", "4", "-row-mt", "1",
        "-lag-in-frames", "25" if options.altref else "0", "-auto-alt-ref", "1" if options.altref else "0", "-g", "90" if options.altref else "30", "-crf", "32",
        "-b:v", "4M" if options.altref else "0", "-colorspace", "bt709", "-color_primaries", "bt709",
        "-color_trc", "bt709", "-color_range", "tv", "-f", "ivf", str(ivf)
    ]
    if options.tile_columns is not None:
        command = command[:-3] + ["-tile-columns", str(options.tile_columns)] + command[-3:]
    if options.altref:
        # libvpx's alt-ref mode needs two-pass encoding; keep stats temporary.
        log = str(pathlib.Path(folder) / "pass")
        subprocess.run(command[:-3] + ["-pass", "1", "-passlogfile", log, "-f", "null", "/dev/null"], check=True)
        command = command[:-3] + ["-pass", "2", "-passlogfile", log] + command[-3:]
    subprocess.run(command, check=True)
    if options.tile_columns is not None:
        trace = subprocess.run([
            "ffmpeg", "-v", "trace", "-i", str(ivf), "-map", "0:v:0", "-c:v", "copy",
            "-bsf:v", "trace_headers", "-f", "null", "-"
        ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=True)
        columns = [int(value) for value in re.findall(r"tile_cols_log2[^\n]*= (\d+)", trace.stderr)]
        assert len(columns) == count and set(columns) == {options.tile_columns}, "Header did not match requested tile layout"
        print(f"Verified {count} headers: tile_cols_log2={options.tile_columns}, {1 << options.tile_columns} columns")
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
    encoded = bytearray(b"T5HC" + struct.pack("<6I", 6 if fps == 60 else 5 if options.four_k else 4 if options.altref else 3, w, h, fps, count, 2))
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
    assert len(encoded) < (32 if fps == 60 else 16) * 1024 * 1024
    destination = root / "assets" / ("hw-vp9-4k60.t5hc" if fps == 60 else "hw-vp9-4k.t5hc" if options.four_k else "hw-vp9-altref.t5hc" if options.altref else "hw-vp9-clip.t5hc")
    if options.tile_columns is not None:
        destination = destination.with_name(destination.stem + f"-tc{options.tile_columns}" + destination.suffix)
    destination.write_bytes(encoded)
    print(f"{destination}: {count} visible frames, {hidden} hidden, {superframes} superframes, {existing} show-existing; {len(encoded)} bytes; software CRC per frame")
