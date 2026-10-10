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
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_DIAGNOSTICS \
    -Isrc "${cflags[@]}" tests/test_pipeline_diagnostics.cpp src/pipeline_diagnostics.cpp src/video_sync_diagnostics.cpp src/video_conversion.cpp \
    "${libs[@]}" -o build/pipeline-diagnostics-test
build/pipeline-diagnostics-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTUBE5_CONVERSION_WRAP -Isrc "${cflags[@]}" tests/test_video_conversion.cpp src/video_conversion.cpp \
    "${libs[@]}" -o build/video-conversion-test
build/video-conversion-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTUBE5_NETWORK_WRAP -Isrc "${cflags[@]}" tests/test_network_source.cpp src/network_source.cpp src/boot_diagnostics.cpp \
    "${libs[@]}" -Wl,--wrap=SDL_GetTicks64 -o build/network-source-test
build/network-source-test
clang -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -D_DEFAULT_SOURCE -DTUBE5_HOST -DTUBE5_NETWORK_TEST -DTUBE5_NETWORK_SOCKET_MOCK \
    -Dgetsockopt=tube5_test_getsockopt -Dsetsockopt=tube5_test_setsockopt \
    tests/test_native_network.c src/native_network.c -o build/native-network-test
build/native-network-test
clang -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -D_DEFAULT_SOURCE -DTUBE5_HOST -DTUBE5_NETWORK_TEST \
    -c src/native_network.c -o build/native-network-host.o
native_network_wraps=-Wl,--wrap=getaddrinfo,--wrap=freeaddrinfo,--wrap=gai_strerror,--wrap=getnameinfo,--wrap=gethostbyname,--wrap=fcntl,--wrap=connect,--wrap=strcasestr,--wrap=isatty
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -Isrc tests/test_native_framebuffer.cpp src/native_framebuffer.cpp -o build/native-framebuffer-test
build/native-framebuffer-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_DIAGNOSTICS \
    -Isrc tests/test_memory_diagnostics.cpp src/memory_diagnostics.cpp -o build/memory-diagnostics-test
build/memory-diagnostics-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTUBE5_AUDIO_WRAP -Isrc -I"$source_root/include" "${cflags[@]}" \
    tests/test_audio_reader.cpp src/audio_reader.cpp \
    build/host-kitchensink/libSDL_kitchensink_static.a "${libs[@]}" \
    -Wl,--wrap=Kit_GetTimerElapsed -o build/audio-reader-test
build/audio-reader-test
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -DTUBE5_AUDIO_WRAP -DTUBE5_HOST -DTUBE5_DIAGNOSTICS -DTUBE5_PIPELINE_WRAP -DTUBE5_CONVERSION_WRAP -DTUBE5_NETWORK_WRAP -Isrc -I"$source_root/include" "${cflags[@]}" tests/media_smoke.cpp src/audio_reader.cpp src/media_player.cpp src/codec_diagnostics.cpp src/thread_diagnostics.cpp src/boot_diagnostics.cpp src/pipeline_diagnostics.cpp src/memory_diagnostics.cpp src/video_sync_diagnostics.cpp src/video_conversion.cpp src/network_source.cpp \
    build/native-network-host.o build/host-kitchensink/libSDL_kitchensink_static.a "${libs[@]}" "$native_network_wraps" \
    -Wl,--wrap=Kit_GetAudioDecoderData,--wrap=av_read_frame,--wrap=avcodec_send_packet,--wrap=avcodec_receive_frame,--wrap=avformat_seek_file,--wrap=sws_scale_frame,--wrap=Kit_WritePacketBuffer,--wrap=Kit_FindAVPixelFormat,--wrap=avformat_open_input,--wrap=_ZN5tube522network_playback_errorEv -o build/media-smoke
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/media-smoke
python3 -m unittest discover -s tests -p test_http_test_server.py -v
python3 -m unittest discover -s tests -p test_youtube_prepare.py -v
clang++ -std=c++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DTUBE5_AUDIO_WRAP -DTUBE5_HOST -DTUBE5_OFFSCREEN -DTUBE5_CONVERSION_WRAP -DTUBE5_NETWORK_WRAP -ffunction-sections -fdata-sections \
    -Isrc -I"$source_root/include" "${cflags[@]}" src/main.cpp src/audio_reader.cpp src/demo_renderer.cpp src/media_player.cpp src/codec_diagnostics.cpp src/thread_diagnostics.cpp src/boot_diagnostics.cpp src/native_sdl.cpp src/pipeline_diagnostics.cpp src/memory_diagnostics.cpp src/video_sync_diagnostics.cpp src/video_conversion.cpp src/network_source.cpp \
    build/native-network-host.o build/host-kitchensink/libSDL_kitchensink_static.a "${libs[@]}" "$native_network_wraps" -Wl,--gc-sections,--wrap=Kit_GetAudioDecoderData,--wrap=sws_scale_frame,--wrap=Kit_WritePacketBuffer,--wrap=Kit_FindAVPixelFormat,--wrap=avformat_open_input,--wrap=av_read_frame -o build/tube5-host
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TUBE5_PREVIEW=build/ui-preview.bmp build/tube5-host
