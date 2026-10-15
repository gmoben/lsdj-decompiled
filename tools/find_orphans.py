#!/usr/bin/env python3
"""Find orphan function starts: instruction addresses that begin a new function
(preceded by an unconditional control-flow terminator) but carry NO label.

These correspond to Emulicious's `_LABEL_xxxx` code entries -- functions reached
only indirectly (via `jp hl` jump tables or the FarCallByte trampoline) so my
per-bank labeler and callgraph.py (direct cross-bank calls) both miss them.

For each orphan we print the first few body lines so it can be named from the
message it shows / RAM it touches.

Usage:
    python3 tools/find_orphans.py [--bank 0xNN] [--peek N]
"""
import argparse, glob, os, re

# unconditional control-flow terminators (fall-through impossible after these)
TERM = re.compile(r'^\t(ret|reti|jp (hl|\$[0-9a-fA-F]|[A-Za-z_.])|jr (\$|[A-Za-z_.]))')
# a terminator must NOT be conditional: `ret nz`, `jp z,`, `jr nc,` still fall/branch
COND = re.compile(r'^\t(ret (n?[zc])|jp (n?[zc]),|jr (n?[zc]),)')
LABEL = re.compile(r'^([A-Za-z_.][\w.]*):')
ADDR = re.compile(r'; \$([0-9a-fA-F]{4}):')
INSN = re.compile(r'^\t\S')

def is_term(line):
    if COND.match(line):
        return False
    return bool(TERM.match(line))

# SDCC function prologues: nearly every real routine opens by saving bc / carving
# a stack frame. Data misfiled as code (strings, dw/db tables) never does.
PROLOGUE = re.compile(r'^\t(push (bc|de|hl|af)|add sp, -|ld hl, sp[+-])')

def looks_like_code(body):
    return bool(body and PROLOGUE.match('\t' + body[0]))

def analyze(path, peek):
    orphans = []
    lines = [l for l in open(path).read().splitlines() if not l.startswith(';')]  # not the routines' comments
    prev_term = False          # previous emitted instruction was a terminator
    labeled_since_term = False # saw a label since the last terminator
    for i, ln in enumerate(lines):
        if LABEL.match(ln):
            labeled_since_term = True
            continue
        if ln.startswith('\tdb ') or ln.startswith('\tdw '):
            prev_term = False
            labeled_since_term = False
            continue
        if INSN.match(ln):
            m = ADDR.search(ln)
            addr = m.group(1) if m else '????'
            if prev_term and not labeled_since_term:
                body = [l.strip() for l in lines[i:i+peek] if l.strip()]
                orphans.append((addr, i + 1, body))
            prev_term = is_term(ln)
            labeled_since_term = False
    return orphans

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--peek', type=int, default=4)
    ap.add_argument('--code-only', action='store_true',
                    help='keep only orphans whose first insn is an SDCC prologue')
    a = ap.parse_args()
    if a.bank:
        b = int(a.bank, 0)
        paths = [f'src/bank{b:02x}.asm']
    else:
        paths = sorted(glob.glob('src/bank*.asm'))
    total = 0
    for p in paths:
        if not os.path.exists(p):
            continue
        orphans = analyze(p, max(a.peek, 1))
        if a.code_only:
            orphans = [o for o in orphans if looks_like_code(o[2])]
        if not orphans:
            continue
        total += len(orphans)
        print(f'\n=== {os.path.basename(p)}: {len(orphans)} orphan function-starts ===')
        for addr, lineno, body in orphans:
            print(f'  ${addr}  (line {lineno})')
            for bl in body:
                print(f'        {bl}')
    print(f'\nTOTAL orphan function-starts: {total}')

if __name__ == '__main__':
    main()
