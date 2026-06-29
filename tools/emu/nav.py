#!/usr/bin/env python3
"""LSDj screen navigation as a verified 2D-grid pathfinder (SameBoy harness).

The SELECT+dir screen map is a 5x3 grid. The top and bottom rows are
column-spanning *stateful* hubs: going up to PROJECT/SYNTH/WAVE or down to
GROOVE and back returns you to the column you left from. So navigation is
modelled in (col,row) space, not as a flat screen graph. Verified against the
in-game nav-map widget and live stepping:

    col:    0        1        2        3        4
    row -1: PROJECT  PROJECT  SYNTH    SYNTH    WAVE
    row  0: SONG     CHAIN    PHRASE   INSTR    TABLE
    row +1: GROOVE   GROOVE   GROOVE   GROOVE   GROOVE

We ALWAYS route via row 0, whose screens are unique and easy to verify, so we
never depend on hub statefulness. NB: SELECT+LEFT on SONG toggles LIVE mode (a
real side effect, not a no-op), so the pathfinder never issues `left` at col 0.

Drill-down moves (SONG->CHAIN->PHRASE) are content-gated: the loaded song must
have a chain/phrase under the cursor. Every step verifies wCurrentScreen ($C401).

Usage:
  nav.py where | here              # current screen (+ resolved grid cell for `here`)
  nav.py step <up|down|left|right> # one robust SELECT+dir (retries, verifies)
  nav.py goto <name|id>            # grid pathfind (re-localises first)
  nav.py probe                     # map the current screen's 4 neighbours
"""
import subprocess, sys, time
from pathlib import Path

EMU = str(Path(__file__).resolve().parent / 'emu.py')
ROOT = Path(__file__).resolve().parent.parent.parent

NAMES = {1: 'PHRASE', 2: 'GROOVE', 3: 'CHAIN', 4: 'SONG', 5: 'TABLE', 6: 'INSTR',
         8: 'WAVE', 9: 'PROJECT', 0x0a: 'SYNTH', 0x0d: 'FILE', 0x0e: 'HELP'}
IDS = {v: k for k, v in NAMES.items()}
DIRS = ['up', 'down', 'left', 'right']
OPP = {'up': 'down', 'down': 'up', 'left': 'right', 'right': 'left'}

# Verified 5x3 grid: (col, row) -> screen id.
GRID = {
    (0, -1): 0x09, (1, -1): 0x09, (2, -1): 0x0a, (3, -1): 0x0a, (4, -1): 0x08,
    (0,  0): 0x04, (1,  0): 0x03, (2,  0): 0x01, (3,  0): 0x06, (4,  0): 0x05,
    (0,  1): 0x02, (1,  1): 0x02, (2,  1): 0x02, (3,  1): 0x02, (4,  1): 0x02,
}
ROW0 = {sid: col for (col, row), sid in GRID.items() if row == 0}   # screen -> col
# Canonical target cell per screen (each reached via its row-0 column):
CELL = {0x04: (0, 0), 0x03: (1, 0), 0x01: (2, 0), 0x06: (3, 0), 0x05: (4, 0),
        0x09: (0, -1), 0x0a: (2, -1), 0x08: (4, -1), 0x02: (0, 1)}


def emu(*a):
    return subprocess.run(['python3', EMU, *a], capture_output=True, text=True, cwd=ROOT).stdout


def screen():
    return int(emu('read', 'c401', '1').strip() or '-1', 16)


def nm(s):
    return NAMES.get(s, '?')


def robust_step(direction, tries=6, settle=0.28):
    """One SELECT+dir, retried until $C401 changes (absorbs focus-warmup misses)."""
    before = screen()
    for _ in range(tries):
        emu('KEY', f'select+{direction}')
        time.sleep(settle)
        now = screen()
        if now != before:
            return now
    return before


def localize():
    """(col,row) of the current position, stepping a hub down/up to row 0 to
    disambiguate the column (hubs span columns but remember where you entered)."""
    s = screen()
    if s in ROW0:
        return (ROW0[s], 0)
    if s == 0x02:                          # GROOVE: up returns to entry column
        return (ROW0.get(robust_step('up'), 0), 0)
    if s in (0x09, 0x0a, 0x08):            # top hub: down returns to entry column
        return (ROW0.get(robust_step('down'), 0), 0)
    if s == 0x0e:                          # HELP: B backs out
        emu('KEY', 'b'); time.sleep(0.3)
        return localize()
    return (ROW0.get(s, 0), 0)             # FILE/unknown: best effort


def goto(dst):
    if dst not in CELL:
        return screen()                    # FILE/HELP aren't in the SELECT+dir grid
    col, _ = localize()                    # now on row 0
    tcol, trow = CELL[dst]
    while col != tcol:                     # horizontal on row 0 (unique, verifiable)
        step = 1 if tcol > col else -1
        exp = GRID[(col + step, 0)]
        got = robust_step('right' if step > 0 else 'left')
        if got != exp:                     # gated/miss: one retry then bail
            got = robust_step('right' if step > 0 else 'left')
            if got != exp:
                break
        col += step
    if trow != 0 and col == tcol:          # then up/down into the hub
        robust_step('up' if trow < 0 else 'down')
    return screen()


def probe():
    s0 = screen()
    print(f"at {s0:#04x} {nm(s0)}")
    for d in DIRS:
        before = screen()
        after = robust_step(d)
        print(f"  {d:5} {'(no change)' if after == before else f'-> {after:#04x} {nm(after)}'}")
        if after != before:
            robust_step(OPP[d])            # self-return (row-0 moves are symmetric)


def main():
    op = sys.argv[1] if len(sys.argv) > 1 else 'where'
    if op in ('where', 'here'):
        s = screen()
        if op == 'here':
            c, r = localize()
            print(f"{s:#04x} {nm(s)}  cell=({c},{r})")
        else:
            print(f"{s:#04x} {nm(s)}")
    elif op == 'step':
        s = robust_step(sys.argv[2])
        print(f"{s:#04x} {nm(s)}")
    elif op == 'probe':
        probe()
    elif op == 'goto':
        t = sys.argv[2]
        target = IDS.get(t.upper()) or int(t, 16)
        s = goto(target)
        print(f"{s:#04x} {nm(s)}  ({'OK' if s == target else 'STUCK'})")
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
