#!/usr/bin/env python3
"""Fold a mislabelled "function" into the function it is part of.

    tools/fold_sym.py NAME [.localname] [NAME2 [.localname2]] ...

A global symbol that is really a case body, a shared tail or the middle of a
loop (it jumps into its neighbour, uses the neighbour's stack frame, or ends
with the neighbour's epilogue) becomes a local label of the global before
it: its `src/bankNN.sym` line turns into `ADDR .localname ; (was NAME) ...`.
Default local name: NAME with a lowercase first letter. References to NAME
elsewhere in the .sym comments, C, docs and tools are rewritten to
`Enclosing.localname`. `make` regenerates the asm (the bytes are unchanged);
a duplicate local name shows up as an assembler error there.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GLOBAL = re.compile(r"^([0-9A-F]{4}) ([A-Za-z_]\w*)(\s+;.*)?$")


def main() -> None:
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    pairs: list[tuple[str, str]] = []
    while args:
        name = args.pop(0)
        local = args.pop(0) if args and args[0].startswith(".") else "." + name[0].lower() + name[1:]
        pairs.append((name, local))
    for name, local in pairs:
        for sym in sorted((ROOT / "src").glob("bank*.sym")):
            lines = sym.read_text().splitlines()
            hit = next((i for i, l in enumerate(lines)
                        if (m := GLOBAL.match(l)) and m.group(2) == name), None)
            if hit is None:
                continue
            m0 = GLOBAL.match(lines[hit])
            assert m0 is not None
            addr = int(m0.group(1), 16)
            # the enclosing function: the closest global below this address
            glob = [(int(m.group(1), 16), m.group(2)) for l in lines if (m := GLOBAL.match(l))]
            enclosing = max((g for g in glob if g[0] < addr), default=None)
            if enclosing is None:
                sys.exit(f"error: nothing encloses {name}")
            comment = (m0.group(3) or "").strip().lstrip(";").strip()
            lines[hit] = f"{addr:04X} {local:20s} ; (was {name}) {comment}".rstrip()
            sym.write_text("\n".join(lines) + "\n")
            qualified = f"{enclosing[1]}{local}"
            files = list((ROOT / "src").glob("bank*.sym")) + list((ROOT / "docs").glob("*.md"))
            files += [p for p in (ROOT / "c").rglob("*") if p.suffix in (".c", ".h")]
            files += [ROOT / "AGENTS.md", ROOT / "README.md"]
            for f in files:
                s = f.read_text()
                t = re.sub(rf"(?<![\w.]){re.escape(name)}\.(\w+)", rf"{enclosing[1]}.\1", s)
                # plain mentions (comments too), except the folded line's `(was NAME)`
                t = "\n".join(l if l.startswith(f"{addr:04X} ") and f == sym else
                              re.sub(rf"(?<![\w.(]){re.escape(name)}\b(?!\))", qualified, l)
                              for l in t.split("\n"))
                if t != s:
                    f.write_text(t)
            print(f"{name} -> {qualified} (${addr:04X}, {sym.name})")
            break
        else:
            sys.exit(f"error: {name} is not a global symbol")


if __name__ == "__main__":
    main()
