#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve a DIAG24 REL offset against the matching, unstripped native ELF."""
import argparse
from pathlib import Path
import subprocess


def resolve_symbols(listing, offset):
    symbols = []
    for line in listing.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3 and fields[1] in ("t", "T"):
            symbols.append((int(fields[0], 16), fields[2]))
    anchors = [address for address, name in symbols if name == "__wrap_posix_memalign"]
    if len(anchors) != 1:
        raise ValueError("ELF must contain exactly one __wrap_posix_memalign anchor")
    caller = anchors[0] + offset
    # REL identifies the return PC, just after the allocating call instruction.
    candidates = [(address, name) for address, name in symbols if address <= caller - 1]
    if not candidates:
        raise ValueError("REL resolves before executable symbols")
    start, name = max(candidates)
    return caller, name, caller - 1 - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("offset", type=lambda value: int(value, 0),
                        help="signed REL value, e.g. +0x1234 (use -- before a negative value)")
    parser.add_argument("--elf", type=Path, default=Path("build/llvm-pie.elf"))
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error("matching native ELF not found")
    try:
        listing = subprocess.check_output(
            ["llvm-nm", "--defined-only", str(args.elf)], text=True)
        caller, name, delta = resolve_symbols(listing, args.offset)
        print(f"Return PC 0x{caller:x}; call site: {name}+0x{delta:x}", flush=True)
        subprocess.run(["llvm-addr2line", "-f", "-C", "-e", str(args.elf),
                        f"0x{caller - 1:x}"], check=True)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        parser.exit(1, f"Cannot resolve caller: {error}\n")


if __name__ == "__main__":
    main()
