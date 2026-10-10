#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Original synthetic media: no downloaded or third-party video content.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ffmpeg -hide_banner -loglevel warning -y \
    -f lavfi -i 'testsrc2=size=640x360:rate=30' \
    -f lavfi -i 'sine=frequency=440:sample_rate=48000' \
    -t 20 -c:v libx264 -profile:v baseline -pix_fmt yuv420p -preset medium \
    -crf 24 -g 30 -c:a aac -b:a 96k -ac 2 -af 'volume=0.15' \
    -movflags +faststart "$root/assets/test-clip.mp4"
