# lsdj-decompiled

A **disassembly of Little Sound Dj (LSDj) 9.4.0** for the Nintendo Game Boy,
in the style of the [pret](https://github.com/pret) projects (`pokered`,
`pokecrystal`, …).

The [RGBDS](https://rgbds.gbdev.io/) source tree **reassembles into a
byte-for-byte identical copy of the original ROM**.

Current state:

- byte-for-byte reproduction, verified on every commit — and from a fresh
  clone plus your ROM in a few seconds
- full symbol coverage: ~8,700 names — 1,833 routines/tables/strings, 6,045
  function-local labels, 808 WRAM/HRAM variables and 37 SRAM song structures —
  across all 13 program banks and the 21 sample kits. Every function is named,
  every referenced data address is labeled, and a debugger loading the
  generated `.sym` shows no auto-labels
- RAM: ~800 semantic addresses in `constants/ram.inc` (arrays carry `[N]` size
  tags and interior accesses render as `base + N`), plus the banked SRAM
  working-song structures (`SramPhraseNotes`, `SramInstruments`, …). What's
  left: one byte (`$CB64`) still accessed unnamed, ~40 addresses that appear
  only as raw 16-bit immediates (mostly buffer bases passed to helpers), and
  ~13 named variables whose exact role is marked `TBD`
- code coverage: 77% of the program banks trace as reachable code and the rest
  classifies as fill/text/graphics, with no code-like bytes unexplained. Known
  fidelity gaps — e.g. bank 0's bank-switched call targets still printed as raw
  addresses — are listed in [`docs/reversal.md`](docs/reversal.md) → *Current status*
- matching C works: ten LSDj functions in [`c/`](c) — a message renderer,
  two `switch`es, a 16-bit SRAM accessor, … — compile byte-for-byte with the
  original GBDK 2.95-3 / SDCC 2.2.1 plus peephole rules recovered from the
  ROM, LSDj's "modified GBDK", and the ROM build takes those functions' bytes
  from the compiled C (`make match`; see
  [`docs/toolchain.md`](docs/toolchain.md)). About 130 functions — the sound
  tick and sequencer core — are hand-written asm and stay that way

## Why

LSDj is a music tracker for the original Game Boy with a deep, decades-long
chiptune community. A reproducible source-level understanding of the ROM
enables preservation, documentation, bug archaeology, and study of the engine.

## Getting the ROM

The repo doesn't include the ROM — grab it from
<https://www.littlesounddj.com/>, where the ROM images are a free download
and the [license](https://www.littlesounddj.com/lsd/latest/rom_images/LICENSE.txt)
allows personal use. LSDj is Johan Kotlinski's work and this project doesn't
redistribute any of it; you point the build at your copy and everything is
generated from there (`*.gb` is git-ignored, and a pre-commit hook refuses
ROMs as a belt-and-braces measure).

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

On Arch: `sudo pacman -S rgbds make python`.

## Quick start

```bash
# 1. Download LSDj 9.4.0 from littlesounddj.com, then either copy the ROM
#    into the repo root:
cp /path/to/lsdj9_4_0.gb .
#    ...or leave it where it is and pass the path:
#    make BASEROM=/path/to/lsdj9_4_0.gb <target>

# 2. Confirm it is the exact expected revision:
make verify-rom

# 3. Generate the disassembly from your ROM, build it, and verify the result
#    is byte-for-byte identical to the original:
make compare
```

A successful `make compare` prints:

```
==> compare OK: build matches the original ROM byte-for-byte.
```

## How it works

The disassembly itself (`src/*.asm`) is generated on your machine and
git-ignored — a byte-exact disassembly is just the ROM in another encoding,
so there's no point tracking it and no need to ship it. What's tracked is the
part that took the work:

- `src/bankNN.sym` — per-bank symbol maps: several thousand hand-verified
  routine, table and string names with comments.
- `constants/ram.inc` — the RAM variable map.
- the code/data split metadata inside `tools/carve_clean.py`.
- `docs/` — the write-ups (memory map, song format, save filesystem, sound
  engine, sync protocol, editor architecture, methodology).

The pipeline: `tools/coverage.py` traces reachable code from the vectors,
bank-switch trampolines and every named symbol; `tools/carve_clean.py` +
`tools/gen_main.py` then emit `src/bankNN.asm` and `src/main.asm` from your
ROM with all names applied; RGBDS assembles it back; `make compare` asserts
the result equals your original, byte for byte. If the bytes drift, the
change was wrong — that invariant has held through every commit.

## Strategy: asm now, matching C later

The project runs on **two tracks** (details in [`AGENTS.md`](AGENTS.md)):

- **Track 1 — byte-matching asm disassembly (active).** The symbol maps and
  carve metadata are the source of truth; the generated asm tree is the
  verification harness.
- **Track 2 — matching C decompilation (the goal, gated).** LSDj is C built with
  a *modified GBDK 2.95* (SDCC). The end goal is **matching** C — source that,
  compiled with the original toolchain, produces byte-identical code — replacing
  asm functions one at a time while `make compare` stays green (the approach
  pret's `pokeemerald` uses with `agbcc`). This is *not* static recompilation
  (which would emit generated native C + a runtime and never rebuild the ROM).

Track 2 is gated behind a **toolchain-recovery spike**: pin a period-correct
GBDK 2.95 + SDCC and reproduce real LSDj functions byte-for-byte before
committing to C. That spike is done and the grind has begun: 250 functions
match, the recovered compiler model is an accumulator-tracking pass plus 68
peephole rules with ROM-wide evidence, and matched C feeds the ROM build (`INCBIN` of the compiled bytes,
gated by `make compare`).
See [`docs/toolchain.md`](docs/toolchain.md), and try it:

```bash
tools/toolchain/build_sdcc221.sh     # period compiler, built natively (gcc -m32, bison, flex)
make match                           # every c/bankNN/*.c vs the ROM (cached)
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar
# ==> MATCH: byte-identical to ROM $01:$74F7-$75BC
```

## Repository layout

```
lsdj-decompiled/
├── Makefile              # build + `compare` (byte-for-byte verification)
├── rgbdscheck.asm        # RGBDS version guard
├── lsdj9_4_0.sha1        # expected base-ROM checksum
├── src/
│   ├── bankNN.sym        # per-bank symbol maps (tracked; the curation)
│   ├── bankNN.asm        # generated disassembly (git-ignored, from your ROM)
│   ├── ramsections.asm   # generated named RAM sections (git-ignored)
│   └── main.asm          # generated top level (git-ignored)
├── c/
│   ├── bankNN/*.c        # matching C (byte-identical under the period compiler)
│   └── matched.json      # the C-owned ROM ranges (written by make match)
├── include/
│   └── hardware.inc      # standard GB hardware definitions (gbdev, CC0)
├── constants/            # symbolic constants
│   ├── ram.inc           # RAM variable map (~800 named wXxx/hXxx vars)
│   ├── screens.inc       # screen indices (wCurrentScreen) -> editor
│   └── song.inc          # song-data memory map (offsets, counts)
├── data/                 # extracted data tables (added as carved)
├── gfx/                  # graphics assets (added as carved)
├── docs/
│   ├── rom_map.md        # ROM / memory map and per-bank notes
│   ├── bank_map.md       # per-bank classification (generated)
│   ├── functionality.md  # what the code does (interrupts, engines)
│   ├── sync.md           # link cable sync protocol (serial IRQ, master/slave)
│   ├── editors.md        # screen/editor architecture + carving plan
│   ├── data_model.md     # song format: chains/phrases/instruments/tables/synths
│   ├── save_format.md    # .sav filesystem, compression, kits, lsdsng
│   ├── reversal.md       # methodology + the road to full symbol coverage
│   └── toolchain.md      # Track 2: period GBDK 2.95 / SDCC 2.2.1 recovery
├── tools/
│   ├── disasm.py         # byte-exact SM83->RGBDS disassembler (self-tested)
│   ├── coverage.py       # whole-ROM reachability tracing
│   ├── carve_clean.py    # regenerate src/bankNN.asm (code/data split + symbols)
│   ├── gen_main.py       # generate main.asm (INCLUDE carved banks, INCBIN kits)
│   ├── gen_sym.py        # build the debugger symbol file (code + RAM + SRAM)
│   ├── gen_ram_sections.py  # named WRAM/SRAM sections for the linker map
│   ├── gen_listing.py    # emit build/listing.asm — whole ROM as one annotated file
│   ├── audit_export.py   # diff a pasted debugger export against our symbols
│   ├── name_locals_full.py  # semantic naming of function-local labels
│   ├── show_func.py      # dump one function's disassembly by bank:addr
│   ├── callgraph.py, farcall_targets.py, find_orphans.py, label_gaps.py,
│   │   data_refs.py      # audits that drove symbol coverage to 100%
│   ├── bankmap.py, annotate_ram.py, statevar.py, name_locals.py,
│   │   name_firstaction.py, gen_baseline.py  # smaller/historical helpers
│   ├── emu/              # SameBoy-based live-verification harness
│   ├── toolchain/        # fetch + run the period compiler (under Wine)
│   └── match/            # matching-decompilation harness (compile C, link at the
│                         #   ROM address, diff), verify_all (make match), code_kind
│                         #   (C vs asm map) + lsdj.peep (recovered rules)
└── .githooks/pre-commit  # ROM-leak guard + `make compare`
```

> `toolchain/` (SDCC 2.2.1 built from the GBDK 2.95-2 sources by
> `tools/toolchain/build_sdcc221.sh`; optionally GBDK 2.95-3's Win32 binaries
> via `fetch_gbdk295.sh`, run under Wine) is git-ignored.

## Development

Enable the pre-commit hook (ROM-leak guard + byte-for-byte check):

```bash
git config core.hooksPath .githooks
```

Common commands:

```bash
make            # build build/lsdj9_4_0.gb
make compare    # build, then assert it equals the original ROM
make carve      # force-regenerate the disassembly from your ROM + src/*.sym
make verify-rom # check your supplied ROM is the right revision
make sym        # emit the BANK:ADDR symbol file for emulator debugging
make listing    # emit build/listing.asm — the whole ROM as one annotated file
make audit      # diff a pasted debugger export (emulicious.out) vs our symbols
make match      # compile c/bankNN/*.c with the period compiler, assert byte-identical, write build/c/ (needs wine)
make clean      # remove build/
```

`make sym` writes a `BANK:ADDR Name` symbol file (`build/lsdj9_4_0.sym`,
BGB/SameBoy/Emulicious format) and deploys it beside the ROM, plus a
*sections-only* copy of the linker `.map` (symbol lines stripped): Emulicious
treats a map with symbols as its sole symbol source, but a symbol-less map
attaches the named WRAM/SRAM sections and still falls back to the `.sym` —
so you get real section names *and* the curated per-symbol comments. Every
routine, data table, string, RAM variable and SRAM song structure is named —
in Emulicious the symbol tree resolves completely, with no
`_LABEL_`/`_DATA_` auto-labels left. This file is arguably the most useful
artifact of the whole project even if you never read the assembly.

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

The original LSDj ROM is **not** covered by this repository's license and is
not included in any form. The tooling, symbol maps, build scripts and
documentation authored here are MIT-licensed (see [`LICENSE`](LICENSE)).
`include/hardware.inc` is CC0 (gbdev).
