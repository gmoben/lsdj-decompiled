#!/usr/bin/env python3
"""Refresh the Track 2 progress numbers quoted in the docs.

Reads c/matched.json (functions, banks, bytes, per-file counts),
tools/match/lsdj.peep (rule count) and, when the banks are carved,
tools/match/code_kind.py's count of hand-written asm and library routines,
and rewrites the sentences that quote them in README.md, AGENTS.md,
docs/toolchain.md, docs/matching_c.md, docs/reversal.md and docs/rom_map.md, plus
the "N functions" cell of each multi-function row of the matched-functions
table, and regenerates the parked-functions table from c/parked.txt (one
line per function: NAME BB:AAAA -- what differs). Run after `make match`;
prints what changed.
"""

from __future__ import annotations

import importlib
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))


def asm_counts() -> tuple[int, int, int] | None:
    """(hand-written asm functions, their bytes, library routines) from the
    carved banks, or None before a carve."""
    banks = sorted((ROOT / "src").glob("bank*.asm"))
    if not banks:
        return None
    code_kind = importlib.import_module("code_kind")
    n = nb = lib = 0
    for p in banks:
        for kind, _count, nbytes in code_kind.classify_bank_full(p).values():
            if kind == "asm":
                n, nb = n + 1, nb + nbytes
            lib += kind == "lib"
    return n, nb, lib


def main() -> None:
    matched = json.loads((ROOT / "c" / "matched.json").read_text())
    n = len(matched)
    banks = len({m["bank"] for m in matched.values()})
    nbytes = sum(int(m["len"]) for m in matched.values())
    per_file = Counter(m["src"] for m in matched.values())
    rules = len(re.findall(r"^replace", (ROOT / "tools/match/lsdj.peep").read_text(), re.M))
    words = {1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven",
             8: "eight", 9: "nine", 10: "ten", 11: "eleven", 12: "twelve"}
    bw = words.get(banks, str(banks))
    subs = [
        (r"\*\*Yes — \d+\*\*, in \w+ banks", f"**Yes — {n}**, in {bw} banks"),
        (r"grind has begun: \d+ functions", f"grind has begun: {n} functions"),
        (r"plus \d+\npeephole rules", f"plus {rules}\npeephole rules"),
        (r"plus \d+ peephole rules", f"plus {rules} peephole rules"),
        (r"pass plus \d+ rules", f"pass plus {rules} rules"),
        (r"An accumulator-tracking pass plus \d+\n   rules", f"An accumulator-tracking pass plus {rules}\n   rules"),
        (r"\*\*Status:\*\* \d+ functions match", f"**Status:** {n} functions match"),
        (r"\d+ functions in \w+ banks match so far", f"{n} functions in {bw} banks match so far"),
        (r"mechanism is in place \(\d+ functions\)", f"mechanism is in place ({n} functions)"),
        (r"\d+ functions in \w+ banks are C in `c/`", f"{n} functions in {bw} banks are C in `c/`"),
        (r"\d+ functions \(\d+ bytes\) are C", f"{n} functions ({nbytes} bytes) are C"),
    ]
    asm = asm_counts()
    if asm:
        n_asm, b_asm, n_lib = asm
        kb = f"{b_asm / 1000:.1f} KB"
        subs += [
            (r"\b\d+ hand-written asm functions", f"{n_asm} hand-written asm functions"),
            (r"(\d+\s+hand-written asm functions[^.]{0,90}?)\b\d+\.\d KB", rf"\g<1>{kb}"),
            (r"\b\d+ GBDK/SDCC library routines", f"{n_lib} GBDK/SDCC library routines"),
        ]
    parked = []
    pf = ROOT / "c" / "parked.txt"
    if pf.is_file():
        for line in pf.read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                name, where, note = line.split(None, 2)
                note = note.lstrip("- ").strip().replace("|", "\\|")
                parked.append(f"| `{name}` | `{where}` | {note} |")
    changed = []
    for rel in ("README.md", "AGENTS.md", "docs/toolchain.md", "docs/matching_c.md", "docs/reversal.md",
                "docs/rom_map.md"):
        p = ROOT / rel
        s = t = p.read_text()
        for pat, rep in subs:
            t = re.sub(pat, rep, t)
        if rel == "docs/matching_c.md" and parked:
            head = "| Function | Where | What differs |\n|----------|-------|--------------|\n"
            t = re.sub(r"(## Parked functions\n.*?\n\n)\| Function \| Where .*?\n\n",
                       lambda m: m.group(1) + head + "\n".join(parked) + "\n\n", t, flags=re.S)
            t = re.sub(r"(\| `[^\n]*\|)\n\n\n+## What's still unknown", r"\1\n\n## What's still unknown", t)
        if rel == "docs/matching_c.md":
            for src, k in per_file.items():
                t = re.sub(rf"(\[`{re.escape(src)}`\]\([^)]*\) \| )\d+ functions", rf"\g<1>{k} functions", t)
        if t != s:
            p.write_text(t)
            changed.append(rel)
    asm_note = f", {asm[0]} asm functions ({asm[1]} bytes), {asm[2]} library" if asm else ""
    print(f"{n} functions, {banks} banks, {nbytes} bytes, {rules} rules{asm_note}; "
          f"updated: {', '.join(changed) or 'nothing'}")


if __name__ == "__main__":
    main()
