#!/usr/bin/env python3
"""Name .L_ locals from their block's first *meaningful* action -- a code-derived
functional name (the named RAM var it sets/reads/clears, the register it writes,
or the function it calls). High-confidence forms only; ambiguous blocks keep
.L_ for the manual semantic pass. Also names assert-targets (.ok, block right
after a `call DebugTrap`).

Per-function scoping: names are deduped within the enclosing global function
(two functions may both have .checkRow). Writes ADDR .name into src/bankNN.sym.

Usage: tools/name_firstaction.py [--bank 0xNN] [--apply]
"""
import re, glob, os, argparse

GLOBAL = re.compile(r'^([A-Za-z_][\w]*):')
LOCAL = re.compile(r'^\.L_([0-9A-F]{4}):')
CLEANVAR = re.compile(r'^(w|h)[A-Z][A-Za-z]+$')   # no _pN / channel element suffixes


def lc(s):
    return s[0].lower() + s[1:] if s else s


def var(v):
    return re.sub(r'^(w|h)', '', v)


def propose(body, prev_is_assert):
    if prev_is_assert:
        return '.ok'
    if not body:
        return None
    b0 = body[0]
    nxt = ' '.join(body[1:3])
    # ld hl, wVar  (+ what happens to it)
    g = re.search(r'\tld hl, ((?:w|h)[A-Za-z]\w+)\b', b0)
    if g and CLEANVAR.match(g.group(1)):
        v = var(g.group(1))
        if re.search(r'\bld \[hl\], ', nxt):
            return '.set' + v
        if re.search(r'\binc \[hl\]', nxt):
            return '.inc' + v
        if re.search(r'\bdec \[hl\]', nxt):
            return '.dec' + v
        if re.search(r'\bld a, \[hl\]', nxt):
            return '.check' + v if re.search(r'cp a,', ' '.join(body[1:4])) else '.load' + v
        return None
    # ld [wVar], a  /  ldh [rReg], a
    g = re.search(r'\tld \[((?:w|h)[A-Za-z]\w+)\], a', b0)
    if g and CLEANVAR.match(g.group(1)):
        return '.store' + var(g.group(1))
    g = re.search(r'\tldh \[(r[A-Z]\w+)\], a', b0)
    if g:
        return '.set' + g.group(1)
    # xor a ; ld hl, wVar ; or a,[hl]  -> zero-test of that var
    if re.search(r'\txor a, a', b0):
        g = re.search(r'ld hl, ((?:w|h)[A-Z][A-Za-z]+)\b', body[1] if len(body) > 1 else '')
        if g and re.search(r'or a, \[hl\]', ' '.join(body[1:4])):
            return '.check' + var(g.group(1))
    # xor a ; ld [wVar]/ld hl,wVar  -> clear
    if re.search(r'\txor a, a', b0):
        for l in body[1:3]:
            g = re.search(r'ld \[((?:w|h)[A-Za-z]\w+)\], a|ld hl, ((?:w|h)[A-Za-z]\w+)', l)
            if g:
                v = g.group(1) or g.group(2)
                if CLEANVAR.match(v):
                    return '.clear' + var(v)
    # leading call to a named function
    g = re.search(r'\tcall ([A-Z]\w+)', b0)
    if g and g.group(1) != 'DebugTrap':
        return '.' + lc(g.group(1))
    return None


def analyze(asm, sym):
    curated = set()
    curnames = {}        # addr -> curated dot-name (from prior passes)
    for ln in open(sym):
        m = re.match(r'([0-9A-Fa-f]{4})\s+(\.\S+)', ln)
        if m:
            curated.add(int(m.group(1), 16))
            curnames[int(m.group(1), 16)] = m.group(2)
    L = open(asm).read().splitlines()
    cur = None
    per_fn = {}          # fn -> {addr: name}
    used = {}            # fn -> set(final names)
    for i, l in enumerate(L):
        g = GLOBAL.match(l)
        if g:
            cur = g.group(1)
            per_fn.setdefault(cur, {})
            used.setdefault(cur, set())
            continue
        # seed used-set with prior-pass locals in this function
        lm0 = LOCAL.match(l)
        if lm0 and cur and int(lm0.group(1), 16) in curnames:
            used[cur].add(curnames[int(lm0.group(1), 16)])
        lm = LOCAL.match(l)
        if not (lm and cur):
            continue
        a = int(lm.group(1), 16)
        if a in curated:
            continue
        prev = L[i - 1].strip() if i else ''
        body = []
        j = i + 1
        while j < len(L) and not (GLOBAL.match(L[j]) or LOCAL.match(L[j])):
            if L[j].strip():
                body.append(L[j])
            j += 1
            if len(body) >= 5:
                break
        nm = propose(body, prev.startswith('call DebugTrap'))
        if not nm:
            continue
        final = nm
        k = 0
        while final in used[cur]:
            k += 1
            final = nm + chr(ord('b') + k - 1)   # .name, .nameb, .namec ...
        used[cur].add(final)
        per_fn[cur][a] = final
    return per_fn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--apply', action='store_true')
    args = ap.parse_args()
    paths = sorted(glob.glob('src/bank*.asm'))
    if args.bank:
        b = int(args.bank, 0)
        paths = [p for p in paths if int(os.path.basename(p)[4:6], 16) == b]
    total = 0
    for p in paths:
        sp = p[:-4] + '.sym'
        per_fn = analyze(p, sp)
        ents = [(a, nm) for fn in per_fn for a, nm in per_fn[fn].items()]
        total += len(ents)
        print(f'{os.path.basename(p)}: {len(ents)}')
        if args.apply and ents:
            with open(sp, 'a') as f:
                f.write('\n; --- first-action / assert local names ---\n')
                for a, nm in sorted(ents):
                    f.write(f'{a:04X} {nm}\n')
    print(f'TOTAL: {total}')


if __name__ == '__main__':
    main()
