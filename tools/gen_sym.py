#!/usr/bin/env python3
"""Generate a comprehensive emulator symbol file for LSDj debugging.

rgblink (`-n`) emits a `BANK:ADDR Name` .sym listing every named ROM routine
(BGB / SameBoy / Emulicious format), but it does NOT export `EQU` constants --
so all of our WRAM/HRAM variable names (constants/ram.inc) are missing and the
debugger shows raw addresses for memory accesses.

This tool merges the two sources into one .sym:

  * build/lsdj9_4_0.sym  -- every named routine (+ .L_xxxx locals) from the link
  * constants/ram.inc    -- `DEF wXxx EQU $AAAA` -> WRAM/HRAM variable symbols

Bank prefix for RAM symbols (per the BGB/no$gmb/Emulicious convention):
  * $C000-$CFFF (WRAM0) and $FF80-$FFFE (HRAM) -> bank 00
  * $D000-$DFFF (WRAMX)                         -> bank 01 (default switchable)

Numeric `EQU` constants that are not addresses (e.g. RAM_INC, BTN_*) are skipped.

Usage:
    tools/gen_sym.py [BUILD_SYM] [RAM_INC] -o OUT.sym
Defaults: BUILD_SYM=build/lsdj9_4_0.sym  RAM_INC=constants/ram.inc
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# DEF <name> EQU $<hex4>  -- only literal 16-bit addresses become RAM symbols.
DEF_RE = re.compile(r"^DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]{4})\b")
# Lines already in the link .sym: "BB:AAAA Name".
SYM_RE = re.compile(r"^([0-9A-Fa-f]{2,3}):([0-9A-Fa-f]{4})\b")


def ram_bank(addr: int) -> int:
    """Return the .sym bank prefix for a RAM/HRAM address."""
    if 0xD000 <= addr <= 0xDFFF:
        return 1  # WRAMX: default switchable bank
    return 0  # WRAM0 ($C000-$CFFF) and HRAM ($FF80-$FFFE)


def parse_ram_vars(ram_inc: Path) -> list[tuple[int, int, str]]:
    """Return (bank, addr, name) for every literal-address DEF in ram.inc."""
    out: list[tuple[int, int, str]] = []
    for line in ram_inc.read_text().splitlines():
        m = DEF_RE.match(line.strip())
        if not m:
            continue
        name, hex4 = m.group(1), m.group(2)
        addr = int(hex4, 16)
        if addr < 0x8000:
            continue  # not a RAM/HRAM address; skip defensively
        out.append((ram_bank(addr), addr, name))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_sym", nargs="?", default=str(ROOT / "build/lsdj9_4_0.sym"))
    ap.add_argument("ram_inc", nargs="?", default=str(ROOT / "constants/ram.inc"))
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    build_sym = Path(args.build_sym)
    ram_inc = Path(args.ram_inc)
    if not build_sym.is_file():
        sys.exit(f"error: link .sym not found: {build_sym} (run `make` first)")
    if not ram_inc.is_file():
        sys.exit(f"error: ram.inc not found: {ram_inc}")

    link_lines = build_sym.read_text().splitlines()
    # Addresses already present in the link .sym, to avoid duplicate entries.
    present: set[tuple[str, str]] = set()
    n_routines = 0
    for line in link_lines:
        m = SYM_RE.match(line)
        if m:
            present.add((m.group(1).lower(), m.group(2).lower()))
            n_routines += 1

    ram_vars = parse_ram_vars(ram_inc)
    ram_lines: list[str] = []
    for bank, addr, name in sorted(ram_vars):
        key = (f"{bank:02x}", f"{addr:04x}")
        if key in present:
            continue
        ram_lines.append(f"{bank:02X}:{addr:04X} {name}")

    out_lines: list[str] = []
    out_lines.extend(link_lines)
    out_lines.append("; --- RAM/HRAM variables (constants/ram.inc) ---")
    out_lines.extend(ram_lines)

    out = Path(args.out)
    out.write_text("\n".join(out_lines) + "\n")
    print(f"==> {out}")
    print(f"    {n_routines} routine symbols + {len(ram_lines)} RAM variables "
          f"= {n_routines + len(ram_lines)} total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
