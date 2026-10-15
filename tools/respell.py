#!/usr/bin/env python3
"""Spell short words out across the names.

    tools/respell.py [--dry-run] [--set OLD=NEW]... [--keep NAME]... SHORT=FULL...
    tools/respell.py --c-locals [--dry-run] [--set OLD=NEW]... SHORT=FULL...

Every global (src/*.sym), RAM name (constants/ram.inc) and C-side alias
(match_function.RUNTIME_ALIASES) with SHORT as a camelCase word gets FULL in
its place (wChanFreq -> wChannelFreq); a plural (Instrs) and a trailing
de-duplication letter (Ptrb) come along. The renames go through rename.py,
so C, docs, tools and the local labels that spell a renamed global follow;
then every other local label with the word (.loopChan -> .loopChannel) is
respelt, checked against its scope. A word the expansion repeats is said
once (FxCmd_A with Fx=Command, Cmd=Command -> Command_A).

With --c-locals the C tree's own identifiers are respelt instead: locals
and parameters (chan -> channel, curCol -> currentColumn), never a C
keyword, a type, a macro or a name the symbol maps define; the docs' code
spans follow.

Untouched: GBDK's lowercase library names and SDCC's runtime helpers
(DivS8, ...), Txt labels (they mirror their text), SCREAMING_SNAKE
constants and every --keep NAME. Before writing anything it stops on a new
name that exists already or that two names would share, and on a name C
sees that as-gbz80 would cut (rename.py's limits); --set OLD=NEW settles a
name by hand, --dry-run prints the plan.
"""

from __future__ import annotations

import importlib
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "match"))
rename = importlib.import_module("rename")
ROOT = rename.ROOT

RUNTIME = re.compile(r"^(?:Div|Mod|Mul|DivMod)[SU](?:8|16)$|^MulCharToInt$|^MulInt$|^DivStep$")
C_WORDS = set("""auto break case char const continue default do double else enum extern float for goto
if int long register return short signed sizeof static struct switch typedef union unsigned void
volatile while sfr _asm _endasm UBYTE BYTE UWORD WORD DWORD UDWORD NULL""".split())
WORD = re.compile(r"[A-Z]+(?![a-z])|[A-Z][a-z]*|[a-z]+|[0-9]+")


def respell(name: str, words: dict[str, str], known: frozenset[str] = frozenset()) -> str:
    """`name` with each short word spelt out (see the module doc). A last word
    one letter (b-f) past a short word is a de-duplication letter only if
    it is not a word of its own, which `known` holds (Mode is not Mod+e)."""
    prefix = name[0] if name[:1] in ("w", "h") and name[1:2].isupper() else ""
    parts = name[len(prefix):].split("_")
    out = []
    for pi, part in enumerate(parts):
        toks = WORD.findall(part)
        if "".join(toks) != part:
            return name
        new: list[str] = []
        for ti, tok in enumerate(toks):
            cap = tok[:1].upper() + tok[1:]
            full = words.get(cap)
            last = pi == len(parts) - 1 and ti == len(toks) - 1
            if full is None and cap.endswith("s") and cap[:-1] in words:
                full = words[cap[:-1]] + "s"
            if full is None and last and len(cap) > 2 and cap[-1] in "bcdef" and cap[:-1] in words and cap not in known:
                full = words[cap[:-1]] + cap[-1]          # the labeller's de-duplication letter
            if full is None:
                piece = tok
            else:
                piece = full if tok[:1].isupper() else full[:1].lower() + full[1:]
            if new and piece.lower() == new[-1].lower() and full is not None:
                continue                                   # a repeated word: said once
            new.append(piece)
        out.append("".join(new))
    return prefix + "_".join(out)


def exempt(name: str, keep: set[str]) -> bool:
    return (name in keep or name.startswith("Txt") or name[:1].islower() and not re.match(r"^[wh][A-Z]", name)
            or name.isupper() or bool(RUNTIME.match(name)))


def global_names() -> tuple[set[str], set[str]]:
    syms = "\n".join(p.read_text() for p in (ROOT / "src").glob("bank*.sym"))
    defined = set(re.findall(r"^[0-9A-F]{4} ([A-Za-z_]\w*)", syms, re.M))
    defined |= set(re.findall(r"^\s*DEF\s+([A-Za-z_]\w*)", (ROOT / "constants" / "ram.inc").read_text(), re.M))
    mf = importlib.import_module("match_function")
    aliases = {k for k in mf.RUNTIME_ALIASES if not k.startswith("_") and not k.islower()}
    return defined, aliases - defined


def known_words(names: set[str]) -> frozenset[str]:
    """Every camelCase word the global names use."""
    return frozenset(w for n in names for w in WORD.findall(n) if w[:1].isupper())


def plan(words: dict[str, str], keep: set[str], sets: dict[str, str]) -> tuple[list[tuple[str, str]], list[str], set[str]]:
    defined, aliases = global_names()
    known = known_words(defined)
    pairs = []
    for n in sorted(defined | aliases):
        new = sets.get(n) or (n if exempt(n, keep) else respell(n, words, known))
        if new != n:
            pairs.append((n, new))
    olds = {o for o, _ in pairs}
    news = [n for _, n in pairs]
    errors = [f"{n}: already a name" for n in news if n in (defined | aliases) and n not in olds]
    errors += [f"{n}: two names would become it" for n in sorted({n for n in news if news.count(n) > 1})]
    errors += rename.limit_errors(pairs, rename.c_names(), rename.c_functions())
    return pairs, errors, aliases


def respell_labels(words: dict[str, str], dry: bool) -> int:
    known = known_words(global_names()[0])
    total = 0
    for p in sorted((ROOT / "src").glob("bank*.sym")):
        lines = p.read_text().split("\n")
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
        for scope, labels in scopes.items():
            renamed = [(idx, lab, respell(lab, words, known)) for idx, lab in labels]
            names = [n for _, _, n in renamed]
            dups = sorted({n for n in names if names.count(n) > 1})
            if dups and any(o != n for _, o, n in renamed):
                sys.exit(f"error: {p.name} {scope}: local label(s) {', '.join('.' + d for d in dups)} would collide")
            for idx, old, new in renamed:
                if new != old:
                    lines[idx] = re.sub(rf"^([0-9A-F]{{4}} \.){old}(?!\w)", rf"\g<1>{new}", lines[idx])
                    total += 1
        if not dry:
            p.write_text("\n".join(lines))
    return total


CODE = re.compile(r"/\*.*?\*/|//[^\n]*|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|\b[A-Za-z_]\w*\b", re.S)


def respell_c_locals(words: dict[str, str], sets: dict[str, str], dry: bool) -> int:
    mc = importlib.import_module("merge_c")
    defined, aliases = global_names()
    reserved = defined | aliases | C_WORDS
    total, errors = 0, []
    changes: dict[Path, str] = {}
    spans: dict[str, str] = {}
    for f in sorted((ROOT / "c").rglob("*.[ch]")):
        text = f.read_text()
        reserved_here = reserved | set(re.findall(r"^\s*#\s*define\s+(\w+)", text, re.M))
        pre, chunks = mc.split(text) if f.suffix == ".c" else (text, [])
        for _, body in [("", pre)] + chunks:
            ids = {m.group(0) for m in CODE.finditer(body) if m.group(0)[0].isalpha() or m.group(0)[0] == "_"}
            local = {i: sets.get(i) or respell(i, words) for i in ids
                     if i not in reserved_here and i[:1].islower() and not exempt_c(i)}
            local = {o: n for o, n in local.items() if n != o}
            for o, n in local.items():
                if n in ids and n not in local:
                    errors.append(f"{f.relative_to(ROOT)}: {o} -> {n}, already a name there")
            spans.update(local)
        mapping = {o: n for o, n in spans.items()}
        new = CODE.sub(lambda m: mapping.get(m.group(0), m.group(0))
                       if m.group(0)[0].isalpha() or m.group(0)[0] == "_" else m.group(0), text)
        if new != text:
            changes[f] = new
            total += sum(1 for m in CODE.finditer(text) if m.group(0) in mapping)
    if errors:
        sys.exit("error:\n  " + "\n  ".join(errors))
    if not dry:
        for f, new in changes.items():
            f.write_text(new)
        for d in [*(ROOT / "docs").glob("*.md"), ROOT / "AGENTS.md"]:
            s = d.read_text()
            t = re.sub(r"`[^`\n]+`", lambda m: rename.TOKEN.sub(
                lambda t2: spans.get(t2.group(0), t2.group(0)), m.group(0)), s)
            if t != s:
                d.write_text(t)
    print(f"{'would respell' if dry else 'respelt'} {total} C identifier use(s) in {len(changes)} file(s): "
          + ", ".join(f"{o} -> {n}" for o, n in sorted(spans.items())))
    return total


def exempt_c(name: str) -> bool:
    return len(name) == 1


def main() -> None:
    args = sys.argv[1:]
    dry = "--dry-run" in args
    c_locals = "--c-locals" in args
    sets, keep, words = {}, set(), {}
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("--set", "--keep"):
            i += 1
            if a == "--set":
                o, n = args[i].split("=", 1)
                sets[o] = n
            else:
                keep.add(args[i])
        elif "=" in a and not a.startswith("--"):
            s, full = a.split("=", 1)
            words[s] = full
        i += 1
    if not words:
        sys.exit(__doc__)
    if c_locals:
        respell_c_locals(words, sets, dry)
        return
    pairs, errors, aliases = plan(words, keep, sets)
    if dry or errors:
        for o, n in pairs:
            print(f"{o} -> {n}")
        if errors:
            print("problems (settle each with --set OLD=NEW):\n  " + "\n  ".join(errors))
            sys.exit(1)
        print(f"{len(pairs)} name(s); local labels: {respell_labels(words, dry=True)} more")
        return
    print(rename.apply([(o, n, None) for o, n in pairs], c_only=frozenset(o for o, _ in pairs if o in aliases)))
    print(f"respelt {respell_labels(words, dry=False)} other local label(s)")


if __name__ == "__main__":
    main()
