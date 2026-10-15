#!/usr/bin/env python3
"""Behaviour test: does a build still do what LSDj 9.4.0 does?

`make compare` proves a build is the original byte for byte; a build that
no longer matches -- functionally equivalent C, a moved routine, an edited
asset that should not change behaviour, a fork's refactor -- needs another
gate. This runs the build next to the original ROM in PyBoy, on the same
SRAM and the same input -- a tour of every screen, recorded on the original
(play the working song, every screen of the map, the file screen and a save
through the name editor), then seeded random input -- and compares, every
frame, the screen and the sound: the sound registers a program can read
back, the wave RAM, and whenever the frame's audio differs the periods PU1,
PU2 and WAV play (write-only on the hardware; read from PyBoy's saved APU
state). SRAM, the song data the input edited, is compared at the end. That
is the lockstep of tools/shift_test.py; a difference counts once it lasts
30 frames. A run that parts from the original reports the frame and what
differed and, if the build crashed, where (`rst $38`: LSDj's crash vector,
its asserts and DebugTrap).

It proves what the input reaches, no more: more seeds and both models reach
more.

Needs PyBoy:  python3 -m venv .venv && .venv/bin/pip install pyboy

    .venv/bin/python tools/behaviour_test.py                  # seed 1, CGB
    .venv/bin/python tools/behaviour_test.py --seeds 1-5 --both
    .venv/bin/python tools/behaviour_test.py --build other.gb  # any build
    make behaviour
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shift_test as st  # noqa: E402  (the lockstep, the tour, the watchdog)

ROOT = Path(__file__).resolve().parents[1]


def seeds(text: str) -> list[int]:
    """'3' or '1-5' or '1,4,9'."""
    out: list[int] = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b or a) + 1))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", type=Path, default=ROOT / "build" / "lsdj9_4_0.gb",
                    help="the ROM to test (default: the build)")
    ap.add_argument("--original", type=Path,
                    default=Path(os.environ.get("LSDJ_BASEROM", ROOT / "lsdj9_4_0.gb")),
                    help="LSDj 9.4.0 itself (default: $LSDJ_BASEROM or lsdj9_4_0.gb)")
    ap.add_argument("--sav", type=Path, help="the SRAM both start from (default: .emu/lsdj_song.sav "
                                             "if present, else a blank SRAM)")
    ap.add_argument("--frames", type=int, default=6000, help="random input after the tour")
    ap.add_argument("--seeds", type=seeds, default=[1], help="e.g. 1-5")
    model = ap.add_mutually_exclusive_group()
    model.add_argument("--dmg", action="store_true", help="run as a DMG instead of a CGB")
    model.add_argument("--both", action="store_true", help="run as a CGB and as a DMG")
    args = ap.parse_args()
    try:
        import pyboy  # noqa: F401
    except ImportError:
        sys.exit("needs PyBoy: python3 -m venv .venv && .venv/bin/pip install pyboy")
    for rom, what in ((args.build, "the build (make)"), (args.original, "the original ROM")):
        if not rom.is_file():
            sys.exit(f"no {what}: {rom}")
    sav_path = args.sav or (ROOT / ".emu" / "lsdj_song.sav")
    sav = sav_path.read_bytes() if sav_path.is_file() else bytes(0x20000)
    bad = 0
    for dmg in ([False, True] if args.both else [args.dmg]):
        tour = st.recorded_tour(args.original, sav, dmg)
        for seed in args.seeds:
            steps = tour + st.monkey(seed, args.frames)
            where = f"{'DMG' if dmg else 'CGB'}, seed {seed}"
            res = st.guarded(st.lockstep, args.original, args.build, sav, steps, dmg)
            if res is None:
                print(f"{where}: the same as LSDj 9.4.0 for {sum(c for c, _ in steps)} frames")
                continue
            bad += 1
            if res[0] in ("hung", "died"):
                print(f"{where}: the emulator {res[0]} after frame {res[1]}")
                continue
            print(f"{where}: parted from LSDj 9.4.0 at frame {res[0]} ({res[1]})")
            why = st.guarded(st.crash_site, args.build, sav, steps, dmg, res[0] + 120)
            if isinstance(why, tuple):
                print(f"  the build {why[0]} after frame {why[1]} while locating a crash")
            else:
                print(f"  {why}" if why else "  no rst $38 ran in the build: not a crash")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
