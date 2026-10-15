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

import symfile

ADDR = re.compile(r'; \$([0-9a-f]{4}):')
GLOBAL = re.compile(r'^([A-Za-z_][\w]*):')
LOCAL = re.compile(r'^\.L_([0-9A-F]{4}):')
BRANCH = re.compile(r'\t(?:jr|jp) (?:n?[zc], )?\.L_([0-9A-F]{4})\b')
BRANCH_FULL = re.compile(r'\t(?:jr|jp) (?:n?[zc], )?[A-Za-z_][\w]*\.L_([0-9A-F]{4})\b')
EPILOGUE = re.compile(r'\t(?:add sp, |pop |ret)')


def analyze(asm_path, sym_path):
    lines = [l for l in open(asm_path).read().splitlines() if not l.startswith(';')]  # not the routines' comments
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


BTN = {1: 'Right', 2: 'Left', 4: 'Up', 8: 'Down',
       0x10: 'A', 0x20: 'B', 0x40: 'Sel', 0x80: 'Start'}
SCREEN = {1: 'Phrase', 2: 'Groove', 3: 'Chain', 4: 'Song', 5: 'Table',
          6: 'Instr', 8: 'Wave', 9: 'Project', 0xA: 'Synth', 0xB: 'Speech',
          0xC: 'Word', 0xD: 'File', 0xE: 'Help'}


def btn_name(v):
    """Compose a .name from a wButtons compare value, mods first."""
    parts = [n for b, n in ((0x40, 'Sel'), (0x80, 'Start'), (0x10, 'A'), (0x20, 'B'))
             if v & b]
    parts += [n for b, n in ((1, 'Right'), (2, 'Left'), (4, 'Up'), (8, 'Down'))
              if v & b]
    if not parts or v & ~0xFF:
        return None
    name = parts[0].lower() + ''.join(parts[1:])
    return '.' + name


def dispatch_names(asm_path, sym_path):
    """Name locals that are `jp/jr z` targets of `cp a, $XX` right after
    loading a known enum variable (wButtons -> button combo, wCurrentScreen ->
    screen name). Honest: the branch IS the handler for that exact value."""
    lines = [l for l in open(asm_path).read().splitlines() if not l.startswith(';')]  # not the routines' comments
    curated = set()
    for ln in open(sym_path):
        m = re.match(r'([0-9A-Fa-f]{4})\s+\.', ln)
        if m:
            curated.add(int(m.group(1), 16))
    cur = None
    var = None           # 'btn' | 'screen' currently in A
    cmp_val = None
    hl_slot = None       # sp+N that hl currently points at
    slots = {}           # per-function: sp+N -> 'btn'/'screen'
    props = {}           # local addr -> proposed base name
    owner = {}           # local addr -> function
    for ln in lines:
        g = GLOBAL.match(ln)
        if g:
            cur = g.group(1); var = None; cmp_val = None
            hl_slot = None; slots = {}
            continue
        lm = LOCAL.match(ln)
        if lm and cur:
            # a label interrupts straight-line flow -- reset the A tracking but
            # keep the slot map (the value parked on the stack survives)
            var = None; cmp_val = None; hl_slot = None
            continue
        if re.search(r'ld hl, wButtons\b', ln) or re.search(r'ld a, \[wButtons\]', ln):
            var = 'btn'; cmp_val = None; hl_slot = None; continue
        if re.search(r'ld hl, wCurrentScreen\b', ln) or re.search(r'ld a, \[wCurrentScreen\]', ln):
            var = 'screen'; cmp_val = None; hl_slot = None; continue
        m = re.search(r'\tld hl, sp\+(\d+)', ln)
        if m:
            hl_slot = int(m.group(1)); continue
        if re.search(r'\tld \[hl\], a', ln):
            if var and hl_slot is not None:
                slots[hl_slot] = var      # parked the tracked value
            continue
        if re.search(r'\tld a, \[hl\]', ln):
            if hl_slot is not None:
                var = slots.get(hl_slot)  # reloaded from a tracked slot (or None)
                cmp_val = None
            continue                      # wButtons/wCurrentScreen deref keeps var
        m = re.search(r'\tcp a, \$([0-9a-f]{2})', ln)
        if m and var:
            cmp_val = int(m.group(1), 16); continue
        m = re.search(r'\t(?:jp|jr) (n?)z, \.L_([0-9A-F]{4})', ln)
        if m and var and cmp_val is not None:
            inv = m.group(1) == 'n'
            a = int(m.group(2), 16)
            if a not in curated:
                nm = None
                if var == 'btn':
                    base = btn_name(cmp_val)
                    if base:
                        nm = ('.not' + base[1].upper() + base[2:]) if inv else base
                elif var == 'screen' and cmp_val in SCREEN:
                    nm = ('.not' if inv else '.on') + SCREEN[cmp_val]
                if nm:
                    props.setdefault(a, nm)
                    owner[a] = cur
            continue
        # anything else that writes A invalidates the tracked variable
        if re.search(r'\t(ld a, |ldh a, |xor a|or a, |and a, |add a|sub a|inc a|dec a|call |rla|rra|swap a|pop af)', ln):
            var = None; cmp_val = None

    # uniqueness per function
    out = []
    used = {}
    for a, nm in sorted(props.items()):
        fn = owner[a]
        key = (fn, nm)
        used[key] = used.get(key, 0) + 1
        final = nm if used[key] == 1 else f'{nm}{used[key]}'
        out.append((a, final, f'{("wButtons" if not nm.startswith(".on") else "wCurrentScreen")} == case in {fn}'))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--dispatch', action='store_true',
                    help='name cp-dispatch targets (buttons/screens) instead')
    args = ap.parse_args()
    paths = sorted(glob.glob('src/bank*.asm'))
    if args.bank:
        b = int(args.bank, 0)
        paths = [p for p in paths if int(os.path.basename(p)[4:6], 16) == b]
    total = 0
    for p in paths:
        sp = p[:-4] + '.sym'
        ents = analyze(p, sp) if not args.dispatch else dispatch_names(p, sp)
        total += len(ents)
        print(f'{os.path.basename(p)}: {len(ents)} structural locals')
        if not args.dry_run and ents:
            with open(sp, 'a') as f:
                f.write('\n; --- structural local names (loop heads / common exits) ---\n')
                for a, name, cmt in sorted(ents):
                    f.write(f'{a:04X} {name} ; {cmt}\n')
    symfile.align_files()
    print(f'TOTAL: {total}')


if __name__ == '__main__':
    main()
