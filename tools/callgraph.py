#!/usr/bin/env python3
"""Find unlabeled control-flow targets (the debugger's `_LABEL_` gaps).

The per-bank disassembler only labels call/jp/jr targets it sees *within* a bank,
so a bank-0 routine called only from another bank never got a label -- Emulicious
then shows `_LABEL_xxxx`. This scans every carved bank's asm for raw-hex CF
operands (`call $07da`), resolves the target bank ($0000-$3FFF -> bank 0, else the
source bank), drops any that already have a symbol, and prints each remaining
target with its reference count and the first instructions at the target -- so it
can be reverse-engineered and named in `src/bankNN.sym`.

Usage:
    tools/callgraph.py                 # summary: unlabeled CF targets per bank
    tools/callgraph.py --bank 0        # detail: targets in bank 0, with context
    tools/callgraph.py --bank 0 --min 3   # only targets referenced >= 3 times
"""
from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
SYM = ROOT / "build" / "lsdj9_4_0.sym"

CF_RE = re.compile(r"\b(?:call|jp|jr)(?: n?[zc],)? \$([0-9a-f]{4})\b")
ADDR_RE = re.compile(r";\s*\$([0-9a-f]{4})\b")
CODE_RE = re.compile(r";\s*\$([0-9a-f]{4}):")   # instruction (has ": bytes"); db has no colon


def load_symbols() -> set[tuple[int, int]]:
    syms: set[tuple[int, int]] = set()
    for line in SYM.read_text().splitlines():
        m = re.match(r"^([0-9a-f]{2,3}):([0-9a-f]{4})", line)
        if m:
            syms.add((int(m.group(1), 16), int(m.group(2), 16)))
    return syms


def bank_asm_lines(bank: int) -> list[str]:
    p = SRC / f"bank{bank:02x}.asm"
    return p.read_text().splitlines() if p.is_file() else []


def index_by_addr(lines: list[str]) -> dict[int, int]:
    """gb addr -> line index (for looking up a target's code)."""
    idx: dict[int, int] = {}
    for i, line in enumerate(lines):
        m = ADDR_RE.search(line)
        if m:
            idx[int(m.group(1), 16)] = i
    return idx


A_IMM = re.compile(r"\bld a, \$([0-9a-f]{2})\b")
HL2000 = re.compile(r"\bld hl, \$2000\b")
HLSET = re.compile(r"\bld \[hl\], \$([0-9a-f]{2})\b")
BANKW = re.compile(r"ldh \[hCurrentBank\], a|ld \[\$2000\], a")


def collect() -> dict[tuple[int, int], int]:
    """(target_bank, addr) -> reference count, for unlabeled CF targets.

    Tracks the bank mapped at $4000-$7FFF by following `ld [$2000],N` writes, so a
    `call $4xxx` from bank 0 resolves to the bank paged in just before it (LSDj's
    dispatcher pattern: `ld [$2000],$01; call $4cf1`)."""
    targets: Counter[tuple[int, int]] = Counter()
    for p in sorted(SRC.glob("bank*.asm")):
        bank = int(p.stem[4:6], 16)
        cur = bank                           # bank mapped at $4000-$7FFF
        last_a: int | None = None
        hl_is_2000 = False
        for line in p.read_text().splitlines():
            t = line.split(";", 1)[0]
            if (m := A_IMM.search(t)):
                last_a = int(m.group(1), 16)
            if HL2000.search(t):
                hl_is_2000 = True
            elif "ld hl," in t:
                hl_is_2000 = False
            if (m := HLSET.search(t)) and hl_is_2000:
                cur = int(m.group(1), 16)    # ld hl,$2000; ld [hl],N
            elif BANKW.search(t) and last_a is not None:
                cur = last_a                 # ld a,N; ld [$2000],a / ldh [hCurrentBank],a
            if not (m := CF_RE.search(t)):
                continue
            a = int(m.group(1), 16)
            if a >= 0x8000:
                continue                     # not ROM (mis-carved data byte)
            tb = 0 if a < 0x4000 else cur    # home, else the mapped bank
            targets[(tb, a)] += 1
    syms = load_symbols()
    # A real function target is a *code* address (instruction) in its bank; a
    # target that lands in a `db` region is a jump-table-into-data phantom.
    code: dict[int, set[int]] = {}
    for p in sorted(SRC.glob("bank*.asm")):
        bank = int(p.stem[4:6], 16)
        # Code line: has "; $XXXX:" and its instruction is not `db` (hex db lines
        # also carry the ": bytes" annotation, so the colon alone isn't enough).
        code[bank] = {int(m.group(1), 16)
                      for line in p.read_text().splitlines()
                      if (m := CODE_RE.search(line)) and not line.lstrip().startswith(("db", "ds"))}
    return {(b, a): v for (b, a), v in targets.items()
            if (b, a) not in syms and a in code.get(b, set())}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bank", type=lambda s: int(s, 0), help="detail one bank")
    ap.add_argument("--min", type=int, default=1, help="min reference count")
    args = ap.parse_args()

    gaps = collect()
    if args.bank is None:
        per_bank = Counter(b for (b, _a) in gaps)
        print(f"{'bank':>4} {'unlabeled CF targets':>22}")
        for b in sorted(per_bank):
            print(f"  ${b:02X} {per_bank[b]:>20}")
        print(f"\nTOTAL: {len(gaps)}")
        return

    lines = bank_asm_lines(args.bank)
    idx = index_by_addr(lines)
    items = sorted(((v, a) for (b, a), v in gaps.items() if b == args.bank),
                   reverse=True)
    print(f"--- bank ${args.bank:02X}: {len(items)} unlabeled CF targets "
          f"(>= {args.min} refs) ---")
    for cnt, a in items:
        if cnt < args.min:
            continue
        snippet = ""
        if a in idx:
            body = []
            for line in lines[idx[a]:idx[a] + 3]:
                t = line.split(";")[0].strip()
                if t:
                    body.append(t)
            snippet = "  |  ".join(body)
        print(f"  ${a:04X}  x{cnt:<3}  {snippet}")


if __name__ == "__main__":
    main()
