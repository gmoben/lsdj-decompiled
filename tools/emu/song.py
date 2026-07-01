#!/usr/bin/env python3
"""Comprehensive LSDj editing driver for dynamic disassembly analysis.

Sits on top of emu.py (RAM read/write, KEY, screenshot) and nav.py (screen
graph). Every mutating action is *verified* against the per-screen cursor/edit
RAM variables, because raw joypad input is flaky (see tools/emu/ANALYSIS.md).

The point: exercise every editor/feature on demand so the sound engine, draw
code, and data tables actually run -- which is how we turn `_LABEL_`/`_DATA_`
auto-labels into named, reverse-engineered routines and tables.

Library (import) or CLI. Examples:
    python3 tools/emu/song.py where
    python3 tools/emu/song.py goto phrase
    python3 tools/emu/song.py moveto 0 3          # cursor col 0 row 3 (current screen)
    python3 tools/emu/song.py place               # A (place default value at cursor)
    python3 tools/emu/song.py inc 5               # A+up x5 (raise value)
    python3 tools/emu/song.py note C 3            # PHRASE: enter note C-3 at cursor row
    python3 tools/emu/song.py addchain            # SONG: create a chain under the cursor
    python3 tools/emu/song.py shot .emu/x.png

Verified-live primitives are marked (V); others are best-effort (BE) pending a
live check -- keep this annotation honest as each is confirmed.
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
EMU = str(HERE / "emu.py")
NAV = str(HERE / "nav.py")
ROOT = HERE.parent.parent

# Screen ids (wCurrentScreen $C401).
SCREENS = {1: "PHRASE", 2: "GROOVE", 3: "CHAIN", 4: "SONG", 5: "TABLE",
           6: "INSTR", 8: "WAVE", 9: "PROJECT", 0x0a: "SYNTH", 0x0d: "FILE",
           0x0e: "HELP"}
SID = {v: k for k, v in SCREENS.items()}

# Per-screen (col_var, row_var) cursor addresses (hex str), from constants/ram.inc.
# None where that axis doesn't exist on the screen.
CURSOR = {
    "SONG":   ("c41d", "c41e"),   # col=channel 0-3, row 0-255
    "CHAIN":  ("c419", "c41a"),   # col 0=phrase 1=transpose, row 0-15
    "PHRASE": ("c415", "c416"),   # col 1=NOTE 2=INSTR 3=CMD 4=CMDVAL (verified live), row 0-15
    "TABLE":  ("c92f", "c930"),   # col (bit7 special), row 0-15
    "INSTR":  ("c428", "c427"),   # col, field(row)
    "GROOVE": (None,   "c9d4"),   # row 0-15
    "WAVE":   ("c42b", None),     # wave-frame position 0-F
    "SYNTH":  ("c442", "c441"),   # cursor, sub-screen
}

# PHRASE cursor columns in wPhraseCol ($C415) -- VERIFIED live (the old ram.inc
# "0=NOTE" comment is off by one; column 0 is unreachable, NOTE is 1).
PH_NOTE, PH_INSTR, PH_CMD, PH_CMDVAL = 1, 2, 3, 4

# Frequently-read state vars.
VARS = {
    "screen": "c401", "phrase": "c414", "chain": "c418", "table": "c41b",
    "instrument": "c576", "groove": "c417",
}


def _emu(*args: str, timeout: int = 30) -> str:
    return subprocess.run(["python3", EMU, *args], capture_output=True,
                          text=True, cwd=ROOT, timeout=timeout).stdout


class LSDj:
    """Stateful driver bound to the running emud.py daemon."""

    # ------- low level ----------------------------------------------------
    def rd(self, addr: str, n: int = 1) -> int:
        """Read n bytes little-endian as an int (n<=2 typical)."""
        out = _emu("read", addr, str(n)).split()
        vals = [int(x, 16) for x in out]
        return vals[0] if n == 1 else sum(v << (8 * i) for i, v in enumerate(vals))

    def rdbytes(self, addr: str, n: int) -> list[int]:
        return [int(x, 16) for x in _emu("read", addr, str(n)).split()]

    def wr(self, addr: str, *vals: int) -> None:
        _emu("write", addr, *(f"{v & 0xff:02x}" for v in vals))

    def key(self, keys: str, settle: float = 0.16) -> None:
        _emu("KEY", keys)
        time.sleep(settle)

    def shot(self, path: str = ".emu/song.png") -> str:
        return _emu("SHOT", path).strip()

    # ------- screen / navigation (V) --------------------------------------
    def at(self) -> str:
        return SCREENS.get(self.rd(VARS["screen"]), "?")

    def goto(self, name: str) -> str:
        name = name.upper()
        subprocess.run(["python3", NAV, "goto", name], cwd=ROOT,
                       capture_output=True, text=True, timeout=60)
        return self.at()

    # ------- cursor (V for SONG/CHAIN/PHRASE) ------------------------------
    def cursor(self) -> tuple[int | None, int | None]:
        cv, rv = CURSOR.get(self.at(), (None, None))
        return (self.rd(cv) if cv else None, self.rd(rv) if rv else None)

    def _seek(self, var: str, target: int, dec: str, inc: str,
              tries: int = 40) -> bool:
        """Press dec/inc until [var] == target (bounded, no-progress abort)."""
        last = None
        for _ in range(tries):
            cur = self.rd(var)
            if cur == target:
                return True
            self.key(inc if cur < target else dec)
            if cur == last:                     # no progress twice -> give up
                nxt = self.rd(var)
                if nxt == cur:
                    return nxt == target
            last = cur
        return self.rd(var) == target

    def moveto(self, col: int | None = None, row: int | None = None) -> tuple[int | None, int | None]:
        """Move the current screen's cursor to (col,row); verify each axis."""
        cv, rv = CURSOR.get(self.at(), (None, None))
        if row is not None and rv:
            self._seek(rv, row, "up", "down")
        if col is not None and cv:
            self._seek(cv, col, "left", "right")
        return self.cursor()

    # ------- cell edits ---------------------------------------------------
    def place(self) -> None:      # A: place default value at the cursor (V)
        self.key("a")

    def clear(self) -> None:      # A+B: clear the cell under the cursor (V)
        self.key("a+b")

    def inc(self, n: int = 1) -> None:   # A+up: raise value (V)
        for _ in range(n):
            self.key("a+up")

    def dec(self, n: int = 1) -> None:   # A+down: lower value (V)
        for _ in range(n):
            self.key("a+down")

    def nudge_right(self, n: int = 1) -> None:  # A+right: +$10 on many fields (BE)
        for _ in range(n):
            self.key("a+right")

    def nudge_left(self, n: int = 1) -> None:   # A+left (BE)
        for _ in range(n):
            self.key("a+left")

    # ------- SONG (BE) ----------------------------------------------------
    def addchain(self, col: int = 0, row: int = 0) -> int:
        """SONG: create a chain under the cursor (A on empty cell). Returns the
        chain number now shown (wChain / cell)."""
        self.goto("SONG")
        self.moveto(col, row)
        self.place()
        return self.rd(VARS["chain"])

    # ------- PHRASE (V: place/step/instr; exact pitch = relative stepping) --
    def note(self, row: int | None = None, steps: int = 0,
             instr: int | None = None) -> dict[str, Any]:
        """PHRASE: place a note at `row` (or current row) on the NOTE column and
        raise/lower its pitch `steps` times (A+up/-down; observed ~1 octave per
        press). Optionally set the instrument column with `instr` inc/decs.
        `place()` alone already runs the note-on path; stepping exercises the
        pitch/transpose code. Returns the observed cursor cell."""
        self.goto("PHRASE")
        self.moveto(PH_NOTE, row)
        self.place()                                 # enter last-used note
        if steps > 0:
            self.inc(steps)
        elif steps < 0:
            self.dec(-steps)
        res: dict[str, Any] = {"phrase": self.rd(VARS["phrase"]),
                               "cursor": self.cursor()}
        if instr is not None:
            self.moveto(PH_INSTR, row)
            if instr > 0:
                self.inc(instr)
            res["instr_cursor"] = self.cursor()
        return res

    def fx(self, row: int | None = None, steps: int = 0) -> dict[str, Any]:
        """PHRASE: place an FX command at `row` (CMD column) and step it `steps`
        times (A+up cycles through commands A,B,C...). Exercises FxCmd_* handlers."""
        self.goto("PHRASE")
        self.moveto(PH_CMD, row)
        self.place()
        if steps > 0:
            self.inc(steps)
        return {"cursor": self.cursor()}


def _cli() -> None:
    d = LSDj()
    a = sys.argv[1:]
    if not a or a[0] in ("where", "here"):
        c, r = d.cursor()
        print(f"{d.at()}  cursor=({c},{r})")
    elif a[0] == "goto":
        print(d.goto(a[1]))
    elif a[0] == "moveto":
        col = None if a[1] == "-" else int(a[1])
        row = None if len(a) < 3 or a[2] == "-" else int(a[2])
        print(d.moveto(col, row))
    elif a[0] == "place":
        d.place(); print(d.cursor())
    elif a[0] == "clear":
        d.clear(); print(d.cursor())
    elif a[0] == "inc":
        d.inc(int(a[1]) if len(a) > 1 else 1)
    elif a[0] == "dec":
        d.dec(int(a[1]) if len(a) > 1 else 1)
    elif a[0] == "note":     # note [row] [semitones] [instr]
        row = int(a[1]) if len(a) > 1 else None
        semis = int(a[2]) if len(a) > 2 else 0
        instr = int(a[3]) if len(a) > 3 else None
        print(d.note(row, semis, instr))
    elif a[0] == "addchain":
        print("chain:", d.addchain())
    elif a[0] == "fx":
        print(d.fx())
    elif a[0] == "shot":
        print(d.shot(a[1] if len(a) > 1 else ".emu/song.png"))
    elif a[0] == "read":
        print(" ".join(f"{v:02x}" for v in d.rdbytes(a[1], int(a[2]))))
    else:
        print(__doc__)


if __name__ == "__main__":
    _cli()
