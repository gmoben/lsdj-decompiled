#!/usr/bin/env python3
"""Shift test: does the ROM still work when code moves?

A fork that changes code changes sizes, and everything after the change moves.
That only works if every reference to a ROM address is a label. This builds
a variant of the ROM with padding inserted in a bank -- taken back from the
bank's trailing $FF fill, so the bank keeps its size and nothing else moves --
and runs it next to the unshifted build in PyBoy, on the same SRAM and the
same input, comparing the screen and the sound every frame. A
reference the carve still holds as a number (a pointer table in data, an
address loaded a byte at a time, a table that relies on page alignment)
points at the wrong bytes in the shifted build, and the two drift apart.

The input is a scripted tour (play the working song, every screen of the
map, the file screen) followed by seeded random input.

Bisecting: a raw reference to address T breaks only when the padding goes in
at or before T. `--bisect` moves the insertion point over the bank's global
labels and reports the last one that still breaks the run: T is at or after it.

Needs PyBoy (a headless, deterministic emulator):
    python3 -m venv .venv && .venv/bin/pip install pyboy
Usage:
    .venv/bin/python tools/shift_test.py --bank 1e           # shift a whole bank
    .venv/bin/python tools/shift_test.py --bank 03 --at 5000 --by 32
    .venv/bin/python tools/shift_test.py --bank 03 --bisect
    .venv/bin/python tools/shift_test.py --all               # each program bank in turn
Options: --sav FILE (default .emu/lsdj_song.sav if present, else a blank
SRAM), --frames N (random input after the tour), --seed S, --dmg.
"""

from __future__ import annotations

import argparse
import io
import multiprocessing
import random
import time
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "shift"
REFERENCE = ROOT / "build" / "lsdj9_4_0.gb"
PROGRAM_BANKS = [0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F]
ROW = re.compile(r"^\t.*; \$([0-9a-f]{4})(?::|$)")
FILL_ROW = re.compile(r"^\tdb ((?:\$ff, )*\$ff)\s*(?:;.*)?$")


# --- the shifted build -------------------------------------------------------

def insert_padding(text: str, at: int, n: int) -> str:
    """`ds n` before the row at address `at` (and the labels in front of it),
    and n bytes fewer of the bank's trailing $FF fill (none to take when the
    bank ends in `ds $8000 - @`, which pads to its end by itself)."""
    lines = text.splitlines()
    idx = next((i for i, l in enumerate(lines)
                if (m := ROW.match(l)) and int(m.group(1), 16) >= at), None)
    if idx is None:
        raise SystemExit(f"no row at or after ${at:04x}")
    while idx > 0 and not lines[idx - 1].startswith("\t") and not lines[idx - 1].startswith("SECTION"):
        idx -= 1                           # keep the labels with their row
    # $FF is rst $38, LSDj's crash vector: a stale jump into the padding fails
    # instead of sliding through it into the code that moved
    lines.insert(idx, f"\tds {n}, $ff ; shift test")
    if any(re.match(r"^\tds \$[0-9a-f]{4} - @, \$ff", l) for l in lines[-8:]):
        return "\n".join(lines) + "\n"   # the bank's free space pads to its end: it shrinks itself
    left = n
    i = len(lines) - 1
    while left:
        m = FILL_ROW.match(lines[i])
        if not m and re.match(r"^\trst \$38\b", lines[i]):     # a lone $FF decoded as code
            lines[i] = ""
            left -= 1
            i -= 1
            continue
        if not m:
            if not lines[i].startswith("\t") or not lines[i].strip():
                i -= 1                     # a label in the fill
                continue
            raise SystemExit(f"bank has less than {n} bytes of trailing $FF fill")
        items = m.group(1).split(", ")
        take = min(left, len(items))
        rest = items[:len(items) - take]
        lines[i] = f"\tdb {', '.join(rest)}" if rest else ""
        left -= take
        i -= 1
    return "\n".join(lines) + "\n"


def build(shifts: dict[int, tuple[int, int]], name: str = "shift") -> Path:
    """The ROM with each bank's (address, count) padding; returns its path."""
    BUILD.mkdir(parents=True, exist_ok=True)
    main = (ROOT / "src" / "main.asm").read_text()
    for bank, (at, n) in sorted(shifts.items()):
        src = (ROOT / "src" / f"bank{bank:02x}.asm").read_text()
        dst = BUILD / f"bank{bank:02x}.asm"
        dst.write_text(insert_padding(src, at, n))
        old = f'INCLUDE "src/bank{bank:02x}.asm"'
        assert old in main, old
        main = main.replace(old, f'INCLUDE "{dst.relative_to(ROOT).as_posix()}"')
    asm, obj, rom = BUILD / f"{name}.asm", BUILD / f"{name}.o", BUILD / f"{name}.gb"
    asm.write_text(main)
    for cmd in (["rgbasm", "-i", "include/", "-i", "src/", "-o", str(obj), str(asm)],
                ["rgblink", "-n", str(rom.with_suffix(".sym")), "-o", str(rom), str(obj)],
                ["rgbfix", "-v", str(rom)]):
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f"{cmd[0]} failed:\n{r.stderr}")
    return rom


# --- the run ------------------------------------------------------------------

BUTTONS = ["a", "b", "start", "select", "up", "down", "left", "right"]


SCREEN = {"phrase": 1, "groove": 2, "chain": 3, "song": 4, "table": 5, "instr": 6,
          "name": 7, "wave": 8, "project": 9, "synth": 10, "word": 12, "file": 13, "help": 14}
W_SCREEN = 0xC401        # wCurrentScreen (constants/screens.inc)
OFF_MAP = {7, 11, 12, 13, 14}             # name, speech, word, file, help: B (or SELECT+LEFT) leaves


class Driver:
    """Plays input into one emulator and records it as (frames, keys) steps,
    so the same input can be replayed to other builds."""

    def __init__(self, pb):
        self.pb, self.steps, self.held = pb, [], set()

    def step(self, frames: int, keys=(), record: bool = True) -> None:
        keys = set(keys)
        for k in self.held - keys:
            self.pb.button_release(k)
        for k in keys - self.held:
            self.pb.button_press(k)
        self.held = keys
        for _ in range(frames):
            self.pb.tick()
        if record:
            self.steps.append((frames, sorted(keys)))

    def tap(self, *keys: str, n: int = 1, wait: int = 12) -> None:
        for _ in range(n):
            self.step(4, keys)
            self.step(wait)

    def chord(self, mod: str, key: str, record: bool = True) -> None:
        self.step(3, [mod], record)
        self.step(4, [mod, key], record)
        self.step(14, (), record)

    def edit(self, direction: str) -> None:
        """A + direction: change the value under the cursor."""
        self.step(3, ["a"])
        self.step(3, ["a", direction])
        self.step(3, ["a"])
        self.step(8)

    def edit_n(self, direction: str, n: int) -> None:
        """A held, `direction` tapped n times: n steps of the value."""
        self.step(4, ["a"])
        for _ in range(n):
            self.step(3, ["a", direction])
            self.step(3, ["a"])
        self.step(10)

    def held_taps(self, mod: str, *keys: str, wait: int = 20) -> None:
        """Keys tapped in turn while `mod` is held: SELECT+[B,A], B,B,B."""
        self.step(3, [mod])
        for k in keys:
            self.step(4, [mod, k])
            self.step(4, [mod])
        self.step(wait)

    def play(self, frames: int) -> None:
        """START, listen, START again."""
        self.tap("start", wait=frames)
        self.tap("start", wait=30)

    def screen(self) -> int:
        return self.pb.memory[W_SCREEN]

    def goto(self, target: int) -> bool:
        """SELECT+direction moves to `target`: a breadth-first search over
        the screen map from save states, then the path played for real."""
        if self.screen() == target:
            return True
        for _ in range(3):                  # off the map: the word, name, file, help screens
            if self.screen() not in OFF_MAP:
                break
            if self.screen() == SCREEN["word"]:
                self.chord("select", "left")
            else:
                self.tap("b", wait=30)
        if self.screen() == target:
            return True
        root = io.BytesIO()
        self.pb.save_state(root)
        frontier, seen = [(root.getvalue(), [])], {self.screen()}
        while frontier:
            nxt = []
            for state, path in frontier:
                for d in ("right", "left", "up", "down"):
                    self.pb.load_state(io.BytesIO(state))
                    self.chord("select", d, record=False)
                    sc = self.screen()
                    if sc == target:
                        self.pb.load_state(io.BytesIO(root.getvalue()))
                        for dd in path + [d]:
                            self.chord("select", dd)
                        return True
                    if sc not in seen and len(path) < 6:
                        seen.add(sc)
                        buf = io.BytesIO()
                        self.pb.save_state(buf)
                        nxt.append((buf.getvalue(), path + [d]))
            frontier = nxt
        self.pb.load_state(io.BytesIO(root.getvalue()))
        return False


def tour_project(d: Driver) -> None:
    """The project screen's fields: the tempo by A+arrows and by tapping A,
    the transpose, every sync mode and back to OFF, clone, look, the key
    delay and repeat, prelisten; the help screen's entries."""
    if not d.goto(SCREEN["project"]):
        return
    d.tap("up", n=12)                                   # TEMPO
    for k in ("up", "down", "right", "right", "left"):
        d.edit(k)
    for _ in range(5):                                  # tap tempo
        d.tap("a", wait=26)
    d.tap("down")                                       # TRANSPOSE
    d.edit("right"); d.edit("left")
    d.tap("down")                                       # SYNC: each mode, then OFF
    for mode in range(1, 6):                            # LSDJ MIDI KEYBD ANA.IN AN.OUT
        d.edit("right"); d.step(40)
        if mode >= 3:                                   # its parameter, the row below
            d.tap("down")
            d.edit("right"); d.edit("left")
            d.tap("up")
    for _ in range(5):
        d.edit("left")
    for _ in range(3):                                  # CLONE, LOOK, KEY DELAY/REPEAT
        d.tap("down")
        for k in ("right", "left"):
            d.edit(k)
        d.tap("right")
        for k in ("right", "left"):
            d.edit(k)
        d.tap("left")
    d.tap("down")                                       # PRELISTEN
    d.edit("right"); d.edit("left")
    d.tap("down")                                       # HELP: open, read each entry
    d.tap("a", wait=40)
    for _ in range(9):
        d.tap("down")
        d.tap("a", wait=30)
        d.tap("b", wait=20)
    d.tap("up", n=3)
    d.tap("b", wait=40)                                 # back to the project screen


def song_cursor(d: Driver, column: int, row: int = 0) -> bool:
    """The song screen, its cursor on (column, row)."""
    if not d.goto(SCREEN["song"]):
        return False
    d.tap("left", n=4, wait=4)
    d.step(3, ["b"]); d.step(30, ["b", "up"]); d.step(4)   # B+UP: a page up, held
    d.tap("up", n=17, wait=4)
    d.tap("right", n=column, wait=4)
    d.tap("down", n=row, wait=4)
    return True


def sweep(d: Driver, rows: int, columns: int = 2, keys=("right", "left", "up", "down")) -> None:
    """Down each row, across its columns, an edit each way on every cell."""
    for _ in range(rows):
        d.tap("down", wait=6)
        for c in range(columns):
            for k in keys:
                d.edit(k)
            if c + 1 < columns:
                d.tap("right", wait=6)
        d.tap("left", n=columns - 1, wait=6)


def tour_phrases(d: Driver) -> None:
    """A phrase on PU1, WAV and NOI: a command on each step, each a little
    further along the letters (A+RIGHT held), with a value; then it plays.
    Cut, paste, clone the instrument, select and copy a block."""
    for column in (0, 2, 3):
        if not song_cursor(d, column):
            continue
        d.chord("select", "right")                      # the chain
        d.chord("select", "right")                      # its phrase
        if d.screen() != SCREEN["phrase"]:
            continue
        d.tap("up", n=16, wait=4)
        d.tap("left", n=3, wait=4)
        d.tap("right", n=2, wait=6)                     # the command column
        d.held_taps("select", "b", "b")                 # the column selected,
        d.chord("select", "a")                          # cut: no commands
        # letters A..T, D..Z, C..Z: one T (tempo) only, PU1's, since the
        # tempo a song stops on is the last T run, and a build a tick ahead
        # would stop on another channel's
        first = {0: 0, 2: 3, 3: 2}[column]
        letter = 0
        for row in range(16):
            target = first + row + (1 if column and first + row >= COMMAND_T else 0)
            d.tap("a", wait=8)                          # A: the column's last command again
            if row == 0:
                d.edit_n("left", 20)                    # letter A,
                d.edit_n("right", target)               # then letter `first`
            else:
                d.edit_n("right", target - letter)      # then the next letter
            letter = target
            d.tap("right", wait=6)                      # its value
            d.edit_n("up", row % 4 + 1)
            d.edit_n("right", row % 3)
            d.tap("left", wait=6)
            if row < 15:                                # (down from the last step: the next phrase)
                d.tap("down", wait=6)
        d.play(500)
        d.tap("left", n=2, wait=6)                      # the note column: edits
        for k in ("right", "up", "left", "down"):
            d.edit(k)
        d.step(4, ["b"]); d.step(4, ["b", "a"]); d.step(10)     # B+A: cut
        d.chord("select", "a")                          # SELECT+A: paste
        d.tap("right", wait=6)                          # the instrument: clone it
        d.held_taps("select", "b", "a")
        d.chord("select", "b")                          # select a block, copy it
        d.tap("down", n=3, wait=6)
        d.tap("b", wait=20)
        d.tap("up", n=2, wait=6)
        d.chord("select", "a")                          # paste it
        d.play(200)


def tour_instruments(d: Driver) -> None:
    """Each instrument type, every field: an edit each way, a double A
    (a new table, a new synth, or the field's help); copy, paste, clone;
    the neighbouring instruments."""
    if not song_cursor(d, 2):                           # the wave channel's phrase
        return
    d.chord("select", "right"); d.chord("select", "right")
    d.tap("up", n=16, wait=4)
    d.tap("left", n=3, wait=4)
    d.tap("right", wait=6)
    d.chord("select", "right")                          # its first step's instrument
    if d.screen() != SCREEN["instr"]:
        return
    for kind in range(4):                               # pulse, wave, kit, noise
        d.tap("up", n=12, wait=4)
        d.tap("down", wait=6)                           # TYPE
        if kind:
            d.edit("right")
        else:
            d.edit_n("left", 3)
        for _ in range(11):
            d.tap("down", wait=6)
            for side in ("left", "right"):
                d.tap(side, wait=6)
                d.edit("right"); d.edit("right"); d.edit("left")
            d.edit("up"); d.edit("down")
            d.tap("a", n=2, wait=6)                     # a new table or synth; help
            d.tap("b", wait=12)
        d.play(200)
    d.chord("select", "b")                              # copy the instrument
    d.step(3, ["b"]); d.step(4, ["b", "right"]); d.step(20)    # the next one
    d.chord("select", "a")                              # paste
    d.held_taps("select", "b", "a")                     # clone its synth or table
    d.step(3, ["b"]); d.step(4, ["b", "left"]); d.step(20)


def tour_song(d: Driver) -> None:
    """The song screen: a new chain, cut, paste, clone; marks and paging;
    scrolling; mute, solo, pan; a block copied and pasted; live mode."""
    if not song_cursor(d, 1, 8):                        # below the song: rows of no chains
        return
    d.tap("a", n=2, wait=8)                             # a new chain
    d.play(480)                                         # to its end: the next row, the loop back
    d.held_taps("select", "b", "a")                     # clone it
    d.step(4, ["b"]); d.step(4, ["b", "a"]); d.step(10)        # cut
    d.chord("select", "a")                              # paste
    d.tap("b", n=3, wait=4)                             # mark the row
    d.tap("down", n=20, wait=4)                         # scroll down
    d.step(4, ["b"]); d.step(4, ["b", "up"]); d.step(20)       # page up, page down
    d.step(4, ["b"]); d.step(4, ["b", "down"]); d.step(20)
    d.tap("up", n=24, wait=4)                           # scroll up
    d.tap("b", n=3, wait=4)                             # unmark
    d.chord("select", "b")                              # a block: copy, paste
    d.tap("down", wait=6); d.tap("right", wait=6)
    d.tap("b", wait=20)
    d.tap("down", n=3, wait=6)
    d.chord("select", "a")
    d.held_taps("select", "b", "b")                     # a column: copy, paste
    d.tap("b", wait=20)
    d.tap("right", wait=6)
    d.chord("select", "a")
    d.tap("start", wait=60)                             # mute, solo, pan
    d.step(3, ["b"]); d.step(4, ["b", "select"]); d.step(40, ["b"]); d.step(30)
    d.tap("b", wait=30)
    d.step(3, ["b"]); d.step(4, ["b", "start"]); d.step(40, ["b"]); d.step(30)
    d.step(3, ["b"]); d.step(4, ["b", "left"]); d.step(10, ["b"]); d.step(4, ["b", "right"]); d.step(20)
    d.tap("b", wait=30)
    d.tap("start", wait=30)
    d.chord("select", "left")                           # live mode
    d.tap("start", wait=200)                            # queue the chain
    d.tap("right", wait=6)
    d.tap("start", wait=4); d.tap("start", wait=200)    # quickstart
    d.chord("select", "start")                          # stop a chain
    d.step(3, ["left"]); d.step(4, ["left", "start"]); d.step(200)  # queue the row
    d.held_taps("select", "start", "start", wait=100)  # quickstop
    d.chord("select", "left")                           # live mode off


def tour_screens(d: Driver) -> None:
    """The chain, table, groove, synth and wave screens: edits on every
    column, cut, paste, clone, a block copied and pasted, a play; a synth
    whose wave was drawn by hand, and its overwrite prompt."""
    if d.goto(SCREEN["chain"]):
        d.tap("up", n=16, wait=4)
        d.tap("a", n=2, wait=8)                         # a new phrase
        sweep(d, 4)
        d.step(4, ["b"]); d.step(4, ["b", "a"]); d.step(10)
        d.chord("select", "a")
        d.held_taps("select", "b", "a")                 # clone the phrase
        d.chord("select", "b"); d.tap("down", n=2, wait=6); d.tap("b", wait=20)
        d.tap("down", n=2, wait=6); d.chord("select", "a")
        d.step(3, ["b"]); d.step(4, ["b", "right"]); d.step(30)    # the next chain
        d.play(200)
        d.chord("select", "start"); d.step(200)         # the song, from here
        d.chord("select", "start"); d.step(30)
    if d.goto(SCREEN["table"]):
        d.tap("up", n=16, wait=4)
        d.tap("left", n=6, wait=4)
        sweep(d, 6, columns=6, keys=("right", "up"))
        d.chord("select", "b"); d.tap("down", n=2, wait=6); d.tap("b", wait=20)
        d.tap("down", n=3, wait=6); d.chord("select", "a")
        d.held_taps("select", "b", "b")                 # select a column, cut it
        d.chord("select", "a")
        d.play(200)
    if d.goto(SCREEN["groove"]):
        sweep(d, 4, columns=1)
        d.step(4, ["b"]); d.step(4, ["b", "a"]); d.step(10)
        d.chord("select", "a")
        d.chord("select", "b"); d.tap("down", n=2, wait=6); d.tap("b", wait=20)
        d.chord("select", "a")
        d.held_taps("select", "b", "b")
        d.chord("select", "a")
    if d.goto(SCREEN["synth"]):
        d.chord("select", "right")                      # its waves: one drawn by hand
        for k in ("up", "down", "left", "right"):
            d.edit(k)
        if d.goto(SCREEN["synth"]):
            d.tap("a", wait=30)                         # "overwrite hand-drawn waves?"
            d.tap("left", wait=10)
            d.tap("a", wait=60)                         # YES
            d.tap("up", n=10, wait=4)
            sweep(d, 10, columns=2)
            d.play(150)
    if d.goto(SCREEN["wave"]):
        for k in ("up", "down", "up"):
            d.tap(k, wait=6)
        d.edit("up")
        d.chord("select", "b"); d.tap("right", n=4, wait=6); d.tap("b", wait=20)
        d.tap("left", n=6, wait=6); d.chord("select", "a")
        d.step(3, ["b"]); d.step(4, ["b", "right"]); d.step(30)
        d.step(3, ["b"]); d.step(4, ["b", "up"]); d.step(30)


def tour_speech(d: Driver) -> None:
    """Speech: instrument $40 on the wave channel's phrase, a word played;
    the speech instrument screen, and a word edited on the word screen."""
    if not song_cursor(d, 2):
        return
    d.chord("select", "right")
    d.chord("select", "right")
    if d.screen() != SCREEN["phrase"]:
        return
    d.tap("up", n=16, wait=4)
    d.tap("left", n=3, wait=4)
    d.tap("right", wait=6)                              # the instrument column
    d.tap("a", wait=8)
    d.edit_n("up", 4); d.edit_n("right", 16)            # up to $40, the speech instrument
    d.tap("left", wait=6)
    d.tap("a", wait=8)
    d.edit_n("right", 2)
    d.play(300)
    d.tap("right", wait=6)
    d.chord("select", "right")                          # the speech instrument: its words
    d.tap("down", n=2, wait=6)
    d.chord("select", "right")                          # the word screen
    for k in ("right", "up", "left"):
        d.edit(k)
    d.tap("down", wait=6); d.tap("right", wait=6); d.edit("up")
    d.step(4, ["b"]); d.step(4, ["b", "a"]); d.step(10)
    d.chord("select", "a")
    d.chord("select", "left")
    d.chord("select", "left")


def tour_name(d: Driver) -> None:
    """The name editor, from the instrument screen's NAME: the keyboard
    cursor moved, letters typed, one erased, done."""
    if not d.goto(SCREEN["instr"]):
        return
    d.tap("up", n=12, wait=4)
    d.tap("a", wait=30)                                 # NAME: the name editor
    if d.screen() != SCREEN["name"]:
        return
    for k in ("right", "right", "down", "a", "left", "down", "a", "up", "a"):
        d.tap(k, wait=8)
    d.tap("select", wait=12)                            # backspace
    d.tap("start", wait=40)                             # done


def tour_sync(d: Driver) -> None:
    """The song played under the sync modes that run without a partner:
    LSDJ and MIDI as the leader, AN.OUT."""
    for steps in (1, 2, 5):
        if not d.goto(SCREEN["project"]):
            return
        d.tap("up", n=12, wait=4)
        d.tap("down", n=2, wait=6)                      # SYNC
        d.edit_n("right", steps)
        if not d.goto(SCREEN["song"]):
            return
        d.play(300)
        if not d.goto(SCREEN["project"]):
            return
        d.tap("up", n=12, wait=4)
        d.tap("down", n=2, wait=6)
        d.edit_n("left", 6)                             # OFF again


def tour_file_erase(d: Driver) -> None:
    """The file screen's ERASE, on the song the tour saved."""
    if not d.goto(SCREEN["project"]):
        return
    d.tap("up", n=12, wait=4)
    d.tap("down", n=10, wait=6)
    d.tap("a", wait=40)                                 # the file screen
    d.tap("right", n=2, wait=8)                         # ERASE
    d.tap("a", wait=30)                                 # the list, on the current file
    d.tap("a", wait=30)                                 # "erase?"
    d.tap("left", wait=8)
    d.tap("a", wait=120)                                # YES
    d.tap("b", wait=30)


def tour_clean(d: Driver) -> None:
    """CLEAN SONG DATA and CLEAN INSTR DATA."""
    if not d.goto(SCREEN["project"]):
        return
    d.tap("up", n=12)
    d.tap("down", n=8)                                  # (no sync parameter row: OFF)
    d.tap("a", wait=300)                                # CLEAN SONG DATA
    d.tap("down")
    d.tap("a", wait=300)                                # CLEAN INSTR DATA


def tour_base(d: Driver) -> None:
    """Play the working song; every screen of the map with cursor moves, an
    edit and a play; every instrument type; then the file screen -- load the
    second song (decompression), play it, save it to the empty slot (the
    name editor, compression)."""
    d.step(180)
    d.tap("start", wait=600)
    d.tap("start", wait=30)
    for name in ("chain", "phrase", "groove", "instr", "table", "synth", "wave", "project", "song"):
        if not d.goto(SCREEN[name]):
            continue
        d.step(30)
        for k in ("down", "down", "right", "down", "up", "left"):
            d.tap(k)
        d.step(4, ["a"]); d.step(4, ["a", "right"]); d.step(4, ["a"]); d.step(16)
        d.tap("start", wait=150)
        d.tap("start", wait=30)
    if d.goto(SCREEN["instr"]):                 # TYPE: pulse, wave, kit, noise
        d.tap("up", n=8)
        d.tap("down")
        for _ in range(4):
            d.step(4, ["a"]); d.step(4, ["a", "right"]); d.step(4, ["a"]); d.step(40)
    if d.goto(SCREEN["project"]):                # file screen: LOAD/SAVE SONG is the last row
        d.tap("down", n=11)
        d.tap("a", wait=40)
        d.tap("a", wait=20)                      # LOAD: the list
        d.tap("up", n=6)                         # the second song
        d.tap("a", wait=20)                      # "lose unsaved work?"
        d.tap("left")
        d.tap("a", wait=400)                     # YES
        d.tap("start", wait=300)
        d.tap("start", wait=30)
    if d.goto(SCREEN["project"]):
        d.tap("down", n=11)
        d.tap("a", wait=40)
        d.tap("right")                           # SAVE
        d.tap("a", wait=30)
        d.tap("down", n=8)                       # the empty slot
        d.tap("a", wait=60)                      # the name editor
        d.tap("a", wait=400)                     # save
        d.tap("b", wait=30)


# the tour, in order: what tools/tour_coverage.py measures
TOUR = [tour_base, tour_project, tour_phrases, tour_instruments, tour_name, tour_song,
        tour_screens, tour_speech, tour_sync, tour_file_erase, tour_clean]


def recorded_tour(rom: Path, sav: bytes, dmg: bool) -> list[tuple[int, list[str]]]:
    """The tour (TOUR), recorded on `rom`, as (frames, keys) steps to replay
    on any build."""
    d = Driver(open_rom(rom, sav, dmg))
    try:
        for section in TOUR:
            section(d)
        return d.steps
    finally:
        d.pb.stop(save=False)


def monkey(seed: int, frames: int) -> list[tuple[int, list[str]]]:
    rng = random.Random(seed)
    s: list[tuple[int, list[str]]] = []
    total = 0
    while total < frames:
        r = rng.random()
        if r < 0.55:
            keys = [rng.choice(["up", "down", "left", "right", "a", "a", "b"])]
        elif r < 0.75:
            keys = [rng.choice(["a", "b", "select"]), rng.choice(["up", "down", "left", "right"])]
        elif r < 0.85:
            keys = ["select", rng.choice(["up", "down", "left", "right"])]
        elif r < 0.92:
            keys = [rng.choice(["start", "b", "select"])]
        else:
            keys = []
        hold, wait = rng.randint(2, 10), rng.randint(1, 30)
        s += [(hold, keys), (wait, [])]
        total += hold + wait
    return s


SEED = 0x1234                       # the random numbers' seed every run gets
COMMAND_T = 15                      # the tempo command's letter, counting A as 0


def routine_address(rom: Path, name: str) -> tuple[int, int]:
    """(bank, address) of a routine, from the rgblink .sym beside the ROM
    (`make` writes one, and so does a shifted build); for LSDj 9.4.0 itself,
    from src/bankNN.sym."""
    sym = rom.with_suffix(".sym")
    if sym.is_file():
        for line in sym.read_text().splitlines():
            m = re.match(r"([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4}) (\S+)", line)
            if m and m.group(3) == name:
                return int(m.group(1), 16), int(m.group(2), 16)
    import hashlib
    if hashlib.sha1(rom.read_bytes()).hexdigest() == (ROOT / "lsdj9_4_0.sha1").read_text().split()[0]:
        for path in sorted((ROOT / "src").glob("bank*.sym")):
            for line in path.read_text().splitlines():
                m = re.match(r"([0-9A-F]{4}) (\S+)", line)
                if m and m.group(2) == name:
                    return int(path.stem[4:], 16) if int(m.group(1), 16) >= 0x4000 else 0, int(m.group(1), 16)
    raise SystemExit(f"{rom}: no {name} in {sym.name}: build it with make")


def open_rom(rom: Path, sav: bytes, dmg: bool, pin_seed: bool = True):
    """PyBoy on `rom` with `sav` as its SRAM. LSDj seeds its random numbers
    (the B and Z commands) from the DIV register as a song starts
    (SoundTick: initrand(DIV_REG)), and DIV counts cycles, so a build whose
    code runs a few cycles longer draws other numbers: with `pin_seed`
    every run gets SEED, so two builds play the same song the same way."""
    from pyboy import PyBoy
    # the APU emulated: without it PyBoy reads every sound register as 0
    pb = PyBoy(str(rom), window="null", sound_emulated=True, cgb=not dmg,
               ram_file=io.BytesIO(sav), log_level="ERROR")
    pb.set_emulation_speed(0)
    if pin_seed:
        bank, addr = routine_address(rom, "initrand")

        def seed(_):                    # initrand(UWORD seed): the seed on the stack
            sp = pb.register_file.SP
            pb.memory[sp + 2] = SEED & 0xFF
            pb.memory[sp + 3] = SEED >> 8
        pb.hook_register(bank, addr, seed, None)
    return pb


# --- what the sound channels play ---------------------------------------------

class _Enough(Exception):
    pass


class _Head:
    """A file for PyBoy.save_state that keeps the first `n` bytes and then
    stops the save. The APU's state comes early in a save state (after the
    CPU and the LCD, before RAM), and saving only reads the emulator, so
    stopping part-way costs nothing but the bytes not written."""

    def __init__(self, n: int):
        self.n, self.data = n, bytearray()

    def write(self, b: bytes) -> int:
        self.data += b
        if len(self.data) >= self.n:
            raise _Enough
        return len(b)

    def flush(self) -> None:
        pass


def _state_head(pb, n: int) -> bytes:
    head = _Head(n)
    try:
        pb.save_state(head)
    except _Enough:
        pass
    return bytes(head.data)


CHANNEL_PERIODS = (("PU1", 0xFF13, 0xFF14), ("PU2", 0xFF18, 0xFF19), ("WAV", 0xFF1D, 0xFF1E))


class Periods:
    """The period each of PU1, PU2 and WAV plays: the 11 bits a program
    writes to NRx3/NRx4, which are write-only (they read back $FF/$BF, so
    comparing the sound registers can't see a pitch). PyBoy's save state
    holds them; where, is found by writing two sentinels to each register
    with nothing run in between and seeing which byte takes both values --
    a PyBoy whose state moved on fails here instead of passing blind.
    The same for the wave RAM, which reads back the byte the wave channel
    is playing while it plays (a position a few cycles' timing moves)."""

    def __init__(self, rom: Path, dmg: bool):
        pb = open_rom(rom, bytes(0x20000), dmg)
        try:
            for _ in range(2):
                pb.tick()
            pb.memory[0xFF26] = 0x80               # APU on: writes count only then
            whole = lambda: _state_head(pb, 1 << 30)
            self.offsets: list[int] = []
            for name, low, high in CHANNEL_PERIODS:
                for reg, first, second in ((low, 0x5A, 0xA5), (high, 0x05, 0x02)):
                    s0 = whole(); pb.memory[reg] = first
                    s1 = whole(); pb.memory[reg] = second
                    s2 = whole()
                    hits = [i for i in range(min(len(s0), len(s1), len(s2)))
                            if s1[i] == first and s2[i] == second and s0[i] != first]
                    if len(hits) != 1:
                        raise RuntimeError(f"can't find {name}'s period in PyBoy's save state "
                                           f"(${reg:04X}: {len(hits)} candidates)")
                    self.offsets += hits
            pb.memory[0xFF1A] = 0x00               # the wave channel off: its RAM takes writes
            tables = []
            for first in (0x10, 0x80):
                pb.memory[0xFF30:0xFF40] = bytes(first + i for i in range(16))
                tables.append(whole())
            hits = [i for i in range(min(map(len, tables)) - 15)
                    if tables[0][i:i + 16] == bytes(0x10 + k for k in range(16))
                    and tables[1][i:i + 16] == bytes(0x80 + k for k in range(16))]
            if len(hits) != 1:
                raise RuntimeError(f"can't find the wave RAM in PyBoy's save state ({len(hits)} candidates)")
            self.wave = hits[0]
        finally:
            pb.stop(save=False)
        self.n = max(self.offsets + [self.wave + 15]) + 1

    def read(self, pb) -> tuple[list[int], bytes]:
        """The periods PU1, PU2 and WAV play, and the wave RAM."""
        head = _state_head(pb, self.n)
        o = self.offsets
        return ([head[o[k]] | (head[o[k + 1]] & 7) << 8 for k in range(0, len(o), 2)],
                head[self.wave:self.wave + 16])

    def __call__(self, pb) -> list[int]:
        return self.read(pb)[0]


W_WAV_PITCH_MODULATING = 0xC3FB   # wChannelPitchModulationActiveWav
# What the wave channel's uploads toggle (SoundLoadWaveRam): NR30 off, NR51's
# CH3 bits cleared, NR52's CH3 bit; register: the bits, offset from $FF10
WAV_UPLOAD_BITS = {0x0A: 0xFF, 0x15: 0x44, 0x16: 0x04}


def sound_difference(a, b, periods: Periods) -> str | None:
    """What differs in what the two play, or None: the sound registers a
    program can read back (volumes, envelopes, duty, sweep, the noise
    channel, panning, the on/off bits) every frame, and the wave RAM while
    the wave channel is off; the periods, and the wave RAM it plays, only
    when the frame's audio differs, since the same values written a few
    cycles apart sound the same but don't sample the same (and the wave
    RAM reads back the byte playing, whose position is that timing).

    While the wave channel's pitch modulates (wChannelPitchModulationActiveWav:
    a vibrato, a slide), LSDj times it by DIV, the CPU's cycles: each step
    SoundModulateWavFrequency adds the time since the last to the vibrato's
    phase, waits on DIV for the moment to restart the wave, and uploads it
    (SoundLoadWaveRam turns the channel off and on again). Code that runs a
    few cycles longer anywhere moves those moments, and the pitch with
    them. Then the wave channel's period, its wave RAM and those bits are
    not compared; the other channels, the screen and the song data are."""
    ra, rb = bytearray(a.memory[0xFF10:0xFF30]), bytearray(b.memory[0xFF10:0xFF30])
    wav_timed = bool(a.memory[W_WAV_PITCH_MODULATING] or b.memory[W_WAV_PITCH_MODULATING])
    if wav_timed:
        for reg, bits in WAV_UPLOAD_BITS.items():
            ra[reg] &= ~bits
            rb[reg] &= ~bits
    if ra != rb:
        return "sound registers"
    wave_off = not a.memory[0xFF26] & 0x04
    if wave_off and not wav_timed and a.memory[0xFF30:0xFF40] != b.memory[0xFF30:0xFF40]:
        return "the wave RAM"
    import numpy as np
    if np.array_equal(a.sound.ndarray, b.sound.ndarray):
        return None
    (pa, wa), (pb_, wb) = periods.read(a), periods.read(b)
    off = [name for (name, _, _), x, y in zip(CHANNEL_PERIODS, pa, pb_)
           if x != y and not (wav_timed and name == "WAV")]
    if off:
        return f"the period {'/'.join(off)} plays"
    return "the wave RAM the wave channel plays" if wa != wb and not wav_timed else None


def sram(pb) -> bytes:
    """The cartridge's 16 SRAM banks (the .sav)."""
    return b"".join(bytes(pb.memory[k, 0xA000:0xBFFF]) + bytes([pb.memory[k, 0xBFFF]])
                    for k in range(16))


def lockstep(rom_a: Path, rom_b: Path, sav: bytes, steps, dmg: bool = False,
             progress=None, grace: int = 30) -> tuple[int, str] | None:
    """Run both ROMs on the same input, comparing the screen and the sound
    (sound_difference) every frame. A shifted build is not cycle-identical -- a
    pointer into moved data crosses a page at another step (`inc [hl] /
    jr nz / inc hl / inc [hl]`), a few cycles -- so a redraw can slip a frame;
    a difference counts once it lasts `grace` frames. At the end, SRAM (the
    song data the input edited) must be identical. The first frame of a
    lasting difference and what differs, or None."""
    import numpy as np
    periods = Periods(rom_a, dmg)
    a, b = open_rom(rom_a, sav, dmg), open_rom(rom_b, sav, dmg)
    held: set[str] = set()
    frame = 0
    since: tuple[int, str] | None = None
    try:
        for count, keys in steps:
            for k in held - set(keys):
                a.button_release(k); b.button_release(k)
            for k in set(keys) - held:
                a.button_press(k); b.button_press(k)
            held = set(keys)
            for _ in range(count):
                a.tick(); b.tick()
                frame += 1
                if progress is not None:
                    progress.value = frame
                what = sound_difference(a, b, periods)
                if what is None and not np.array_equal(a.screen.ndarray, b.screen.ndarray):
                    what = "screen"
                if what is None:
                    since = None
                elif since is None:
                    since = (frame, what)
                    save_frames(a, b, frame)
                elif frame - since[0] >= grace:
                    return since
        for _ in range(grace + 60):              # settle, then the song data
            a.tick(); b.tick()
            frame += 1
        if since is not None:
            return since
        sa, sb = sram(a), sram(b)
        if sa != sb:
            k = next(i for i in range(len(sa)) if sa[i] != sb[i])
            return frame, f"SRAM (bank {k // 0x2000}, ${0xA000 + k % 0x2000:04X})"
        return None
    finally:
        a.stop(save=False); b.stop(save=False)


def symbol_at(rom: Path, bank: int, addr: int) -> str:
    """`Name+off` for an address, from the rgblink .sym beside the ROM, or
    the bare address when it has none."""
    best = None
    sym = rom.with_suffix(".sym")
    lines = sym.read_text().splitlines() if sym.is_file() else []
    for line in lines:
        m = re.match(r"^([0-9a-f]{2}):([0-9a-f]{4}) (\S+)", line, re.I)
        if not m or re.search(r"\.L\d{5}$", m.group(3)):
            continue
        b, a = int(m.group(1), 16), int(m.group(2), 16)
        if (b == (0 if addr < 0x4000 else bank)) and a <= addr and (best is None or a >= best[0]):
            best = (a, m.group(3))
    where = f"${0 if addr < 0x4000 else bank:02X}:${addr:04X}"
    return f"{best[1]}+${addr - best[0]:X}" if best else where


def crash_site(rom: Path, sav: bytes, steps, dmg: bool, until: int, progress=None) -> str | None:
    """Where the build executed an $FF (rst $38: LSDj's crash vector, also
    its asserts' `jr cc, @ + 1` and DebugTrap), with the return addresses on
    the stack -- usually the stale reference itself is one call up."""
    pb = open_rom(rom, sav, dmg)
    hit: list[list[int]] = []

    def on_rst(_ctx) -> None:
        if not hit:
            sp = pb.register_file.SP
            hit.append([pb.memory[0xFF8E]] +
                       [pb.memory[sp + i] | pb.memory[sp + i + 1] << 8 for i in range(0, 20, 2)])
    pb.hook_register(0, 0x0038, on_rst, None)
    held: set[str] = set()
    frame = 0
    try:
        for count, keys in steps:
            for k in held - set(keys):
                pb.button_release(k)
            for k in set(keys) - held:
                pb.button_press(k)
            held = set(keys)
            for _ in range(count):
                pb.tick()
                frame += 1
                if progress is not None:
                    progress.value = frame
                if hit or frame > until:
                    break
            if hit or frame > until:
                break
    finally:
        pb.stop(save=False)
    if not hit:
        return None
    bank, *stack = hit[0]
    where = [symbol_at(rom, bank, w) for w in stack]
    return (f"frame {frame}: $FF executed at {where[0]}-1 (bank ${bank:02X} mapped); "
            f"stack words: {' '.join(f'${w:04x}' for w in stack[1:])} "
            f"({' < '.join(where[1:])})")


def guarded(fn, *args, stall: float = 10.0):
    """Run fn(*args, progress=...) in a child process: PyBoy's tick() is
    native code that never returns if the emulated CPU stops for good (a
    stale jump into `stop` or `halt` with nothing to wake it), which no
    Python-level timer can interrupt. Returns fn's result, or ("hung",
    frame) when the frame count stops advancing for `stall` seconds."""
    ctx = multiprocessing.get_context("fork")
    progress = ctx.Value("i", 0)
    out = ctx.Queue()
    proc = ctx.Process(target=lambda: out.put(fn(*args, progress=progress)), daemon=True)
    proc.start()
    last, since = -1, time.monotonic()
    while True:
        proc.join(0.5)
        if not out.empty() or not proc.is_alive():
            break
        if progress.value != last:
            last, since = progress.value, time.monotonic()
        elif time.monotonic() - since > stall:
            proc.kill()
            proc.join()
            return ("hung", last)
    result = out.get(timeout=5) if not out.empty() else ("died", progress.value)
    proc.join()
    return result


def save_frames(a, b, frame: int) -> None:
    for pb, tag in ((a, "ref"), (b, "shift")):
        img = pb.screen.ndarray[:, :, :3]
        h, w = img.shape[:2]
        path = BUILD / f"diverged_{frame}_{tag}.ppm"
        path.write_bytes(f"P6 {w} {h} 255\n".encode() + img.tobytes())


def labels_of(bank: int) -> list[int]:
    """Addresses of the bank's global labels, in order (bisect points)."""
    out = []
    lines = (ROOT / "src" / f"bank{bank:02x}.asm").read_text().splitlines()
    for i, l in enumerate(lines):
        if re.match(r"^[A-Za-z_]\w*:", l):
            for nxt in lines[i + 1:i + 6]:
                m = ROW.match(nxt)
                if m:
                    out.append(int(m.group(1), 16))
                    break
    return sorted(set(out))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bank", type=lambda s: int(s, 16))
    ap.add_argument("--all", action="store_true", help="each program bank in turn")
    ap.add_argument("--together", action="store_true", help="every program bank shifted at once")
    ap.add_argument("--at", type=lambda s: int(s, 16),
                    help="insert at this address (default: the bank's start; in bank 0 $0391, past "
                         "the page-aligned tables at $0200-$0390 that code reaches with `ld d, $02` "
                         "/ `ld h, $03`)")
    ap.add_argument("--by", type=int, default=16)
    ap.add_argument("--bisect", action="store_true")
    ap.add_argument("--sav", type=Path)
    ap.add_argument("--frames", type=int, default=6000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--dmg", action="store_true", help="run as a DMG instead of a CGB")
    args = ap.parse_args()
    try:
        import pyboy  # noqa: F401
    except ImportError:
        sys.exit("needs PyBoy: python3 -m venv .venv && .venv/bin/pip install pyboy")
    if not REFERENCE.is_file():
        sys.exit("build the ROM first (make)")
    sav_path = args.sav or (ROOT / ".emu" / "lsdj_song.sav")
    sav = sav_path.read_bytes() if sav_path.is_file() else bytes(0x20000)
    # the reference build, assembled the same way with nothing inserted
    ref = build({}, "reference")
    steps = recorded_tour(ref, sav, args.dmg) + monkey(args.seed, args.frames)

    def trial(bank: int, at: int) -> tuple[int, str] | None:
        return guarded(lockstep, ref, build({bank: (at, args.by)}), sav, steps, args.dmg)

    def report(res, where: str) -> None:
        if res[0] in ("hung", "died"):
            print(f"{where}: the emulator {res[0]} after frame {res[1]} (CPU stopped for good?)")
            return
        print(f"{where}: diverged at frame {res[0]} ({res[1]})")
        why = guarded(crash_site, BUILD / "shift.gb", sav, steps, args.dmg, res[0] + 120)
        if isinstance(why, tuple):
            print(f"  the shifted build {why[0]} after frame {why[1]} while locating the crash")
        else:
            print(f"  {why}" if why else "  no rst $38 in the shifted build: not a crash")

    def default_at(bank: int) -> int:
        return args.at if args.at is not None else (0x0391 if bank == 0 else 0x4000)

    if args.together:
        shifts = {b: (default_at(b), args.by) for b in PROGRAM_BANKS}
        res = guarded(lockstep, ref, build(shifts), sav, steps, args.dmg)
        where = f"every bank, {args.by} bytes"
        if res is None:
            print(f"{where}: same as the reference for {sum(c for c, _ in steps)} frames")
            return 0
        report(res, where)
        return 1
    banks = PROGRAM_BANKS if args.all else [args.bank] if args.bank is not None else []
    if not banks:
        ap.error("give --bank BB, --all or --together")
    bad = 0
    for bank in banks:
        start = default_at(bank)
        res = trial(bank, start)
        where = f"bank ${bank:02X}, {args.by} bytes at ${start:04X}"
        if res is None:
            print(f"{where}: same as the reference for {sum(c for c, _ in steps)} frames")
            continue
        bad += 1
        report(res, where)
        if args.bisect:
            points = [a for a in labels_of(bank) if a >= start]
            lo, hi = 0, len(points) - 1        # points[lo] breaks; find the last that does
            if trial(bank, points[hi]) is not None:
                lo = hi
            while hi - lo > 1:
                mid = (lo + hi) // 2
                if trial(bank, points[mid]) is not None:
                    lo = mid
                else:
                    hi = mid
            print(f"  still breaks with the padding at ${points[lo]:04X}, not at "
                  f"${points[min(hi, len(points) - 1)]:04X}: a raw reference points into that range")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
