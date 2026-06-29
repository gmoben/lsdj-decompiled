#!/usr/bin/env python3
"""State-machine analysis for a WRAM address: find writes (transitions) and
reads (checks) with values + the enclosing function. Tracks `ld hl,$ADDR` then
`[hl]` ops, plus direct `ld a,[$ADDR]` / `ld [$ADDR],a`.

Caveat: the `hl`-tracking is heuristic and can misattribute address arithmetic
(e.g. indexing past the var into the next field) as a write to this var, so
always confirm the cited sites in the disassembly.

Usage: tools/statevar.py <addr-hex> [more addrs...]"""
import re, sys, glob, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
addrs = [a.lower().lstrip('$') for a in sys.argv[1:]]
glre = re.compile(r'^([A-Za-z][A-Za-z0-9_]*):')
def strip(l): return re.sub(r'\s*; \$[0-9a-f]+:.*', '', l).rstrip()

for addr in addrs:
    print(f"\n========== ${addr} ==========")
    writes_imm = {}   # value -> [func, ...]
    writes_dyn = []   # (func, src)
    reads_cp = {}     # value -> count
    read_funcs = set(); write_funcs = set()
    for f in sorted(glob.glob(os.path.join(ROOT, 'src', 'bank*.asm'))):
        lines = [strip(l) for l in open(f).read().split('\n')]
        cur = None
        hl_is = False  # is hl currently = our addr?
        for i, l in enumerate(lines):
            m = glre.match(l)
            if m: cur = m.group(1); continue
            mh = re.match(r'\tld hl, \$([0-9a-f]{4})', l)
            if mh:
                hl_is = (mh.group(1) == addr); continue
            if hl_is:
                mw = re.match(r'\tld \[hl\], \$([0-9a-f]+)', l)
                if mw:
                    writes_imm.setdefault(mw.group(1), []).append(cur); write_funcs.add(cur); continue
                if re.match(r'\tld \[hl\], [aebcdhl]', l):
                    writes_dyn.append((cur, l.strip())); write_funcs.add(cur); continue
                if re.match(r'\t(inc|dec) \[hl\]', l):
                    writes_dyn.append((cur, l.strip()+" (inc/dec)")); write_funcs.add(cur); continue
                # read+compare: ld a,[hl] then look ahead a few lines for a cp
                if re.match(r'\tld a, \[hl\]', l):
                    read_funcs.add(cur)
                    for j in range(i+1, min(i+4, len(lines))):
                        mc = re.match(r'\tcp a, \$([0-9a-f]+)', lines[j])
                        if mc: reads_cp[mc.group(1)] = reads_cp.get(mc.group(1), 0)+1; break
            # direct (non-hl) forms
            md = re.match(r'\tld \[\$'+addr+r'\], ([aebcdhl])', l)
            if md: writes_dyn.append((cur, l.strip())); write_funcs.add(cur)
    print("WRITES (immediate value -> functions):")
    for v, fns in sorted(writes_imm.items()):
        print(f"  =${v}: {', '.join(sorted(set(fns)))}")
    if writes_dyn:
        print("WRITES (computed):")
        for fn, src in writes_dyn[:12]: print(f"  [{fn}] {src}")
    print("READS (cp value -> count):", {('$'+k): v for k, v in sorted(reads_cp.items())})
    print(f"#write-funcs={len(write_funcs)} #read-funcs={len(read_funcs)}")
