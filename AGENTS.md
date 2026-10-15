# AGENTS.md — LSDj 9.4.0 Disassembly

Guidelines for humans and AI coding agents working in this repository. Read this
before making changes.

## The Golden Rule

**`make compare` must stay green after every change.** This project's entire
purpose is a byte-for-byte reproduction of the original ROM. If a change makes
the build differ from the original, the change is wrong — not the ROM. Verify
before you commit; never commit a red build.

## The ROM stays out of the repo

The ROM is a free download for personal use from littlesounddj.com, but it is
still Johan Kotlinski's work and this repo does not redistribute it:

- **Never commit a ROM or save** (`*.gb`, `*.gbc`, `*.gba`, `*.sav`, `*.srm`).
  They are git-ignored, and `.githooks/pre-commit` rejects them — do not
  defeat these guards.
- **Do not paste large ROM dumps** (raw hex of big regions) into commits, issues,
  chat, or generated files. Small, relevant byte snippets needed to document a
  routine are fine; wholesale dumps are not.
- The repository contains the *means to regenerate* the disassembly from a
  downloaded ROM, plus original documentation and labels — the same shape as
  the pret projects.
- When you need to confirm the ROM is present/correct, use `make verify-rom`
  (checks the sha1) rather than printing ROM contents.

## Maintaining This Document

When you change how the project works, **update `AGENTS.md` and `README.md` in
the same commit** — do not defer to a follow-up. This includes:

- New build targets, tools, scripts, or directories
- Changes to the carving workflow, conventions, or quality gates
- New constants/include files or naming schemes

Also keep [`docs/rom_map.md`](docs/rom_map.md) current as you identify regions.

## Project Overview

A disassembly of **LSDj 9.4.0** (`lsdj9_4_0.gb`, 1 MiB, MBC5+RAM+battery,
CGB-enhanced) targeting [RGBDS](https://rgbds.gbdev.io/) **v1.0.1**. LSDj is
written in C and compiled with a **modified GBDK 2.95**, so the machine code is
SDCC/GBDK-flavoured, not hand-written assembly. The long-term goal is a clean,
commented asm tree that can seed a C decompilation.

This repo is the "take one ROM apart" kind of project: a single ROM, taken to
pieces, with the pieces named.

## Strategy: asm baseline → matching C decompilation

This project has **two tracks**. The asm disassembly is the foundation; the
matching C decompilation is the goal built on top of it. They are not
alternatives — Track 2 depends on Track 1.

**Track 1 — byte-matching asm disassembly (essentially complete; refinement).**
All 13 program banks are carved into labelled, commented RGBDS assembly, every
function is named, and `make compare` proves the build equals the original ROM
byte-for-byte. Remaining Track 1 work is fidelity/naming polish (see
[`docs/reversal.md`](docs/reversal.md) → *Current status*). This is the source
of truth and the verification harness for everything else.

**Track 2 — matching C decompilation (the goal, gated).** Because LSDj is C built
with a modified GBDK 2.95 (SDCC), the end goal is *matching* C: source that, when
compiled with the **original** toolchain, produces byte-identical code. Functions
are replaced asm → C one at a time; un-decompiled functions stay as asm, so the
ROM stays byte-perfect throughout. This is exactly how pret's C decomps (e.g.
`pokeemerald` via the original `agbcc`) work. Note this is *matching* decompilation,
**not** static recompilation (e.g. `gb-recompiled`), which emits machine-generated
native C plus an emulation runtime and does **not** rebuild the ROM.

**Why asm first, always.** You can only know a C function "matches" by compiling
it and diffing the bytes against the asm/ROM target. No asm baseline ⇒ no way to
verify C. Track 2 cannot start for a region before Track 1 covers it.

**The hard prerequisite: toolchain recovery.** Matching C requires the *exact*
original compiler — a period-correct, modified **GBDK 2.95 + its SDCC build**, and
a matching bank/link layout. SDCC matching is less-trodden than the MIPS/ARM/PPC
decomp scene, and "modified GBDK" was an unknown, so Track 2 is **gated behind a
toolchain-recovery spike**: reproduce real LSDj functions byte-for-byte before
betting on C. **Status:** 678 functions match (`c/`, `make match`) — including
a 15-case `switch`, 16-bit SRAM addressing and most of the instrument screen's
helpers — using stock GBDK 2.95-3 / SDCC 2.2.1 plus peephole rules recovered
from the ROM (`tools/match/lsdj.peep`). Go: the rule set still grows with new
idioms, and the ~200 hand-written asm functions (sound tick, sequencer core,
~15 KB) and the GBDK/SDCC library routines stay asm. See
[`docs/toolchain.md`](docs/toolchain.md).

## Repository Structure

| Path | Role |
|------|------|
| `Makefile` | Build, `compare`, `verify-rom`, `sym`, `carve`, `listing`, `audit`, `match`, `clean`/`tidy`, `baseline` |
| `rgbdscheck.asm` | Fails the build on an unsupported RGBDS version (requires ≥ 1.0; developed on 1.0.1, also verified on 1.0.3) |
| `lsdj9_4_0.sha1` | Expected base-ROM checksum |
| `src/main.asm` | **Generated** (git-ignored) by `tools/gen_main.py`: INCLUDE carved banks, labeled INCBIN sections for the kits |
| `src/bankNN.asm` | **Generated** (git-ignored) disassembly of the 13 program banks (`00`-`07`, `1b`-`1f`) |
| `src/ramsections.asm` | **Generated** by `tools/gen_ram_sections.py`: named WRAM/HRAM/SRAM sections for the linker map |
| `src/bankNN.sym` | **Tracked, hand-curated** per-bank symbol maps: `ADDR Name ; comment` for routines, data, and function-local labels |
| `include/hardware.inc` | Standard GB hardware defs (gbdev, CC0) — do not edit |
| `constants/song.inc` | Song-data memory map (offsets, counts) as RGBDS EQUs |
| `constants/ram.inc` | WRAM/HRAM variable map (~800 names; arrays tagged `[N]`) |
| `constants/screens.inc` | Screen indices (`wCurScreen`) → editor name |
| `data/`, `gfx/` | Reserved for extracted data/graphics (currently empty) |
| `docs/rom_map.md` | ROM/memory map and per-bank notes |
| `docs/bank_map.md` | Heuristic per-bank classification (generated by `tools/bankmap.py`) |
| `docs/functionality.md` | What the code does (interrupts, engines, banking, RAM map) |
| `docs/sync.md` | Link cable sync protocol (serial IRQ, master/slave) |
| `docs/editors.md` | Screen/editor architecture: draw/update/input dispatch per screen |
| `docs/data_model.md` | Song format: chains/phrases/instruments/tables/synths/… |
| `docs/save_format.md` | `.sav` filesystem, compression, sample-kit & lsdsng formats |
| `docs/reversal.md` | Current status + known gaps, the coverage metric, and the methodology log |
| `docs/toolchain.md` | Track 2: period GBDK 2.95 / SDCC 2.2.1, match harness, current goal |
| `tools/disasm.py` | SM83→RGBDS disassembler (`--selftest`, labels, symbols, hw regs) |
| `tools/coverage.py` | Whole-ROM, bank-aware reachability trace ("% reversed") |
| `tools/carve_clean.py` | Regenerate `src/bankNN.asm` (trace-driven code/data split + `MANUAL` data ranges) |
| `tools/gen_main.py` | Generate `src/main.asm` (INCLUDE carved banks, labeled kit INCBINs) |
| `tools/gen_sym.py` | Build the debugger `.sym` (code + locals + RAM + SRAM, with comments) |
| `tools/gen_ram_sections.py` | Generate `src/ramsections.asm` from `constants/ram.inc` |
| `tools/gen_listing.py` | `build/listing.asm`: the whole ROM as one annotated file |
| `tools/audit_export.py` | Diff a pasted Emulicious export (`emulicious.out`) against our symbols |
| `tools/label_gaps.py`, `data_refs.py`, `callgraph.py`, `farcall_targets.py`, `find_orphans.py` | Audits that drove naming/labeling to zero gaps |
| `tools/name_locals_full.py`, `name_locals.py`, `name_firstaction.py`, `name_targets.py` | Semantic naming of function-local labels (generated into the `.sym`); `name_targets.py` names the targets an operand points at (jump tables after their variable, strings after their text) so no `.L_xxxx` reaches the source |
| `tools/rename.py`, `tools/fold_sym.py` | Symbol maintenance: rename a global everywhere (`.sym`, constants, C, docs, tools; refuses collisions), or fold a mislabelled "function" (a loop head, case body or shared tail) into a local label of the function it belongs to |
| `tools/show_func.py`, `rom_strings.py`, `annotate_ram.py`, `statevar.py`, `bankmap.py` | Inspection helpers: dump a function by name or address (`--bare`; a matched one is disassembled from the ROM), list the strings at an address, annotate a RAM dump, … |
| `tools/emu/` | SameBoy-based live-verification harness (see `tools/emu/README.md`) |
| `tools/shift_test.py` | Shift test for a movable build: pad a bank (`--bank 1f`, `--all`, `--at`, `--by`), run it next to the reference in PyBoy on the same input, report the first divergence, the `rst $38` site and call chain; `--bisect`. Needs `python3 -m venv .venv && .venv/bin/pip install pyboy`; run with `.venv/bin/python` |
| `tools/toolchain/` | The period GBDK 2.95-3 / SDCC 2.2.1: fetch and run under Wine, or build natively (`build_sdcc221.sh`; `--debug` adds the instrumented copy `trace_alloc.py` uses) |
| `tools/match/` | Track 2: `match_function.py --target` (compile C, link at the ROM address, diff; `--diff` aligns both disassemblies), `verify_all.py` (`make match`; cached batch check, also of files outside `c/`), `peep.py` (our peephole matcher), `code_kind.py` (which functions are matched, C, hand-written asm or library; share of C bytes matched), `shapes.py` (unmatched C functions by size, kind and parked state, or by shape), `rom_grep.py` (count a byte pattern across the code banks by owner: the evidence for a rule), `trace_alloc.py` (the allocator's view of one function: live ranges, homes, extended ranges, hoisting), `to_rgbds.py` (matched C as RGBDS source; `--check` assembles all of it byte-identically: the relocatable build's first step), `merge_c.py` (move matching functions into a module), `split_c.py` (split a module into modules named for what they hold: each takes its functions in ROM order and only the declarations they use), `update_counts.py` (refresh the function/rule counts in the docs and the parked table from `c/parked.txt`) |
| `tools/match/lsdj.peep` | Peephole rules recovered from LSDj's code — the "modified GBDK" part (`docs/toolchain.md`) |
| `tools/match/test_peep.py` | Focused regressions for `gb_cmp16` (branch/boolean semantics, HL/SP-relative stores, the global-operand HL reload, zero thresholds), `switch_chain` and `ifx_word_hl` (the HL reload after `if (w)`); run with `python3 tools/match/test_peep.py` after changing any of them |
| `tools/match/test_tools.py` | Regressions for the helper tools: `rom_grep.py`'s pattern syntax, `code_kind.py`'s never-from-SDCC operand marks, `--score`'s local-address normalisation |
| `c/include/lsdj.h` | Shared declarations for the matching C: hardware/HRAM/WRAM variables, the SRAM layout (literal-address macros), every settled prototype |
| `c/bankNN/*.c` | **Matching C** sources: byte-identical to the ROM when compiled with the period toolchain + `lsdj.peep`. Their output feeds the ROM build (`c/matched.json`, written by `make match`, lists the C-owned ranges; the carve renders their RGBDS translation from `build/c/`, or INCBINs the ROM without the compiler) A file marked `@stock-rules` is GBDK library code, matched with SDCC's stock rules only. |
| `tools/gen_baseline.py` | Historical one-time bootstrap baseline generator |
| `.githooks/pre-commit` | ROM-leak guard + `make compare` |

## Build & Verify

Daily commands:

```sh
make            # (re)generate src/*.asm if stale, assemble + link + fix -> build/lsdj9_4_0.gb
make compare    # build, then assert byte-for-byte equality with the original
make verify-rom # confirm your supplied ROM is the correct revision
make carve      # force-regenerate src/bankNN.asm + src/main.asm (tools/carve_clean.py)
make sym        # enrich build/lsdj9_4_0.sym (code+RAM+SRAM) and deploy it (+ a sections-only .map) beside the ROMs
make listing    # build/listing.asm: the whole ROM as one annotated file
make audit      # diff a pasted Emulicious export (emulicious.out) against our symbols
make match      # compile every c/bankNN/*.c with the period compiler, assert it matches, write build/c/ + c/matched.json (needs wine)
make clean      # remove build/ (and the deployed root .sym); `make tidy` removes only link outputs
make baseline   # historical: the original INCBIN bootstrap — clobbers src/main.asm
```

Quality gates — all must pass before committing:

| Gate | Command | Threshold |
|------|---------|-----------|
| Reproduction | `make compare` | byte-for-byte identical (exit 0) |
| Assembles cleanly | `make` | no errors/warnings |
| No ROM/saves staged | `.githooks/pre-commit` | zero blocked files |

Enable the hook once per clone:

```sh
git config core.hooksPath .githooks
```

## The Disassembly Workflow

All 13 program banks are carved, so day-to-day work is **refinement**: the
generated `src/bankNN.asm` is never edited by hand — you change the curated
inputs and regenerate. (Historically the build started as opaque `INCBIN`s and
was carved region by region with `tools/disasm.py`; the same byte-safety rules
apply.)

1. **Pick a target.** A routine, table, or variable whose name/comment is weak
   or wrong, or an entry in *Known gaps* in `docs/reversal.md`. Prefer
   following call/jump edges from already-understood code over guessing.
2. **Understand it.** Read it (`tools/show_func.py`, `make listing`), trace its
   callers/readers, and confirm behaviour in an emulator (`tools/emu/`) when the
   role isn't clear from the code. Check the [manual](https://www.littlesounddj.com/lsd/latest/documentation/LSDj_9_2_6.pdf)
   for what a screen field or command does before naming it: its Appendix C
   gives the SRAM map and every instrument type's byte layout (kit byte 2
   bit 6 = SPEED 0.5X, bytes 3/11 = LEN 1/2 with 0 = ALL, …).
3. **Change the curated inputs.** Names and comments go in `src/bankNN.sym`
   (routines, data, locals) or `constants/ram.inc` (WRAM/HRAM); a data range the
   trace decodes as code goes in `carve_clean.py`'s `MANUAL` dict.
4. **Keep the bytes identical.** `make compare` regenerates and must still pass.
   Names and code↔data re-rendering never change bytes; if `compare` breaks, a
   data/code boundary or operand was mis-decoded.
5. **Redeploy + document.** `make sym` refreshes the debugger symbols; record
   findings in the relevant `docs/` page.
6. **Commit small.** One coherent change per commit keeps `compare` bisectable.

Tips:
- A region is "done enough" when it is symbolic asm with meaningful labels and
  comments — not when it merely assembles.
- Use a debugger (Emulicious, mGBA, SameBoy) to confirm behaviour and trace
  banked calls before naming things.
- Consult [Pan Docs](https://gbdev.io/pandocs/) for hardware/memory-map context
  and the [GB Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf)
  for exact instruction semantics/timing when analysing a routine.

### The matching-C workflow (Track 2)

0. Build the period compiler once: `tools/toolchain/build_sdcc221.sh` (SDCC
   2.2.1 from the GBDK 2.95-2 sources, natively; the Win32 binaries under Wine
   crash on large units — `docs/toolchain.md`).
1. Pick C functions: `tools/match/shapes.py --list` lists the unmatched ones
   by size (asm-tagged ones excluded), `--show NAME...` prints their code,
   and the default mode groups them by instruction shape (one C template can
   match a whole family).
2. Write them in a scratch file (`#include "lsdj.h"`; function names must be
   `src/*.sym` names) following `docs/toolchain.md` → *Writing C that
   matches*; check a batch with `tools/match/verify_all.py /tmp/x.c` and one
   function with `tools/match/match_function.py --c FILE --target Name
   --diff` (both disassemblies aligned; `--score` for a one-line distance,
   `--show-asm`, `--stock` to see what the rules do). Settle a callee's prototype in `c/include/lsdj.h`: every call
   site shares it (SDCC 2.2.1 rejects a repeated prototype). Then move the
   matching functions into `c/bankNN/*.c` with `tools/match/merge_c.py
   BATCH.c c/bankNN/Module.c` (it re-verifies, keeps ROM order).
3. When a remaining difference is a compiler behaviour rather than a source
   form, add a rule to `tools/match/lsdj.peep` **only with ROM-wide evidence**
   (count the stock "before" shape and the rewritten shape over the code
   banks with `tools/match/rom_grep.py`, as in `docs/toolchain.md`), then
   `make match` to confirm every earlier
   function still matches. For `gb_cmp16` changes, also run
   `python3 tools/match/test_peep.py`: the add form reverses carry, clobbers HL,
   and needs an inverted numeric result when the comparison is stored.
4. `make compare` (which now runs `make match`'s compile when Wine is present)
   must stay green; commit the C, the `.sym` fixes it uncovered, and
   `c/matched.json`.

Keep work-in-progress C outside `c/bankNN/` (a scratch dir is fine for
throwaway attempts): a non-matching file there fails the build by design.
When a function won't match, park it: a line in `c/parked.txt` and the
closest attempt as `c/parked/NAME.c` (tracked, never compiled by
`make match`), so the next attempt starts from it. `tools/match/
trace_alloc.py` shows the register allocator's side of a stubborn
difference; when that side is a register the ROM holds for no symbol, an
`lsdj-alloc(Func): BUSYC=lo-hi` comment carries the window
(docs/toolchain.md, "Allocator directives"): six functions need one, the
last resort after the spellings are exhausted. Anything you'd run twice belongs in `tools/`, not in a scratch
directory.

### The disassembler (`tools/disasm.py`)

A faithful, **byte-exact** SM83→RGBDS disassembler. Because it decodes every byte
(emitting `db` for illegal opcodes and declared data ranges), linearly
disassembling a whole region always reassembles to identical bytes — so you can
bulk-convert an `INCBIN` to assembly and keep `make compare` green, then refine
semantics afterwards.

```sh
# Validate the opcode table against real rgbasm (run after any table change):
tools/disasm.py --selftest

# Re-carve all (or specific) banks — THIS is the canonical command. It runs the
# coverage trace, splits code/data, applies known-data ranges (carve_clean.py's
# MANUAL dict) and --globals, then rewires main.asm:
tools/carve_clean.py            # all carved banks
tools/carve_clean.py 00 1f      # just banks $00 and $1F

# Under the hood it invokes disasm.py per bank, e.g. bank 0 expands to:
tools/disasm.py --rom lsdj9_4_0.gb --bank 0 --symbols src/bank00.sym \
    --data <ranges from MANUAL + the trace> --comments -o src/bank00.asm

# Disassemble an explicit range at a given address:
tools/disasm.py --rom lsdj9_4_0.gb --start 0x150 --end 0x200 --org 0x150
```

Carve a ROMX bank (pass `--globals src/bank00.sym` so cross-bank `call`s into
home/bank-0 routines render as names like `call FarCall`):

```sh
tools/disasm.py --rom lsdj9_4_0.gb --bank 0x04 --symbols src/bank04.sym \
    --globals src/bank00.sym --comments -o src/bank04.asm
tools/gen_main.py        # rewire main.asm
```

To name a routine: add `ADDR Name` to the bank's `.sym`, regenerate, `make
compare`. Track overall progress with `tools/coverage.py` (see `docs/reversal.md`). Names become labels and are
substituted at all call/jump sites (byte-safe). Record what the routine *does*
in `docs/functionality.md`.

- `--data lo:hi[,lo:hi]` marks file-offset ranges as `db` (e.g. the header,
  graphics, tables) so they are not mis-decoded as code.
- Labels are generated for in-range branch/call targets, plus named interrupt
  vectors and `Entry`; `$FFxx` I/O accesses become `hardware.inc` register
  names. Both are byte-safe (they resolve to the same addresses).
- ROM-address operands print as labels (`rom_pointers()`), so code can move:
  a banked call's target by its bank (`ld bc, InstrEnvHasSecond` pushed before
  `ld a, $1f / push af / inc sp / call FarCall*`), bank 0's inline trampolines
  (`call ProcessLinkQueue` after `ld [hl], $07` to `rROMB0`, the bank tracked
  through the function), a ROMX bank's own `ld rr, $4000-$7FFF` (jump tables,
  strings, tables; a target inside a data run gets a row and a label of its
  own), bank 0's `ld rr` to its own global names, and MBC5 writes (`rROMB0`,
  `rRAMB`), addresses loaded a byte at a time (SDCC's `Table[i]` as
  `ld a, LOW(T) / add a, c / … / adc a, $00`, a banked call's target as
  `ld c, LOW(F) / ld b, HIGH(F) / push bc`, `AsciiCharMap - $20`), and the
  words of tagged pointer tables (`[N] dw`). Constants stay numbers:
  `gb_cmp16`'s add form (`ld hl, $5000 / add hl, de / jp c`), kit-format
  addresses, small numbers from ROMX code. A `jr` to an unlabelled address
  prints as `@ ± k`. `tools/carve_clean.py --pointer-notes` lists what stays a
  number; `tools/shift_test.py` checks the result by running a shifted build.
- `--trace` (with `--seed ADDR,…`, auto-seeded for `--bank 0`) is disasm.py's
  own single-bank recursive descent; it **cannot** follow computed `jp hl`
  dispatch or cross-bank calls. The canonical carve (`carve_clean.py`) instead
  uses `tools/coverage.py`'s bank-aware global trace, which follows jump tables,
  trampolines and the `FarCall*` helpers, and marks bytes `db` only when they
  are both unreached and data-like. Symbols the naming convention marks as
  data (below) are not seeded and always render as `db` up to the next global
  symbol; a data table with a name outside the convention must be listed in
  `carve_clean.py`'s `MANUAL` (or renamed) to render as data.
- **Re-run `--selftest` after any opcode-table change**, and `make compare`
  after every carve.

## RGBDS & Coding Conventions

- **Sections.** Bank 0 is `ROM0[$0000]`; banks 1–63 are
  `ROMX[$4000], BANK[$NNN]`. Keep section names descriptive once carved
  (e.g. `SECTION "Sound Engine", ROMX, BANK[$002]`).
- **Labels.** `PascalCase` for routines/data (`UpdateSoundChannel`,
  `WaveTableData`); local labels with a leading `.` (`.loop`, `.done`);
  exported/shared symbols use `::`.
- **Data symbols.** The tools tell data from code by the name/comment in the
  `.sym`: a `Txt`/`Tiles`/`Default`/`ScreenPal`/`Unused` prefix, a comment
  that opens with a string literal (`"OUT OF MEMORY..."`) or an `[N]` size
  tag, or a `*Names` table whose comment quotes its strings
  (`coverage.is_data_symbol`). Name data that way and it is never traced or
  rendered as instructions. A table of addresses is tagged `[N] dw` (N
  words; `[N] dw @$BB` when they point into bank BB): the carve prints it as
  `dw Label` so the targets can move.
- **Vocabulary.** One spelling per word in every name (routines, data,
  local labels, RAM, C identifiers): `Chan` (channel), `Instr`
  (instrument), `Proj` (project), `Env` (envelope), `Vib` (vibrato), `Cmd`
  (command), `Fx` (effect), `Col` (column), `Cur` (*current*, never
  cursor), `Cursor`, `Ptr`, `Buf`, `Val`, `Len`, `Pos`, `Pal` (palette),
  `Idx`, `Kbd` (the PS/2 keyboard; the name editor's on-screen keyboard is
  `NameEdit`), `Selection` (the clipboard selection; `Select` is only the
  SELECT button: the item under a cursor is an `Entry`, stepping a value is
  `Next`/`Prev`), `Highlight` (the cursor's colouring), `Shadow` (a RAM copy,
  `wTempoShadow`; the DMG font's dotted ground is `Shaded`), `Message`, `Number`, `Count`, `Transpose` (never `Trans`;
  `Transport` is sync), `Compress`/`Decompress`. The four channels are
  `Pu1`, `Pu2`, `Wav`, `Noi` (`TriggerPu1`, `wChanFreqPu2`). Exempt: `Txt`
  labels mirror their text, `SCREAMING_SNAKE` constants mirror the song
  format's documentation (`SONG_INSTRUMENTS`).
- **Name patterns.** A prefix names a domain (`Snd` the sound driver,
  `File`, `Sync`, `Fx`, `Kbd`), never the source file or bank a routine
  sits in. A screen's cursor highlight pair is `Draw<Screen>Cursor` /
  `Erase<Screen>Cursor`; a screen's handlers are `Draw<Screen>Screen` /
  `<Screen>ScreenInput` / `<Screen>ScreenUpdate`; a banked-call wrapper is
  `<Name>Far`. `Get`/`Read` return a value; a routine that only points
  state somewhere is `Set` (`SetVramCursor` aims the next draw). A grid screen's cursor is `w<Screen>Row`/`w<Screen>Col`
  (`wPhraseRow`, `wWordCol`), a one-index cursor `w<Screen>Cursor`
  (`wSynthCursor`). The *word* screen (screen 12) edits a word's allophones; the
  *speech* instrument screen lists the words (`SpeechInstrInput`).
- **Constants.** `SCREAMING_SNAKE_CASE` via `EQU`/`DEF` in `constants/`
  (`rRAMG`, `SOUND_CHANNEL_COUNT`). Use `hardware.inc` names for MMIO registers
  rather than raw `$FFxx` literals.
- **Numbers.** Hex as `$XXXX` (lower-case digits or upper — be consistent within
  a file); prefer hardware-meaningful constants over magic numbers.
- **Comments.** Explain *why*, document register in/out and clobbers for each
  routine. Keep a banner comment per carved region.
- **Indentation.** Tabs for instruction columns, matching RGBDS/pret style.
- Do **not** edit `include/hardware.inc` (vendored, CC0). Add project constants
  to `constants/` instead.

### Recognising GBDK/SDCC idioms

LSDj is compiled C; labelling goes faster once you spot the patterns:

- `crt0`/startup, banked-call trampolines, and `__sdcc_call_*` style helpers.
- SDCC register-pair calling convention and stack-frame setup/teardown.
- GBDK library routines (`set_sprite_*`, `gb/`-style hardware wrappers).
- Compiler-emitted jump tables and switch dispatch.

When writing *matching C*, the source forms matter as much as the logic —
`extern volatile sfr` for bytes stored with `ldh`, MBC registers as variables
(`rROMB0 = n`), `if (!x)` rather than `== 0`, `&buf[0]` for address loads,
`(unsigned int)x << 4` for a 16-bit shift, one statement per update of a
local; see [`docs/toolchain.md`](docs/toolchain.md) → *Writing C that matches*.
Check `tools/match/code_kind.py` first: ~200 functions (~15 KB: the
interrupt handlers, sound tick, bank `$02`'s sequencer core) are hand-written
asm and ~30 are GBDK/SDCC library routines; neither is a C target. Its
summary also gives the share of C bytes matched so far.

Name helpers after their GBDK/SDCC equivalents where identifiable. The GBDK 2.95
source is fetched locally to `toolchain/gbdk-2.95` (gitignored); its `.s` sources
(`libc/gb/*.s`, `libc/asm/gbz80/*.s`) give the canonical exported names and exact
assembly, so library routines can be matched **byte-for-byte**. Found this way so
far: `set_bkg_palette` (`cgb.s`), `strcpy` (`asm_string.s`), `display_off`
(`crt0.s`), `set_bkg_tiles` (`set_bk_t.s`) with its internal
`set_xy_btt`/`set_xy_wtt`/`set_xy_tt` (`set_xy_t.s`, GBDK's `.set_xy_*` minus
the dot), and the SDCC `mul` runtime. **Convention:** label GBDK/SDCC library
functions with their canonical lowercase name (`set_bkg_palette`, `strcpy`,
`printf`, `abs`, `randw`, `initrand`); reserve PascalCase for LSDj's own
routines. Exceptions: rgbasm reserves `STRCMP` and `STRLEN` in any case, so
those two are `StrCompare` and `StrLength`; the SDCC runtime the compiler
calls (`__divschar` ...) and GBDK's dot-prefixed internals (`.div8`,
`.delay`, which would collide with `delay`) are named for their operation
(`DivS8`, `DivModS8`, `DelayTicks`), the canonical name in the `.sym`
comment; the matcher aliases SDCC's names onto them (`RUNTIME_ALIASES`). Note LSDj rolls its own for most code,
so the library footprint is small.

## Tooling for Agents

Pick the right research tool (see the global agent rules for the full matrix):

| Need | Tool |
|------|------|
| RGBDS / Pan Docs / hardware reference | **context7** (resolve then query), then **exa** |
| How GBDK/SDCC emits a pattern; real asm usage | **grep.app** (search GitHub source), **exa** |
| Inspect ROM bytes / disassemble a region | shell: `xxd`, `rgbasm`/`rgblink`, an emulator's debugger |
| Find a known symbol's usages in this tree | **LSP** `findReferences` |
| Find structural asm/text patterns in this tree | **grep/rg** (RGBDS isn't an ast-grep language) |
| Verify behaviour interactively | mGBA / SameBoy / Emulicious |

Do not call the same MCP server repeatedly with minor query tweaks; switch tools
if one returns nothing useful.

### Intent classification

Classify the task first:

- **Explore** — read-only: understand a region, trace calls. Use grep/rg, the
  emulator, ROM inspection. No edits.
- **Plan** — decompose a carving effort into ordered steps before touching files.
- **Execute** — carve/label/document, then **`make compare`**. Stop and re-plan
  if a "simple" carve balloons in scope.
- **Review** — read diffs; the key review question is always "does it still
  reproduce the ROM byte-for-byte, and is the labelling correct?"
- **Debug** — a `compare` mismatch is a decode bug: check operand widths,
  endianness, addresses, and code/data boundaries before anything else.

## Code Style (tooling scripts)

For helpers in `tools/`:

- **Python** (`#!/usr/bin/env python3`): `snake_case` funcs, `PascalCase`
  classes, `UPPER_CASE` consts; prefer stdlib; keep scripts runnable standalone.
- **Shell** (`#!/usr/bin/env bash`): `set -o errexit` / `set -o pipefail` for
  fail-fast scripts; `snake_case` functions; 4-space indent.

## Commit Messages

[Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <description>
```

Types: `feat`, `fix`, `docs`, `refactor`, `chore`, `build`, `test`.
Suggested scopes: `disasm` (carving a region), `bank0`…`bankNN`, `sound`,
`ui`, `fs` (save/file system), `gfx`, `constants`, `build`, `tools`, `docs`.

Examples:

```
disasm(bank0): label entry point and interrupt vectors
disasm(sound): carve channel-1 frequency sweep update
build: add rgbfix validation to the link step
docs(rom_map): record SRAM bank switching for save access
chore: enable pre-commit hook path
```

## Verification Before Committing

Always, before committing:

1. `make compare` is green.
2. New/changed scripts run without error (`python3 tools/<script>.py --help`).
3. `git status` shows no ROM/save files staged (the hook enforces this, but
   check anyway).
4. `AGENTS.md`, `README.md`, and `docs/rom_map.md` reflect any new behaviour.

## Roadmap

**Phase 0 — Foundation (done)**
- [x] Verified, byte-perfect bootstrap baseline (`INCBIN` per bank)
- [x] Build + `compare` + `verify-rom` (RGBDS v1.0.1)
- [x] ROM-leak guards (`.gitignore`, pre-commit), checksum baseline
- [x] Vendored `hardware.inc`, RGBDS version guard, ROM map seed

**Phase 1 — Toolchain-recovery spike (the current Track 2 milestone)**
- [x] Fetch/pin the period compiler: GBDK 2.95-3 / SDCC 2.2.1 (`tools/toolchain/`,
      SourceForge Win32 release; its default small model is LSDj's calling
      convention). The "modified" part is extra peephole rules, recovered
      from the ROM into `tools/match/lsdj.peep` (28 rules, five families)
- [x] Match harness with a link step (`match_function.py --target NAME`) and a
      regression suite (`make match`)
- [x] **Goal:** byte-match `DrawMessageBar` (`$01:$74F7`) in C — matched, then
      `ShowOutOfMemory`/`ShowTapAddChain`, `ScreenDrawDispatch` (`switch`),
      `ReadInstrByte` (16-bit math, asserts) and `DrawTileHexCell` (the
      register-reload exception): six functions, three banks
- [x] Map what is C at all: `tools/match/code_kind.py` — ~200 functions are
      hand-written asm (sound tick, envelope/pitch, tile loader, sequencer core)
- [ ] Match a large editor function (a screen input handler) to grow the rule set
- [x] Matched C feeds the RGBDS build: `make match` writes each function's
      RGBDS translation, the carve renders it in place (ROM INCBIN without the
      compiler), `make compare` is the gate
- [x] Go/no-go on Track 2: **go** — the rule set still grows with new idioms
      (`docs/toolchain.md`); 678 functions in 13 banks match so far

**Phase 2 — asm skeleton (Track 1)**
- [x] Byte-exact SM83→RGBDS disassembler (`tools/disasm.py`, opcode self-test)
- [x] Carve bank 0 to real asm (entry, RST/interrupt vectors, init), labels + hw regs
- [x] Name the interrupt handlers, sound tick, sync, input, LCD, OAM DMA, init
      (`src/bank00.sym`, `docs/functionality.md`)
- [x] Identify MBC5 bank-switch trampolines (`ld [$2000], a`) and dispatch
- [x] Map the 16 `jp hl` jump tables to complete bank 0's call graph (followed by
      `tools/coverage.py`, carved as data)
- [x] Establish `constants/` RAM map (WRAM/HRAM in `ram.inc`; SRAM song
      structures exported by `tools/gen_sym.py`)

**Phase 3 — ROM & data model mapped (Track 1)**
- [x] Whole-ROM bank map: code vs sample kits vs empty (`docs/bank_map.md`)
- [x] Identify + name all 21 sample kits (banks `$08`-`$1A`, `$20`-`$21`)
- [x] Document the song data model (chains/phrases/instruments/tables/synths/
      grooves/words/sequence/properties) → `docs/data_model.md`, `constants/song.inc`
- [x] Document the `.sav` filesystem + compression + kit/lsdsng → `docs/save_format.md`
- [x] Locate the SRAM song accessor (`ReadSongByte`) in bank 0
- [x] Map the link cable **sync** subsystem (`docs/sync.md`)
- [x] Map the boot→`main.mainLoop` spine, the bank-switch mechanism (`$FF8E`/`$2000`),
      and the UI string table (bank `$01`); `docs/editors.md`
- [x] Multi-bank carving infrastructure (`tools/gen_main.py`)

**Phase 4 — Carve the editors (Track 1, done)**
- [x] Find the screen-draw + update dispatch (the editor index) in bank 0
- [x] Identify **all 12 screens** (Song/Chain/Phrase/Instrument/Table/Groove/
      Wave/Synth/Project/Word/File/Help) → `constants/screens.inc`, `docs/editors.md`
- [x] Carve **every editor's bank** (`$03`-`$07`, `$1B`-`$1D`); each screen's
      draw routine is named (`Draw<Screen>Screen`), `make compare` stays green
- [x] Disassembler: symbol-anchored decoding + bank-prefixed labels so all
      ROMX banks can be INCLUDEd together without label collisions
- [x] Locate + name the save/load **compressor + decompressor** (`docs/save_format.md`)
- [x] Name per-screen update handlers; carve all editor+support banks (11/64)
- [x] Name each editor's input/edit handler and per-field routines (input index:
      `DispatchScreenInput`, see `docs/editors.md`)
- [x] Mark embedded data tables in the carved banks (refine first-pass carves) —
      residue tracked in Phase 5b

**Phase 5 — Toward 100% reversal (`docs/reversal.md`)**
- [x] Coverage metric: whole-ROM reachability (`tools/coverage.py`)
- [x] Tracer follows `jp`/address jump tables (42% → 56% reachable code)
- [x] Reverse the bank `$02` sound engine, verified against live RAM/SRAM dumps:
      sequencer cursors (`wChanSongRow`→`wChanChain`→`wChanPhrase`→note) with
      `AdvanceSongRow`/`ReadSongChain`/`ReadChainPhrase`/`AdvanceChanCursors`;
      pitch (`NoteToPeriod`, `wNotePeriods` `$CF28`, `wChanFreq`, `WriteChanFreq`);
      note-on (`SetupChanSound`→per-channel + `rAUDx` shadows); FX dispatch
      (`DispatchFxCmd`, all `FxCmd_*`, `hFxVal`, `ApplyPitchBend`)
- [x] Cross-bank entry discovery (banked-call helper) to cover `$1E`/`$1F`
      (`FarCall`/`FarCallArg`/`FarCallByte` call sites resolved)
- [x] Name all routines: 100% of function entries (call- and jp-reached) have
      real global names; internal control flow is RGBDS local labels;
      zero global `L0x_xxxx` auto-labels remain
- [x] Drive code-coverage toward 100%: 0 code-like bytes unreached (79% of the
      code/data banks reached; the rest is fill/text/graphics); the
      interface-only `<Screen>Func/Proc` names are all upgraded
- [x] Every referenced data address labeled (`tools/data_refs.py` → 0); every
      WRAM/HRAM variable accessed by memory operand named except `$CB64`

**Phase 5b — Fidelity polish (Track 1; details in `docs/reversal.md` → *Current status*)**
- [x] Stop seeding named data as code: `coverage.is_data_symbol` (the naming
      convention below) is skipped by the trace and rendered as `db` by
      `carve_clean.py` — all 415 named data symbols now render as data
      (coverage 79% → an honest 77%, still 0 code-like bytes)
- [x] Render bank 0's 22 inline-trampoline call targets by name (disasm.py
      tracks the `$2000` bank write through each function)
- [ ] Re-verify/rename the five draw-named per-screen input handlers and
      `SndReadGrooveOrSpeed`; regenerate local labels that echo pre-July RAM names
- [ ] Name `$CB64`, the ~40 immediate-only RAM addresses, the WRAM routine at
      `$D480`, and the ramp table at `$00:$09B9`; resolve the ~13 `TBD` RAM roles

**Phase 6 — Matching C decompilation (Track 2, if green-lit by Phase 1)**
- [x] Per-function match workflow (`match_function.py --target`, `make match`)
- [ ] Reproduce the original bank/link layout under the recovered toolchain
      (only needed to build banks from C wholesale)
- [x] Replace asm functions with matching C, keeping `make compare` green —
      the mechanism is in place (678 functions); now it is a grind: bank by
      bank, editor code first, asm functions stay asm
- [ ] Recover C structures/types toward a full, readable C source tree

**Phase 7 — A movable build, for a fork that adds features (`docs/toolchain.md`)**
- [x] Matched C assembled from its RGBDS translation (`to_rgbds.py`)
- [x] Every ROM address in code is a label: operands, addresses loaded a byte
      at a time, banked-call targets, pointer tables (`[N] dw`); the vectors
      are code
- [x] Shift test (`tools/shift_test.py`): each bank's contents moved by 16
      bytes run the same as the original over a tour + 6,000 random frames
- [x] Wider shift coverage: DMG, odd sizes, all banks at once, more seeds,
      the file screen's load/save (a tour recorded on the reference)
- [ ] Bank 0's page-aligned tables (`$0200`-`$0390`) in fixed sections
- [ ] Behaviour tests for builds that no longer match: a fork's gate

## References

- [pret](https://github.com/pret) — methodology (`pokered`, `pokecrystal`).
- [GBDK 2.95](https://github.com/rbong/gbdk/tree/master/gbdk-2.95) — LSDj's toolkit.
- [Pan Docs](https://gbdev.io/pandocs/) — hardware / memory map / I/O registers.
- [GB Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf) — exact CPU/instruction semantics & timing.
- [RGBDS docs](https://rgbds.gbdev.io/docs/) — assembler/linker reference.
- [LSDj manual v9.2.6](https://www.littlesounddj.com/lsd/latest/documentation/LSDj_9_2_6.pdf)
  — the closest manual to 9.4.0 ([all versions](https://www.littlesounddj.com/lsd/latest/documentation/)):
  every screen and field, the 19 commands, sync modes, and Appendix C's SRAM
  map and instrument byte layouts. Names follow it.
- [liblsdj](https://github.com/gb-archive/liblsdj) · [lsdj-doc](https://github.com/jkotlinski/lsdj-doc) — format/internals.
