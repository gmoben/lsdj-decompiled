#!/usr/bin/env python3
"""Dump a function body from a carved bank .asm file.

Usage: python3 tools/show_func.py BANK ADDR [ADDR ...]
  BANK: hex bank number (e.g. 05 or 0x05)
  ADDR: hex address (e.g. 4233 or $4233)

Prints from the address up to (but not including) the next global label,
capped at 80 lines -- enough to read one routine.
"""
import re, sys

def show(bank, addr):
    b = int(bank, 16) if not bank.startswith('0x') else int(bank, 0)
    a = addr.lstrip('$')
    path = f'src/bank{b:02x}.asm'
    lines = open(path).read().splitlines()
    start = None
    for i, ln in enumerate(lines):
        if re.search(rf'; \${a.lower()}:', ln):
            start = i
            break
    if start is None:
        print(f'-- bank{b:02x}:${a}: not found'); return
    # back up to an immediately-preceding label line, if any
    s = start
    while s > 0 and re.match(r'^[A-Za-z_.][\w.]*:', lines[s-1]):
        s -= 1
    print(f'=== bank{b:02x}:${a} ===')
    for j in range(s, min(s + 80, len(lines))):
        ln = lines[j]
        if j > start and re.match(r'^[A-Za-z_][\w.]*:', ln):  # next global label
            break
        print(ln)

if __name__ == '__main__':
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        bank = args[i]; i += 1
        while i < len(args) and not (len(args[i]) <= 2 or args[i].startswith('0x')):
            show(bank, args[i]); i += 1
