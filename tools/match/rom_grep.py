#!/usr/bin/env python3
"""Count a byte pattern across the ROM's code and say whose code it is in.

A new peephole rule needs ROM-wide evidence (AGENTS.md): how often the stock
"before" shape occurs in LSDj's compiled code (ideally never) against the
rewritten "after" shape. This finds every occurrence in the code banks,
names the function it falls in, and tags it: `C` (matching C owns it),
`unmatched` (a C candidate per code_kind.py), `asm`/`lib`, or `data`.

Pattern syntax: hex bytes separated by spaces; `??` any byte; `c2|ca`
alternatives; `40-4f` a range; parentheses group (and capture) tokens;
`\\1` repeats capture 1. Or pass a Python bytes regex with -e.

Examples:
  tools/match/rom_grep.py "e6 ?? 20 03 c3 ?? ?? c3"      # and n; jr nz,+3; jp; jp
  tools/match/rom_grep.py "21 (?? ??) 7e b7 c2|ca ?? ?? 21 \\1" --list
  tools/match/rom_grep.py -e '\\x2a\\xb6[\\xc2\\xca]..(.)' --tally   # what follows
"""

from __future__ import annotations

import argparse
import importlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
coverage = importlib.import_module("coverage")   # tools/coverage.py

CODE_BANKS = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F]


def compile_pattern(text: str) -> re.Pattern[bytes]:
    out = []
    for tok in text.split():
        pre = post = ""
        while tok.startswith("("):
            pre += "("
            tok = tok[1:]
        while tok.endswith(")"):
            post += ")"
            tok = tok[:-1]
        if tok in ("??", ".."):
            body = "."
        elif re.fullmatch(r"\\\d", tok):
            body = tok
        elif re.fullmatch(r"[0-9a-fA-F]{2}-[0-9a-fA-F]{2}", tok):
            lo, hi = tok.split("-")
            body = f"[\\x{lo}-\\x{hi}]"
        elif re.fullmatch(r"[0-9a-fA-F]{2}(\|[0-9a-fA-F]{2})+", tok):
            body = "[" + "".join(f"\\x{b}" for b in tok.split("|")) + "]"
        elif re.fullmatch(r"[0-9a-fA-F]{2}", tok):
            body = f"\\x{tok}"
        else:
            sys.exit(f"error: bad pattern token {tok!r}")
        out.append(pre + body + post)
    return re.compile("".join(out).encode(), re.S)


def owners() -> tuple[dict[int, list[tuple[int, str, bool]]], dict[int, list[tuple[int, int]]]]:
    """bank -> sorted (address, global symbol, is data); bank -> matched C ranges."""
    syms: dict[int, list[tuple[int, str, bool]]] = {}
    for p in (ROOT / "src").glob("bank*.sym"):
        bank = int(p.stem[4:6], 16)
        syms[bank] = sorted((a, n, coverage.is_data_symbol(n, c))
                            for a, n, c in coverage.read_sym(p)
                            if not n.startswith(".") and "@deploy-only" not in c)
    matched: dict[int, list[tuple[int, int]]] = {}
    mp = ROOT / "c" / "matched.json"
    if mp.is_file():
        for m in json.loads(mp.read_text()).values():
            a = int(m["addr"], 16)
            matched.setdefault(int(m["bank"], 16), []).append((a, a + int(m["len"])))
    return syms, matched


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pattern", nargs="?", help="hex pattern (see above)")
    ap.add_argument("-e", "--regex", help="a Python bytes regex instead, e.g. '\\x7b\\x9a'")
    ap.add_argument("--rom", type=Path, default=ROOT / "lsdj9_4_0.gb")
    ap.add_argument("--all-banks", action="store_true",
                    help="also search the sample-kit and empty banks")
    ap.add_argument("--list", action="store_true", help="print every hit")
    ap.add_argument("--tally", action="store_true",
                    help="count hits by the bytes of capture group 1")
    ap.add_argument("--kinds", action="store_true",
                    help="tag unmatched hits with code_kind.py's verdict (slower)")
    args = ap.parse_args()
    if bool(args.pattern) == bool(args.regex):
        ap.error("give a hex pattern or -e REGEX")
    pat = (re.compile(args.regex.encode("latin1"), re.S)     # the engine reads \xNN itself
           if args.regex else compile_pattern(args.pattern))
    rom = args.rom.read_bytes()
    syms, matched = owners()
    kinds: dict[str, str] = {}
    if args.kinds:
        code_kind = importlib.import_module("code_kind")
        for p in sorted((ROOT / "src").glob("bank*.asm")):
            for n, (k, _c) in code_kind.classify_bank(p).items():
                kinds[f"{int(p.stem[4:], 16):02X}:{n}"] = k

    banks = range(len(rom) // 0x4000) if args.all_banks else CODE_BANKS
    tags: Counter[str] = Counter()
    tally: Counter[str] = Counter()
    hits = 0
    for bank in banks:
        base = bank * 0x4000
        org = 0 if bank == 0 else 0x4000
        for m in pat.finditer(rom, base, base + 0x4000):
            hits += 1
            addr = m.start() - base + org
            entries = syms.get(bank, [])
            owner = next(((a, n, d) for a, n, d in reversed(entries) if a <= addr), None)
            if any(lo <= addr < hi for lo, hi in matched.get(bank, [])):
                tag = "C"
            elif owner and owner[2]:
                tag = "data"
            elif args.kinds and owner:
                tag = {"C": "unmatched", "C?": "unmatched?"}.get(
                    kinds.get(f"{bank:02X}:{owner[1]}", ""), kinds.get(f"{bank:02X}:{owner[1]}", "?"))
            else:
                tag = "unmatched"
            tags[tag] += 1
            if args.tally and m.groups():
                tally[m.group(1).hex(" ")] += 1
            if args.list:
                where = f"{owner[1]}+${addr - owner[0]:X}" if owner else "?"
                print(f"  {bank:02X}:{addr:04X}  {tag:10s} {where:36s} "
                      f"{rom[m.start():m.start() + max(len(m.group(0)), 4)].hex(' ')}")
    summary = ", ".join(f"{n} {t}" for t, n in tags.most_common())
    print(f"{hits} hit(s){': ' + summary if summary else ''}")
    for k, n in tally.most_common():
        print(f"  {n:5d}  group 1 = {k}")


if __name__ == "__main__":
    main()
