#!/usr/bin/env python3
"""Structurally name the auto locals (.L_xxxx) that have one honest name:

  .loop / .loopN   -- targets of at least one BACKWARD branch (loop heads),
                      numbered in address order within the function
  .done / .doneN   -- forward-only targets whose row starts the function
                      epilogue (`add sp, N` / `pop` / `ret`), i.e. the common
                      exit that the body branches to

Everything else keeps its .L_xxxx and waits for a manual, semantic pass.

Writes `ADDR .name` entries into src/bankNN.sym (skipping addresses that
already have a dot-name), then the caller re-carves. Idempotent.

Usage: tools/name_locals.py [--bank 0xNN] [--dry-run]
"""
import argparse, glob, os, re

ADDR = re.compile(r'; \$([0-9a-f]{4}):')
GLOBAL = re.compile(r'^([A-Za-z_][\w]*):')
LOCAL = re.compile(r'^\.L_([0-9A-F]{4}):')
BRANCH = re.compile(r'\t(?:jr|jp) (?:n?[zc], )?\.L_([0-9A-F]{4})\b')
BRANCH_FULL = re.compile(r'\t(?:jr|jp) (?:n?[zc], )?[A-Za-z_][\w]*\.L_([0-9A-F]{4})\b')
EPILOGUE = re.compile(r'\t(?:add sp, |pop |ret)')


def analyze(asm_path, sym_path):
    lines = open(asm_path).read().splitlines()
    # existing dot-names in the sym (already curated)
    curated = set()
    for ln in open(sym_path):
        m = re.match(r'([0-9A-Fa-f]{4})\s+\.', ln)
        if m:
            curated.add(int(m.group(1), 16))

    # walk: track current global fn, local definitions, branch refs w/ direction
    funcs = {}           # global label -> {local_addr: {'back': bool}}
    local_line = {}      # local addr -> line index
    cur = None
    cur_addr = 0
    for i, ln in enumerate(lines):
        g = GLOBAL.match(ln)
        if g:
            cur = g.group(1)
            funcs.setdefault(cur, {})
            continue
        lm = LOCAL.match(ln)
        if lm and cur:
            a = int(lm.group(1), 16)
            funcs[cur][a] = {'back': False}
            local_line[a] = i
            continue
        am = ADDR.search(ln)
        if am:
            cur_addr = int(am.group(1), 16)
        bm = BRANCH.search(ln)
        if bm and cur:
            # same-function references only: cross-function jumps into a label
            # are shared tails, not loop evidence
            tgt = int(bm.group(1), 16)
            if tgt <= cur_addr and tgt in funcs.get(cur, {}):
                funcs[cur][tgt]['back'] = True

    out = []             # (addr, name, comment)
    for f, ls in funcs.items():
        if not ls:
            continue
        loops = sorted(a for a, st in ls.items() if st['back'] and a not in curated)
        for n, a in enumerate(loops):
            name = '.loop' if n == 0 else f'.loop{n+1}'
            out.append((a, name, f'loop head in {f}'))
        # epilogue targets: forward-only locals whose row starts epilogue code
        for a, st in sorted(ls.items()):
            if st['back'] or a in curated or a in loops:
                continue
            i = local_line.get(a)
            if i is None or i + 1 >= len(lines):
                continue
            nxt = [l for l in lines[i + 1:i + 4] if l.strip()][:3]
            is_epi = bool(nxt) and (
                nxt[0].startswith('\tret') or nxt[0].startswith('\tadd sp, ')
                or (nxt[0].startswith('\tpop ')
                    and any(l.startswith('\tret') for l in nxt[1:])))
            if is_epi:
                # ensure uniqueness within the function
                taken = [nm for aa, nm, _ in out if aa in ls]
                name = '.done'
                k = 2
                while name in taken:
                    name = f'.done{k}'
                    k += 1
                out.append((a, name, f'common exit of {f}'))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--dry-run', action='store_true')
    args = ap.parse_args()
    paths = sorted(glob.glob('src/bank*.asm'))
    if args.bank:
        b = int(args.bank, 0)
        paths = [p for p in paths if int(os.path.basename(p)[4:6], 16) == b]
    total = 0
    for p in paths:
        sp = p[:-4] + '.sym'
        ents = analyze(p, sp)
        total += len(ents)
        print(f'{os.path.basename(p)}: {len(ents)} structural locals')
        if not args.dry_run and ents:
            with open(sp, 'a') as f:
                f.write('\n; --- structural local names (loop heads / common exits) ---\n')
                for a, name, cmt in sorted(ents):
                    f.write(f'{a:04X} {name:<20} ; {cmt}\n')
    print(f'TOTAL: {total}')


if __name__ == '__main__':
    main()
