#!/usr/bin/env python3
"""Shift test: does the ROM still work when code moves?

A fork that changes code changes sizes, and everything after the change moves.
That only works if every reference to a ROM address is a label. This builds
a variant of the ROM with padding inserted in a bank -- taken back from the
bank's trailing $FF fill, so the bank keeps its size and nothing else moves --
and runs it next to the unshifted build in PyBoy, on the same SRAM and the
same input, comparing the screen and the sound registers every frame. A
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
import random
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
    and n bytes fewer of the bank's trailing $FF fill."""
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


def tour() -> list[tuple[int, list[str]]]:
    """(frames to hold, buttons) steps: play the working song, walk the screen
    map, step through fields, load a song in the file screen."""
    s: list[tuple[int, list[str]]] = [(180, [])]
    tap = lambda *b: [(4, list(b)), (12, [])]
    chord = lambda d: [(3, ["select"]), (4, ["select", d]), (12, [])]
    s += tap("start") + [(600, [])] + tap("start")                 # the song
    for d in ["right", "right", "right", "right", "down", "up",    # chain .. table
              "left", "left", "left", "down", "up", "left", "up",   # groove, project
              "down", "right", "right", "up", "right", "down"]:     # synth / wave
        s += chord(d) + [(30, [])]
        for k in ["down", "down", "right", "down", "up", "left"]:
            s += tap(k)
    s += tap("start") + [(240, [])] + tap("start")
    # project screen: the LOAD/SAVE button is the last field; load the 2nd song
    s += chord("left") * 4 + chord("up") + [(30, [])]
    s += tap("up") * 2 + tap("a") + [(40, [])] + tap("a") + [(30, [])]
    s += tap("down") + tap("a") + [(400, [])]
    s += tap("start") + [(400, [])] + tap("start")
    return s


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


def open_rom(rom: Path, sav: bytes, dmg: bool):
    from pyboy import PyBoy
    pb = PyBoy(str(rom), window="null", sound_emulated=False, cgb=not dmg,
               ram_file=io.BytesIO(sav), log_level="ERROR")
    pb.set_emulation_speed(0)
    return pb


def lockstep(rom_a: Path, rom_b: Path, sav: bytes, steps, dmg: bool = False) -> tuple[int, str] | None:
    """Run both ROMs on the same input; the first frame where the screen or
    the sound registers differ, and which, or None."""
    import numpy as np
    a, b = open_rom(rom_a, sav, dmg), open_rom(rom_b, sav, dmg)
    held: set[str] = set()
    frame = 0
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
                if a.memory[0xFF10:0xFF40] != b.memory[0xFF10:0xFF40]:
                    return frame, "sound registers"
                if not np.array_equal(a.screen.ndarray, b.screen.ndarray):
                    save_frames(a, b, frame)
                    return frame, "screen"
        return None
    finally:
        a.stop(save=False); b.stop(save=False)


def symbol_at(rom: Path, bank: int, addr: int) -> str:
    """`Name+off` for an address, from the build's rgblink .sym."""
    best = None
    for line in rom.with_suffix(".sym").read_text().splitlines():
        m = re.match(r"^([0-9a-f]{2}):([0-9a-f]{4}) (\S+)", line, re.I)
        if not m or re.search(r"\.L\d{5}$", m.group(3)):
            continue
        b, a = int(m.group(1), 16), int(m.group(2), 16)
        if (b == (0 if addr < 0x4000 else bank)) and a <= addr and (best is None or a >= best[0]):
            best = (a, m.group(3))
    return f"{best[1]}+${addr - best[0]:X}" if best else f"${addr:04x}"


def crash_site(rom: Path, sav: bytes, steps, dmg: bool, until: int) -> str | None:
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
    steps = tour() + monkey(args.seed, args.frames)
    # the reference build, assembled the same way with nothing inserted
    ref = build({}, "reference")

    def trial(bank: int, at: int) -> tuple[int, str] | None:
        return lockstep(ref, build({bank: (at, args.by)}), sav, steps, args.dmg)

    banks = PROGRAM_BANKS if args.all else [args.bank] if args.bank is not None else []
    if not banks:
        ap.error("give --bank BB or --all")
    bad = 0
    for bank in banks:
        start = args.at if args.at is not None else (0x0391 if bank == 0 else 0x4000)
        res = trial(bank, start)
        where = f"bank ${bank:02X}, {args.by} bytes at ${start:04X}"
        if res is None:
            print(f"{where}: same as the reference for {sum(c for c, _ in steps)} frames")
            continue
        bad += 1
        print(f"{where}: diverged at frame {res[0]} ({res[1]})")
        why = crash_site(BUILD / "shift.gb", sav, steps, args.dmg, res[0] + 120)
        print(f"  {why}" if why else "  no rst $38 in the shifted build: not a crash")
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
