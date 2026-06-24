#!/usr/bin/env python3
"""Whole-ROM reachability / coverage analysis -- the metric for 100% reversal.

Collects entry points from every way LSDj transfers control:
  * the reset/interrupt vectors and `Entry`
  * bank-switch trampolines (`ld a,N; ld [$2000],a; call/jp $4000-$7FFF`)
  * the dispatch tables (screen draw/update) and hand-named symbols (src/*.sym)
Then runs the recursive-descent tracer (tools/disasm.trace_code) per bank and
reports how much of each bank is provably reachable code vs the rest (data,
unreached code behind computed jumps, padding, sample kits, empty space).

Use it to (a) track progress toward a full disassembly, and (b) generate the
seed/data split for cleaner carves.

Usage: tools/coverage.py [ROM]
"""

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from importlib import import_module
disasm = import_module("disasm")  # tools/disasm.py (run standalone)

BANK = 0x4000
ROOT = Path(__file__).resolve().parents[1]


def fileoff(bank: int, cpu: int) -> int:
    return bank * BANK + (cpu - 0x4000 if bank else cpu)


def collect_entries(rom: bytes) -> dict[int, set[int]]:
    """bank -> set of CPU entry addresses."""
    ent: dict[int, set[int]] = defaultdict(set)
    # Reset / interrupt vectors and the cartridge entry, all in bank 0.
    ent[0] |= {0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
               0x40, 0x48, 0x50, 0x58, 0x60, 0x100, 0x150}

    # Bank-switch trampolines anywhere in the ROM.
    def nearby_target(i: int) -> tuple[int, int] | None:
        for k in range(i, min(i + 18, len(rom) - 2)):
            if rom[k] in (0xCD, 0xC3):  # call/jp nn
                t = rom[k + 1] | (rom[k + 2] << 8)
                if 0x4000 <= t <= 0x7FFF:
                    return t, k
        return None

    for i in range(len(rom) - 5):
        bank = None
        # ld a, N ; ld [$2000], a
        if rom[i] == 0x3E and rom[i + 2] == 0xEA and rom[i + 3] == 0x00 and rom[i + 4] == 0x20:
            bank = rom[i + 1]
            scan_from = i + 5
        # ld hl, $2000 ; ld [hl], N
        elif rom[i] == 0x21 and rom[i + 1] == 0x00 and rom[i + 2] == 0x20 and rom[i + 3] == 0x36:
            bank = rom[i + 4]
            scan_from = i + 5
        else:
            continue
        if bank is None or bank >= len(rom) // BANK:
            continue
        hit = nearby_target(scan_from)
        if hit:
            ent[bank].add(hit[0])

    # Hand-named symbols (src/bankNN.sym) are known code starts.
    for sym in sorted((ROOT / "src").glob("bank*.sym")):
        m = re.match(r"bank([0-9a-fA-F]{2})\.sym", sym.name)
        if not m:
            continue
        bank = int(m.group(1), 16)
        for line in sym.read_text().splitlines():
            line = line.split(";", 1)[0].split()
            if len(line) >= 2:
                try:
                    ent[bank].add(int(line[0].lstrip("$"), 16))
                except ValueError:
                    pass
    return ent


def main() -> None:
    rom_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "lsdj9_4_0.gb"
    rom = rom_path.read_bytes()
    nbanks = len(rom) // BANK
    ent = collect_entries(rom)
    carved = {int(p.name[4:6], 16) for p in (ROOT / "src").glob("bank*.asm")}

    print(f"{'bank':5} {'entries':>7} {'code%':>6}  kind")
    tot_code = tot_size = 0
    for b in range(nbanks):
        start, end = b * BANK, (b + 1) * BANK
        org = 0 if b == 0 else 0x4000
        seeds = [fileoff(b, a) for a in ent.get(b, set())]
        code = disasm.trace_code(rom, start, end, org, seeds) if seeds else set()
        seg = rom[start:end]
        if seg[:2] == bytes([0x60, 0x40]):
            kind = "kit"
        elif seg.count(0xFF) + seg.count(0x00) > 0.85 * BANK:
            kind = "empty"
        else:
            kind = "code/data"
        pct = 100 * len(code) // BANK
        if kind == "code/data":
            tot_code += len(code)
            tot_size += BANK
        mark = " *carved" if b in carved else ""
        if seeds or kind == "code/data":
            print(f"${b:02X}   {len(seeds):>7} {pct:>5}%  {kind}{mark}")
    print(f"\nReachable code in code/data banks: {tot_code}/{tot_size} "
          f"({100*tot_code//max(tot_size,1)}%)")


if __name__ == "__main__":
    main()
