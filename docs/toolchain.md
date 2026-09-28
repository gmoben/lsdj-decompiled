# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95-3** (19 Aug 2000) → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker, small memory model — **plus an accumulator-tracking optimisation stock SDCC lacks** (the "modified GBDK"), modelled here as 39 peephole rules in six families (below) |
| Can we run it today? | **Yes** — the period `sdcc.exe` runs under **Wine** (32-bit PE; re-verified on wine-11.17); `tools/toolchain/fetch_gbdk295.sh` downloads it |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock output does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95-3 lacks |
| Matched real LSDj functions? | **Yes — seven**, in four banks: a message renderer, two wrappers, a 15-case `switch`, a sparse `switch`, a 16-bit SRAM accessor, a two-branch draw helper. `make match` re-verifies them all in ~3 s, and **their bytes in the ROM build come from the compiled C** (below) |
| Is all of LSDj C? | **No.** ~130 functions (7% of the instructions) are hand-written assembly — the interrupt handlers, sound tick, envelope/pitch and tile-loader code in bank 0, and the sequencer core in bank `$02` (`tools/match/code_kind.py`). Those stay asm; the ~830 others (every editor) are Track 2 targets |
| Go/no-go on Track 2? | **Go, with the rule set still growing.** Every function tried has matched (each new one has cost a few rules so far), the C feeds the ROM build with `make compare` as the gate, and the hand-written asm (7%) is mapped and excluded |

## Matched functions

| Function | Where | Size | What it exercises | Source |
|----------|-------|------|-------------------|--------|
| `DrawMessageBar` | `$01:$74F7` | 198 B | globals, `sfr` HRAM stores, two loops, calls | [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) |
| `ShowOutOfMemory`, `ShowTapAddChain` | `$01:$75BD`, `$01:$75F8` | 10 B each | one-line wrappers, 16-bit argument | [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) |
| `ScreenDrawDispatch` | `$00:$254F` | 252 B | 15-case `switch` → `jp hl` table, bank switching, cross-bank calls | [`c/bank00/ScreenDrawDispatch.c`](../c/bank00/ScreenDrawDispatch.c) |
| `ReadInstrByte` | `$00:$0A91` | 81 B | byte args, pointer local, 16-bit shift loop, asserts, return value | [`c/bank00/ReadInstrByte.c`](../c/bank00/ReadInstrByte.c) |
| `DrawTileHexCell` | `$06:$7524` | 130 B | consecutive updates of a stack local, sign test, `-t`, pointer table | [`c/bank06/DrawTileHexCell.c`](../c/bank06/DrawTileHexCell.c) |
| `DrawSyncField` | `$1B:$58B1` | 262 B | sparse `switch` (`cp` chain), if/else, a pointer local chosen by a condition, 4-arg calls | [`c/bank1b/DrawSyncField.c`](../c/bank1b/DrawSyncField.c) |

```sh
make match                      # every c/bankNN/*.c linked at its ROM address and compared
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar --stock   # without the rules: 205 bytes, mismatch
```

The C sources are the regression suite for the compiler model: a rule change
that breaks an earlier match shows up in `make match` — and in `make compare`,
since the build takes those functions' bytes from the compiled C (see *How
matched C enters the build*). Matching `DrawSyncField` also fixed the symbol
map: its second half had been labelled as a separate routine
(`DrawSyncModeField`), but the `push bc` there is an argument push and the
`pop bc; ret` is `DrawSyncField`'s epilogue.

## The "modified GBDK": recovered peephole rules

Stock 2.95-3 compiles the C above to *almost* LSDj's bytes; every remaining
difference is a local rewrite that stock SDCC 2.2.1 doesn't do. They are
encoded as SDCC peephole rules in [`tools/match/lsdj.peep`](../tools/match/lsdj.peep)
(loaded via `--peep-file`; `--stock` drops them). Each family is supported
**ROM-wide**, not just in the matched functions: across LSDj's compiled code the
stock "before" shape essentially never survives while the rewritten form is
common (byte-pattern counts over banks `$01`-`$07`, `$1B`-`$1F`):

| Family | Rewrite | "Before" left in ROM | "After" in ROM |
|--------|---------|---------------------:|---------------:|
| Redundant flag ops | `or a,X` / `or a,a` → `or a,X`; `ld r,a` / `xor a,a` / `or a,r` → `ld r,a` / `or a,r` | 0 | 575, 41 |
| Register reload | `ld r,a` / `ld a,r` → `ld r,a` (r = b, c, d, e, h, l; **not** `(hl)`); `ld c,e` … `ld e,c` (a returned value) even across `add sp,N` | 2 (both with a jump label between) + 1 (below) | 69, 34 |
| Reload of a computed value | *ALU op*; `ld (hl),a`; `ld a,(hl)` → reload dropped; **kept** after a pointer read (`ld a,(de)`; `ld hl,sp+N`; `ld (hl),a`; `ld a,(hl)` survives 30/30) | 0 | 31 |
| Dead stores | `ld X,a`; *a-only ops*; `ld X,a` → first store dropped (X a register or `(hl)`, also when the first store is followed by its reload) | 0 | (small; e.g. `and 3; add a,a; ld c,a` ×2) |
| Constant reuse | `ld a,N` / `push af; inc sp` / `ld a,N` → reload dropped, also across `ldh (n),a` + `ld hl,nn; push hl`; `xor a` / `ld hl,sp+N` / `ld a,#0` → reload dropped | 0 | 10, 5, 2 |
| Compare chains | a sparse `switch` re-reads its global before every `cp` in stock output; LSDj loads it once (one rule per chain position, so the pattern always carries the variable). SDCC's `==` trampoline `jp cc,T; jr F; T: jp X; F:` → `jp cc,X` (the only survivor in the ROM is inside GBDK's stock-compiled `gprintf`) | 0, 1 (library) | 72 |

The one register reload LSDj keeps without a label in the way is
`ld a,(bc); ld c,a; ld a,c` (`DrawTileHexCell`, `$06:$7553`): the pointer
read went through the register pair that receives the result. It is the only
overlapping case in the ROM, so the rule file protects exactly that shape
(rewriting the reload as `LD a,c`, same bytes, invisible to the case-sensitive
matcher). One data point — treat it as a hypothesis until a second one turns up.

Where the rules come from: SDCC's rule files are plain text, and
[juju.nz/src/juju/gbdk-releases](https://juju.nz/src/juju/gbdk-releases) has
the full GBDK+SDCC source for every release tag (2.91-2.96a). The flag-op and
register-reload rules are in `sdcc/src/z80/peeph.def` at `gbdk-2.96a` (not at
2.95/2.95-2); 2.96a also removes `(hl)` reloads generically, which LSDj does
not (46 survive), so LSDj's set is its own. The dead-store and reload-after-ALU
behaviour looks less like text rules and more like the code generator tracking
what `a` holds; the textual rules reproduce it for the shapes seen so far.
Expect `lsdj.peep` to keep growing: `DrawMessageBar` needed 6 rules,
`DrawTileHexCell` added 10, `ReadInstrByte` 2, `DrawSyncField` 11 (ten of
them the compare-chain positions), the other three none.

Two limits of SDCC 2.2.1's own rule matcher shaped how the rules are applied:
a `%n` variable never spans a comma (`cp %1` cannot match `cp a,#1`), and a
source line is compared *with* its trailing comment (`ld a,(hl) ; 2`), which
no rule can name because `;` opens a comment in the rule file. That is one
more sign LSDj's compiler did this in its code generator, not in text rules.
The harness therefore applies `lsdj.peep` twice: once inside SDCC
(`--peep-file`) and once afterwards with our own matcher
(`tools/match/peep.py`, same syntax, comments stripped).

## Compiler versions checked

| Build | SDCC | Result |
|-------|------|--------|
| **GBDK 2.95-3 Win32** (SourceForge `gbdk-win32/2.95-3`, 2000-08-19) — `tools/toolchain/` | 2.2.1 | small model by default: direct `call`s, arguments at `sp+4`, exactly LSDj's convention; with the recovered rules → **all six match** |
| GBDK 2.95 Win32 (rbong mirror, July 2000) | 2.2.1 | same code generator, but every function must be declared `nonbanked` or it is compiled for `banked_call` (arguments at `sp+8`, callees through a trampoline); 2.95-3's `--model-medium` reproduces this |
| GBDK 2.96a Linux i586 (SourceForge `gbdk/2.96`, 2001-11-04; runs natively with 32-bit glibc) | 2.3.1 | **different codegen lineage**: no `push bc` frame, byte args pushed as 16-bit pairs, `add hl,de` indexing → ruled out, though its peephole file overlaps ours. SFRs must be written `extern volatile sfr NAME;` (`sfr` is the whole type) |

So LSDj's compiler is **GBDK 2.95-3's SDCC 2.2.1 + extra peephole rules** —
consistent with LSDj's own "modified GBDK 2.95", and with LSDj's first
releases (2001) postdating 2.95-3.

## What is C and what is asm

`tools/match/code_kind.py` tags every named function by compiler fingerprints
(SDCC addresses locals/arguments with `ld hl,sp+N` / `add sp,N` and branches
with `jp cc`; hand-written code uses `jr` freely and `add a,l; ld l,a` address
arithmetic):

| | C | ambiguous leaf | asm | tiny (<6 instr.) | asm share of instructions |
|---|--:|--:|--:|--:|--:|
| bank `$00` | 90 | 55 | 50 | 109 | 22% |
| bank `$02` (sound engine) | 29 | 54 | 57 | 17 | 39% |
| editor banks `$01`, `$03`-`$07`, `$1B`-`$1F` | 559 | 40 | 25 | 410 | 0-8% |
| **all** | **678** | **149** | **132** | **536** | **7%** |

The asm is where you'd expect it: `TimerHandler` and the sound tick, the
pitch/vibrato/envelope steppers, the tile loader, `ReadJoypad`, the OAM DMA
stub, GBDK library routines, and bank `$02`'s note/period/sequencer core
(`NoteToPeriod`, `WriteChannelFreq`, …). Those are kept as asm, pret-style.

## Writing C that matches (what we learned)

- **Bytes stored with `ldh`** (HRAM/IO) are `extern volatile sfr UBYTE NAME;`
  (GBDK 2.95's `USE_SFR_FOR_REG` style). `*(UBYTE *)0xFFxx = v` compiles to
  `ld de,#0xFFxx; ld a,v; ld (de),a` instead.
- **SDCC 2.2.1 crashes** on `if (*(UBYTE *)0xFFxx)` (2.3.1 segfaults too).
  Bytes LSDj *tests* through `ld hl,$FFxx; or a,(hl)` (`hGameboyType`) are
  plain `extern` variables placed there at link time.
- **MBC registers are variables**: `rROMB0 = n` gives the ROM's
  `ld hl,#0x2000; ld (hl),#n`; a literal-pointer store goes through `de`.
  LSDj's bank switch is `hCurrentBank = n; rROMB0 = n`.
- **Empty `case N: break;`** produce their own `jp end` trampolines in source
  order — the ROM has them, so spell the empty cases out.
- **16-bit shifts need the promotion**: `(unsigned int)instr << 4` is the
  `rl c / rl b` loop; `instr << 4` alone is shifted in 8 bits.
- **Statement shape is codegen shape**: `t = v + 1; t >>= 2; t -= 9;` (one
  store, thanks to the dead-store rules) vs `t = ((v+1) >> 2) - 9` (shift in
  `b`); `if (!x)` / `if (t & 0x80)` test in place, `x == 0` / `t < 0` go
  through a `jp`/`jr` trampoline or an `xor #0x80; cp` sequence.
- `&array[0]` (or a cast) loads the address into `de` first, as LSDj does;
  plain `array` stores the two immediate bytes directly.
- Byte arguments are pushed right-to-left with `push af; inc sp`; a 16-bit
  argument with `push hl`. Return values come back in `e`.
- With 2.95-3 no `nonbanked` is needed; with plain 2.95 every definition and
  prototype needs it.

## How to run it

```sh
# 1. Fetch the period toolchain into ./toolchain (git-ignored). Needs curl, unzip, wine.
tools/toolchain/fetch_gbdk295.sh

# 2. Confirm the compiler runs.
tools/toolchain/sdcc295 --version            # -> SDCC : gbz80/z80 2.2.1

# 3. Compile C and compare against the ROM.
make match                                                     # all of c/
tools/match/match_function.py --c FILE --target NAME           # one function
tools/match/match_function.py --c FILE --target NAME --show-asm --dump out.bin
tools/match/match_function.py --c tools/match/examples/add8.c --func add8 --search
```

The harness keeps a `wineserver` alive for 60 s after a run, so a batch or a
sequence of single runs costs ~2-3 s each. Note that Wine's session services
(`services.exe`, `explorer.exe`, …) are spawned by the first client and inherit
its stdout/stderr: capturing a Wine client's output through a pipe blocks until
they exit — forever with a persistent server. The harness therefore never
captures Wine output through pipes (it uses files); keep that in mind if you
script the tools yourself.

### Wine quirks (handled by the wrappers)

- `sdcpp`/`as-gbz80` must be discoverable → the bin dir is added via `WINEPATH`.
- SDCC 2.2.1 writes its temp file at the **current drive's root**, so the tools
  must run from Wine's writable `C:` drive, or you get
  `sdcpp: \s??.: undocumented I/O error`. The wrappers `cd` into `drive_c`.
- File arguments are resolved to absolute paths before the directory switch.
- A crashing `sdcc.exe` otherwise hangs in `winedbg`; the wrappers set
  `WINEDLLOVERRIDES=winedbg.exe=d` (and the harness a timeout) so it fails fast.
- `sdcc -S` also assembles, leaving `.lst/.o/.sym/.cdb` beside the input; the
  harness works in a temp dir.

## How the match harness works

`tools/match/match_function.py`:

```
file.c --[sdcc -mgbz80 -S --peep-file lsdj.peep]--> file.asm
       --[tools/match/peep.py lsdj.peep]--> file.asm  (rules SDCC's matcher can't apply)
       --[as-gbz80 -l -o]--> file.o + file.lst
--target NAME|BB:AAAA:
       --[link-gbz80 -i -b_CODE=<addr> -b_HOME=<addr> -g_<sym>=<addr> ...]--> file.ihx
       -> bytes between ___<func>_start / ___<func>_end, diffed against the ROM
without --target (leaf functions only, no relocations):
       -> diff vs ROM@offset   (--offset)  or scan the whole ROM (--search)
```

Every extern is resolved from GBDK's `libc/gb/sfr.s` register names,
`include/hardware.inc` (`rROMB0` …), `constants/ram.inc`, the working-song
SRAM structures (`SramInstruments` …, from `tools/gen_sym.py`) and
`src/bank*.sym` (a note lists callees that live in other ROM banks);
`--define NAME=ADDR` adds or overrides one. On a mismatch it prints the first
differing bytes and the listing lines around them; `--dump` writes the linked
bytes for a full diff. `tools/match/verify_all.py` (`make match`) runs every
function defined under `c/` the same way.

## How matched C enters the build

`make match` (`tools/match/verify_all.py --write build/c`) compiles every
`c/bankNN/*.c`, links each function at its ROM address, and on a match writes
the bytes to `build/c/bankNN/Name.bin` and records the function in the
tracked manifest `c/matched.json` (bank, address, length, source). The carve
(`tools/carve_clean.py` → `tools/disasm.py --incbin`) then renders each such
range not as instructions but as `INCBIN` slices of the compiled bytes — split
at every symbol inside the range, so `ScreenDrawTable`, `.loop` and friends
keep their labels and still reach the debugger `.sym`. With Wine and the
toolchain present, `src/main.asm` depends on those bytes: change a C file and
`make compare` recompiles it, re-carves, and fails if it no longer matches.
Without Wine the same ranges `INCBIN` the ROM itself, so `compare` works
everywhere. `make listing` disassembles the slices back inline.

This is the pret loop for this project: asm functions are replaced by
matching C one at a time while `make compare` stays green.

## What's still unknown

1. **The complete rule set.** 28 rules from six functions; the last two
   functions each added some, so more will surface. The reload-after-ALU and
   dead-store families are probably one codegen mechanism (tracking what `a`
   holds) that we model textually; the overlapping-pointer exception rests on
   one site.
2. **Flags.** The default `lcc` compile is `sdcc -mgbz80 --c1mode` after a
   separate `sdcpp` pass (`-DGB=1 -DGAMEBOY=1 -DINT_16_BITS …`); nothing so far
   needed more.
3. **Whole translation units.** Per-function matching links one function at
   its address. Reproducing LSDj's original object/link layout (function
   order within a bank, string literals and `const` tables after their
   functions, as `DrawTileHexCell`'s table shows) is only needed to build
   banks from C wholesale.
4. **Building whole banks from C.** Solved per function (below); building a
   bank from C wholesale would additionally need LSDj's link order.

## Next steps

- [x] Link step in the harness; `DrawMessageBar` matched.
- [x] Validate on harder functions: a `switch` (`ScreenDrawDispatch`), 16-bit
      math + locals + asserts (`ReadInstrByte`), the register-reload exception
      (`DrawTileHexCell`) — all matched; rule set 6 → 28.
- [x] `make match` regression suite; `code_kind.py` C/asm map.
- [x] Matched C feeds the ROM build (`INCBIN` of the compiled bytes, ROM slice
      without Wine; `make compare` is the gate).
- [x] A larger, branchier function: `DrawSyncField` (262 B).
- [ ] A screen input handler (300+ instructions) to shake out more rules in one
      go; then generalise the accumulator-tracking rules into one pass if the
      per-shape rules keep multiplying.
- [ ] Grow the C tree bank by bank (editor code first; the asm functions
      stay asm).

## References

- GBDK binaries: <https://sourceforge.net/projects/gbdk/files/> (`gbdk-win32/2.95-3/`, `gbdk/2.96/`)
- GBDK releases with SDCC source per tag: <https://juju.nz/src/juju/gbdk-releases>
- GBDK 2.95 mirror (with binaries): <https://github.com/rbong/gbdk> (`gbdk-2.95/`)
- SDCC: <https://sdcc.sourceforge.net/>
- Matching-decomp methodology (C via original compiler): pret `pokeemerald`.
