# AGENTS.md — LSDj 9.4.0 Disassembly

Guidelines for humans and AI coding agents working in this repository. Read this
before making changes.

## The Golden Rule

**`make compare` must stay green after every change.** This project's entire
purpose is a byte-for-byte reproduction of the original ROM. If a change makes
the build differ from the original, the change is wrong — not the ROM. Verify
before you commit; never commit a red build.

## Copyrighted Material and Sensitive Data

LSDj is **commercial software**. Treat the ROM the way you would treat a secret:

- **NEVER commit a ROM or save** (`*.gb`, `*.gbc`, `*.gba`, `*.sav`, `*.srm`).
  They are git-ignored, and `.githooks/pre-commit` will reject them — do not
  defeat these guards.
- **Do not paste large ROM dumps** (raw hex of big regions) into commits, issues,
  chat, or generated files. Small, relevant byte snippets needed to document a
  routine are fine; wholesale dumps are not.
- The repository must only contain the *means to reconstruct* the ROM from a
  legally-owned copy, plus original documentation/labels. This mirrors the pret
  posture exactly.
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

Sibling project: [`gmolsdj`](../gmolsdj) (LSDj archive manager). This repo is the
"take one ROM apart" counterpart to gmolsdj's "manage many saves/ROMs".

## Strategy: asm baseline → matching C decompilation

This project has **two tracks**. The asm disassembly is the foundation; the
matching C decompilation is the goal built on top of it. They are not
alternatives — Track 2 depends on Track 1.

**Track 1 — byte-matching asm disassembly (active).** Carve `src/main.asm` from
opaque `INCBIN`s into labelled, commented RGBDS assembly. `make compare` proves
the build still equals the original ROM byte-for-byte. This is the source of
truth and the verification harness for everything else.

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
decomp scene, and "modified GBDK" is an unknown, so Track 2 is **gated behind a
toolchain-recovery spike**: reproduce *one* real LSDj function byte-for-byte before
betting on C. Until that succeeds, treat C as unproven. See
[`docs/toolchain.md`](docs/toolchain.md).

## Repository Structure

| Path | Role |
|------|------|
| `Makefile` | Build, `compare` (byte verification), `verify-rom`, `baseline`, `clean` |
| `rgbdscheck.asm` | Fails the build on an unsupported RGBDS version |
| `lsdj9_4_0.sha1` | Expected base-ROM checksum |
| `src/main.asm` | **Generated** by `tools/gen_main.py`: INCLUDE carved banks, INCBIN rest |
| `src/bankNN.asm` | Disassembled carved banks (`00`, `03`-`07`, `1b`-`1f`) |
| `src/bankNN.sym` | Hand-maintained per-bank symbol maps (addr → routine name) |
| `include/hardware.inc` | Standard GB hardware defs (gbdev, CC0) — do not edit |
| `constants/song.inc` | Song-data memory map (offsets, counts) as RGBDS EQUs |
| `constants/ram.inc` | Runtime RAM/HRAM variables (screen, sync, playback) |
| `constants/screens.inc` | Screen indices (`wCurrentScreen`) → editor name |
| `data/`, `gfx/` | Extracted data tables and graphics (added as carved) |
| `docs/rom_map.md` | ROM/memory map and per-bank notes |
| `docs/bank_map.md` | Per-bank classification (generated by `tools/bankmap.py`) |
| `docs/functionality.md` | What the code does (interrupts, engines, RAM map) |
| `docs/sync.md` | Link cable sync protocol (serial IRQ, master/slave) |
| `docs/editors.md` | Screen/editor architecture + systematic carving plan |
| `docs/data_model.md` | Song format: chains/phrases/instruments/tables/synths/… |
| `docs/save_format.md` | `.sav` filesystem, compression, sample-kit & lsdsng formats |
| `tools/gen_baseline.py` | One-time bootstrap baseline generator |
| `tools/gen_main.py` | Generate `src/main.asm` (INCLUDE carved banks, INCBIN rest) |
| `tools/disasm.py` | SM83→RGBDS disassembler (`--selftest`, labels, symbols, hw regs) |
| `tools/bankmap.py` | Classify all banks; detect/name sample kits |
| `.githooks/pre-commit` | ROM-leak guard + `make compare` |

## Build & Verify

Daily commands:

```sh
make            # assemble + link + fix -> build/lsdj9_4_0.gb
make compare    # build, then assert byte-for-byte equality with the original
make verify-rom # confirm your supplied ROM is the correct revision
make clean      # remove build/
make baseline   # (one-time) regenerate src/main.asm from the ROM — clobbers edits
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

## The Disassembly Workflow (Carving)

The build starts as opaque `INCBIN`s and is refined into real source with the
help of `tools/disasm.py` (a byte-exact SM83→RGBDS disassembler — see below).
Work in small, verifiable steps:

1. **Pick a target.** A routine reachable from the entry point (`0x150`), an
   interrupt vector, or a referenced data table. Prefer following call/jump
   edges from already-understood code over guessing.
2. **Carve it out.** Replace the relevant slice of an `INCBIN` range with real
   RGBDS instructions/data. Split the surrounding `INCBIN` into the bytes
   *before* and *after* the carved region so the rest stays opaque.
3. **Keep the bytes identical.** `make compare` must still pass. RGBDS will emit
   the same opcodes for the same mnemonics; if `compare` breaks, you mis-decoded
   an operand, an address, or a data/code boundary.
4. **Label and document.** Give the routine a meaningful symbol, comment its
   inputs/outputs/side-effects, and record findings in `docs/rom_map.md`. Lift
   addresses into named constants in `constants/`.
5. **Commit small.** One coherent region per commit keeps `compare` bisectable.

Tips:
- A region is "done enough" when it is symbolic asm (no raw `INCBIN`) with
  labels and comments — not when it merely assembles.
- Use a debugger (Emulicious, mGBA, SameBoy) to confirm behaviour and trace
  banked calls before naming things.
- Consult [Pan Docs](https://gbdev.io/pandocs/) for hardware/memory-map context
  and the [GB Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf)
  for exact instruction semantics/timing when analysing a routine.

### The disassembler (`tools/disasm.py`)

A faithful, **byte-exact** SM83→RGBDS disassembler. Because it decodes every byte
(emitting `db` for illegal opcodes and declared data ranges), linearly
disassembling a whole region always reassembles to identical bytes — so you can
bulk-convert an `INCBIN` to assembly and keep `make compare` green, then refine
semantics afterwards.

```sh
# Validate the opcode table against real rgbasm (run after any table change):
tools/disasm.py --selftest

# Regenerate bank 0 (symbols + known data tables) — this is the canonical command:
tools/disasm.py --rom lsdj9_4_0.gb --bank 0 --symbols src/bank00.sym \
    --data 0x10:0x20,0x104:0x150 --comments -o src/bank00.asm

# Disassemble an explicit range at a given address:
tools/disasm.py --rom lsdj9_4_0.gb --start 0x150 --end 0x200 --org 0x150
```

To name a routine: add `ADDR Name` to `src/bank00.sym`, regenerate `bank00.asm`
with the command above, then `make compare`. Names become labels and are
substituted at all call/jump sites (byte-safe). Record what the routine *does*
in `docs/functionality.md`.

- `--data lo:hi[,lo:hi]` marks file-offset ranges as `db` (e.g. the header,
  graphics, tables) so they are not mis-decoded as code.
- Labels are generated for in-range branch/call targets, plus named interrupt
  vectors and `Entry`; `$FFxx` I/O accesses become `hardware.inc` register
  names. Both are byte-safe (they resolve to the same addresses).
- `--trace` (with `--seed ADDR,…`, auto-seeded for `--bank 0`) does
  recursive-descent reachability to split code from data. It **cannot** follow
  computed `jp hl` dispatch or cross-bank calls, so it under-approximates code
  (dispatch-reached functions look like data). Use it for data-dominated regions
  or diagnosis — **not** for code-dense banks like bank 0, which is best left as
  full linear disassembly.
- **Re-run `--selftest` after any opcode-table change**, and `make compare`
  after every carve.

## RGBDS & Coding Conventions

- **Sections.** Bank 0 is `ROM0[$0000]`; banks 1–63 are
  `ROMX[$4000], BANK[$NNN]`. Keep section names descriptive once carved
  (e.g. `SECTION "Sound Engine", ROMX, BANK[$002]`).
- **Labels.** `PascalCase` for routines/data (`UpdateSoundChannel`,
  `WaveTableData`); local labels with a leading `.` (`.loop`, `.done`);
  exported/shared symbols use `::`.
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

Name helpers after their GBDK/SDCC equivalents where identifiable (compare
against the [GBDK 2.95 source](https://github.com/rbong/gbdk/tree/master/gbdk-2.95)).

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

**Phase 1 — Toolchain-recovery spike (the first real engineering task)**
- [ ] Fetch/pin a period-correct modified GBDK 2.95 + its SDCC (`tools/toolchain/`)
- [ ] Stand up a one-function match harness (compile C → diff bytes vs ROM region)
- [ ] Byte-match a single real LSDj function; log findings in `docs/toolchain.md`
- [ ] Go/no-go decision on Track 2 (matching C) vs asm-only

**Phase 2 — asm skeleton (Track 1)**
- [x] Byte-exact SM83→RGBDS disassembler (`tools/disasm.py`, opcode self-test)
- [x] Carve bank 0 to real asm (entry, RST/interrupt vectors, init), labels + hw regs
- [x] Name the interrupt handlers, sound tick, sync, input, LCD, OAM DMA, init
      (`src/bank00.sym`, `docs/functionality.md`)
- [x] Identify MBC5 bank-switch trampolines (`ld [$2000], a`) and dispatch
- [ ] Map the 16 `jp hl` jump tables to complete bank 0's call graph
- [ ] Establish `constants/` RAM map (WRAM/HRAM/SRAM symbols)

**Phase 3 — ROM & data model mapped (Track 1)**
- [x] Whole-ROM bank map: code vs sample kits vs empty (`docs/bank_map.md`)
- [x] Identify + name all 21 sample kits (banks `$08`-`$1A`, `$20`-`$21`)
- [x] Document the song data model (chains/phrases/instruments/tables/synths/
      grooves/words/sequence/properties) → `docs/data_model.md`, `constants/song.inc`
- [x] Document the `.sav` filesystem + compression + kit/lsdsng → `docs/save_format.md`
- [x] Locate the SRAM song accessor (`ReadSongByte`) in bank 0
- [x] Map the link cable **sync** subsystem (`docs/sync.md`)
- [x] Map the boot→`MainLoop` spine, the bank-switch mechanism (`$FF8E`/`$2000`),
      and the UI string table (bank `$01`); `docs/editors.md`
- [x] Multi-bank carving infrastructure (`tools/gen_main.py`)

**Phase 4 — Carve the editors (Track 1, in progress)**
- [x] Find the screen-draw + update dispatch (the editor index) in bank 0
- [x] Identify **all 12 screens** (Song/Chain/Phrase/Instrument/Table/Groove/
      Wave/Synth/Project/Word/File/Help) → `constants/screens.inc`, `docs/editors.md`
- [x] Carve **every editor's bank** (`$03`-`$07`, `$1B`-`$1D`); each screen's
      draw routine is named (`Draw<Screen>Screen`), `make compare` stays green
- [x] Disassembler: symbol-anchored decoding + bank-prefixed labels so all
      ROMX banks can be INCLUDEd together without label collisions
- [ ] Name each editor's input/edit handler and per-field routines
- [ ] Mark embedded data tables in the carved banks (refine first-pass carves)
- [ ] Locate load/save/compress/format routines in ROM and name them

**Phase 5 — Matching C decompilation (Track 2, if green-lit by Phase 1)**
- [ ] Per-translation-unit match workflow (objdiff-style byte diff vs ROM)
- [ ] Reproduce the original bank/link layout under the recovered toolchain
- [ ] Replace asm functions with matching C, keeping `make compare` green
- [ ] Recover C structures/types toward a full, readable C source tree

## References

- [pret](https://github.com/pret) — methodology (`pokered`, `pokecrystal`).
- [GBDK 2.95](https://github.com/rbong/gbdk/tree/master/gbdk-2.95) — LSDj's toolkit.
- [Pan Docs](https://gbdev.io/pandocs/) — hardware / memory map / I/O registers.
- [GB Complete Technical Reference](https://gekkio.fi/files/gb-docs/gbctr.pdf) — exact CPU/instruction semantics & timing.
- [RGBDS docs](https://rgbds.gbdev.io/docs/) — assembler/linker reference.
- [liblsdj](https://github.com/gb-archive/liblsdj) · [lsdj-doc](https://github.com/jkotlinski/lsdj-doc) — format/internals.
