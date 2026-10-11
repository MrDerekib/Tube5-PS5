#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed if SDL's fixed YUV plane allocation or release route changes."""
import subprocess
import sys
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("parser_guard", Path(__file__).with_name("check-parser-wrap.py"))
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)

def validate(listing):
    anchor, _ = parser.body(listing, "SDL_SW_CreateYUVTexture")
    if [value - anchor for value in parser.returns(listing, "SDL_SW_CreateYUVTexture", "__wrap_SDL_SIMDAlloc_REAL")] != [0xc8]:
        raise ValueError("SDL YUV plane allocation callsite changed")
    for name in ("SDL_SW_CreateYUVTexture", "SDL_SW_DestroyYUVTexture"):
        if "<__wrap_SDL_SIMDFree_REAL>" not in parser.body(listing, name)[1]:
            raise ValueError(f"{name} bypasses native plane cleanup")
    if "<SDL_SIMDRealloc_REAL>" in parser.body(listing, "SDL_SW_CreateYUVTexture")[1]:
        raise ValueError("YUV planes are no longer fixed-size")

if __name__ == "__main__":
    try:
        validate(subprocess.check_output(["llvm-objdump", "--disassemble-symbols=SDL_SW_CreateYUVTexture,SDL_SW_DestroyYUVTexture", sys.argv[1]], text=True))
        print("Pinned SDL YUV allocation and release routes validated.")
    except (ValueError, IndexError, subprocess.SubprocessError) as error:
        sys.exit(f"Unsafe SDL YUV wrap: {error}")
