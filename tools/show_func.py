#!/usr/bin/env python3
"""Dump a function body from a carved bank .asm file.

Usage: python3 tools/show_func.py [--bare] [-n LINES] [--skip N] TARGET...
  TARGET: a symbol name (looked up in every src/bankNN.sym, or only in the
          bank given before it), or BANK followed by hex addresses -- the
          original form: `show_func.py 05 4233 4253`
  --bare: drop the `; $addr: bytes` comments
  -n:     lines to print (default 80; 0 = the whole function)
  --skip: start this many lines into the function

Prints from the address up to (but not including) the next global label.
A function that matching C owns (`INCBIN` slices in the carve) is shown
disassembled from the ROM, its jump targets named from the .sym.
"""
import argparse
import importlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
GLOBAL = re.compile(r'^[A-Za-z_]\w*:')   # (not `Func.L00102:`, matching C's own labels)
INCBIN = re.compile(r'\s*INCBIN "[^"]+", \$([0-9a-fA-F]+), \$([0-9a-fA-F]+)')


def sym_table(bank):
    """[(addr, name)] for the bank, globals and locals, from src/bankNN.sym."""
    rows = []
    p = ROOT / f'src/bank{bank:02x}.sym'
    for ln in p.read_text().splitlines():
        m = re.match(r'\s*\$?([0-9A-Fa-f]{4})\s+([A-Za-z_.][\w.]*)', ln)
        if m and '@deploy-only' not in ln:
            rows.append((int(m.group(1), 16), m.group(2)))
    return rows


def find(name, bank=None):
    hits = []
    for p in sorted((ROOT / 'src').glob('bank*.sym')):
        b = int(p.stem[4:6], 16)
        if bank is not None and b != bank:
            continue
        hits += [(b, a) for a, n in sym_table(b) if n == name]
    return hits


def incbin_rows(bank, start, ln, func_addr, rom, labels):
    """Disassemble one INCBIN slice of a matched function from the ROM."""
    disasm = importlib.import_module('disasm')
    addr = func_addr + start
    off = bank * 0x4000 + (addr - (0x4000 if bank else 0))
    out = []
    for r in disasm.disassemble(rom[off:off + ln], 0, ln, addr, []):
        text = re.sub(r'\$([0-9a-f]{4})\b',
                      lambda m: labels.get(int(m.group(1), 16), m.group(0)), r[2])
        if r[2].split()[0] not in ('jp', 'jr', 'call'):
            text = r[2]
        out.append(f'\t{text:28s} ; ${r[0]:04x}: {r[1].hex(" ")}')
    return out


def show(bank, addr, bare=False, lines=80, skip=0, rom=None):
    path = ROOT / f'src/bank{bank:02x}.asm'
    text = path.read_text().splitlines()
    start = None
    names = {a: n for a, n in sym_table(bank)}
    label = names.get(addr)
    for i, ln in enumerate(text):
        if label and not label.startswith('.') and ln.startswith(label + ':'):
            start = i
            break
        if not ln.startswith(';') and re.search(rf'; \${addr:04x}:', ln):
            start = i
            while start > 0 and re.match(r'^[A-Za-z_.][\w.]*:', text[start - 1]):
                start -= 1
            break
    if start is None:
        print(f'-- bank{bank:02x}:${addr:04X}: not found')
        return
    doc = start
    while doc > 0 and text[doc - 1].startswith(';'):    # its comment and contract
        doc -= 1
    body = text[doc:start + 1]
    for ln in text[start + 1:]:
        if GLOBAL.match(ln):
            break
        body.append(ln)
    while body and body[-1].startswith(';'):            # the next routine's
        body.pop()
    if any(INCBIN.match(ln) for ln in body):
        rom = rom or (ROOT / 'lsdj9_4_0.gb').read_bytes()
        # jump targets inside the function get their (local) label names
        labels = {a: n for a, n in sym_table(bank)}
        expanded = []
        for ln in body:
            m = INCBIN.match(ln)
            if m:
                expanded += incbin_rows(bank, int(m.group(1), 16), int(m.group(2), 16),
                                        addr, rom, labels)
            else:
                expanded.append(ln)
        body = expanded
    print(f'=== bank{bank:02x}:${addr:04X} ===')
    body = body[skip:]
    for ln in body[:lines] if lines else body:
        if bare:
            ln = re.sub(r'\s*;\s*\$[0-9a-f]{4}:.*$', '', ln)
        print(ln)
    if lines and len(body) > lines:
        print(f'... ({len(body) - lines} more lines; -n 0 for all)')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bare', action='store_true')
    ap.add_argument('-n', '--lines', type=int, default=80)
    ap.add_argument('--skip', type=int, default=0)
    ap.add_argument('targets', nargs='+')
    args = ap.parse_args()
    bank = None
    for t in args.targets:
        if len(t) <= 2 or t.startswith('0x'):
            bank = int(t, 0) if t.startswith('0x') else int(t, 16)
        elif re.fullmatch(r'\$?[0-9A-Fa-f]{4}', t):
            if bank is None:
                sys.exit(f'error: {t}: give the bank first (e.g. 05 {t})')
            show(bank, int(t.lstrip('$'), 16), args.bare, args.lines, args.skip)
        else:
            hits = find(t, bank)
            if not hits:
                print(f'-- {t}: no such symbol')
            for b, a in hits:
                show(b, a, args.bare, args.lines, args.skip)


if __name__ == '__main__':
    main()
