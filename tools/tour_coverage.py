#!/usr/bin/env python3
"""Which routines the behaviour test's input reaches.

`make behaviour` and the shift test prove what their input reaches, no
more. This plays that input -- the tour of tools/shift_test.py and seeded
random input -- on the original ROM with a PyBoy hook at every routine's
entry (matched C, hand-written asm, the GBDK library), and reports the
routines it ran, per bank, and the ones it never did, so the tour can be
extended where it matters.

A hook replaces the byte at its address with $DB, so the routines whose
bytes are copied rather than run (OamDmaRoutine, MixerKernel) get none:
their copies in RAM run instead. A routine that opens with `halt`
(WaitInterrupt) is hooked after it: PyBoy, stepping a halt off a hook,
never finishes the frame. The tests' hook at initrand, which gives every
run one random seed, is left out: one address takes one hook.

    .venv/bin/python tools/tour_coverage.py                # CGB, seed 1
    .venv/bin/python tools/tour_coverage.py --dmg --seeds 1-3 --missed
"""
from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "match"))
import shift_test as st  # noqa: E402
import code_kind  # noqa: E402
import behaviour_test  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
COPIED = {"OamDmaRoutine", "MixerKernel"}          # run from their copies in RAM


def routines() -> dict[tuple[int, int], tuple[str, str]]:
    """{(bank, address): (name, kind)} for every routine of the carved banks."""
    out = {}
    syms = {}
    for sym in sorted((ROOT / "src").glob("bank*.sym")):
        bank = int(sym.name[4:6], 16)
        for addr, name, _c in code_kind.coverage.read_sym(sym):
            syms[(bank, name)] = addr
    for asm in sorted((ROOT / "src").glob("bank*.asm")):
        bank = int(asm.stem[4:], 16)
        for name, (kind, _n, _b) in code_kind.classify_bank_full(asm).items():
            addr = syms.get((bank, name))
            if addr is not None and name not in COPIED:
                out[(bank if addr >= 0x4000 else 0, addr)] = (name, kind)
    return out


def hook_address(rom: bytes, key: tuple[int, int]) -> tuple[int, int]:
    """Where a routine's hook goes: its entry, or past an opening `halt`."""
    bank, addr = key
    offset = addr if addr < 0x4000 else bank * 0x4000 + addr - 0x4000
    return (bank, addr + 1) if rom[offset] == 0x76 else key


def run(rom: Path, sav: bytes, steps, dmg: bool, targets) -> set[tuple[int, int]]:
    image = rom.read_bytes()
    pb = st.open_rom(rom, sav, dmg, pin_seed=False)    # a hook of its own at initrand
    hit: set[tuple[int, int]] = set()
    pending: list[tuple[int, int]] = []

    def reached(key):                  # PyBoy has lifted the hook while it runs:
        pending.append(key)            # take it away after the frame

    try:
        for key in targets:
            at = hook_address(image, key)
            pb.hook_register(at[0], at[1], reached, key)
        held: set[str] = set()
        for count, keys in steps:
            for k in held - set(keys):
                pb.button_release(k)
            for k in set(keys) - held:
                pb.button_press(k)
            held = set(keys)
            for _ in range(count):
                pb.tick(1, False)
                while pending:
                    key = pending.pop()
                    if key not in hit:
                        hit.add(key)
                        pb.hook_deregister(*hook_address(image, key))
    finally:
        pb.stop(save=False)
    return hit


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, default=ROOT / "lsdj9_4_0.gb")
    ap.add_argument("--sav", type=Path)
    ap.add_argument("--frames", type=int, default=6000, help="random input after the tour")
    ap.add_argument("--seeds", type=behaviour_test.seeds, default=[1])
    ap.add_argument("--dmg", action="store_true")
    ap.add_argument("--missed", action="store_true", help="list the routines never reached")
    ap.add_argument("--tour-only", action="store_true", help="the tour alone, no random input")
    args = ap.parse_args()
    sav_path = args.sav or (ROOT / ".emu" / "lsdj_song.sav")
    sav = sav_path.read_bytes() if sav_path.is_file() else bytes(0x20000)
    targets = routines()
    tour = st.recorded_tour(args.rom, sav, args.dmg)
    hit: set[tuple[int, int]] = set()
    for seed in ([0] if args.tour_only else args.seeds):
        steps = tour if args.tour_only else tour + st.monkey(seed, args.frames)
        hit |= run(args.rom, sav, steps, args.dmg, targets)
    per_bank: Counter = Counter()
    per_bank_hit: Counter = Counter()
    for key in targets:
        per_bank[key[0]] += 1
        per_bank_hit[key[0]] += key in hit
    for bank in sorted(per_bank):
        print(f"bank ${bank:02X}: {per_bank_hit[bank]:4d} of {per_bank[bank]:4d} routines")
    print(f"all:       {len(hit & set(targets)):4d} of {len(targets):4d} "
          f"({100 * len(hit & set(targets)) / len(targets):.0f}%)")
    if args.missed:
        for key in sorted(set(targets) - hit):
            name, kind = targets[key]
            print(f"  ${key[0]:02X}:${key[1]:04X} {name} ({kind})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
