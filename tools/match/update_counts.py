#!/usr/bin/env python3
"""Refresh the Track 2 progress numbers quoted in the docs.

Reads c/matched.json (functions, banks, bytes, per-file counts) and
tools/match/lsdj.peep (rule count) and rewrites the sentences that quote
them in README.md, AGENTS.md, docs/toolchain.md and docs/reversal.md, plus
the "N functions" cell of each multi-function row of the matched-functions
table. Run after `make match`; prints what changed.
"""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


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
    changed = []
    for rel in ("README.md", "AGENTS.md", "docs/toolchain.md", "docs/reversal.md"):
        p = ROOT / rel
        s = t = p.read_text()
        for pat, rep in subs:
            t = re.sub(pat, rep, t)
        if rel == "docs/toolchain.md":
            for src, k in per_file.items():
                t = re.sub(rf"(\[`{re.escape(src)}`\]\([^)]*\) \| )\d+ functions", rf"\g<1>{k} functions", t)
        if t != s:
            p.write_text(t)
            changed.append(rel)
    print(f"{n} functions, {banks} banks, {nbytes} bytes, {rules} rules; updated: {', '.join(changed) or 'nothing'}")


if __name__ == "__main__":
    main()
