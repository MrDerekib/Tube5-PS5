#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Requires upstream SDL_kitchensink 2.0.0-a2 plus PacBrew's video timing patch.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
source_root=.deps/host/SDL_kitchensink
[[ $(git -C "$source_root" rev-parse HEAD) == c1bac5287e14122b2cf886628bdb802cf3737d83 ]]
if ! rg -q 'double pts = Kit_GetCurrentPTS\(decoder\) \+ 0\.1;' "$source_root/src/internal/video/kitvideo.c"; then
    echo 'Apply tools/patches/kitchensink-video-timing.patch to the pinned SDL_kitchensink checkout first.' >&2
    exit 1
fi
cmake -S "$source_root" -B build/host-kitchensink -G Ninja \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED=OFF -DBUILD_STATIC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-kitchensink -j 8
read -r -a cflags <<< "$(pkg-config --cflags sdl2 libavformat libavcodec libswscale libswresample libavutil libass)"
read -r -a libs <<< "$(pkg-config --libs sdl2 libavformat libavcodec libswscale libswresample libavutil libass)"
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_DIAGNOSTICS \
    -Isrc "${cflags[@]}" tests/test_texture_diagnostics.cpp src/texture_diagnostics.cpp \
    "${libs[@]}" -o build/texture-diagnostics-test
build/texture-diagnostics-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_DIAGNOSTICS \
    -Isrc "${cflags[@]}" tests/test_codec_diagnostics.cpp src/codec_diagnostics.cpp \
    "${libs[@]}" -o build/codec-diagnostics-test
build/codec-diagnostics-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_DIAGNOSTICS \
    -Isrc "${cflags[@]}" tests/test_thread_diagnostics.cpp src/thread_diagnostics.cpp \
    "${libs[@]}" -o build/thread-diagnostics-test
build/thread-diagnostics-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTUBE5_HOST -Isrc -I"$source_root/include" "${cflags[@]}" tests/media_smoke.cpp src/media_player.cpp src/codec_diagnostics.cpp src/thread_diagnostics.cpp src/boot_diagnostics.cpp \
    build/host-kitchensink/libSDL_kitchensink_static.a "${libs[@]}" -o build/media-smoke
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/media-smoke
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_HOST -DTUBE5_OFFSCREEN -ffunction-sections -fdata-sections \
    -Isrc -I"$source_root/include" "${cflags[@]}" src/main.cpp src/demo_renderer.cpp src/media_player.cpp src/codec_diagnostics.cpp src/thread_diagnostics.cpp src/boot_diagnostics.cpp src/native_sdl.cpp \
    build/host-kitchensink/libSDL_kitchensink_static.a "${libs[@]}" -Wl,--gc-sections -o build/tube5-host
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TUBE5_PREVIEW=build/ui-preview.bmp build/tube5-host
