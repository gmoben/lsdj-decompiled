#!/usr/bin/env python3
"""Split a C module into modules named for what they hold.

    tools/match/split_c.py SRC.c SPEC.json

SPEC maps destination modules to the functions they take:

    {"WaveScreen.c": {"title": "The wave screen", "what": "The wave graph, ...",
                      "functions": ["DrawWaveScreen", ...]},
     "DrawChanStatus.c": {"functions": ["DrawAllChanStatus"]},
     "SynthScreen.c": {"title": "The synth screen"}}

A destination is a path, or a file name in SRC's directory. A new one gets
a header comment from its "title" and "what" (an entry for SRC itself
retitles what stays in SRC). Each gets its functions in ROM order
(src/bankNN.sym), the declarations of SRC's preamble
its functions refer to (macros defined before SRC's `#include` lead the new
file the same way), and prototypes for the functions it refers to that SRC
defined above the referring function and that now sit in another module (a
reference SRC compiled before the definition stays implicit: adding a
prototype there could change the code). A destination that already exists
gets the functions merged in ROM order and only the declarations it lacks.
Functions no destination lists stay in SRC; SRC is deleted when none are
left. The bytes cannot change -- `make match` checks it.
"""

from __future__ import annotations

import importlib
import json
import re
import sys
import textwrap
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
mc = importlib.import_module("merge_c")
ROOT = HERE.parent.parent

TYPE = r"(?:extern\s+)?(?:const\s+)?(?:volatile\s+)?(?:sfr\s+)?(?:UBYTE|BYTE|UWORD|WORD|void|char|int|unsigned|signed|long)\b[\w \t*]*"


def rom_addresses(bank_dir: Path) -> dict[str, int]:
    sym = ROOT / "src" / f"{bank_dir.name}.sym"
    out: dict[str, int] = {}
    if sym.is_file():
        for line in sym.read_text().splitlines():
            m = re.match(r"^([0-9A-F]{4}) ([A-Za-z_]\w*)", line)
            if m:
                out.setdefault(m.group(2), int(m.group(1), 16))
    return out


def declared(line: str) -> set[str]:
    """The names a top-level declaration line declares."""
    s = re.sub(r"/\*.*?\*/", "", line).strip()
    if s.startswith("#define"):
        m = re.match(r"#define\s+(\w+)", s)
        return {m.group(1)} if m else set()
    m = re.match(rf"^{TYPE}\b(\w+)\s*\(", s)
    if m and not s.startswith("extern"):
        return {m.group(1)}                       # a prototype
    m = re.match(rf"^{TYPE}\(\s*\*\s*(\w+)\s*\)", s)
    if m:
        return {m.group(1)}                       # a function-pointer variable
    if m := re.match(rf"^{TYPE}\b(\w+)\s*\(", s):
        return {m.group(1)}                       # extern prototype
    return set(re.findall(r"\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?=[,;=])", s))


def refs(text: str) -> set[str]:
    return set(re.findall(r"\b([A-Za-z_]\w*)\b", re.sub(r"/\*.*?\*/", "", text, flags=re.S)))


def signature(chunk: str, name: str) -> str | None:
    for line in chunk.splitlines():
        if re.match(rf"^[A-Za-z_][\w \t*]*\b{name}\s*\(", line):
            return line.strip()
    return None


def header(title: str, what: str | None, bank: str) -> str:
    """A module's leading comment, in the style of the other C modules."""
    lines = ["/*", f" * {title} -- LSDj 9.4.0, ROM bank ${bank}.", " *"]
    if what:
        lines += [" * " + w for w in textwrap.wrap(what, 73, break_on_hyphens=False)] + [" *"]
    lines += [" * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);",
              " * `make match` verifies every function here against the ROM.", " */"]
    return "\n".join(lines) + "\n"


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src_path = Path(sys.argv[1]).resolve()
    spec = json.loads(Path(sys.argv[2]).read_text())
    src = src_path.read_text()
    pre, chunks = mc.split(src)
    order = [n for n, _ in chunks]
    by_name = dict(chunks)
    owner: dict[str, Path] = {}
    for dest, d in spec.items():
        dp = ((src_path.parent / dest) if "/" not in dest else (ROOT / dest)).resolve()
        if dp == src_path:
            continue                              # SRC's own entry only retitles it
        for n in d["functions"]:
            if n not in by_name:
                sys.exit(f"error: {n} is not defined in {src_path.name}")
            owner[n] = dp
    head, decls = [], []
    seen_include = False
    for line in pre.splitlines():
        s = line.strip()
        if s.startswith("#include"):
            seen_include = True
            continue
        if not s or s.startswith(("/*", "*", "//")):
            continue
        (decls if seen_include else head).append(line)
    sigs = {n: sig for n, c in chunks if (sig := signature(c, n))}
    addr = rom_addresses(src_path.parent)
    stay = [n for n in order if n not in owner]
    targets = {p: [n for n in order if owner.get(n) == p] for p in dict.fromkeys(owner.values())}
    if stay:
        targets[src_path] = stay
    written = []
    for path, names in targets.items():
        body_text = "\n".join(by_name[n] for n in names)
        existing = path.read_text() if path.is_file() and path != src_path else ""
        all_text = body_text + "\n" + existing
        need = [d for d in decls if declared(d) & refs(body_text)
                and not (existing and d.strip() in existing)]
        protos = []
        for caller in names:
            for g in sorted(refs(by_name[caller]) - {caller}):
                if g in sigs and owner.get(g, src_path) != path \
                        and order.index(g) < order.index(caller) \
                        and not re.search(rf"^{TYPE}\b{g}\s*\(", all_text, re.M) \
                        and not any(g in declared(d) for d in need) and sigs[g] + ";" not in protos:
                    protos.append(sigs[g] + ";")
        if existing:
            e_pre, e_chunks = mc.split(existing)
            merged = dict(e_chunks)
            merged.update({n: by_name[n] for n in names})
            sorted_names = sorted(merged, key=lambda n: addr.get(n, 1 << 20))
            add = "\n".join(protos + need)
            e_pre = e_pre.replace('#include "lsdj.h"\n', '#include "lsdj.h"\n' + (add + "\n" if add else ""), 1)
            text = e_pre.rstrip("\n") + "\n\n" + "\n".join(merged[n].rstrip("\n") + "\n" for n in sorted_names)
        else:
            meta = next((d for k, d in spec.items() if Path(k).name == path.name), {})
            bank = path.parent.name.replace("bank", "").upper()
            if meta.get("title"):
                hdr = header(meta["title"], meta.get("what"), bank)
            elif path == src_path:
                m = re.match(r"\s*(/\*.*?\*/\n)", pre, re.S)  # keep SRC's leading comment
                hdr = m.group(1) if m else ""
            else:
                hdr = header(path.stem, None, bank)
            parts = [hdr, "".join(h + "\n" for h in head) + '#include "lsdj.h"\n']
            decl = "".join(p + "\n" for p in protos) + "".join(d + "\n" for d in need)
            if decl:
                parts.append(decl)
            parts.append("\n".join(by_name[n].rstrip("\n") + "\n" for n in names))
            text = "\n".join(part for part in parts if part)
        path.write_text(text)
        written.append(f"{path.relative_to(ROOT)} ({len(names)})")
    if not stay:
        src_path.unlink()
        written.append(f"{src_path.relative_to(ROOT)} removed")
    print("wrote " + ", ".join(written))


if __name__ == "__main__":
    main()
