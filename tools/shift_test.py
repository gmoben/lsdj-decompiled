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


SCREEN = {"phrase": 1, "groove": 2, "chain": 3, "song": 4, "table": 5, "instr": 6,
          "name": 7, "wave": 8, "project": 9, "synth": 10, "word": 12, "file": 13, "help": 14}
W_SCREEN = 0xC401        # wCurrentScreen (constants/screens.inc)


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

    def screen(self) -> int:
        return self.pb.memory[W_SCREEN]

    def goto(self, target: int) -> bool:
        """SELECT+direction moves to `target`: a breadth-first search over
        the screen map from save states, then the path played for real."""
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


def recorded_tour(rom: Path, sav: bytes, dmg: bool) -> list[tuple[int, list[str]]]:
    """The tour, recorded on `rom`: play the working song; every screen of
    the map with cursor moves, an edit and a play; every instrument type;
    then the file screen -- load the second song (decompression), play it,
    save it to the empty slot (the name editor, compression)."""
    d = Driver(open_rom(rom, sav, dmg))
    try:
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


def open_rom(rom: Path, sav: bytes, dmg: bool):
    from pyboy import PyBoy
    pb = PyBoy(str(rom), window="null", sound_emulated=False, cgb=not dmg,
               ram_file=io.BytesIO(sav), log_level="ERROR")
    pb.set_emulation_speed(0)
    return pb


def sram(pb) -> bytes:
    """The cartridge's 16 SRAM banks (the .sav)."""
    return b"".join(bytes(pb.memory[k, 0xA000:0xBFFF]) + bytes([pb.memory[k, 0xBFFF]])
                    for k in range(16))


def lockstep(rom_a: Path, rom_b: Path, sav: bytes, steps, dmg: bool = False,
             progress=None, grace: int = 30) -> tuple[int, str] | None:
    """Run both ROMs on the same input, comparing the screen and the sound
    registers every frame. A shifted build is not cycle-identical -- a
    pointer into moved data crosses a page at another step (`inc [hl] /
    jr nz / inc hl / inc [hl]`), a few cycles -- so a redraw can slip a frame;
    a difference counts once it lasts `grace` frames. At the end, SRAM (the
    song data the input edited) must be identical. The first frame of a
    lasting difference and what differs, or None."""
    import numpy as np
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
                what = None
                if a.memory[0xFF10:0xFF40] != b.memory[0xFF10:0xFF40]:
                    what = "sound registers"
                elif not np.array_equal(a.screen.ndarray, b.screen.ndarray):
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
