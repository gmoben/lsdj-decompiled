# lsdj-decompiled

A work-in-progress **disassembly of Little Sound Dj (LSDj) 9.4.0** for the
Nintendo Game Boy, in the style of the [pret](https://github.com/pret) projects
(`pokered`, `pokecrystal`, …).

The goal is a fully commented [RGBDS](https://rgbds.gbdev.io/) source tree that
**reassembles into a byte-for-byte identical copy of the original ROM**, as a
foundation for an eventual C decompilation (LSDj is written in C and built with
a modified GBDK 2.95).

## Why

LSDj is a music tracker for the original Game Boy with a deep, decades-long
chiptune community. A reproducible source-level understanding of the ROM
enables preservation, documentation, bug archaeology, and study of the engine.
This repository is a sibling to [`gmolsdj`](../gmolsdj) — where `gmolsdj`
manages an *archive* of LSDj saves and ROMs, this project takes one of those
ROMs apart.

## Legal: the ROM is not included

**LSDj is commercial software. No ROM is distributed here, under any name.**
This repository contains only the source and tooling needed to reconstruct the
ROM from a copy *you* legally own. `*.gb` files are git-ignored and a
pre-commit hook actively refuses to commit them.

- Buy/obtain LSDj from <https://www.littlesounddj.com/>.
- Read the [LSDj license](https://www.littlesounddj.com/lsd/latest/rom_images/LICENSE.txt).
- This is the same posture pret takes (supply your own ROM).

## Target ROM

| Field | Value |
|-------|-------|
| Version | LSDj 9.4.0 (stable) |
| File | `lsdj9_4_0.gb` |
| Size | 1 MiB · MBC5 + RAM + battery · 128 KiB SRAM · CGB-enhanced |
| sha1 | `58ba2e44c726c61d8c91423ec82152319dddc2ca` |
| md5 | `b4c353122033f538e88eb00c083c0572` |

See [`docs/rom_map.md`](docs/rom_map.md) for the decoded header and memory map.

## Prerequisites

- [RGBDS](https://rgbds.gbdev.io/) **v1.0.1** (`rgbasm`, `rgblink`, `rgbfix`, `rgbgfx`)
- `make`
- `python3` (bootstrap/codegen helpers in `tools/`)

On Arch/Manjaro: `sudo pacman -S rgbds make python`.

## Quick start

```bash
# 1. Put your legally-obtained LSDj 9.4.0 ROM in the repo root:
cp /path/to/your/lsdj9_4_0.gb .

# 2. Confirm it is the exact expected revision:
make verify-rom

# 3. Build and verify the reproduction is byte-for-byte identical:
make compare
```

A successful `make compare` prints:

```
==> compare OK: build matches the original ROM byte-for-byte.
```

## How it works

A from-scratch disassembly of a 1 MiB ROM is a long road, so the build starts
from a **verified baseline** and is refined incrementally:

1. **Bootstrap** — `src/main.asm` begins as one `INCBIN` per 16 KiB bank of the
   base ROM (generated once by `tools/gen_baseline.py`). This reassembles to an
   identical ROM, so `make compare` is green from commit one.
2. **Carve** — replace `INCBIN` ranges with real disassembled code and data, one
   routine/table at a time, labelling and commenting as you go.
3. **Verify** — `make compare` must stay green after *every* change. If the
   bytes drift, the change is wrong.

This keeps the project always-buildable and always-correct while the
human-readable surface area grows.

## Strategy: asm now, matching C later

The project runs on **two tracks** (details in [`AGENTS.md`](AGENTS.md)):

- **Track 1 — byte-matching asm disassembly (active).** The carving loop above.
  The asm tree is the source of truth and the verification harness.
- **Track 2 — matching C decompilation (the goal, gated).** LSDj is C built with
  a *modified GBDK 2.95* (SDCC). The end goal is **matching** C — source that,
  compiled with the original toolchain, produces byte-identical code — replacing
  asm functions one at a time while `make compare` stays green (the approach
  pret's `pokeemerald` uses with `agbcc`). This is *not* static recompilation
  (which would emit generated native C + a runtime and never rebuild the ROM).

Track 2 is gated behind a **toolchain-recovery spike**: pin a period-correct
GBDK 2.95 + SDCC and reproduce one real LSDj function byte-for-byte before
committing to C. See [`docs/toolchain.md`](docs/toolchain.md) for status.

## Repository layout

```
lsdj-decompiled/
├── Makefile              # build + `compare` (byte-for-byte verification)
├── rgbdscheck.asm        # RGBDS version guard
├── lsdj9_4_0.sha1        # expected base-ROM checksum
├── src/
│   ├── main.asm          # top level: INCLUDEs carved banks, INCBINs the rest
│   ├── bank00.asm        # disassembled bank 0 (generated from ROM + symbols)
│   └── bank00.sym        # bank 0 symbol map (addr -> routine name)
├── include/
│   └── hardware.inc      # standard GB hardware definitions (gbdev, CC0)
├── constants/            # symbolic constants (RAM addrs, enums, …)
├── data/                 # extracted data tables (added as carved)
├── gfx/                  # graphics assets (added as carved)
├── docs/
│   ├── rom_map.md        # ROM / memory map and per-bank notes
│   ├── functionality.md  # what the code does (interrupts, engines, RAM map)
│   └── toolchain.md      # Track 2: period GBDK 2.95 / SDCC 2.2.1 recovery
├── tools/
│   ├── gen_baseline.py   # one-time bootstrap baseline generator
│   ├── disasm.py         # byte-exact SM83->RGBDS disassembler (self-tested)
│   ├── toolchain/        # fetch + run the period compiler (under Wine)
│   └── match/            # matching-decompilation harness (compile C, diff vs ROM)
└── .githooks/pre-commit  # ROM-leak guard + `make compare`
```

> `toolchain/` (the downloaded GBDK 2.95 / SDCC 2.2.1) is git-ignored and fetched
> on demand by `tools/toolchain/fetch_gbdk295.sh`. It requires `wine`.

## Development

Enable the pre-commit hook (ROM-leak guard + byte-for-byte check):

```bash
git config core.hooksPath .githooks
```

Common commands:

```bash
make            # build build/lsdj9_4_0.gb
make compare    # build, then assert it equals the original ROM
make verify-rom # check your supplied ROM is the right revision
make clean      # remove build/
```

See [`AGENTS.md`](AGENTS.md) for the full contributor/agent workflow, coding
conventions, and the carving methodology.

## References

- [pret](https://github.com/pret) — disassembly projects that define the
  methodology (`pokered`, `pokecrystal`).
- [GBDK 2.95 source](https://github.com/rbong/gbdk/tree/master/gbdk-2.95) — the
  toolkit LSDj is built with (modified).
- [Pan Docs](https://gbdev.io/pandocs/) — Game Boy hardware reference.
- [GB Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf) —
  exact CPU/instruction semantics and timing.
- [RGBDS docs](https://rgbds.gbdev.io/docs/) — assembler/linker reference.
- [gbdev/hardware.inc](https://github.com/gbdev/hardware.inc) — vendored in
  `include/`.
- [LSDj documentation](https://github.com/jkotlinski/lsdj-doc) and
  [liblsdj](https://github.com/gb-archive/liblsdj) — save/format internals.

## Acknowledgements

LSDj is created by Johan Kotlinski. This is an independent, non-commercial
reverse-engineering and preservation effort and is not affiliated with or
endorsed by the author. Thanks to the pret community for charting
the way for Game Boy disassemblies.

## License

The original LSDj ROM is **not** covered by this repository's license and is not
included. The disassembly tooling, build scripts, and documentation authored
here are released under the MIT License (see `LICENSE` when added).
`include/hardware.inc` is CC0 (gbdev).
