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

Local labels that spell a renamed global follow it: at the label's start
(.drawChainCellFar), after a verb (.loadSelectDigitPal for wSelectDigitPal)
or with the labeller's de-duplication letter (.checkIsCGBb). A match that is
really a longer global's name (.songRowPalettes while renaming wSongRow) is
left alone, and a new label that would collide inside its scope (the global
before it by address) stops the rename before anything is written.
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


def label_core(name: str) -> str:
    """The part of a global a local label spells: wSongRow -> SongRow."""
    return name[1:] if name[:1] in ("w", "h") and name[1:2].isupper() else name


def _core_at(label: str, core: str, i: int) -> bool:
    """Whether `core` is spelt at label[i] as a whole camelCase run."""
    if i and not (label[i - 1].islower() or label[i - 1].isdigit()):
        return False
    first = core[0].lower() if i == 0 else core[0].upper()
    if label[i:i + len(core)] != first + core[1:]:
        return False
    tail = label[i + len(core):]
    return not tail or bool(re.match(r"[A-Z0-9_]|[b-z](?:_\d+)?$", tail))


def relabel_text(text: str, pairs: list[tuple[str, str]], cores: set[str], where: str = "") -> tuple[str, int]:
    """Rename the local labels in one .sym file's text; see the module doc."""
    swaps = sorted(((label_core(o), label_core(n)) for o, n in pairs), key=lambda c: -len(c[0]))
    swaps = [(o, n) for o, n in swaps if len(o) >= 5]
    lines = text.split("\n")
    ents = []
    for idx, line in enumerate(lines):
        m = re.match(r"^([0-9A-F]{4}) (\.?)([A-Za-z_]\w*)", line)
        if m:
            ents.append((int(m.group(1), 16), m.group(2) == ".", idx, m.group(3)))
    ents.sort(key=lambda e: (e[0], e[1]))
    scopes: dict[str, list[tuple[int, str]]] = {}
    scope = ""
    for _, local, idx, name in ents:
        if not local:
            scope = name
        else:
            scopes.setdefault(scope, []).append((idx, name))
    count = 0
    for scope, labels in scopes.items():
        renamed = []
        for idx, label in labels:
            new = label
            for old_core, new_core in swaps:
                hit = next((i for i in range(len(label)) if _core_at(label, old_core, i)), None)
                if hit is None or any(len(c) > len(old_core) and c != old_core and _core_at(label, c, hit)
                                      and c.lower().startswith(old_core.lower()) for c in cores):
                    continue
                rep = (new_core[0].lower() if hit == 0 else new_core[0].upper()) + new_core[1:]
                new = label[:hit] + rep + label[hit + len(old_core):]
                break
            renamed.append((idx, label, new))
        names = [n for _, _, n in renamed]
        dups = sorted({n for n in names if names.count(n) > 1})
        if dups and any(o != n for _, o, n in renamed):
            sys.exit(f"error: {where} {scope}: local label(s) {', '.join('.' + d for d in dups)} would collide")
        for idx, old, new in renamed:
            if new != old:
                lines[idx] = re.sub(rf"^([0-9A-F]{{4}} \.){old}(?!\w)", rf"\g<1>{new}", lines[idx])
                count += 1
    return "\n".join(lines), count


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
    cores = {label_core(m.group(1)) for m in re.finditer(r"^(?:[0-9A-F]{4}|DEF) ([A-Za-z_]\w*)", syms, re.M)}
    cores |= {label_core(n) for _, n, _ in pairs}
    relabelled = {}
    for p in (ROOT / "src").glob("bank*.sym"):
        relabelled[p] = relabel_text(p.read_text(), [(o, n) for o, n, _ in pairs], cores, p.name)
    changed = []
    for p, (text, n) in relabelled.items():
        if n:
            p.write_text(text)
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
    labels = sum(n for _, n in relabelled.values())
    print(f"renamed {', '.join(f'{o} -> {n}' for o, n, _ in pairs)} in {len(changed)} file(s)"
          f" and {labels} local label(s): {', '.join(changed)}")


if __name__ == "__main__":
    main()
