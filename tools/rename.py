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

A name C sees must survive as-gbz80, which keeps 32 characters of a symbol
(NCPS) and C prefixes an underscore: such a NEW must be unique in its first
31 characters, and a C function's at most 27 long (its ___<name>_start and
___<name>_end labels would otherwise be cut to the same string).
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOKEN = re.compile(r"(?<![\w$.])[A-Za-z_]\w*")
C_NAME_MAX, C_FUNC_MAX = 31, 27


def files() -> list[Path]:
    out = list((ROOT / "src").glob("bank*.sym")) + list((ROOT / "constants").glob("*.inc"))
    out += [p for p in (ROOT / "c").rglob("*") if p.suffix in (".c", ".h")
            and not p.name.startswith((".", "#"))]   # not editor lock/autosave files
    out += list((ROOT / "docs").glob("*.md")) + [ROOT / "AGENTS.md", ROOT / "README.md"]
    out += [p for p in (ROOT / "tools").rglob("*.py") if "__pycache__" not in p.parts
            and p.name not in ("rename.py", "respell.py")]   # their docs spell old names on purpose
    return [p for p in out if p.is_file()]


def label_core(name: str) -> str:
    """The part of a global a local label spells: wSongRow -> SongRow."""
    return name[1:] if name[:1] in ("w", "h") and name[1:2].isupper() else name


def _norm(s: str) -> str:
    return s[:1].lower() + s[1:]


def _word_starts(label: str) -> list[int]:
    return [0] + [k for k in range(1, len(label))
                  if label[k].isupper() and (label[k - 1].islower() or label[k - 1].isdigit())]


def _end_ok(label: str, j: int) -> bool:
    tail = label[j:]
    return not tail or bool(re.match(r"[A-Z0-9_]|[b-z](?:_\d+)?$", tail))


def relabel_one(label: str, swaps: dict[str, str], keys: set[str]) -> str:
    """`label` with the first renamed core it spells replaced; `swaps` maps
    normalised old cores to new ones, `keys` holds every normalised core."""
    for i in _word_starts(label):
        for j in range(len(label), i, -1):
            key = _norm(label[i:j])
            if key in keys and _end_ok(label, j):
                if key in swaps:     # the longest name spelt here is a renamed one
                    new = swaps[key]
                    new = _norm(new) if i == 0 else new[:1].upper() + new[1:]
                    return label[:i] + new + label[j:]
                break                # a longer, unrenamed name owns this position
    return label


def relabel_text(text: str, pairs: list[tuple[str, str]], cores: set[str], where: str = "") -> tuple[str, int]:
    """Rename the local labels in one .sym file's text; see the module doc."""
    swaps = {_norm(label_core(o)): label_core(n) for o, n in pairs if len(label_core(o)) >= 5}
    keys = {_norm(c) for c in cores} | set(swaps)
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
        renamed = [(idx, label, relabel_one(label, swaps, keys) if swaps else label) for idx, label in labels]
        names = [n for _, _, n in renamed]
        dups = sorted({n for n in names if names.count(n) > 1})
        if dups and any(o != n for _, o, n in renamed):
            sys.exit(f"error: {where} {scope}: local label(s) {', '.join('.' + d for d in dups)} would collide")
        for idx, old, new in renamed:
            if new != old:
                lines[idx] = re.sub(rf"^([0-9A-F]{{4}} \.){old}(?!\w)", rf"\g<1>{new}", lines[idx])
                count += 1
    return "\n".join(lines), count


def c_names() -> set[str]:
    """Every identifier the C tree spells (comments excluded)."""
    out: set[str] = set()
    for p in (ROOT / "c").rglob("*"):
        if p.suffix in (".c", ".h"):
            out |= set(re.findall(r"\b[A-Za-z_]\w*\b", re.sub(r"/\*.*?\*/", "", p.read_text(), flags=re.S)))
    return out


def c_functions() -> set[str]:
    manifest = ROOT / "c" / "matched.json"
    return set(json.loads(manifest.read_text())) if manifest.is_file() else set()


def limit_errors(pairs: list[tuple[str, str]], cnames: set[str], cfuncs: set[str]) -> list[str]:
    """The NCPS limits (module doc) a rename would break."""
    mapping = dict(pairs)
    after = {mapping.get(n, n) for n in cnames}
    prefix: dict[str, set[str]] = {}
    for n in after:
        prefix.setdefault(n[:C_NAME_MAX], set()).add(n)
    errors = []
    for old, new in pairs:
        if old not in cnames:
            continue
        if old in cfuncs and len(new) > C_FUNC_MAX:
            errors.append(f"{new}: a C function's name is at most {C_FUNC_MAX} characters ({len(new)})")
        elif len(new) > C_NAME_MAX:
            errors.append(f"{new}: a name C sees is at most {C_NAME_MAX} characters ({len(new)})")
        if len(prefix.get(new[:C_NAME_MAX], ())) > 1:
            errors.append(f"{new}: shares its first {C_NAME_MAX} characters with "
                          f"{', '.join(sorted(prefix[new[:C_NAME_MAX]] - {new}))}")
    return errors


def apply(pairs: list[tuple[str, str, str | None]], c_only: frozenset[str] = frozenset()) -> str:
    """Validate and perform the renames; return a one-line summary. Names in
    `c_only` are C-side aliases (no .sym/ram.inc definition to check)."""
    syms = "\n".join(p.read_text() for p in (ROOT / "src").glob("bank*.sym"))
    syms += (ROOT / "constants" / "ram.inc").read_text()
    defined = set(re.findall(r"^(?:[0-9A-F]{4}|DEF) ([A-Za-z_]\w*)", syms, re.M))
    cnames = c_names()
    olds = {o for o, _, _ in pairs}
    for old, new, _ in pairs:
        if old not in c_only and not re.search(rf"\b{re.escape(old)}\b", syms):
            sys.exit(f"error: {old} is not a symbol")
        if old in c_only and old not in cnames:
            sys.exit(f"error: {old} is not a C name")
        # defined names only (`ADDR NAME` / `DEF NAME`); comments may mention NEW
        if (new in defined and new not in olds) or (new in c_only and new not in olds):
            sys.exit(f"error: {new} is already a symbol")
    news = [n for _, n, _ in pairs]
    if len(set(news)) != len(news):
        sys.exit("error: two names would become " + ", ".join(sorted({n for n in news if news.count(n) > 1})))
    errors = limit_errors([(o, n) for o, n, _ in pairs], cnames, c_functions())
    if errors:
        sys.exit("error: as-gbz80 keeps 32 characters of a symbol:\n  " + "\n  ".join(errors))
    cores = {label_core(n) for n in defined} | {label_core(n) for n in news}
    relabelled = {}
    for p in (ROOT / "src").glob("bank*.sym"):
        relabelled[p] = relabel_text(p.read_text(), [(o, n) for o, n, _ in pairs], cores, p.name)
    for p, (text, n) in relabelled.items():
        if n:
            p.write_text(text)
    mapping = {o: n for o, n, _ in pairs}
    changed = []
    for f in files():
        s = f.read_text()
        t = TOKEN.sub(lambda m: mapping.get(m.group(0), m.group(0)), s)
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
    shown = ", ".join(f"{o} -> {n}" for o, n, _ in pairs[:20]) + (f", ... ({len(pairs)} names)" if len(pairs) > 20 else "")
    return f"renamed {shown} in {len(changed)} file(s) and {labels} local label(s): {', '.join(changed)}"


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
    print(apply(parse(sys.argv[1:])))


if __name__ == "__main__":
    main()
