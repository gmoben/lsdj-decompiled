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
    python3 tools/emu/song.py aup 5               # A+up x5 (raise value under cursor)
    python3 tools/emu/song.py note C 3            # PHRASE: enter note C-3 at cursor row
    python3 tools/emu/song.py addchain            # SONG: create a chain under the cursor
    python3 tools/emu/song.py shot .emu/x.png

Verified-live primitives are marked (V); others are best-effort (BE) pending a
live check -- keep this annotation honest as each is confirmed.
"""
from __future__ import annotations

import re
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

# Working song = 32 KiB in SRAM banks 0-3 at $A000 (docs/data_model.md). Each
# entry: (byte offset in the 32 KiB song, row stride). offset -> SRAM address:
#   bank = offset >> 13 ; cpu = $A000 + (offset & $1FFF).
SONG = {
    "phrase_notes":       (0x0000, 16),   # [255][16]
    "grooves":            (0x1090, 16),   # [32][16]
    "sequence":           (0x1290, 4),    # [256][4]  chain# per (row, channel)
    "table_env":          (0x1690, 16),   # [32][16]
    "chain_phrases":      (0x2080, 16),   # [128][16]
    "chain_transposes":   (0x2880, 16),   # [128][16]
    "instruments":        (0x3080, 16),   # [64][16]
    "table_transpose":    (0x3480, 16),   # [32][16]
    "table_cmd1":         (0x3680, 16),   # [32][16]
    "table_cmd2":         (0x3A80, 16),   # [32][16]
    "synth_params":       (0x3EB2, 16),   # [16][16]
    "properties":         (0x3FB2, 1),
    "phrase_fx":          (0x4000, 16),   # [255][16]
    "phrase_fx_val":      (0x4FF0, 16),   # [255][16]
    "wave_frames":        (0x6000, 32),   # [16][16][32] nibbles
    "phrase_instruments": (0x7000, 16),   # [255][16]
}

_X_RE = re.compile(r"^[0-9a-fA-F]{4}:\s+((?:[0-9a-fA-F]{2}\s+)+)")


def _parse_x(text: str) -> list[int]:
    for line in text.splitlines():
        m = _X_RE.match(line.strip())
        if m:
            return [int(b, 16) for b in m.group(1).split()]
    return []


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

    # ------- working song in SRAM (V) -------------------------------------
    def _raw(self, cmd: str) -> str:
        return _emu("RAW", cmd)

    def sram_read(self, offset: int, n: int = 1) -> list[int]:
        """Read `n` bytes of the 32 KiB working song at `offset` ($0-$7FFF) via
        MBC5 SRAM banking. Pauses, enables cart RAM (LSDj leaves it disabled
        between accesses), reads across bank boundaries, resumes if it was
        running. See docs/data_model.md for the offset map."""
        was = _emu("STATE").strip() == "running"
        if was:
            _emu("BREAK")
        self._raw("print [$0000] = $0a")            # MBC RAM enable
        out: list[int] = []
        o = offset
        while len(out) < n:
            self._raw(f"print [$4000] = ${(o >> 13) & 0xf:x}")   # select SRAM bank
            vals = _parse_x(self._raw(f"x ${0xA000 + (o & 0x1FFF):04x}"))
            avail = 0x2000 - (o & 0x1FFF)           # bytes left in this bank
            take = min(16, avail, n - len(out))
            out.extend(vals[:take])
            o += take
        if was:
            _emu("CONT")
        return out

    def sram_write(self, offset: int, data: list[int]) -> None:
        """Write bytes into the working song at `offset` (SRAM). NB: does not
        touch allocation tables or redraw -- exercise or re-enter the screen to
        reflect changes; for authentic edits prefer the input-driven helpers."""
        was = _emu("STATE").strip() == "running"
        if was:
            _emu("BREAK")
        self._raw("print [$0000] = $0a")
        for i, v in enumerate(data):
            o = offset + i
            self._raw(f"print [$4000] = ${(o >> 13) & 0xf:x}")
            self._raw(f"print [${0xA000 + (o & 0x1FFF):04x}] = ${v & 0xff:02x}")
        if was:
            _emu("CONT")

    def row(self, struct: str, index: int) -> list[int]:
        """Read one indexed record of a song structure, e.g. row('instruments',5)
        or row('chain_phrases', chain). Returns `stride` bytes."""
        base, stride = SONG[struct]
        return self.sram_read(base + index * stride, stride)

    def seq(self, row: int, channel: int) -> int:
        """sequence[row][channel] -- the chain number ($FF = empty)."""
        base, stride = SONG["sequence"]
        return self.sram_read(base + row * stride + channel, 1)[0]

    # ------- song value encoding + named get/set (DSL foundation) ----------
    # A phrase-note byte's *meaning/rendering depends on the row's instrument
    # type* (phrase_instruments -> instruments[n] byte0 & 3):
    #   PULSE(0)/WAVE(1): a tonal pitch, 1 = C-2, each semitone +1 (verified).
    #   KIT(2):   selects a kit sample -- the Phrase screen shows the SAMPLE NAME
    #             (e.g. BD-/SD-/CHH from the kit's $4022 table), not a pitch.
    #   NOISE(3): a noise value, rendered on its own scheme (not a chromatic pitch).
    # note_val/note_str below are the TONAL (PULSE/WAVE) codec only.
    NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

    @staticmethod
    def note_val(name: str, octave: int) -> int:
        """('C', 4) -> tonal phrase-note byte (PULSE/WAVE). 1 = C-2, semitone +1."""
        return 12 * (octave - 2) + LSDj.NOTE_NAMES.index(name.upper()) + 1

    @staticmethod
    def note_str(val: int, instr_type: int = 0) -> str:
        """Tonal decode (PULSE/WAVE). For KIT/NOISE the byte isn't a chromatic
        pitch, so return a raw form until those encodings are pinned."""
        if val in (0, 0xFF):
            return "---"
        if instr_type == 2:
            return f"kit:{val:02x}"      # sample index/pitch -- shows a name on-screen
        if instr_type == 3:
            return f"noi:{val:02x}"      # noise value
        v = val - 1
        return f"{LSDj.NOTE_NAMES[v % 12]}-{v // 12 + 2}"

    def get_phrase(self, phrase: int) -> dict[str, list[int]]:
        """All four columns of a phrase (notes/instr/fx/fx_val), 16 rows each."""
        return {k: self.row(f"phrase_{k}", phrase)
                for k in ("notes", "instruments", "fx", "fx_val")}

    def set_note(self, phrase: int, row: int, note, instr: int | None = None) -> None:
        """DSL fast path: write a note straight to SRAM. `note` = a ('C',4) tuple,
        a raw byte, or None (clear). Call redraw()/re-enter the screen to see it."""
        val = 0 if note is None else (note if isinstance(note, int)
                                      else self.note_val(*note))
        base, stride = SONG["phrase_notes"]
        self.sram_write(base + phrase * stride + row, [val])
        if instr is not None:
            b, s = SONG["phrase_instruments"]
            self.sram_write(b + phrase * s + row, [instr])

    def set_cell(self, struct: str, index: int, col: int, value: int) -> None:
        """Generic DSL setter: struct[index] byte `col` = value (SRAM write).
        e.g. set_cell('instruments', 5, 0, 3) -> instr 5 type NOISE;
             set_cell('sequence', row, ch, chain); set_cell('phrase_fx', ph, row, cmd)."""
        base, stride = SONG[struct]
        self.sram_write(base + index * stride + col, [value])

    def set_seq(self, row: int, channel: int, chain: int) -> None:
        """sequence[row][channel] = chain number (SONG screen cell)."""
        self.set_cell("sequence", row, channel, chain)

    def redraw(self) -> str:
        """Force the current screen to repaint from song data (SELECT down+up).
        (Routes through the neighbouring screen briefly; the value is reread.)"""
        self.key("select+down")
        self.key("select+up")
        return self.at()

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

    # A+direction edits the value under the cursor. The chord is generic; its
    # effect depends on the field, so these are named for the KEY, not the effect:
    #   NOTE cell : A+up/down = octave +/-1,  A+left/right = semitone -/+1
    #   hex field : A+up/down = +/-1,         A+left/right = +/-$10 (coarse)
    #   TYPE field: A+left/right cycles PULSE<WAVE<KIT<NOISE (clamped)
    def a_up(self, n: int = 1) -> None:      # A+up (V)
        for _ in range(n):
            self.key("a+up")

    def a_down(self, n: int = 1) -> None:    # A+down (V)
        for _ in range(n):
            self.key("a+down")

    def a_right(self, n: int = 1) -> None:   # A+right (V)
        for _ in range(n):
            self.key("a+right")

    def a_left(self, n: int = 1) -> None:    # A+left (V)
        for _ in range(n):
            self.key("a+left")

    def a_vert(self, n: int) -> None:        # signed A+up/down (n>0 up, n<0 down)
        self.a_up(n) if n > 0 else self.a_down(-n)

    def a_horiz(self, n: int) -> None:       # signed A+right/left (n>0 right, n<0 left)
        self.a_right(n) if n > 0 else self.a_left(-n)

    # ------- SONG (BE) ----------------------------------------------------
    def addchain(self, col: int = 0, row: int = 0) -> int:
        """SONG: create a chain under the cursor (A on empty cell). Returns the
        chain number now shown (wChain / cell)."""
        self.goto("SONG")
        self.moveto(col, row)
        self.place()
        return self.rd(VARS["chain"])

    # ------- PHRASE (V) ---------------------------------------------------
    def note(self, row: int | None = None, octaves: int = 0, semitones: int = 0,
             instr: int | None = None) -> dict[str, Any]:
        """PHRASE: place a note at `row` (or current row) on the NOTE column, then
        shift it by `octaves` (A+up/down) and `semitones` (A+right/left) to reach
        an exact pitch relative to the placed default. Optionally set the
        instrument column with `instr` A+up steps. Exercises the note-on + pitch/
        transpose engine. Returns the observed cursor cell."""
        self.goto("PHRASE")
        self.moveto(PH_NOTE, row)
        self.place()                                 # enter last-used note
        self.a_vert(octaves)                         # A+up/down = octave
        self.a_horiz(semitones)                      # A+right/left = semitone
        res: dict[str, Any] = {"phrase": self.rd(VARS["phrase"]),
                               "cursor": self.cursor()}
        if instr is not None:
            self.moveto(PH_INSTR, row)
            self.a_up(instr)                         # A+up = +1 on the hex instr field
            res["instr_cursor"] = self.cursor()
        return res

    def fx(self, row: int | None = None, steps: int = 0) -> dict[str, Any]:
        """PHRASE: place an FX command at `row` (CMD column) and step it `steps`
        times (A+up cycles through commands A,B,C...). Exercises FxCmd_* handlers."""
        self.goto("PHRASE")
        self.moveto(PH_CMD, row)
        self.place()
        self.a_up(steps)
        return {"cursor": self.cursor()}

    # ------- INSTR (V: TYPE via clamp; field nav). Fields (wInstrField $C427):
    # 0=NAME 1=TYPE 2=ENV... (layout below TYPE changes with the type). KIT type
    # shows a KIT selector (index+name), VOLUME/PITCH/FINETUNE/OFFSET/LEN/LOOP/
    # SPEED/CLIP. -------------------------------------------------------------
    ITYPE = {"PULSE": 0, "WAVE": 1, "KIT": 2, "NOISE": 3}
    INAME = {v: k for k, v in ITYPE.items()}

    def set_instr_type(self, t: int | str) -> str:
        """INSTR: set the current instrument's TYPE. It cycles PULSE<WAVE<KIT<NOISE
        and CLAMPS at both ends (verified), so a+left x4 -> PULSE, then a+right x N
        lands on type N deterministically. NB: changing type rewrites the
        instrument's params -- edit a scratch instrument. Returns the type name."""
        idx = t if isinstance(t, int) else self.ITYPE[t.upper()]
        self.goto("INSTR")
        self.moveto(0, 1)               # TYPE field (wInstrField=1)
        self.a_left(4)                  # a+left x4: clamp down to PULSE(0)
        self.a_right(idx)               # a+right x idx: step up to target
        return self.INAME.get(idx, "?")

    def instr_field(self, row: int, col: int = 0) -> tuple[int | None, int | None]:
        """INSTR: move the field cursor to (col,row) = (wInstrCursorCol,wInstrField)."""
        self.goto("INSTR")
        return self.moveto(col, row)


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
    elif a[0] in ("aup", "adown", "aleft", "aright"):   # A+dir x N
        getattr(d, "a_" + a[0][1:])(int(a[1]) if len(a) > 1 else 1)
    elif a[0] == "note":     # note [row] [octaves] [semitones] [instr]
        row = int(a[1]) if len(a) > 1 else None
        octs = int(a[2]) if len(a) > 2 else 0
        semis = int(a[3]) if len(a) > 3 else 0
        instr = int(a[4]) if len(a) > 4 else None
        print(d.note(row, octs, semis, instr))
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
