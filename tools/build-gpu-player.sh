#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
bash tools/setup-gpu-sdk-integration.sh
PS5_CLANG=clang USE_CCACHE=0 make \
    APP_SOURCE_DIR=src \
    APP_DEFINITIONS='GL_GLEXT_PROTOTYPES TUBE5_DIAGNOSTICS TUBE5_HW_PROBE TUBE5_HW_FRAME TUBE5_HW_PLAYER TUBE5_GPU_GL TUBE5_GPU_PLAYER TUBE5_VP9_HIDDEN_CONTROL TUBE5_VP9_PIPELINE_CONTROL TUBE5_YT_4K60_CONTROL' \
    APP_INCLUDE_PATHS='.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/sdk/include' \
    APP_STATIC_ARCHIVES='build/gpu-sdk/libPS5OpenGL.a' \
    APP_WRAP_SYMBOLS='malloc calloc realloc free posix_memalign malloc_usable_size PS5_Keyboard_Init PS5_Keyboard_Open sceVideoOutOpen SDL_CreateRGBSurface_REAL avcodec_open2 SDL_CreateThread_REAL pthread_create SDL_RunThread SDL_SYS_SetupThread SDL_LockMutex_REAL av_read_frame avcodec_send_packet avcodec_receive_frame avformat_seek_file Kit_LockVideoDecoderRaw Kit_BeginPacketBufferRead Kit_FinishPacketBufferRead Kit_CancelPacketBufferRead Kit_GetTimerElapsed av_frame_ref' \
    BUILD_LABEL=BOOT-DIAG-105-VP9-MARKED-VISIBLE
