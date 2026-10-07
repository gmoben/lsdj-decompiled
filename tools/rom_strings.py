#!/usr/bin/env python3
"""List the NUL-terminated strings at a ROM address, one per line.

For naming a function's string literals (SDCC places them right after the
code, in source order) or a table of messages: prints each string's address,
length and text, non-printable bytes as \\xNN, and stops at the first run that
doesn't look like text (a byte under $20 other than the terminator, or more
than half the bytes above $7E) unless --raw.

Usage: tools/rom_strings.py BB:AAAA [COUNT] [--raw]
       tools/rom_strings.py NAME [COUNT]      (a src/bankNN.sym symbol)
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def locate(target: str) -> tuple[int, int]:
    m = re.fullmatch(r"\$?([0-9A-Fa-f]{1,2}):\$?([0-9A-Fa-f]{4})", target)
    if m:
        return int(m.group(1), 16), int(m.group(2), 16)
    for p in sorted((ROOT / "src").glob("bank*.sym")):
        for ln in p.read_text().splitlines():
            m = re.match(r"\s*\$?([0-9A-Fa-f]{4})\s+(\S+)", ln)
            if m and m.group(2) == target:
                return int(p.stem[4:6], 16), int(m.group(1), 16)
    sys.exit(f"error: {target}: not BB:AAAA or a symbol")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target")
    ap.add_argument("count", nargs="?", type=int, default=20)
    ap.add_argument("--raw", action="store_true", help="don't stop at non-text")
    ap.add_argument("--rom", type=Path, default=ROOT / "lsdj9_4_0.gb")
    args = ap.parse_args()
    rom = args.rom.read_bytes()
    bank, addr = locate(args.target)
    base = bank * 0x4000 - (0 if bank == 0 else 0x4000)
    end_addr = 0x4000 if bank == 0 else 0x8000
    for _ in range(args.count):
        off = base + addr
        if addr >= end_addr:
            break
        nul = rom.find(b"\0", off, base + end_addr)
        if nul < 0:
            print(f"{bank:02X}:{addr:04X}  (no terminator before the end of the bank)")
            break
        s = rom[off:nul]
        texty = all(b >= 0x20 for b in s) and sum(b > 0x7E for b in s) * 2 <= len(s)
        if not texty and not args.raw:
            print(f"{bank:02X}:{addr:04X}  (not text: {rom[off:off + 12].hex(' ')} ...)")
            break
        shown = "".join(chr(b) if 0x20 <= b < 0x7F else f"\\x{b:02x}" for b in s)
        print(f"{bank:02X}:{addr:04X} {len(s):4d}  \"{shown}\"")
        addr += len(s) + 1


if __name__ == "__main__":
    main()
