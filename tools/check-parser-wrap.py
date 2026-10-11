#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed if pinned FFmpeg parser allocation/release routes change."""
import re
import subprocess
import sys


def body(listing, name):
    match = re.search(rf"^([0-9a-f]+) <{name}>:", listing, re.M)
    if not match:
        raise ValueError(f"Missing {name}")
    return int(match[1], 16), listing[match.end():].split("\n\n", 1)[0]


def returns(listing, name, target):
    _, text = body(listing, name)
    lines = text.splitlines()
    result = []
    for i, line in enumerate(lines):
        if "callq" in line and f"<{target}>" in line:
            following = re.match(r"\s*([0-9a-f]+):", lines[i + 1])
            if not following:
                raise ValueError("Missing instruction after call")
            result.append(int(following[1], 16))
    return result


def validate(listing):
    anchor, _ = body(listing, "av_fast_malloc")
    offsets = [address - anchor for address in returns(listing, "fast_malloc", "__wrap_posix_memalign")]
    if offsets != [0xfc, 0x142]:
        raise ValueError("fast_malloc aligned callsites changed")
    anchor, _ = body(listing, "ff_h2645_packet_split")
    offsets = [address - anchor for address in returns(listing, "ff_h2645_packet_split", "__wrap_av_mallocz")]
    if offsets != [0x126]:
        raise ValueError("H.264 RBSP zero allocation changed")
    anchor, _ = body(listing, "av_fast_realloc")
    offsets = [anchor - address for address in returns(listing, "mov_read_trun", "__wrap_av_fast_realloc")]
    if offsets != [0xfcf03e, 0xfcf003]:
        raise ValueError("MOV index growth callsites changed")
    if "<__wrap_realloc>" not in body(listing, "av_fast_realloc")[1]:
        raise ValueError("MOV native growth bypassed owned realloc")
    if "<av_fast_realloc>" not in body(listing, "__wrap_av_fast_realloc")[1]:
        raise ValueError("MOV growth no longer delegates to FFmpeg")
    for name in ("av_free", "av_freep"):
        if "<__wrap_free>" not in body(listing, name)[1]:
            raise ValueError(f"{name} bypasses owned-pointer cleanup")
    if "<__wrap_av_free>" not in body(listing, "av_buffer_default_free")[1]:
        raise ValueError("Refcounted parser buffer release changed")
    if "<__wrap_realloc>" not in body(listing, "av_realloc")[1]:
        raise ValueError("Owned parser realloc migration bypassed")


if __name__ == "__main__":
    try:
        validate(subprocess.check_output([
            "llvm-objdump", "--disassemble-symbols=av_fast_malloc,fast_malloc,ff_h2645_packet_split,av_free,av_freep,av_buffer_default_free,av_realloc,av_fast_realloc,mov_read_trun,__wrap_av_fast_realloc",
            sys.argv[1]], text=True))
        print("Pinned parser allocation and release routes validated.")
    except (ValueError, IndexError, subprocess.SubprocessError) as error:
        sys.exit(f"Unsafe parser wrap: {error}")
