#!/usr/bin/env python3
"""The layout of the curated symbol files, and the aligner that keeps it.

src/bankNN.sym lines are `ADDR Name`, with a comment's `;` at column 32
(a longer name takes one space); constants/ram.inc lines are
`DEF Name EQU value`, `EQU` at column 38 and a comment's `;` at column 49,
and a comment continued on the next lines keeps its `;` in the same column.
In the C modules (c/bankNN/*.c) a top-level declaration's comment
(`extern UBYTE wChain;  /* $C418 */`) starts at column 44.
Every tool that writes these files ends with align_files(), and
tools/match/test_tools.py checks they stay aligned. A bank's file names an
address once and a name once: of two entries the tools keep the last, so
the first is dead text.

    tools/symfile.py           # align them in place
    tools/symfile.py --check   # exit 1 if any is not aligned, or names twice
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYM_COMMENT = 32
EQU_COLUMN = 38
RAM_COMMENT = 49
C_COMMENT = 44

SYM_LINE = re.compile(r"^([0-9A-Fa-f]{4} \S+)[ \t]*(;.*)?$")
DEF_LINE = re.compile(r"^(DEF \S+)[ \t]+EQU[ \t]+([^;]*?)[ \t]*(;.*)?$")
CONTINUED = re.compile(r"^[ \t]+(;.*)$")
C_DECLARATION = re.compile(r"^([A-Za-z_][^/\n]*?;)[ \t]*(/\*.*\*/)[ \t]*$")


def pad(text: str, column: int) -> str:
    """`text` padded with spaces to `column`, and at least one space."""
    return text + " " * max(1, column - len(text))


def _lines(text: str, align_line) -> str:
    out = [align_line(line) for line in text.splitlines()]
    return "\n".join(out) + ("\n" if text.endswith("\n") else "")


def align_sym(text: str) -> str:
    def line(s: str) -> str:
        m = SYM_LINE.match(s)
        if not m:
            return s.rstrip()
        return pad(m.group(1), SYM_COMMENT) + m.group(2) if m.group(2) else m.group(1)
    return _lines(text, line)


def align_ram(text: str) -> str:
    def line(s: str) -> str:
        m = DEF_LINE.match(s)
        if m:
            head = pad(m.group(1), EQU_COLUMN) + "EQU " + m.group(2)
            return pad(head, RAM_COMMENT) + m.group(3) if m.group(3) else head
        m = CONTINUED.match(s)
        if m:
            return " " * RAM_COMMENT + m.group(1)
        return s.rstrip()
    return _lines(text, line)


def align_c(text: str) -> str:
    """A C module: each top-level declaration's trailing comment at C_COMMENT."""
    def line(s: str) -> str:
        m = C_DECLARATION.match(s)
        return pad(m.group(1), C_COMMENT) + m.group(2) if m else s
    return _lines(text, line)


def files() -> list[tuple[Path, object]]:
    return ([(p, align_sym) for p in sorted((ROOT / "src").glob("bank*.sym"))]
            + [(ROOT / "constants" / "ram.inc", align_ram)]
            + [(p, align_c) for p in sorted((ROOT / "c").glob("bank*/*.c"))])


def unaligned() -> list[Path]:
    return [p for p, align in files() if align(p.read_text()) != p.read_text()]


def duplicates() -> list[str]:
    """A bank's global symbols that share an address or a name."""
    out = []
    for p in sorted((ROOT / "src").glob("bank*.sym")):
        seen: dict[str, str] = {}
        for line in p.read_text().splitlines():
            m = re.match(r"^([0-9A-Fa-f]{4}) ([A-Za-z_]\w*)", line)
            if not m:
                continue
            for key in (m.group(1).upper(), m.group(2)):
                if key in seen:
                    out.append(f"{p.name}: {m.group(2)} ({m.group(1)}) repeats {seen[key]}")
                seen[key] = f"{m.group(2)} ({m.group(1)})"
    return out


def align_files() -> list[Path]:
    """Align every curated symbol file in place; the ones that changed."""
    changed = []
    for p, align in files():
        text = p.read_text()
        new = align(text)
        if new != text:
            p.write_text(new)
            changed.append(p)
    return changed


def main() -> int:
    if sys.argv[1:] == ["--check"]:
        bad = unaligned()
        for p in bad:
            print(f"{p.relative_to(ROOT)}: not aligned (tools/symfile.py aligns it)")
        twice = duplicates()
        for line in twice:
            print(line)
        return 1 if bad or twice else 0
    if sys.argv[1:]:
        print(__doc__)
        return 2
    for p in align_files():
        print(f"aligned {p.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
