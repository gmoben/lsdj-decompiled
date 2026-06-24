#!/usr/bin/env python3
"""Re-carve the disassembled banks with a trace-driven code/data split.

Uses the bank-aware reachability trace (tools/coverage) to find each bank's
*unreached* byte runs, and marks as `db` only the runs that clearly look like
**data** -- illegal-opcode-dense (graphics/tables), printable-ASCII (strings), or
`$00`/`$FF` fill (padding). Code-like unreached runs are left as instructions, so
no real code is hidden. Reached code is always disassembled.

This produces cleaner carves (strings/graphics/tables read as data) without
losing code, and keeps `make compare` byte-for-byte identical. Re-run after the
coverage trace improves.

Usage: tools/carve_clean.py            # re-carve every src/bankNN.asm
       tools/carve_clean.py 04 1d      # just these banks
"""

from __future__ import annotations

import subprocess
import sys
from importlib import import_module
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
cov = import_module("coverage")
disasm = import_module("disasm")

BANK = 0x4000
ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "lsdj9_4_0.gb"

# Known data tables the content heuristic alone would miss (bytes that decode as
# legal opcodes but are really data).
MANUAL: dict[int, list[tuple[int, int]]] = {
    # bitmask table, cartridge header, signed modulation LUTs (triangle/ramp/
    # clamp tables at $0200; the leading fill run already covers up to $0206),
    # instrument param-label/char tables ($0824), and a 16-bit offset LUT ($09B9).
    0x00: [(0x10, 0x20), (0x104, 0x150), (0x207, 0x2C0),
           (0x824, 0x908), (0x9B9, 0xA91)],
}


def data_like(seg: bytes) -> bool:
    """Conservative: only true for clear data, never for code. SM83 code has many
    opcodes in the ASCII range, so a plain printable ratio is NOT safe; instead
    require a long run of consecutive printable bytes (real strings), or dense
    illegal opcodes (graphics/random), or `$00`/`$FF` fill (padding)."""
    n = len(seg)
    if not n:
        return False
    illegal = sum(1 for b in seg if b in disasm.ILLEGAL) / n
    fill = (seg.count(0) + seg.count(0xFF)) / n
    # Fraction of bytes inside a *long* printable run (>= 4) -- real text. Code's
    # printable opcodes are short, scattered runs, so its fraction stays low.
    str_bytes = i = 0
    while i < n:
        if 0x20 <= seg[i] <= 0x7E:
            j = i
            while j < n and 0x20 <= seg[j] <= 0x7E:
                j += 1
            if j - i >= 4:
                str_bytes += j - i
            i = j
        else:
            i += 1
    return illegal >= 0.06 or fill >= 0.60 or str_bytes / n >= 0.55


def data_runs(code: set[int], rom: bytes, bank: int, minlen: int = 4) -> list[tuple[int, int]]:
    lo, hi = bank * BANK, (bank + 1) * BANK
    runs: list[tuple[int, int]] = []
    o = lo
    while o < hi:
        if o in code:
            o += 1
            continue
        s = o
        while o < hi and o not in code:
            o += 1
        if o - s >= minlen and data_like(rom[s:o]):
            runs.append((s, o))
    # Union with manual ranges (file offsets).
    for ca, cb in MANUAL.get(bank, []):
        runs.append((bank * BANK + ca, bank * BANK + cb))
    return sorted(runs)


def main() -> None:
    rom = ROM.read_bytes()
    ent = cov.collect_entries(rom)
    seeds = [(b, a) for b, addrs in ent.items() for a in addrs]
    code, _ = cov.global_trace(rom, seeds)

    want = {int(a, 16) for a in sys.argv[1:]}
    banks = sorted(int(p.name[4:6], 16) for p in (ROOT / "src").glob("bank*.sym"))
    for b in banks:
        if want and b not in want:
            continue
        runs = data_runs(code, rom, b)
        data_arg = ",".join(f"0x{s:X}:0x{e:X}" for s, e in runs)
        cmd = ["python3", "tools/disasm.py", "--rom", "lsdj9_4_0.gb",
               "--bank", f"0x{b:02x}", "--symbols", f"src/bank{b:02x}.sym",
               "--comments", "-o", f"src/bank{b:02x}.asm"]
        if b != 0:
            cmd += ["--globals", "src/bank00.sym"]
        if data_arg:
            cmd += ["--data", data_arg]
        subprocess.run(cmd, cwd=ROOT, check=True)
        db = sum(e - s for s, e in runs)
        print(f"bank ${b:02X}: {len(runs)} data run(s), {db} bytes marked db")

    subprocess.run(["python3", "tools/gen_main.py"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
