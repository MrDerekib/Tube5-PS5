#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Owned fixture for the real player's H.264 B-frame + AAC hardware integration.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ffmpeg -hide_banner -loglevel warning -y \
    -f lavfi -i 'testsrc2=size=1920x1080:rate=30' \
    -f lavfi -i 'sine=frequency=440:sample_rate=48000' \
    -t 20 -c:v libx264 -profile:v high -pix_fmt yuv420p -preset ultrafast \
    -crf 28 -g 30 -bf 2 -c:a aac -b:a 96k -ac 2 -af 'volume=0.15' \
    -movflags +faststart "$root/assets/hw-player-test.mp4"
