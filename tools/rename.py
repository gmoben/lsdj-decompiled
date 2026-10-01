#!/usr/bin/env python3
"""Rename a ROM/RAM symbol everywhere it is written down.

    tools/rename.py OLD NEW ["new .sym comment"] [OLD2 NEW2 ["comment"]] ...

Whole-word replacement in src/*.sym, constants/*.inc, c/ (sources and
headers), docs/*.md, AGENTS.md, README.md and tools/ (Python). Refuses a NEW
name that is already a symbol. With a comment, the global symbol's `.sym`
comment is replaced (column kept aligned). `make` regenerates the asm; the
bytes cannot change, so `make compare` stays green.

Pairs are given as consecutive arguments; a pair's optional comment is any
argument containing a space (names never do).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def files() -> list[Path]:
    out = list((ROOT / "src").glob("bank*.sym")) + list((ROOT / "constants").glob("*.inc"))
    out += [p for p in (ROOT / "c").rglob("*") if p.suffix in (".c", ".h")
          and not p.name.startswith((".", "#"))]   # not editor lock/autosave files
    out += list((ROOT / "docs").glob("*.md")) + [ROOT / "AGENTS.md", ROOT / "README.md"]
    out += [p for p in (ROOT / "tools").rglob("*.py") if "__pycache__" not in p.parts]
    return [p for p in out if p.is_file()]


def parse(argv: list[str]) -> list[tuple[str, str, str | None]]:
    out: list[tuple[str, str, str | None]] = []
    i = 0
    while i < len(argv):
        old, new = argv[i], argv[i + 1]
        comment = None
        i += 2
        if i < len(argv) and " " in argv[i]:
            comment = argv[i]
            i += 1
        out.append((old, new, comment))
    return out


def main() -> None:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    pairs = parse(sys.argv[1:])
    syms = "\n".join(p.read_text() for p in (ROOT / "src").glob("bank*.sym"))
    syms += (ROOT / "constants" / "ram.inc").read_text()
    for old, new, _ in pairs:
        if not re.search(rf"\b{re.escape(old)}\b", syms):
            sys.exit(f"error: {old} is not a symbol")
        # defined names only (`ADDR NAME` / `DEF NAME`); comments may mention NEW
        if re.search(rf"^(?:[0-9A-F]{{4}}|DEF) {re.escape(new)}\b", syms, re.M):
            sys.exit(f"error: {new} is already a symbol")
    changed = []
    for f in files():
        s = f.read_text()
        t = s
        for old, new, _ in pairs:
            t = re.sub(rf"(?<![\w$.]){re.escape(old)}\b", new, t)
        if t != s:
            f.write_text(t)
            changed.append(f.relative_to(ROOT).as_posix())
    for old, new, comment in pairs:
        if comment is None:
            continue
        for p in (ROOT / "src").glob("bank*.sym"):
            s = p.read_text()
            s2, n = re.subn(rf"^([0-9A-F]{{4}}) ({re.escape(new)})\s+;.*$",
                            lambda m: f"{m.group(1)} {m.group(2):26s} ; {comment}", s, flags=re.M)
            if n:
                p.write_text(s2)
    print(f"renamed {', '.join(f'{o} -> {n}' for o, n, _ in pairs)} in {len(changed)} file(s): {', '.join(changed)}")


if __name__ == "__main__":
    main()
