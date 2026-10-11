#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
# Owned synthetic fixture, not a downloaded YouTube video. One complete Annex B
# access unit: SPS/PPS, AUD and four IDR slices; no B-frame or delayed frame.
ffmpeg -hide_banner -loglevel error -y -f lavfi \
    -i 'testsrc2=size=1920x1080:rate=30' -frames:v 1 -an \
    -c:v libx264 -threads 1 -preset medium -crf 24 -profile:v high -level:v 4.0 \
    -pix_fmt yuv420p -x264-params 'keyint=1:bframes=0:ref=1:slices=4:repeat-headers=1:aud=1' \
    -f h264 assets/hw-frame.h264
ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,profile,width,height,pix_fmt \
    -of default=noprint_wrappers=1 assets/hw-frame.h264
