#!/usr/bin/env python3
"""Move matching functions from a work-in-progress C file into a module.

    tools/match/merge_c.py BATCH.c c/bankNN/Module.c [NAME ...]

Every function defined in BATCH.c (or only the NAMEs given) that matches the
ROM (checked with verify_all.py first) is added to the module -- replacing a
definition of the same name -- and the module's functions are re-sorted into
ROM order (src/*.sym addresses). A function's leading comment block moves
with it. Declarations in BATCH.c are not copied: settle prototypes in
c/include/lsdj.h and module-private data externs in the module by hand.
Creates the module (with a stub header comment) if it does not exist.
"""

from __future__ import annotations

import importlib
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
mf = importlib.import_module("match_function")

DEF = re.compile(r"^(?:[A-Za-z_][\w \t*]*?\b)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:nonbanked|banked|critical)?\s*$")


def split(src: str) -> tuple[str, list[tuple[str, str]]]:
    """(preamble, [(name, chunk)]) -- a chunk is a definition plus the comment
    block directly above it, up to the closing brace at column 0."""
    lines = src.splitlines()
    starts: list[tuple[int, str]] = []
    for i, line in enumerate(lines):
        m = DEF.match(line.strip())
        if m and not line.startswith((" ", "\t", "#", "/", "*")):
            nxt = lines[i + 1].strip() if i + 1 < len(lines) else ""
            if nxt.startswith("{") or line.rstrip().endswith("{"):
                j = i
                while j > 0 and lines[j - 1].strip().startswith(("/*", "*", "*/")) and not lines[j - 1].strip() == "":
                    j -= 1
                starts.append((j, m.group(1)))
    if not starts:
        return src, []
    pre = "\n".join(lines[:starts[0][0]]).rstrip() + "\n"
    chunks = []
    begin = starts[0][0]
    for k, (j, name) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(lines)
        # stop at the function's closing brace
        close = j + max(n for n, l in enumerate(lines[j:end]) if l.startswith("}"))
        # a chunk carries whatever precedes its function since the previous
        # one closed -- file-scope data such as a const table placed right
        # before it (UiHelpers.c's SpeedGlyphTiles) travels with it
        chunks.append((name, "\n".join(lines[begin:close + 1]).strip("\n") + "\n"))
        begin = close + 1
    return pre, chunks


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    batch, module = Path(sys.argv[1]), Path(sys.argv[2])
    only = set(sys.argv[3:])
    r = subprocess.run([sys.executable, str(HERE / "verify_all.py"), str(batch.resolve())],
                       capture_output=True, text=True)
    matched = {m.group(1) for m in re.finditer(r"^\s+(\w+)\s+\$\w+:\$\w+\s+\d+ B\s+MATCH\b", r.stdout, re.M)}
    _, new = split(batch.read_text())
    take = {n: c for n, c in new if n in matched and (not only or n in only)}
    if not take:
        sys.exit("nothing to merge (no matching functions)")
    if module.is_file():
        pre, old = split(module.read_text())
    else:
        bank = module.parent.name
        pre = (f"/*\n * TODO: describe this module -- LSDj 9.4.0, ROM {bank}.\n *\n"
               f" * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);\n"
               f" * `make match` verifies every function here against the ROM.\n */\n\n"
               f"#include \"lsdj.h\"\n")
        old = []
    chunks = dict(old)
    chunks.update(take)
    syms = mf.load_symbols()
    order = sorted(chunks, key=lambda n: (syms.get(n, (0xFF, 0xFFFF))[1], n))
    module.parent.mkdir(parents=True, exist_ok=True)
    module.write_text(pre.rstrip() + "\n\n" + "\n".join(chunks[n] for n in order))
    skipped = sorted(n for n, _ in new if n not in take)
    print(f"merged {len(take)} function(s) into {module}: {', '.join(sorted(take))}")
    if skipped:
        print(f"not merged (no match or not selected): {', '.join(skipped)}")


if __name__ == "__main__":
    main()
