#!/usr/bin/env python3
"""Name EVERY remaining .L_ local from the strongest signature in its block.

For each .L_ block (up to the next label) we scan the instructions and pick the
first meaningful, code-derived name:

  named var touched   .setScreen / .loadRow / .checkCol / .clearFlag / .incStep
  leading/first call  .drawHexByte / .readSongByte
  hardware reg write  .setrAUD1ENV
  return-value set    .ret00 / .ret01 / .retFF        (numeric = reserved-word safe)
  tail jump to global .toWriteChannelFreq
  loop head           .loop      (target of a backward branch in-function)
  epilogue            .done      (block starts the ret/add sp/pop tail)
  assert-ok           .ok        (block right after `call DebugTrap`)
  branch condition    .not80 / .zero / .bit7Set / .levelZero (the test every
                      conditional branch into the block makes, when they agree)
  fallthrough join    .cont      (nothing nameable -- pure stack/branch glue)

Per-function scope, collision-proof (letter suffixes), seeded against locals
already named by earlier passes. Writes `ADDR .name` into src/bankNN.sym.

Usage: tools/name_locals_full.py [--bank 0xNN] [--apply] [--stats]
"""
import re, glob, os, argparse

GLOBAL = re.compile(r'^([A-Za-z_][\w]*):')
LOCAL = re.compile(r'^\.L_([0-9A-F]{4}):')
ADDR = re.compile(r'; \$([0-9a-f]{4}):')


def lc(s):
    return s[0].lower() + s[1:] if s else s


def cleanvar(v):
    v = re.sub(r'^(w|h)', '', v)
    v = re.sub(r'_p\d+$', '', v)
    v = re.sub(r'(Pu1|Pu2|Wav|Noi)$', '', v)
    return v or 'Var'


def block_backrefs(lines):
    """addresses that are the target of a backward jr/jp within the same fn."""
    back = set()
    cur_addr = 0
    fn_start = 0
    for l in lines:
        if GLOBAL.match(l):
            fn_start = None
        m = ADDR.search(l)
        if m:
            cur_addr = int(m.group(1), 16)
            if fn_start is None:
                fn_start = cur_addr
        b = re.search(r'\t(?:jr|jp) (?:n?[zc], )?\.L_([0-9A-F]{4})\b', l)
        if b:
            t = int(b.group(1), 16)
            if t <= cur_addr:
                back.add(t)
    return back


COND = {'z': 0, 'nz': 1, 'c': 2, 'nc': 3}


def branch_condition(prev, cc):
    """The name a conditional branch's test gives its target, from the
    instructions before it (`prev`, nearest last), or None."""
    k = COND[cc]
    t = prev[-1] if prev else ''
    m = re.match(r'\tcp a, \$([0-9a-f]{2})\b', t)
    if m:
        v = m.group(1).upper()
        return ['.is', '.not', '.below', '.atLeast'][k] + v
    m = re.match(r'\tbit ([0-7]), [a-l]\b', t)
    if m and k < 2:
        return f'.bit{m.group(1)}' + ['Clear', 'Set'][k]
    m = re.match(r'\tand a, \$([0-9a-f]{2})\b', t)
    if m and k < 2:
        return f'.mask{m.group(1).upper()}' + ['Clear', 'Set'][k]
    if re.match(r'\t(?:or|and) a, a\b', t) and k < 2:
        src = prev[-2] if len(prev) > 1 else ''
        g = re.match(r'\tld a, \[((?:w|h)[A-Z]\w*)\]', src)
        return ('.' + lc(cleanvar(g.group(1))) if g else '') + (['Zero', 'Nonzero'][k] if g else ['.zero', '.nonzero'][k])
    return None


def incoming_conditions(lines):
    """target address -> the names its conditional branches' tests give it
    (None for a branch without a nameable test, 'jump' for an unconditional one)."""
    out, prev = {}, []
    for l in lines:
        if GLOBAL.match(l):
            prev = []
            continue
        b = re.match(r'\t(?:jr|jp) (?:(n?[zc]), )?\.L_([0-9A-F]{4})\b', l)
        if b:
            t = int(b.group(2), 16)
            out.setdefault(t, []).append(branch_condition(prev, b.group(1)) if b.group(1) else 'jump')
        if l.startswith('\t'):
            prev = (prev + [l.split(';')[0].rstrip()])[-3:]
        elif l.strip():
            prev = []
    return out


def propose(body, addr, backrefs, prev_assert, conditions=None):
    if prev_assert:
        return '.ok'
    if addr in backrefs:
        return '.loop'
    # epilogue start
    if body:
        nb = body[0]
        if nb.startswith('\tret') or nb.startswith('\tadd sp, ') or \
           (nb.startswith('\tpop ') and any(x.startswith('\tret') for x in body[1:3])):
            return '.done'
    # scan block for the first strong signature
    for k, l in enumerate(body[:10]):
        # absolute load of a named var: ld a, [wVar]
        g = re.search(r'\tld a, \[((?:w|h)[A-Z][A-Za-z]\w*)\]', l)
        if g:
            return '.check' + cleanvar(g.group(1)) if 'cp a,' in ' '.join(body[k+1:k+3]) else '.load' + cleanvar(g.group(1))
        # hardware/hram read
        g = re.search(r'\tldh a, \[(r[A-Z]\w+|h[A-Z]\w+)\]', l)
        if g:
            return '.read' + re.sub(r'^(r|h)', '', g.group(1))
        # bit test
        g = re.search(r'\tbit (\d), ', l)
        if g:
            return '.testBit' + g.group(1)
        # stack-arg compare
        if re.match(r'\tld hl, sp\+\d', l) and k+2 < len(body) \
           and re.match(r'\tld a, \[hl\]', body[k+1]) and re.match(r'\tcp a,', body[k+2]):
            return '.checkArg'
        # named var: ld hl, wVar (+ how it's used next), or ld [wVar],a
        g = re.search(r'\tld hl, ((?:w|h)[A-Z][A-Za-z]\w*)\b', l)
        if g:
            win = body[k + 1:k + 5]
            nx = ' '.join(win)
            v = cleanvar(g.group(1))
            if re.search(r'\binc \[hl\]', nx):
                return '.inc' + v
            if re.search(r'\bdec \[hl\]', nx):
                return '.dec' + v
            # any store through hl (ld [hl],r / ld [hl+],a / ld [hl-],a)
            if re.search(r'\bld \[hl[+-]?\], ', nx):
                return '.set' + v
            # compare / test
            if re.search(r'\b(cp a, \[hl\])', nx) or ('ld a, [hl]' in nx and 'cp a,' in ' '.join(win)):
                return '.check' + v
            if re.search(r'\bor a, \[hl\]', nx):
                return '.check' + v
            # any load through hl into a register
            if re.search(r'\bld [abcdehl], \[hl[+-]?\]', nx):
                return '.load' + v
            # arithmetic against the var
            if re.search(r'\b(add|adc|sub|sbc|and|xor) [a-z], \[hl\]', nx):
                return '.use' + v
        g = re.search(r'\tld \[((?:w|h)[A-Z][A-Za-z]\w*)\], a', l)
        if g:
            return '.store' + cleanvar(g.group(1))
        g = re.search(r'\tldh \[(r[A-Z]\w+)\], a', l)
        if g:
            return '.set' + g.group(1)
        # return-value setter (numeric, reserved-word safe): ld e,$VV then a
        # jump OR the fall-through into the epilogue (next line is a label/ret)
        g = re.match(r'\tld e, \$([0-9a-f]{2})', l)
        if g:
            nxt = body[k + 1] if k + 1 < len(body) else '\tret'
            if re.match(r'\t(?:jp|jr|ret|pop|add sp)', nxt) or len(body) == k + 1:
                return f'.ret{g.group(1).upper()}'
        # meaningful call
        g = re.search(r'\tcall ([A-Z]\w+)', l)
        if g and g.group(1) != 'DebugTrap':
            return '.' + lc(g.group(1))
    # tail jump to a global
    g = re.search(r'\t(?:jp|jr) ([A-Z]\w[\w]+)\b', body[0]) if body else None
    if g and not g.group(1).startswith('L') and '.' not in g.group(1):
        return '.to' + g.group(1)
    # arithmetic / structural glue named by dominant operation
    txt = ' '.join(body[:5])
    if re.search(r'\t(?:srl|sla|rr|rl|swap) ', body[0] if body else ''):
        return '.shift'
    if re.search(r'\badd hl, ', txt):
        return '.index'
    if re.search(r'\b(adc|sbc) ', txt):
        return '.add16'
    if body and re.match(r'\t(?:inc|dec) [a-z]', body[0]):
        return '.step'
    if body and re.match(r'\tcp a, \$', body[0]):
        return '.branch'
    names = [c for c in (conditions or {}).get(addr, []) if c != 'jump']
    if names and None not in names and len(set(names)) == 1:
        return names[0]
    return '.cont'


def analyze(asm, sym):
    curated, curnames = set(), {}
    for ln in open(sym):
        m = re.match(r'([0-9A-Fa-f]{4})\s+(\.\S+)', ln)
        if m:
            curated.add(int(m.group(1), 16))
            curnames[int(m.group(1), 16)] = m.group(2)
    L = open(asm).read().splitlines()
    back = block_backrefs(L)
    conds = incoming_conditions(L)
    # pre-pass: map every local addr -> enclosing fn, and seed used-sets with
    # ALL prior-pass dot-names per function (order-independent)
    per_fn, used = {}, {}
    fn_of = {}
    cur = None
    for l in L:
        g = GLOBAL.match(l)
        if g:
            cur = g.group(1); per_fn.setdefault(cur, {}); used.setdefault(cur, set())
            continue
        lm = LOCAL.match(l)
        if lm and cur:
            fn_of[int(lm.group(1), 16)] = cur
        # ANY existing local label (.foo:) -- curated names render as their name,
        # not .L_, so seed the used-set from the asm directly (order-independent)
        am = re.match(r'^(\.\S+):', l)
        if am and cur:
            used[cur].add(am.group(1))
    cur = None
    for i, l in enumerate(L):
        g = GLOBAL.match(l)
        if g:
            cur = g.group(1)
            continue
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
            if len(body) >= 10:
                break
        nm = propose(body, a, back, prev.startswith('call DebugTrap'), conds)
        final, kk = nm, 1
        while final in used[cur]:
            kk += 1
            final = f'{nm}_{kk}'          # .cont, .cont_2, .cont_3 (rgbds-safe)
        used[cur].add(final)
        per_fn[cur][a] = final
    return per_fn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--stats', action='store_true')
    args = ap.parse_args()
    paths = sorted(glob.glob('src/bank*.asm'))
    if args.bank:
        b = int(args.bank, 0)
        paths = [p for p in paths if int(os.path.basename(p)[4:6], 16) == b]
    from collections import Counter
    kinds = Counter()
    total = 0
    for p in paths:
        sp = p[:-4] + '.sym'
        per_fn = analyze(p, sp)
        ents = [(a, nm) for fn in per_fn for a, nm in per_fn[fn].items()]
        total += len(ents)
        for _, nm in ents:
            base = re.sub(r'[b-z]$', '', nm)
            kinds['.cont' if base == '.cont' else 'meaningful'] += 1
        print(f'{os.path.basename(p)}: {len(ents)}')
        if args.apply and ents:
            with open(sp, 'a') as f:
                f.write('\n; --- full local naming (block signature) ---\n')
                for a, nm in sorted(ents):
                    assert nm.startswith('.'), nm
                    f.write(f'{a:04X} {nm}\n')
    if args.stats:
        print('meaningful:', kinds['meaningful'], ' .cont fallback:', kinds['.cont'])
    print(f'TOTAL: {total}')


if __name__ == '__main__':
    main()
