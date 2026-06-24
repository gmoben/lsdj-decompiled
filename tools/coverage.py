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


def _ilen(rom: bytes, phys: int) -> int:
    op = rom[phys]
    if op == 0xCB:
        return 2
    if op == 0x10:
        return 2 if (phys + 1 < len(rom) and rom[phys + 1] == 0) else 1
    return disasm.MAIN[op][0]


def global_trace(rom: bytes, seeds: list[tuple[int, int]],
                 max_states: int = 6_000_000) -> tuple[set[int], bool]:
    """Bank-aware whole-ROM reachability. State = (cpu, pending_bank); a window
    byte's bank is its physical offset's bank, but `call/jp $4000-$7FFF` resolve
    against the *pending* bank set by the most recent `$2000` write in the path.
    Follows trampolines through into the callee bank, and `jp hl` jump tables in
    the current bank context. Returns (code physical-offsets, hit_cap)."""
    JR, JP, CALL, RST = disasm.JR_OPS, disasm.JP_OPS, disasm.CALL_OPS, disasm.RST_OPS
    STOP, ILL = disasm.STOP_FLOW, disasm.ILLEGAL
    code: set[int] = set()
    seen: set[tuple[int, int]] = set()
    work: list[tuple[int, int]] = [(cpu, bank) for bank, cpu in seeds]
    n = 0
    while work:
        cpu, pbank = work.pop()
        last_a = last_hl = None
        while True:
            if n >= max_states:
                return code, True
            n += 1
            phys = cpu if cpu < 0x4000 else pbank * BANK + (cpu - 0x4000)
            if not (0 <= phys < len(rom)) or (cpu, pbank) in seen:
                break
            seen.add((cpu, pbank))
            op = rom[phys]
            length = min(_ilen(rom, phys), len(rom) - phys)
            for i in range(length):
                code.add(phys + i)

            if op == 0x3E:
                last_a = rom[phys + 1]
            elif op == 0x21 and length == 3:
                last_hl = rom[phys + 1] | (rom[phys + 2] << 8)
            if op == 0xEA and rom[phys + 1] == 0 and rom[phys + 2] == 0x20 and last_a is not None:
                pbank = last_a                      # ld [$2000], a
            elif op == 0x36 and last_hl == 0x2000:
                pbank = rom[phys + 1]               # ld [hl], n  (hl == $2000)

            tgt = None
            if op in JR and length == 2:
                off = rom[phys + 1] - 256 if rom[phys + 1] > 127 else rom[phys + 1]
                tgt = (cpu + 2 + off) & 0xFFFF
            elif (op in JP or op in CALL) and length == 3:
                tgt = rom[phys + 1] | (rom[phys + 2] << 8)
            elif op in RST:
                tgt = op & 0x38
            if tgt is not None:
                work.append((tgt, pbank))

            if op == 0xE9 and last_hl is not None:  # jp hl over a jump table
                addr_form = phys >= 3 and rom[phys - 3:phys] == bytes([0x2A, 0x66, 0x6F])
                p = last_hl
                for _ in range(128):
                    pe = p if p < 0x4000 else pbank * BANK + (p - 0x4000)
                    if addr_form:
                        if pe + 2 > len(rom):
                            break
                        t, step = rom[pe] | (rom[pe + 1] << 8), 2
                    else:
                        if pe + 3 > len(rom) or rom[pe] != 0xC3:
                            break
                        t, step = rom[pe + 1] | (rom[pe + 2] << 8), 3
                    tp = t if t < 0x4000 else pbank * BANK + (t - 0x4000)
                    if not (0 <= tp < len(rom)) or rom[tp] in ILL:
                        break
                    work.append((t, pbank))
                    p += step

            if op in STOP:
                break
            cpu += length
    return code, False


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

    # Banked-call helpers (farcall): caller pushes addr (ld bc/de/hl, nn; push)
    # then bank (ld a, N; push af; inc sp), then `call $179A` (FarCall) or
    # `call $17BB` (FarCallArg, which also pushes one byte arg in between). Both
    # share the same bank/addr push convention, so one extractor handles both.
    # This is LSDj's main cross-bank call mechanism.
    nbanks = len(rom) // BANK
    for helper in (0x179A, 0x17BB):
        farcall = bytes([0xCD, helper & 0xFF, helper >> 8])
        j = rom.find(farcall)
        while j >= 0:
            caller_bank = j // BANK
            win = rom[max(0, j - 40):j]
            bank = imm_addr = last_hl = table = None
            for k in range(len(win)):
                if (k + 3 < len(win) and win[k] == 0x3E
                        and win[k + 2] == 0xF5 and win[k + 3] == 0x33):
                    bank = win[k + 1]                          # bank arg (immediate)
                if win[k] in (0x01, 0x11, 0x21) and k + 2 < len(win):
                    v = win[k + 1] | (win[k + 2] << 8)
                    if 0x4000 <= v <= 0x7FFF:
                        imm_addr = v
                        if win[k] == 0x21:
                            last_hl = v
                if win[k:k + 3] == bytes([0x2A, 0x66, 0x6F]):  # addr read from a table
                    table = last_hl
            if bank is not None and bank < nbanks:
                if table is not None:
                    # Pointer table lives in the caller's bank; targets run in `bank`.
                    tbase = (caller_bank * BANK + (table - 0x4000)) if table >= 0x4000 else table
                    for n in range(24):
                        pe = tbase + 2 * n
                        if pe + 2 > len(rom):
                            break
                        t = rom[pe] | (rom[pe + 1] << 8)
                        tp = bank * BANK + (t - 0x4000)
                        if not (0x4000 <= t <= 0x7FFF) or tp >= len(rom) or rom[tp] in disasm.ILLEGAL:
                            break
                        ent[bank].add(t)
                elif imm_addr is not None:
                    ent[bank].add(imm_addr)
            j = rom.find(farcall, j + 1)

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

    seeds = [(b, a) for b, addrs in ent.items() for a in addrs]
    code, capped = global_trace(rom, seeds)
    if capped:
        print("(state cap hit; figures are a lower bound)\n")
    bank_code = [0] * nbanks
    for o in code:
        bank_code[o // BANK] += 1

    print(f"{'bank':5} {'code%':>6}  kind")
    tot_code = tot_size = 0
    for b in range(nbanks):
        seg = rom[b * BANK:(b + 1) * BANK]
        if seg[:2] == bytes([0x60, 0x40]):
            kind = "kit"
        elif seg.count(0xFF) + seg.count(0x00) > 0.85 * BANK:
            kind = "empty"
        else:
            kind = "code/data"
        pct = 100 * bank_code[b] // BANK
        if kind == "code/data":
            tot_code += bank_code[b]
            tot_size += BANK
        mark = " *carved" if b in carved else ""
        if kind == "code/data":
            print(f"${b:02X}   {pct:>5}%  {kind}{mark}")
    print(f"\nReachable code in code/data banks: {tot_code}/{tot_size} "
          f"({100*tot_code//max(tot_size,1)}%)  [bank-aware global trace]")


if __name__ == "__main__":
    main()
