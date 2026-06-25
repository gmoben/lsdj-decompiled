#!/usr/bin/env python3
"""Annotate a raw WRAM/HRAM memory dump against constants/ram.inc.

Takes raw memory dumps (e.g. from BGB's "save dump to file") and prints every
named RAM variable from constants/ram.inc with its live value, so a runtime
snapshot can be read symbolically.

Dumps are raw bytes with an implied base address:
  - WRAM  : $C000-$DFFF  (8192 bytes)
  - HRAM  : $FF80-$FFFE  (127-128 bytes)
The base is inferred from each file's size, or pass `path@HEXBASE` to force it.

Usage:
  tools/annotate_ram.py [wram.dump] [hram.dump] ...
  tools/annotate_ram.py build/foo.bin@C000
Defaults: build/lsdj9_4_0.dump, build/lsdj9_4_0.ff80.dump
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RAMINC = ROOT / "constants" / "ram.inc"
SCREENSINC = ROOT / "constants" / "screens.inc"

# Keep captures in dumps/ (NOT build/, which `make clean` wipes).
DEFAULT_DUMPS = ["dumps/lsdj9_4_0.dump", "dumps/lsdj9_4_0.ff80.dump"]

# Variables known to be 16-bit pointers (show the little-endian word too).
WORD_VARS = {0xC564, 0xC573, 0xC574}
SYNC = {0: "OFF", 1: "LSDJ", 3: "MIDI", 5: "KEYBD", 6: "ANA.IN", 7: "AN.OUT"}


def infer_base(size: int) -> int | None:
    if size in (0x2000,):                 # 8 KiB -> WRAM $C000-$DFFF
        return 0xC000
    if size in (0x1000,):                 # 4 KiB -> a single WRAM bank $D000
        return 0xD000
    if size in (0x7F, 0x80):              # HRAM $FF80-$FFFE/$FFFF
        return 0xFF80
    if size in (0x2000 * 8,):             # whole 32 KiB CGB WRAM
        return 0xC000
    return None


def load_regions(argv: list[str]) -> list[tuple[int, bytes]]:
    specs = argv[1:] or DEFAULT_DUMPS
    regions: list[tuple[int, bytes]] = []
    for spec in specs:
        path, _, base_s = spec.partition("@")
        p = Path(path)
        if not p.is_absolute():
            p = ROOT / p
        if not p.exists():
            print(f"  (skip: {p} not found)", file=sys.stderr)
            continue
        data = p.read_bytes()
        base = int(base_s, 16) if base_s else infer_base(len(data))
        if base is None:
            print(f"  (skip: cannot infer base for {p} ({len(data)} bytes); "
                  f"use {path}@HEXBASE)", file=sys.stderr)
            continue
        regions.append((base, data))
    return regions


def read(regions: list[tuple[int, bytes]], addr: int, n: int = 1) -> bytes | None:
    for base, data in regions:
        if base <= addr and addr + n <= base + len(data):
            off = addr - base
            return data[off:off + n]
    return None


def parse_defs(path: Path) -> list[tuple[int, str, str]]:
    out = []
    pat = re.compile(r"\s*DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)\s*(?:;\s*(.*))?")
    for line in path.read_text().splitlines():
        m = pat.match(line)
        if m:
            out.append((int(m.group(2), 16), m.group(1), (m.group(3) or "").strip()))
    return out


def screen_names() -> dict[int, str]:
    names = {}
    for addr, name, _ in parse_defs(SCREENSINC):
        if name.startswith("SCREEN_"):
            names[addr] = name[len("SCREEN_"):].title()
    return names


def interp(addr: int, b: int, screens: dict[int, str]) -> str | None:
    if addr == 0xC401:
        return f"-> {screens.get(b, f'screen {b}')} screen"
    if addr == 0xCB66:
        return f"-> sync {SYNC.get(b, '?')}"
    return None


def main() -> None:
    regions = load_regions(sys.argv)
    if not regions:
        print("no dumps found; pass a WRAM dump path (or path@HEXBASE)")
        return
    screens = screen_names()
    print("regions: " + ", ".join(f"${b:04X}-${b+len(d)-1:04X}" for b, d in regions))
    print()
    for addr, name, comment in sorted(parse_defs(RAMINC)):
        bs = read(regions, addr, 1)
        if bs is None:
            where = "(SRAM, see .sav)" if 0xA000 <= addr < 0xC000 else "(not in dump)"
            print(f"  ${addr:04X}  {name:<16} = {where:<16}        ; {comment}")
            continue
        b = bs[0]
        word = ""
        if addr in WORD_VARS:
            wbs = read(regions, addr, 2)
            if wbs:
                word = f" [LE ${wbs[0] | (wbs[1] << 8):04X}]"
        it = interp(addr, b, screens)
        it = f"  {it}" if it else ""
        print(f"  ${addr:04X}  {name:<16} = ${b:02X} ({b:>3}){word:<14}{it}")
        if comment:
            print(f"  {'':22}; {comment}")


if __name__ == "__main__":
    main()
