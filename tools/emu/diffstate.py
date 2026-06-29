#!/usr/bin/env python3
"""Deterministic state diff: snapshot a WRAM region, optionally perform an action
(emu.py args, e.g. KEY a+right), snapshot again, and print changed bytes annotated
with their documented names from constants/ram.inc (so unknowns stand out).

The core verification tool: change something in the game, then see exactly which
RAM bytes moved. See ANALYSIS.md (technique A) for recipes.

Usage: diffstate.py <start-hex> <len> [emu-op args...]
  diffstate.py c400 256 KEY right     # which byte is the cursor column?
  diffstate.py c520 48 KEY a+up       # which byte does this setting edit?
"""
import subprocess, sys, time, re
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
EMU = str(REPO / 'tools' / 'emu' / 'emu.py')

def emu(*a):
    return subprocess.run(['python3', EMU, *a], capture_output=True, text=True, cwd=REPO).stdout

def load_names():
    names = {}
    for line in (REPO / 'constants' / 'ram.inc').read_text().splitlines():
        m = re.match(r'\s*DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]+)', line)
        if m and int(m.group(2), 16) >= 0x8000:
            names[int(m.group(2), 16)] = m.group(1)
    return names

NAMES = load_names()

def name_of(a):
    if a in NAMES:
        return NAMES[a]
    for base in range(a - 1, a - 32, -1):           # nearest preceding base -> base+off
        if base in NAMES:
            return f'{NAMES[base]}+{a - base}'
    return '?'

def snap(start, length):
    return [int(x, 16) for x in emu('read', start, str(length)).split()]

def main():
    start, length = sys.argv[1], int(sys.argv[2])
    action = sys.argv[3:]
    base = int(start, 16)
    A = snap(start, length)
    if action:
        emu(*action); time.sleep(0.4)
    B = snap(start, length)
    n = min(len(A), len(B))
    changes = [(base + i, A[i], B[i]) for i in range(n) if A[i] != B[i]]
    print(f"action={action or '(none)'}  region=${base:04x}-${base+length-1:04x}  {len(changes)} changed:")
    for a, x, y in changes:
        print(f"  ${a:04x}  {x:02x} -> {y:02x}   {name_of(a)}")

if __name__ == '__main__':
    main()
