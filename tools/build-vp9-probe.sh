#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
PS5_CLANG=clang USE_CCACHE=0 make \
    APP_SOURCE_DIR=experiments/gpu-vp9 \
    APP_DEFINITIONS='GL_GLEXT_PROTOTYPES TUBE5_HW_PROBE TUBE5_HW_FRAME TUBE5_GPU_GL TUBE5_GPU_PLAYER TUBE5_HW_TIMING TUBE5_VP9_DEEP_PROBE' \
    APP_INCLUDE_PATHS='src .deps/gpu-sdk/ps5-opengl-sdk-1.0.1/sdk/include' \
    APP_STATIC_ARCHIVES='.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/sdk/lib/libPS5OpenGL.a' \
    APP_WRAP_SYMBOLS='malloc calloc realloc free posix_memalign malloc_usable_size' \
    PACBREW_PACKAGES='sdl2' BUILD_LABEL=BOOT-DIAG-100-VP9-FLAG-POLICY
