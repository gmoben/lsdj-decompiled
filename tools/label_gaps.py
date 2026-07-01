#!/usr/bin/env python3
"""Find addresses an emulator debugger (Emulicious/BGB) would auto-label.

Emulicious shows `_LABEL_bb_aaaa` for a *code* address that is a branch/call
target with no symbol, and `_DATA_bb_aaaa` for a *data* address referenced by an
instruction operand with no symbol. Driving our function/RAM naming to "100% (no
auto-labels)" means: every such referenced address must have a symbol.

This tool reproduces that classification statically, so we can measure and locate
the gaps without the emulator. It reuses disasm.py's recursive-descent tracer and
opcode table, seeds each bank from the known symbols (build/lsdj9_4_0.sym), and
reports referenced-but-unnamed addresses per bank, split into:

  LABEL  - control-flow target (jr/jp/call) not in the symbol map  -> _LABEL_
  DATA   - ROM data operand (ld rr,nn / ld a,[nn] / ld [nn],a)      -> _DATA_
           resolved to a concrete bank (in-bank $4000-$7FFF or home $0000-$3FFF)
  DATA?  - same, but bank is ambiguous (a $4000-$7FFF operand executed from a
           bank that can map any ROMX bank) -- reported separately, not counted

Runtime-only code (computed jumps our static trace can't follow) is NOT found
here; use the dynamic trace workflow for those. This is the static backbone.

Usage:
    tools/label_gaps.py                     # summary table for every bank
    tools/label_gaps.py --bank 0x02         # detail: list gap addresses in a bank
    tools/label_gaps.py --kind data --bank 6
"""
from __future__ import annotations

import argparse
import importlib.util
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Load sibling tools by path (tools/ is a script dir, not a package), so this
# works whether run as a script or imported, without a relative-import.
def _load(name: str):
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).resolve().parent / f"{name}.py")
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


disasm = _load("disasm")
carve = _load("carve_clean")   # reuse the REAL code/data split (+ carve.cov)

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "lsdj9_4_0.gb"
SYM = ROOT / "build" / "lsdj9_4_0.sym"

# Direct memory-access opcodes: the 16-bit operand is ALWAYS a real memory
# reference (08 ld [nn],sp / EA ld [nn],a / FA ld a,[nn]) -> always a data ref.
MEM_OPS = {0x08, 0xEA, 0xFA}
# Immediate 16-bit loads (01 ld bc / 11 ld de / 21 ld hl): the operand is only a
# pointer when it lands on a real region *start*; otherwise it's a constant
# (loop count, size, bitmask) that merely happens to fall in ROM range.
IMM_OPS = {0x01, 0x11, 0x21}


SymMap = dict[tuple[int, int], str]      # (bank, addr) -> name
Sites = list[tuple[int, str]]            # referencing (addr, instruction text)
Row = tuple[int, bytes, str, "int | None"]   # matches disasm.Row
Rows = list[Row]


@dataclass
class BankInfo:
    """Per-bank code/data layout (pass 1), used to classify cross-bank refs."""
    instr_starts: set[int] = field(default_factory=set)  # gb addrs: instruction starts
    data_starts: set[int] = field(default_factory=set)   # gb addrs: start of a data run
    code_pct: int = 0


@dataclass
class BankGaps:
    code_pct: int
    label: dict[int, Sites] = field(default_factory=dict)             # addr -> sites
    data: dict[tuple[int, int], Sites] = field(default_factory=dict)  # (bank,addr) -> sites
    data_ambig: dict[int, Sites] = field(default_factory=dict)        # addr -> sites


def load_symbols() -> SymMap:
    """(bank, addr) -> name from the link .sym ('BB:AAAA Name' lines)."""
    syms: SymMap = {}
    if not SYM.is_file():
        raise SystemExit(f"error: {SYM} missing (run `make` first)")
    for line in SYM.read_text().splitlines():
        m = re.match(r"^([0-9A-Fa-f]{2,3}):([0-9A-Fa-f]{4})\b", line)
        if m:
            syms[(int(m.group(1), 16), int(m.group(2), 16))] = "x"
    return syms


def bank_of(addr: int, cur_bank: int) -> int | None:
    """ROM bank an operand address resolves to, or None if ambiguous.
    $0000-$3FFF -> home bank 0; $4000-$7FFF -> current bank (None if cur is 0)."""
    if addr < 0x4000:
        return 0
    if addr < 0x8000:
        return None if cur_bank == 0 else cur_bank
    return -1  # not ROM (RAM/HRAM/IO): not a ROM data gap


def _bank_geom(bank: int) -> tuple[int, int, int, int]:
    start = bank * 0x4000
    return start, start + 0x4000, (0x0000 if bank == 0 else 0x4000), 0x4000


def scan_bank(data: bytes, bank: int, symbols: SymMap,
              code: set[int]) -> tuple[BankInfo, Rows]:
    """Pass 1: decode a bank with the SAME code/data split the carve produces
    (whole-ROM coverage trace + carve_clean.data_runs), so the analyzer reflects
    the real disassembly -- a table marked as data (e.g. PitchBendTable) is not
    re-traced as code and its bytes are not mistaken for references."""
    start, end, org, size = _bank_geom(bank)
    data_ranges = carve.data_runs(code, data, bank)
    anchors = {start + (a - org) for (b, a) in symbols
               if b == bank and org <= a < org + size}
    rows = disasm.disassemble(data, start, end, org, data_ranges, anchors)
    bank_code = sum(1 for o in range(start, end) if o in code)
    info = BankInfo(
        instr_starts={a for a, _r, t, _ref in rows if not t.startswith("db")},
        data_starts={org + (lo - start) for lo, _hi in data_ranges},
        code_pct=100 * bank_code // size,
    )
    return info, rows


def analyze_bank(bank: int, symbols: SymMap, index: dict[int, BankInfo],
                 rows: Rows) -> BankGaps:
    """Pass 2: classify each referenced-but-unnamed address into a gap kind."""
    _start, _end, org, size = _bank_geom(bank)
    here = index[bank]
    g = BankGaps(code_pct=here.code_pct)

    def add_ptr(tb: int, taddr: int, site: tuple[int, str]) -> None:
        """Classify a resolved pointer target as a code (_LABEL_) or data gap."""
        if (tb, taddr) in symbols:
            return
        ti = index.get(tb)
        if ti and taddr in ti.instr_starts:
            g.label.setdefault(taddr, []).append(site)     # code pointer
        elif (ti is None) or (taddr in ti.data_starts):
            g.data.setdefault((tb, taddr), []).append(site)  # data table

    for addr, raw, text, ref in rows:
        if text.startswith("db"):
            continue
        site = (addr, text)
        # Control-flow target -> _LABEL_ if unnamed and a real in-bank code start.
        if ref is not None and org <= ref < org + size and ref in here.instr_starts:
            if (bank, ref) not in symbols:
                g.label.setdefault(ref, []).append(site)
        op = raw[0]
        if len(raw) != 3 or op not in MEM_OPS | IMM_OPS:
            continue
        operand = raw[1] | (raw[2] << 8)
        tb = bank_of(operand, bank)
        if tb == -1:
            continue
        if tb is None:                       # $4000-$7FFF from bank 0: ambiguous bank
            g.data_ambig.setdefault(operand, []).append(site)
            continue
        if op in MEM_OPS:                    # real memory access: always a data ref
            if (tb, operand) not in symbols:
                g.data.setdefault((tb, operand), []).append(site)
        elif operand >= 0x0010:              # immediate load -> pointer if a region
            add_ptr(tb, operand, site)       # start; skip <$0010 (loop counts, flags)
    return g


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bank", type=lambda s: int(s, 0), help="detail one bank")
    ap.add_argument("--kind", choices=["label", "data", "data_ambig"],
                    help="in --bank detail, list only this kind")
    ap.add_argument("--dump", action="store_true",
                    help="in --bank data detail, show each target's bytes (hex+ascii)")
    args = ap.parse_args()

    data = ROM.read_bytes()
    symbols = load_symbols()
    banks = [args.bank] if args.bank is not None else list(range(0x40))

    # Whole-ROM reachability once (same seeds/trace as the carve), so each bank's
    # code/data split matches src/bankNN.asm.
    ent = carve.cov.collect_entries(data)
    seeds = [(b, a) for b, addrs in ent.items() for a in addrs]
    code, _ = carve.cov.global_trace(data, seeds)

    # Pass 1: index every bank's code/data layout (so cross-bank refs classify).
    index: dict[int, BankInfo] = {}
    bank_rows: dict[int, Rows] = {}
    for b in range(0x40):
        index[b], bank_rows[b] = scan_bank(data, b, symbols, code)

    # Pass 2: classify referenced-but-unnamed addresses per bank.
    tot_label = tot_data = 0
    print(f"{'bank':>4} {'code%':>5} {'LABEL':>6} {'DATA':>6} {'DATA?':>6}")
    results: dict[int, BankGaps] = {}
    for b in banks:
        r = analyze_bank(b, symbols, index, bank_rows[b])
        results[b] = r
        tot_label += len(r.label)
        tot_data += len(r.data)
        if r.label or r.data or r.data_ambig or args.bank is not None:
            print(f"${b:02X}  {r.code_pct:>4}% {len(r.label):>6} "
                  f"{len(r.data):>6} {len(r.data_ambig):>6}")
    if args.bank is None:
        print(f"\nTOTAL  _LABEL_ gaps: {tot_label}   _DATA_ gaps: {tot_data}")
        return

    r = results[args.bank]

    def target_bytes(tb: int, taddr: int, n: int = 20) -> str:
        """hex + ascii of a resolved target, to classify it at a glance."""
        off = tb * 0x4000 + (taddr - (0 if taddr < 0x4000 else 0x4000))
        raw = data[off:off + n]
        hexs = " ".join(f"{x:02x}" for x in raw[:8])
        txt = "".join(chr(x) if 32 <= x <= 0x7E else "." for x in raw)
        return f"{hexs}  |{txt}|"

    def show(kind: str, items: dict[Any, Sites], is_data: bool) -> None:
        if (args.kind and kind != args.kind) or not items:
            return
        print(f"\n--- {kind} ({len(items)}) ---")
        for key, sites in sorted(items.items()):
            refs = " ".join(f"{ra:04X}" for ra, _t in sites[:4])
            if is_data:
                tb, taddr = key
                extra = f"  {target_bytes(tb, taddr)}" if args.dump else ""
                print(f"  {tb:02X}:{taddr:04X}  <- {refs}{extra}")
            else:
                print(f"  {key:04X}  <- {refs}")

    show("label", r.label, False)
    show("data", r.data, True)
    show("data_ambig", r.data_ambig, False)


if __name__ == "__main__":
    main()
