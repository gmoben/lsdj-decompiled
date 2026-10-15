#!/usr/bin/env python3
"""Group the not-yet-matched C functions by instruction *shape* (Track 2 grind).

Operands (addresses, symbols, immediates) are abstracted to #1, #2, ... in
order of first appearance and local labels to L1, L2, ...; functions with the
same shape were compiled from the same C template, so one template matches a
whole family. Excluded: functions already owned by matching C
(c/matched.json), data symbols, and hand-written asm (code_kind.py).

Usage: tools/match/shapes.py [--min N]            # families with >= N members
       tools/match/shapes.py --show NAME [NAME..]  # compact asm of functions
       tools/match/shapes.py --list [--max-bytes B] [--bank BB] [--no-parked] [--tiny]
                                                   # unmatched C functions by size: kind
                                                   # (C, or C? for an ambiguous leaf),
                                                   # P = parked (c/parked.txt), .sym note
"""

from __future__ import annotations

import argparse
import importlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
code_kind = importlib.import_module("code_kind")
coverage = importlib.import_module("coverage")

LABEL = re.compile(r"^([A-Za-z_]\w*)::?")
LOCAL = re.compile(r"^(\.\w+):")
ADDR = re.compile(r";\s*\$([0-9a-f]{4})")
REGS = {"a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp",
        "nz", "z", "nc", "[hl]", "[hl+]", "[hl-]", "[bc]", "[de]", "[c]"}


class Func:
    def __init__(self, bank: int, name: str, addr: int):
        self.bank, self.name, self.addr = bank, name, addr
        self.lines: list[str] = []      # instructions and ".local:" lines
        self.size = 0
        self.kind = "?"                 # code_kind verdict: C / C? / asm / lib / tiny
        self.note = ""                  # the .sym comment

    @property
    def key(self) -> str:
        return f"{self.bank:02X}:{self.name}"


def load() -> list[Func]:
    matched = json.loads((ROOT / "c" / "matched.json").read_text()) if (ROOT / "c" / "matched.json").is_file() else {}
    owned = [(int(m["bank"], 16), int(m["addr"], 16), int(m["addr"], 16) + int(m["len"])) for m in matched.values()]
    data_names = set()
    notes: dict[tuple[int, str], str] = {}
    syms: dict[tuple[int, str], int] = {}
    for p in (ROOT / "src").glob("bank*.sym"):
        for a, n, c in coverage.read_sym(p):
            if coverage.is_data_symbol(n, c):
                data_names.add(n)
            notes[(int(p.stem[4:6], 16), n)] = c
            syms[(int(p.stem[4:6], 16), n)] = a
    funcs: list[Func] = []
    for p in sorted((ROOT / "src").glob("bank*.asm")):
        bank = int(p.stem[4:], 16)
        kinds = code_kind.classify_bank(p)
        cur: Func | None = None
        pending_addr = False
        for line in p.read_text().splitlines():
            m = LABEL.match(line)
            if m:
                cur = Func(bank, m.group(1), -1)
                funcs.append(cur)
                pending_addr = True
                continue
            if cur is None:
                continue
            a = ADDR.search(line)
            if pending_addr and a:
                cur.addr = int(a.group(1), 16)
                pending_addr = False
            s = line.split(";")[0].strip()
            if not s:
                continue
            if LOCAL.match(s):
                cur.lines.append(s)
            elif not s.startswith(("SECTION",)):
                cur.lines.append(s)
        for f in funcs:
            if f.bank == bank and f.name in kinds:
                f.kind = kinds[f.name][0]
            if f.bank == bank:
                f.note = notes.get((bank, f.name), "")
    out = []
    for i, f in enumerate(funcs):
        if f.addr < 0:                  # carved as data: no address comment
            f.addr = syms.get((f.bank, f.name), -1)
        nxt = next((g.addr for g in funcs[i + 1:] if g.bank == f.bank and g.addr > f.addr), None)
        f.size = (nxt - f.addr) if nxt else 0
        if f.addr < 0 or f.name in data_names or f.kind in ("asm", "lib", "matched"):
            continue
        if any(b == f.bank and lo <= f.addr < hi for b, lo, hi in owned):
            continue
        if any(l.startswith(("db", "dw", "ds", "RGB", "INCBIN")) for l in f.lines) and f.kind != "C":
            continue                    # (code_kind calls a `db` carve with SDCC's prologue C)
        out.append(f)
    return out


def parked_names() -> set[str]:
    p = ROOT / "c" / "parked.txt"
    if not p.is_file():
        return set()
    return {l.split()[0] for l in p.read_text().splitlines() if l.strip() and not l.startswith("#")}


def by_size(f: Func) -> int:
    return f.size


def shape(f: Func) -> tuple[str, list[str]]:
    vals: list[str] = []
    labels: dict[str, str] = {}
    norm = []
    for l in f.lines:
        lm = LOCAL.match(l)
        if lm:
            labels.setdefault(lm.group(1), f"L{len(labels) + 1}")
            norm.append(labels[lm.group(1)] + ":")
            continue
        parts = l.split(None, 1)
        mn, ops = parts[0], (parts[1] if len(parts) > 1 else "")
        new_ops = []
        for op in [o.strip() for o in ops.split(",")] if ops else []:
            if op in REGS or re.fullmatch(r"sp\+-?\d+|-?\d+", op):
                new_ops.append(op)
                continue
            if op.startswith("."):
                labels.setdefault(op, f"L{len(labels) + 1}")
                new_ops.append(labels[op])
                continue
            if op.startswith("[") and op.endswith("]") and op not in REGS:
                inner = op[1:-1]
                if inner not in vals:
                    vals.append(inner)
                new_ops.append(f"[#{vals.index(inner) + 1}]")
                continue
            if op not in vals:
                vals.append(op)
            new_ops.append(f"#{vals.index(op) + 1}")
        norm.append(mn + (" " + ",".join(new_ops) if new_ops else ""))
    return " | ".join(norm), vals


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--min", type=int, default=3)
    ap.add_argument("--show", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--max-bytes", type=int, default=10**6)
    ap.add_argument("--bank", action="append", default=[], metavar="BB",
                    help="with --list: only this bank (hex; repeatable)")
    ap.add_argument("--no-parked", action="store_true", help="with --list: hide parked functions")
    ap.add_argument("--tiny", action="store_true",
                    help="with --list: include leaves under 6 instructions")
    args = ap.parse_args()
    funcs = load()
    if args.show:
        by = {f.name: f for f in funcs}
        for n in args.show:
            f = by.get(n)
            if not f:
                print(f"{n}: not an unmatched C function")
                continue
            print(f"== {f.key} ${f.addr:04X} {f.size}B")
            print("\n".join("  " + l for l in f.lines))
        return
    if args.list:
        parked = parked_names()
        banks = {int(b, 16) for b in args.bank}
        rows = [f for f in funcs
                if (f.kind in ("C", "C?") or (args.tiny and f.kind == "tiny"))
                and (not banks or f.bank in banks)
                and not (args.no_parked and f.name in parked)
                and f.size <= args.max_bytes]
        for f in sorted(rows, key=by_size):
            print(f"{f.size:5d}  {f.kind:4s} {'P' if f.name in parked else ' '} "
                  f"{f.key:30s} ${f.addr:04X}  {f.note[:60]}")
        pk = [f for f in rows if f.name in parked]
        print(f"{len(rows)} unmatched C functions, {sum(f.size for f in rows)} bytes "
              f"({len(pk)} parked, {sum(f.size for f in pk)} bytes)")
        return
    fam: dict[str, list[tuple[Func, list[str]]]] = defaultdict(list)
    for f in funcs:
        s, vals = shape(f)
        fam[s].append((f, vals))
    rows = sorted(fam.items(), key=lambda kv: -len(kv[1]))
    total = 0
    for s, members in rows:
        if len(members) < args.min:
            continue
        total += len(members)
        print(f"\n[{len(members)} x] {s}")
        for f, vals in members[:4]:
            print(f"    {f.key:30s} {vals}")
        if len(members) > 4:
            print(f"    ... {', '.join(m[0].name for m in members[4:12])}{' ...' if len(members) > 12 else ''}")
    print(f"\n{len(funcs)} unmatched C functions; {total} in families of >= {args.min}")


if __name__ == "__main__":
    main()
