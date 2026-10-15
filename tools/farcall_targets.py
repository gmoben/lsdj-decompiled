#!/usr/bin/env python3
"""Resolve FarCall / FarCallByte targets that are passed as immediates.

LSDj reaches most cross-bank routines through the FarCall ($179a) and
FarCallByte ($0bfe) trampolines rather than a direct `call NAME`. The caller
pushes an immediate bank + address:

    ld c, $d7          ; addr lo
    ld b, $6c          ; addr hi   (or a single `ld bc, $6cd7`)
    push bc
    ld a, $01          ; bank
    push af
    inc sp
    call FarCall

The target (bank $01 : $6cd7 here) therefore has no direct `call` referencing
it, so my per-bank labeler and callgraph.py both miss it -- these are exactly
the code addresses Emulicious shows as `_LABEL_xxxx`.

This scans every FarCall/FarCallByte site, resolves the immediate bank+addr,
and reports the ones whose target has no label yet.

Usage:
    python3 tools/farcall_targets.py [--all] [--bank 0xNN]
    # default: only unlabeled targets; --all: every resolved target
"""
import argparse, glob, os, re

ADDR = re.compile(r'; \$([0-9a-fA-F]{4}):')
LABEL = re.compile(r'^([A-Za-z_][\w.]*):')
LD_C = re.compile(r'^\tld c, \$([0-9a-fA-F]{1,2})\b')
LD_B = re.compile(r'^\tld b, \$([0-9a-fA-F]{1,2})\b')
LD_BC = re.compile(r'^\tld bc, \$([0-9a-fA-F]{1,4})\b')
LD_A = re.compile(r'^\tld a, \$([0-9a-fA-F]{1,2})\b')
CALL_FAR = re.compile(r'^\tcall (FarCall|FarCallByte|FarCallArgument)\b')

def bank_label_addrs(path):
    """Return set of addresses that carry a global label in this bank file."""
    labeled = set()
    lines = open(path).read().splitlines()
    pending = False
    for ln in lines:
        if LABEL.match(ln):
            pending = True
            continue
        m = ADDR.search(ln)
        if m and pending:
            labeled.add(int(m.group(1), 16))
            pending = False
        elif m:
            pending = False
    return labeled

def scan(path):
    """Yield (site_addr, bank, target_addr) for each resolvable FarCall site."""
    lines = open(path).read().splitlines()
    for i, ln in enumerate(lines):
        if not CALL_FAR.search(ln):
            continue
        m = ADDR.search(ln)
        site = int(m.group(1), 16) if m else None
        # look back a small window for the immediate bank + address
        lo = hi = bank = full = None
        swapped = False   # `ld l, b / ld h, c` moves b->lo, c->hi (FarCallByte hl arg)
        for j in range(i - 1, max(i - 9, -1), -1):
            pj = lines[j]
            if re.match(r'^\tld l, b\b', pj) or re.match(r'^\tld h, c\b', pj):
                swapped = True
                continue
            mb = LD_A.match(pj)
            if mb and bank is None:
                bank = int(mb.group(1), 16); continue
            mbc = LD_BC.match(pj)
            if mbc and full is None and lo is None:
                full = int(mbc.group(1), 16); continue
            mc = LD_C.match(pj)
            if mc and lo is None:
                lo = int(mc.group(1), 16); continue
            mh = LD_B.match(pj)
            if mh and hi is None:
                hi = int(mh.group(1), 16); continue
        if full is not None:
            target = full
        elif lo is not None and hi is not None:
            target = (lo << 8) | hi if swapped else (hi << 8) | lo
        else:
            target = None
        if bank is not None and target is not None:
            yield site, bank, target

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--all', action='store_true', help='show every resolved target, not just unlabeled')
    ap.add_argument('--bank', help='restrict to callers in this bank')
    a = ap.parse_args()

    # preload label sets for every bank we might target
    labels = {}
    for p in glob.glob('src/bank*.asm'):
        b = int(os.path.basename(p)[4:6], 16)
        labels[b] = bank_label_addrs(p)

    paths = sorted(glob.glob('src/bank*.asm'))
    if a.bank:
        bb = int(a.bank, 0)
        paths = [p for p in paths if int(os.path.basename(p)[4:6], 16) == bb]

    # aggregate unique targets, tracking which are unlabeled
    unlabeled = {}   # (bank,target) -> list of caller sites "bankXX:site"
    resolved = skipped = 0
    for p in paths:
        cb = int(os.path.basename(p)[4:6], 16)
        for site, bank, target in scan(p):
            # a real GB call target is $0000-$7fff; >=$8000 means the back-scan
            # grabbed a stray immediate (the site built the addr in registers).
            if target >= 0x8000:
                skipped += 1
                continue
            resolved += 1
            # code < $4000 always executes in home bank 0 regardless of the
            # bank paged in for that call's data.
            eff = 0 if target < 0x4000 else bank
            has = target in labels.get(eff, set())
            if a.all:
                mark = '' if has else '  <-- UNLABELED'
                print(f'  bank{cb:02x}:${site:04x}  ->  bank{eff:02x}:${target:04x}{mark}')
            if not has:
                unlabeled.setdefault((eff, target), []).append(f'bank{cb:02x}:${site:04x}')

    print(f'\nresolved FarCall sites: {resolved} (skipped {skipped} register-computed)')
    print(f'unique UNLABELED targets: {len(unlabeled)}')
    for (bank, target), sites in sorted(unlabeled.items()):
        print(f'  bank{bank:02x}:${target:04x}   <- {", ".join(sites[:4])}'
              + (f' (+{len(sites)-4} more)' if len(sites) > 4 else ''))

if __name__ == '__main__':
    main()
