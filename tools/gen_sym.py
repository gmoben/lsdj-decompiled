#!/usr/bin/env python3
"""Generate a comprehensive emulator symbol file for LSDj debugging.

rgblink (`-n`) emits a `BANK:ADDR Name` .sym listing every named ROM routine
(BGB / SameBoy / Emulicious format), but it does NOT export `EQU` constants --
so all of our WRAM/HRAM variable names (constants/ram.inc) are missing and the
debugger shows raw addresses for memory accesses.

This tool merges the two sources into one .sym:

  * build/lsdj9_4_0.sym  -- every named routine (+ .L_xxxx locals) from the link
  * constants/ram.inc    -- `DEF wXxx EQU $AAAA` -> WRAM/HRAM variable symbols
  * SRAM_MAP (below)     -- working-song SRAM structures (docs/data_model.md),
                            emitted as bank:addr since SRAM is banked

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
# Marker beginning our appended RAM section. Everything from this line on is
# dropped when re-reading, so the tool is idempotent even when OUT == input
# (i.e. enriching rgblink's build/lsdj9_4_0.sym in place).
RAM_SECTION_MARKER = "; --- RAM/HRAM variables"


def ram_bank(addr: int) -> int:
    """Return the .sym bank prefix for a RAM/HRAM address."""
    if 0xD000 <= addr <= 0xDFFF:
        return 1  # WRAMX: default switchable bank
    return 0  # WRAM0 ($C000-$CFFF) and HRAM ($FF80-$FFFE)


# LSDj working-song SRAM structures (docs/data_model.md). SRAM is 4 banks of
# 8 KiB mapped at $A000-$BFFF; offset -> (bank = off>>13, cpu = $A000 + off&$1FFF).
# The debugger addresses SRAM by (bank, cpu-addr), so emit `bank:addr`.
SRAM_MAP: list[tuple[int, str]] = [
    (0x0000, "SramPhraseNotes"),      (0x0FF0, "SramBookmarks"),
    (0x1090, "SramGrooves"),          (0x1290, "SramSequence"),
    (0x1690, "SramTableEnvelopes"),   (0x1890, "SramWords"),
    (0x1DD0, "SramWordNames"),        (0x1E78, "SramInitFlag1"),
    (0x1E7A, "SramInstrNames"),       (0x2020, "SramTableAlloc"),
    (0x2040, "SramInstrAlloc"),       (0x2080, "SramChainPhrases"),
    (0x2880, "SramChainTransposes"),  (0x3080, "SramInstruments"),
    (0x3480, "SramTableTransposes"),  (0x3680, "SramTableCmd1"),
    (0x3A80, "SramTableCmd2"),        (0x3E80, "SramInitFlag2"),
    (0x3E82, "SramPhraseAlloc"),      (0x3EA2, "SramChainAlloc"),
    (0x3EB2, "SramSynthParams"),      (0x3FB2, "SramProperties"),
    # song-property fields (verified in code)
    (0x3FB4, "SramPropTempo"),        (0x3FBA, "SramPropKeyDelay"),
    (0x3FBB, "SramPropKeyRepeat"),    (0x3FBC, "SramPropPrelisten"),
    (0x3FBD, "SramPropSyncMode"),     (0x3FBE, "SramPropColorSet"),
    (0x3FBF, "SramPropSyncMode2"),    (0x3FC0, "SramPropCloneMode"),
    (0x3FC1, "SramPropActiveFile"),   (0x3FCC, "SramPropTempoMode"),
    (0x4000, "SramPhraseFx"),         (0x4FF0, "SramPhraseFxVal"),
    (0x6000, "SramWaveFrames"),       (0x7000, "SramPhraseInstruments"),
    (0x7FF0, "SramInitFlag3"),
]


def sram_syms() -> list[tuple[int, int, str]]:
    """(sram_bank, cpu_addr, name) for each working-song SRAM structure."""
    out = []
    for off, name in SRAM_MAP:
        out.append((off >> 13, 0xA000 + (off & 0x1FFF), name))
    return out


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

    # Drop any RAM section from a previous run so enriching in place is idempotent.
    link_lines: list[str] = []
    for line in build_sym.read_text().splitlines():
        if line.startswith(RAM_SECTION_MARKER):
            break
        link_lines.append(line)
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
    # SRAM working-song structures (bank:addr in $A000-$BFFF)
    for bank, addr, name in sram_syms():
        ram_lines.append(f"{bank:02X}:{addr:04X} {name}")

    out_lines: list[str] = []
    out_lines.extend(link_lines)
    # deploy-only entries from src/bank*.sym (tagged @deploy-only): symbols at
    # mid-instruction addresses that must not anchor the carve but should
    # resolve in the debugger.
    import glob as _glob
    import re as _re
    for sp in sorted(_glob.glob("src/bank*.sym")):
        bank = int(Path(sp).stem[4:6], 16)
        for ln in Path(sp).read_text().splitlines():
            if "@deploy-only" not in ln:
                continue
            m = _re.match(r"([0-9A-Fa-f]{4})\s+(\S+)", ln)
            if m:
                out_lines.append(f"{bank:02X}:{m.group(1).upper()} {m.group(2)}")
    out_lines.append(f"{RAM_SECTION_MARKER} (constants/ram.inc) ---")
    out_lines.extend(ram_lines)

    out = Path(args.out)
    out.write_text("\n".join(out_lines) + "\n")
    print(f"==> {out}")
    print(f"    {n_routines} routine symbols + {len(ram_lines)} RAM variables "
          f"= {n_routines + len(ram_lines)} total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
