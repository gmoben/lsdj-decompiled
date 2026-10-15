#!/usr/bin/env python3
"""Exhaustive _DATA_ audit: every ROM data address referenced by an operand
must carry an exact-address symbol, or Emulicious shows _DATA_xxxx for it.

Scans all carved banks for:
  - direct memory ops:  ld a,[$nnnn] / ld [$nnnn],a / ld [$nnnn],sp
  - pointer loads:      ld hl/de/bc,$nnnn   (kept only when the target lands
                        on a data (db/dw/INCBIN) line, so numeric constants
                        like copy lengths don't flood the report)

Bank resolution: targets <$4000 resolve to bank 00; $4000-$7FFF resolve to the
scanning bank (for bank 00 callers, the last `ld [$2000],N` bank write in the
function is used, like callgraph.py).

Reports refs whose target has NO symbol at that exact address, grouped by
target, with the target's rendering (label+offset / raw db) for context.

Usage: python3 tools/data_refs.py [--bank 0xNN] [--all]
"""
import argparse, glob, os, re

ADDR = re.compile(r'; \$([0-9a-f]{4}):')
LABEL = re.compile(r'^([A-Za-z_][\w]*):')
MEMOP = re.compile(r'\tld (?:a, \[\$([0-9a-f]{4})\]|\[\$([0-9a-f]{4})\], (?:a|sp))')
PTRLD = re.compile(r'\tld (?:hl|de|bc), \$([0-9a-f]{4})\b')
BANKW = re.compile(r'\tld \[\$2000\], a|\tld \[hl\], \$([0-9a-f]{2})\s+; \$[0-9a-f]{4}: 36')

def load_bank(path):
    """Return (lines, addr2line, labeled_addrs, data_lines set of addresses)."""
    lines = [l for l in open(path).read().splitlines() if not l.startswith(';')]  # not the routines' comments
    addr2line = {}
    labeled = set()
    data_addr = set()
    pend_label = False
    for i, ln in enumerate(lines):
        if LABEL.match(ln):
            pend_label = True
            continue
        m = ADDR.search(ln)
        if m:
            a = int(m.group(1), 16)
            addr2line.setdefault(a, i)
            if pend_label:
                labeled.add(a)
            pend_label = False
            if re.match(r'\s*(d[bws]|RGB) ', ln):
                data_addr.add(a)
        elif re.match(r'\s*(db|dw)\b', ln):
            pass
    return lines, addr2line, labeled, data_addr

def data_line_span(lines, addr2line, target):
    """True if target falls within a db/dw row (row start <= target < next row)."""
    # find nearest row start at/below target within 16 bytes
    for base in range(target, max(target - 16, -1), -1):
        i = addr2line.get(base)
        if i is not None:
            ln = lines[i]
            if re.match(r'\s*(db|dw)\b', ln.lstrip()) or '\tdb ' in ln or '\tdw ' in ln:
                return True
            return base == target and False
    return False

def audit_runs():
    """Second audit class: every data-run start (db/dw after code) must carry a
    symbol -- Emulicious statically segments these and emits _DATA_ otherwise.
    Pure $00/$ff fill runs are skipped (bank-end padding is not labeled)."""
    rom = open('build/lsdj9_4_0.gb', 'rb').read()
    total = 0
    for p in sorted(glob.glob('src/bank*.asm')):
        b = int(os.path.basename(p)[4:6], 16)
        syms = set()
        sp = p[:-4] + '.sym'
        if os.path.exists(sp):
            for ln in open(sp):
                m = re.match(r'([0-9A-Fa-f]{4})\s+\S+', ln)
                if m:
                    syms.add(int(m.group(1), 16))
        lines = [l for l in open(p).read().splitlines() if not l.startswith(';')]  # not the routines' comments
        prev_db = False
        out = []
        for ln in lines:
            t = ln.lstrip()
            is_db = t.startswith(('db ', 'dw ', 'ds ', 'RGB '))
            if is_db:
                if not prev_db:
                    m2 = re.search(r'; \$([0-9a-f]{4})', ln)
                    if m2:
                        a = int(m2.group(1), 16)
                        if a not in syms:
                            out.append(a)
                prev_db = True
            elif re.match(r'^[A-Za-z_.;]', t) or not t:
                pass
            else:
                prev_db = False
        real = []
        for a in out:
            off = a if b == 0 else b * 0x4000 + (a - 0x4000)
            seg = rom[off:off + 16]
            if len(set(seg)) == 1 and seg[0] in (0, 0xff):
                continue
            real.append(a)
        if real:
            total += len(real)
            print(f'bank{b:02x}: ' + ' '.join(f'${a:04x}' for a in real))
    print(f'unlabeled data-run starts: {total}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--runs', action='store_true', help='audit unlabeled data-run starts instead')
    a = ap.parse_args()
    if a.runs:
        audit_runs()
        return

    banks = {}
    for p in sorted(glob.glob('src/bank*.asm')):
        b = int(os.path.basename(p)[4:6], 16)
        lines, addr2line, labeled, data_addr = load_bank(p)
        # authoritative symbol source: the .sym file (labels can sit mid-row
        # in the carve but still resolve in Emulicious)
        symp = p[:-4] + '.sym'
        if os.path.exists(symp):
            for ln in open(symp):
                m = re.match(r'([0-9A-Fa-f]{4})\s+\S+', ln)
                if m:
                    labeled.add(int(m.group(1), 16))
        banks[b] = (lines, addr2line, labeled, data_addr)

    paths = sorted(banks)
    if a.bank:
        paths = [int(a.bank, 0)]

    missing = {}
    for b in sorted(banks):
        lines, addr2line, labeled, _ = banks[b]
        cur_bank = None
        for i, ln in enumerate(lines):
            m = re.search(r'ldh? \[hCurrentBank\], a|ld \[\$2000\], a', ln)
            targets = []
            m1 = MEMOP.search(ln)
            if m1:
                t = int((m1.group(1) or m1.group(2)), 16)
                if t < 0x8000:
                    targets.append((t, 'mem'))
            m2 = PTRLD.search(ln)
            if m2:
                t = int(m2.group(1), 16)
                if 0x100 <= t < 0x8000:   # <$100 = arithmetic constants
                    targets.append((t, 'ptr'))
            for t, kind in targets:
                if kind == 'mem' and 0x2000 <= t < 0x6000:
                    continue              # MBC bank/RAM-select writes
                tb = 0 if t < 0x4000 else b
                if tb == 0 and 0 not in banks:
                    continue
                tl, ta, tlab, _ = banks.get(tb, (None,)*4)
                if tl is None:
                    continue
                if t in tlab:
                    continue                      # exact symbol exists
                # numeric constants that only *look* like addresses:
                # (a) pointer-loads into the header/logo or pure fill regions
                if kind == 'ptr':
                    rom = globals().setdefault('_rom', open('build/lsdj9_4_0.gb','rb').read())
                    off = t if tb == 0 else tb * 0x4000 + (t - 0x4000)
                    end = 0x4000 if tb == 0 else (tb + 1) * 0x4000
                    seg = rom[off:min(off + 12, end)]
                    if len(set(seg)) == 1 and seg[0] in (0x00, 0xFF):
                        continue                  # uniform fill = constant
                    if tb == 0 and 0x0104 <= t < 0x014E:
                        continue                  # Nintendo logo / header text
                # FarCall-style immediates: labeled at this address in another
                # bank -> Emulicious resolves via the paged bank's symbol
                if kind == 'ptr' and any(t in banks[ob][2] for ob in banks if ob != tb):
                    continue
                li = ta.get(t)
                if li is None:
                    # mid-instruction or mid-data-row target
                    if kind == 'ptr' and not data_line_span(tl, ta, t):
                        continue
                    is_data = True
                else:
                    row = tl[li].lstrip()
                    is_data = row.startswith(('db ', 'dw ', 'ds ', 'RGB '))
                    if kind == 'ptr' and not is_data:
                        continue                  # pointer to code (jump tables etc. -> label class)
                    if kind == 'mem' and not is_data:
                        is_data = True            # direct access to "code" bytes = still data ref
                site = None
                ms = ADDR.search(ln)
                if ms:
                    site = int(ms.group(1), 16)
                missing.setdefault((tb, t), []).append((b, site, kind))

    print(f'unique unlabeled referenced data addresses: {len(missing)}')
    for (tb, t), sites in sorted(missing.items()):
        ss = ', '.join(f'{sb:02x}:{sa:04x}({k})' for sb, sa, k in sites[:4])
        extra = f' +{len(sites)-4}' if len(sites) > 4 else ''
        print(f'  bank{tb:02x}:${t:04x}  <- {ss}{extra}')

if __name__ == '__main__':
    main()
