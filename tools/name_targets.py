#!/usr/bin/env python3
"""Name the targets the disassembler still labels `.L_xxxx` from what is there.

They are addresses an operand points at that src/bankNN.sym has no name for
(branch targets all have one): a switch's jump table, a string a pointer
hands to DrawString, an entry of a pointer table another table points into.
Each gets a name from its content, deduplicated within its function:

  jump table       -- after the variable its index comes from (`ld e, X`
                      before `ld hl, table`): `.currentScreenCases`, else
                      `.jumpTable`
  string           -- after its text: `.strOn`, `.strEmpty`
  `dw Name` entry  -- after what it points to: `.toName` (a help page's
                      entry after the page: `HelpPagePtrs.song`)

Writes `ADDR .name` lines into the .sym (then re-carve with `make`); prints
any target it cannot name. Idempotent: named targets are no longer `.L_`.

Usage: tools/name_targets.py [--apply]
"""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "match"))
import to_rgbds  # noqa: E402  (switch_name: the same rule the C translation uses)

ROOT = os.path.dirname(HERE)
GLOBAL = re.compile(r"^([A-Za-z_]\w*):")
DEF = re.compile(r"^\.L_([0-9A-F]{4}):")
REF = re.compile(r"(?:\b[A-Za-z_]\w*)?\.L_([0-9A-F]{4})\b(?!:)")


def text_at(rom: bytes, bank: int, addr: int) -> str | None:
    """The zero-terminated ASCII string at bank:addr, or None if it isn't one."""
    off = (addr if bank == 0 else bank * 0x4000 + addr - 0x4000)
    raw = rom[off:off + 64].split(b"\x00")[0]
    if len(raw) == 64 or any(not 0x20 <= c < 0x7F for c in raw):
        return None
    return raw.decode("ascii")


def str_name(text: str) -> str:
    words = re.findall(r"[A-Za-z0-9]+", text)
    return ".str" + ("".join(w.capitalize() for w in words)[:24] or "Empty")


def propose(rom, bank, addr, body, refs):
    """A name for the target at `addr`, from its first rows (`body`) and the
    lines that refer to it (`refs`, each with the code before it)."""
    first = body[0] if body else ""
    if first.startswith("jp ") and any(r.startswith("ld hl, ") for r, _b in refs):
        before = next(b for r, b in refs if r.startswith("ld hl, "))
        return to_rgbds.Translator.switch_name(before)
    m = re.fullmatch(r"dw TxtHelpPage(\w+)", first)
    if m:                                 # HelpPagePtrs' entries: the page they open
        return "." + m.group(1)[0].lower() + m.group(1)[1:]
    m = re.fullmatch(r"dw (\w+)", first)
    if m:
        return ".to" + m.group(1)
    text = text_at(rom, bank, addr)
    to_string = any(r.startswith("dw ") for r, _b in refs) or any(
        r.startswith("ld hl, ") and any(x.startswith("call Draw") for x in b[-0:]) for r, b in refs)
    if text is not None and (first.startswith("db ") or to_string):
        return str_name(text)
    return None


def analyze(asm_path, rom):
    bank = int(os.path.basename(asm_path)[4:6], 16)
    lines = [l.split(";")[0].rstrip() for l in open(asm_path)]
    texts = [l.strip() for l in lines]
    refs: dict[str, list[tuple[str, list[str]]]] = {}
    for i, t in enumerate(texts):
        if t.endswith(":"):
            continue
        for m in REF.finditer(t):
            # the referring line, the code before it and the two lines after
            before = [x for x in texts[max(0, i - 8):i] if x and not x.endswith(":")]
            after = [x for x in texts[i + 1:i + 4] if x and not x.endswith(":")]
            refs.setdefault(m.group(1), []).append((t, before + after))
    out, unnamed = [], []
    used: dict[str, set[str]] = {}
    func = None
    for i, t in enumerate(texts):
        g = GLOBAL.match(t)
        if g:
            func = g.group(1)
            continue
        if t.startswith(".") and t.endswith(":") and func:
            used.setdefault(func, set()).add(t[:-1])
        m = DEF.match(t)
        if not m or not func:
            continue
        addr = int(m.group(1), 16)
        body = [x for x in texts[i + 1:i + 4] if x and not x.endswith(":")]
        rf = refs.get(m.group(1), [])
        name = propose(rom, bank, addr, body, rf)
        if not name:
            unnamed.append((bank, func, addr, body[:1]))
            continue
        final, k = name, 1
        while final in used.setdefault(func, set()):
            k += 1
            final = f"{name}{k}"
        used[func].add(final)
        out.append((addr, final, func))
    return bank, out, unnamed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()
    rom = open(os.path.join(ROOT, "lsdj9_4_0.gb"), "rb").read()
    total = 0
    for p in sorted(glob.glob(os.path.join(ROOT, "src", "bank*.asm"))):
        bank, named, unnamed = analyze(p, rom)
        for b, f, a, body in unnamed:
            print(f"unnamed: ${b:02X}:${a:04X} in {f}: {body}")
        if not named:
            continue
        total += len(named)
        for a, n, f in named:
            print(f"${bank:02X}:${a:04X} {f}{n}")
        if args.apply:
            with open(p[:-4] + ".sym", "a") as fh:
                fh.write("\n; --- pointer targets (tools/name_targets.py) ---\n")
                for a, n, _f in sorted(named):
                    fh.write(f"{a:04X} {n}\n")
    print(f"TOTAL: {total}")


if __name__ == "__main__":
    main()
