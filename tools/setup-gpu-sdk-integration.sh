#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk_lib="$root/.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/sdk/lib"
output="$root/build/gpu-sdk"
mkdir -p "$output"
# Mechanical binary symbol renaming: change definitions and internal callers
# together. Preserve the frozen SDK; never permit ABI collisions with json-c.
llvm-objcopy --redefine-syms="$root/tooling/native/gpu-parson-symbols.txt" \
    "$sdk_lib/libpsbc.ps5.a" "$output/libpsbc.ps5.a"
# Generate an equivalent linker GROUP with one namespaced archive. Other
# components keep their exact SDK bytes and full paths for relocatable builds.
sed -E "s@^(  )([A-Za-z0-9_.-]+\\.a)\$@\\1$sdk_lib/\\2@" \
    "$sdk_lib/libPS5OpenGL.a" |
    sed "s@$sdk_lib/libpsbc.ps5.a@$output/libpsbc.ps5.a@" > "$output/libPS5OpenGL.a"
