#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
# One newly needed, owned VP9 Profile 0 picture. At cpu-used=8 this takes
# less than a second on the development host; no companion or transcoding
# service is involved in the eventual console playback path.
mkdir -p .local
ffmpeg -hide_banner -loglevel error -y -f lavfi \
    -i 'testsrc2=size=2560x1440:rate=30' -frames:v 1 -an \
    -c:v libvpx-vp9 -pix_fmt yuv420p -profile:v 0 -deadline realtime -cpu-used 8 \
    -threads 4 -row-mt 1 -lag-in-frames 0 -auto-alt-ref 0 -g 1 -crf 32 -b:v 0 \
    -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv \
    -f ivf .local/hw-vp9-1440.ivf
# IVF produced by FFmpeg: 32-byte file header + 12-byte first packet header.
dd if=.local/hw-vp9-1440.ivf of=assets/hw-vp9-1440.frame bs=1 skip=44 status=none
ffmpeg -v error -i .local/hw-vp9-1440.ivf -frames:v 1 -f rawvideo -pix_fmt nv12 pipe:1 |
    python3 -c 'import sys,zlib
w,h=2560,1440
data=sys.stdin.buffer.read()
assert len(data)==w*h*3//2
samples=bytearray()
for y in range(0,h,31):
    for x in range(0,w,31):
        c=w*h+(y//2)*w+(x//2)*2
        samples.extend((data[y*w+x],data[c],data[c+1]))
print("%08X" % zlib.crc32(samples))' | tee assets/hw-vp9-1440.crc
ffprobe -v error -show_entries stream=codec_name,profile,width,height,pix_fmt \
    -of default=noprint_wrappers=1 .local/hw-vp9-1440.ivf
