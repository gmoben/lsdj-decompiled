#!/usr/bin/env python3
"""Consume an Emulicious debugger export and print only the *actionable* diff
against our symbols -- so the occasional manual paste becomes a one-command
check instead of eyeballing 100k lines.

Emulicious names anything unresolved `_LABEL_<off>_` (code), `_DATA_<off>_`
(data) or `_RAM_<off>_` (RAM), where <off> is the linear file offset. We split
each into:
  * MISCARVE / MISSING -- the address has no symbol in our deployed .sym; this
    is real work (usually code Emulicious reached at runtime that our static
    carve marked as data).
  * operand-quirk -- a symbol DOES exist at that address; Emulicious just kept
    an old auto-label on that operand in its persisted analysis DB. Cosmetic;
    clears when it re-analyses. Counted, not listed.

Usage: tools/audit_export.py [emulicious.out]
"""
import re, sys

EXPORT = sys.argv[1] if len(sys.argv) > 1 else "emulicious.out"


def load_syms(path="lsdj9_4_0.sym"):
    syms = {}
    for ln in open(path):
        m = re.match(r"([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+(\S+)", ln)
        if m:
            syms[(int(m.group(1), 16), int(m.group(2), 16))] = m.group(3)
    return syms


def off_to_ba(off):
    bank = off >> 14
    return (bank, off if bank == 0 else 0x4000 + (off & 0x3FFF))


def main():
    syms = load_syms()
    txt = open(EXPORT).read()
    for kind in ("_LABEL", "_DATA", "_RAM"):
        toks = sorted(set(re.findall(kind + r"_([0-9A-F]+)_", txt)))
        missing, quirk = [], 0
        for t in toks:
            off = int(t, 16)
            if kind == "_RAM":
                ba = (0, off) if off < 0x4000 else (0, off)  # RAM is bank-0 addr space
                ba = (0, off if off < 0x4000 else off)
            else:
                ba = off_to_ba(off)
            # RAM addresses live in the $C000+ space; map straight
            if kind == "_RAM":
                ba = (0, off)
            if ba in syms:
                quirk += 1
            else:
                missing.append(ba)
        label = {"_LABEL": "code", "_DATA": "data", "_RAM": "RAM"}[kind]
        print(f"\n=== {kind} ({label}): {len(missing)} actionable, "
              f"{quirk} operand-quirks ===")
        for bank, addr in missing:
            print(f"  bank{bank:02x}:${addr:04x}")


if __name__ == "__main__":
    main()
