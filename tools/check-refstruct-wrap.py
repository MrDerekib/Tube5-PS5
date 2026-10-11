#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refuse to sign if the pinned RefStruct allocation/free ABI has changed."""
import re
import subprocess
import sys


def validate(listing):
    match = re.search(r"^([0-9a-f]+) <ff_refstruct_pool_get>:", listing, re.M)
    if not match:
        raise ValueError("Missing ff_refstruct_pool_get")
    start = int(match[1], 16)
    body = listing[match.end():].split("\n\n", 1)[0]
    lines = body.splitlines()
    sites = [i for i, line in enumerate(lines) if "callq" in line and "<__wrap_av_malloc>" in line]
    if len(sites) != 1 or sites[0] + 1 >= len(lines):
        raise ValueError("Expected exactly one wrapped RefStruct allocation")
    address = re.match(r"\s*([0-9a-f]+):", lines[sites[0] + 1])
    if not address or int(address[1], 16) - start != 0x99:
        raise ValueError("RefStruct allocation return offset changed")
    for name in ("ff_refstruct_pool_get", "pool_return_entry", "refstruct_pool_uninit"):
        match = re.search(rf"^[0-9a-f]+ <{name}>:", listing, re.M)
        if not match or "<__wrap_av_free>" not in listing[match.end():].split("\n\n", 1)[0]:
            raise ValueError(f"Missing wrapped release in {name}")


if __name__ == "__main__":
    try:
        validate(subprocess.check_output([
            "llvm-objdump", "--disassemble-symbols=ff_refstruct_pool_get,pool_return_entry,refstruct_pool_uninit",
            sys.argv[1]], text=True))
        print("Pinned RefStruct allocation and release ABI validated.")
    except (ValueError, IndexError, subprocess.SubprocessError) as error:
        sys.exit(f"Unsafe RefStruct wrap: {error}")
