# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95-3** (19 Aug 2000) → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker, small memory model — **plus an accumulator-tracking optimisation stock SDCC lacks** (the "modified GBDK"), modelled here as an accumulator-tracking pass plus 68 peephole rules (below) |
| Can we run it today? | **Yes, natively.** SDCC 2.2.1 + `sdcpp`/`as-gbz80`/`link-gbz80` build from the GBDK 2.95-2 sources (`tools/toolchain/build_sdcc221.sh`, ~15 s) and emit **byte-identical** asm, objects, listings and images to GBDK 2.95-3's Win32 binaries — which still run under Wine (`fetch_gbdk295.sh`), but crash on large units (below) |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock output does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95-3 lacks |
| Matched real LSDj functions? | **Yes — 265**, in eleven banks (the list: [`c/matched.json`](../c/matched.json)): a message renderer, a 15-case `switch`, a sparse `switch`, the SRAM accessors, and the instrument screen's field accessors and draw helpers. `make match` re-verifies them all (cached: only changed sources reach the compiler), and **their bytes in the ROM build come from the compiled C** (below) |
| Is all of LSDj C? | **No.** ~130 functions (7% of the instructions) are hand-written assembly — the interrupt handlers, sound tick, envelope/pitch and tile-loader code in bank 0, and the sequencer core in bank `$02` (`tools/match/code_kind.py`). Those stay asm; the ~830 others (every editor) are Track 2 targets |
| Go/no-go on Track 2? | **Go, with the rule set still growing.** Every function tried has matched (each new one has cost a few rules so far), the C feeds the ROM build with `make compare` as the gate, and the hand-written asm (7%) is mapped and excluded |

## Matched functions

| Source | Functions | What they exercise |
|--------|-----------|--------------------|
| [`c/bank00/InstrByte.c`](../c/bank00/InstrByte.c) | `WriteInstrByte`, `ReadInstrByte`, `ReadCurInstrByte` | byte args, pointer local, 16-bit shift loop, asserts (one compound), both forms of pointer + byte |
| [`c/bank00/ReadSeqByte.c`](../c/bank00/ReadSeqByte.c) | `ReadSeqByte` | `?:` on bit 7 of a call result |
| [`c/bank00/ScreenDrawDispatch.c`](../c/bank00/ScreenDrawDispatch.c) | `ScreenDrawDispatch` (252 B) | 15-case `switch` → `jp hl` table, bank switching, cross-bank calls |
| [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) | `DrawMessageBar` (198 B) | globals, `sfr` HRAM stores, two loops, calls |
| [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) | `ShowOutOfMemory`, `ShowTapAddChain` | one-line wrappers, 16-bit argument |
| [`c/bank03/InstrScreen.c`](../c/bank03/InstrScreen.c) | 62 functions of the instrument screen | field get/set/step helpers, `FarCall` in both address forms, SRAM literal addresses, a string literal placed after its function, `hl` kept across a `!=` compare |
| [`c/bank1c/FileUpgrade.c`](../c/bank1c/FileUpgrade.c) | 28 functions: the song-format upgrades | per-type instrument callbacks through a function pointer (`FileForEachInstrType`), the upgrade steps' field fix-ups, a 16-bit `\|` forced by an `int` constant |
| [`c/bank1d/SynthScreen.c`](../c/bank1d/SynthScreen.c) | 20 functions: synth and groove screens | a `switch` through a jump table, SRAM param/groove accessors, a `?:` over `long` constants (> 32767), the cache walker |
| [`c/bank1f/InstrHelpers.c`](../c/bank1f/InstrHelpers.c) | 42 functions: bank $1F helpers | the used-table/phrase/chain scans and counters (one template, four limits), SRAM resets bracketed by `_asm di _endasm;` and MBC register writes, a function that returns `e` by accident (no `return`) |
| [`c/bank04/PhraseScreen.c`](../c/bank04/PhraseScreen.c) | 21 functions: phrase screen | far-call wrappers (`FarCallArg2V`), the phrase instrument reader and the backward search for the instrument in effect (a return through `e`, `track_e`), `?:` returns |
| [`c/bank05/TableScreen.c`](../c/bank05/TableScreen.c) | 20 functions: table screen and screen map | table step accessors through column pointers held in `UWORD` globals, the map moves, materialized `<=` tests, a two-case `switch` |
| [`c/bank06/Screens.c`](../c/bank06/Screens.c) | 22 functions: chain, wave and word screens | an early `return` under `==` (SDCC's `jp nz,L; jp end; L:`), the chain row buffers through `ReadSongBytes`, `?:` over string literals as an argument |
| [`c/bank06/DrawTileHexCell.c`](../c/bank06/DrawTileHexCell.c) | `DrawTileHexCell` (130 B) | consecutive updates of a stack local, sign test, `-t`, pointer table |
| [`c/bank07/SongScreen.c`](../c/bank07/SongScreen.c) | 20 functions: song screen and bookmarks | page-up/down scroll arithmetic, bookmark add/toggle in SRAM, `WriteBookmarkByte`'s pointer-range asserts (16-bit compares: `ld hl,#-lit; add hl,de`), a `!=` sprite loop |
| [`c/bank1b/ProjectScreen.c`](../c/bank1b/ProjectScreen.c) | 20 functions: project and help screens | property writes to SRAM, a busy-wait on a global, `(UWORD)x == 9` (a 16-bit compare: one `jp nz` per byte through one trampoline), `?:` string arguments, a NULL-terminated pointer table |
| [`c/bank1b/DrawSyncField.c`](../c/bank1b/DrawSyncField.c) | `DrawSyncField` (262 B) | sparse `switch` (`cp` chain), if/else, a pointer local chosen by a condition, 4-arg calls |

Shared prototypes, variables and the SRAM layout live in
[`c/include/lsdj.h`](../c/include/lsdj.h); a signature is part of the match, so
it is settled there once.

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
| Compare chains | a sparse `switch` re-reads its global before every `cp` in stock output; LSDj loads it once (one rule per chain position, so the pattern always carries the variable). SDCC's `==` trampoline `jp cc,T; jr F; T: jp X; F:` → `jp cc,X`, every byte's `jp cc,T` of a multi-byte compare included (the only survivor in the ROM is inside GBDK's stock-compiled `gprintf`) | 0, 1 (library) | 72 |
| Kept across a test | `ld a,r` / `bit n,a` / `jp cc` / `ld a,r` → reload dropped; `ld hl,#x` / `ld a,(hl)` / `cp a,#n` / `jp z,F` / *(dead label)* / `ld hl,#x` → reload dropped (a `!=` test; stock SDCC's Rule 5 leaves the trampoline's label behind, which makes its codegen forget `hl`); `xor a` / `ld a,#0` → `xor a` | 0, 1 (a real jump target in between), 0 | 68, 7, 6 |
| Register tracking (`track_a`, below) | a reload of what `a` already holds: `ld a,r` / `cp` / `jp cc` / *(dead label)* / `ld a,r` (an `\|\|` chain) → reload dropped; a stored stack local reread: `ld hl,sp+N` / `ld (hl),r` / [`add sp,M`] / `ld a,(hl)` → `ld a,r`; `ld (hl),a` / `ld de,#T` / `ld l,(hl)` → `ld l,a` (indexing a table by the value just stored) | 0, 0, 0 | 35, 25 + 141, 32 |
| More dead stores | `ld r,#n` / `ld r,X` → `ld r,X`; a discarded sign extension `rla` / `sbc a,a` / `ld r,a` / `ld a,X` / `and a,#n` → store dropped; the `== 0` test of a fresh `&`: `and a,#n` / `ld r,a` / `or a,a` / `jp nz` → `and a,#n` / `jp nz`; after *ALU* `a,r` / `ld (hl),a` the `ld a,(hl)` reload is dropped too | 0, 0, 0 (1 `jp z` / `if (t)` site keeps it), 0 | —, 3, 12, 69 |
| 16-bit compare with a constant (`gb_cmp16`, below) | `xor a,a` / `ld a,lo` / `sbc a,#lo` / `ld a,hi` / `sbc a,#hi` / `jp cc` → `ld hl,#-lit` / `add hl,de` (or `bc` when the value already sits there) / `jp` on the inverted carry | 0 (its 5 `sbc` chains are 32-bit) | 28 |

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
them the compare-chain positions); the other six needed none.

Two more passes follow `track_a`. It also drops `ld a,(hl)` right after an
ALU result was stored through `hl` (the "reload of a computed value" family,
now also across `dec hl`/`inc hl`; a value from a pointer read keeps its
reload, 30/30). `dead_stores` removes `ld r,a` that a later `ld r,a`
rewrites, within a basic block, before anything reads r: none of the 1999
`ld r,a` in the ROM's compiled code is such a store, while stock SDCC
emits them (the low byte of a 16-bit `&` rewritten by the `|` after it).
Stores killed any other way (`pop bc`, `ld r,#n`, or across a call or a
branch) survive in the ROM, so the pass stops there.

`track_e` does the same for `e`, which carries call results and return
values: after `ld c,e` (a result kept in `c`) a later `ld e,c` (returning
it) is dropped while neither register changed and no jump target
intervened. Of the 740 `ld r,e` copies in the ROM's compiled code, no
redundant `ld e,r` follows one (`FindCursorInstr` returns its value so).

One family is code generation rather than a peephole: LSDj's compiler
compares a 16-bit value with a constant by adding the negated constant
(`p < 0xAFF0` is `ld hl,#0x5010; add hl,de; jp c,skip`), the Game Boy
special case later SDCC releases have in `genCmp`, where SDCC 2.2.1
subtracts byte by byte. `gb_cmp16` rewrites the stock form before the rules
run: a stack operand is fetched into `de`, a value already in `bc` or `de`
is used in place.

Most of these are one mechanism — the code generator knowing what `a` holds
— so besides the textual rules `peep.py` runs `track_a` over the result: it
follows the registers and constants `a` equals (`ld a,c`, `ld c,a`,
`ld a,#5`, `xor a`), drops a later load of the same value (and `xor a,a`
before `or a,r` when `a` already equals `r`), and forgets at anything that
changes `a` or the source, at calls and jumps, and at every label that
SDCC's *own* output jumps to. Labels its stock peephole rules left dead
don't count — that is exactly where LSDj's compiler kept tracking and stock
SDCC reloads. (Local `nnnnn$` labels are scoped per function in asxxxx and
SDCC restarts their numbering per function, so references are resolved per
scope.) The textual rules remain for the shapes they name.

The `==`/`!=` asymmetry is telling: for `if (x == N) x--;` the ROM keeps the
`hl` reload (17 sites), for `if (x != N) x++;` it drops it (7 sites; the one
`jp z` site that keeps it has a real jump target in between). Stock SDCC
reloads in both; the rule needs `if labelRefCount %5 0` (later SDCC's
syntax), which `peep.py` implements.

SDCC 2.2.1's own rule matcher can't apply these rules: a `%n` variable never
spans a comma (`cp %1` cannot match `cp a,#1`), a source line is compared
*with* its trailing comment (`ld a,(hl) ; 2`), which no rule can name because
`;` opens a comment in the rule file, and with rules whose mnemonic is a
variable (`%1 a`) loaded it crashes (exit 5) on larger units. That is one
more sign LSDj's compiler did this in its code generator, not in text rules.
The harness therefore compiles with SDCC's stock rules and applies
`lsdj.peep` afterwards with its own matcher (`tools/match/peep.py`, same
syntax, comments stripped).

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
  `rl c / rl b` loop; `instr << 4` alone is shifted in 8 bits. Likewise
  `p + field` and `p + (unsigned int)field` are different code (hl/de add vs
  the byte extended into bc first); the ROM uses both.
- **Zero tests**: `x == 0` on a stack byte is `ld a,(hl); or a,a`, `!x` is
  `xor a; or a,(hl)`; on a pointer target `!*p` is direct and `*p == 0` goes
  through a trampoline.
- **Addresses computed as integers**: `UWORD p = (UWORD)(SramPhraseFx +
  (unsigned int)row); … p += (UWORD)phrase << 4; ReadSongByte(2, (UBYTE *)p)`
  keeps the sum in `bc` and stores it (a cast is never packed into its
  destination) and pushes a `bc` copy; with `UBYTE *p` the sum goes straight
  into `p`'s stack slot (`ld hl,#base; add hl,bc`). The phrase accessors of
  bank `$04` use both (`PhraseReadFx` vs `WritePhraseInstrument`).
- **Addresses held in integers**: `extern UWORD wTableCellAPtr;` copied
  with `p = (UBYTE *)wTableCellAPtr;` goes byte by byte through `a`
  (`genCast`); a pointer-typed global copies as `ld a,(hl+); ld e,(hl)`
  (`genAssign`'s two-byte case). The ROM has both (`TableReadCmd`).
- **A comparison kept as a value**: `ok = v <= 0x13; if (!ok) …` stores the
  0/1 (`ld a,#0; rla`) and its negation before testing it; the same test
  written inside the `if` compiles to a plain `cp` / `jp` (`DrawCmdLetter`).
- **Spill victims**: when registers run out, SDCC 2.2.1 spills a *named*
  local (it has a spill location) before any unnamed temporary, and among
  named ones the least used (a use inside a loop counts 3, one in a block
  that leaves the loop counts 1). So a value the ROM keeps in a register
  while a named local goes to the stack was an expression in the source:
  `if (IsBookmarked(i + wSongRow)) wSongRow = i + wSongRow;` (one CSE'd
  temporary; `SongFindNextRow`), `DrawTextField(…, c && hGameboyType)`
  (`ChainDrawCellLo`).
- **A repeated expression is one temporary**: `for (i = 0; T[i]; i++)
  DrawString(T[i]);` loads `T[i]` once; SDCC keeps it in `bc` and spills `i`
  to the stack. Naming it (`s = T[i]`) makes `s` the spilled one
  (`DrawHelpMenu`).
- **An `if` whose body is only a jump** keeps SDCC's two-jump form:
  `if (x == 15) return;` is `cp; jp nz,L; jp end; L:` (50 such pairs in the C
  banks), while `if (x != 15) { … }` is a single `jp z,end`
  (`DrawWaveCellPair`).
- **Statement shape is codegen shape**: `t = v + 1; t >>= 2; t -= 9;` (one
  store, thanks to the dead-store rules) vs `t = ((v+1) >> 2) - 9` (shift in
  `b`); `if (!x)` / `if (t & 0x80)` test in place, `x == 0` / `t < 0` go
  through a `jp`/`jr` trampoline or an `xor #0x80; cp` sequence.
- `&array[0]` (or a cast) loads the address into `de` first, as LSDj does;
  plain `array` stores the two immediate bytes directly.
- Byte arguments are pushed right-to-left with `push af; inc sp`; a 16-bit
  argument with `push hl`. Return values come back in `e`.
- **SRAM is addressed with numbers.** `(UBYTE *)0xA040 + (UWORD)i` computes
  `i` first and adds the constant (`ld a,c; add a,$40; …; adc a,$a0`): SDCC
  swaps a *literal* left operand of `+` to the right, but an extern's address
  stays left (`ld a,#<sym; add a,c`). So the `SramXxx` names in `lsdj.h` are
  literal-pointer macros, not linker symbols. `(UWORD)i + ptr` does not help:
  pointer arithmetic is canonicalised to `ptr + int`.
- **Banked calls** (`FarCall(bank, fn)`, `FarCallArg(bank, fn, arg)`): passing
  the function loads it with `ld bc,#fn`; some call sites instead load it
  byte-wise (`ld c,#<fn; ld b,#>fn`), which is a cast through an integer
  (`FAR_FN(fn)` in `lsdj.h`). FarCallArg's address parameter is a `UWORD`
  (`(UWORD)fn` at its call sites). A cast to another pointer type, `banked`
  or `nonbanked` all fold away.
- **String literals** are emitted right after the function that uses them, as
  in the ROM (`DrawInstrByte11`'s `"--"` at `$03:$49E4`).
- **One operation per statement** is how LSDj's source reads: `v = 0x3F - v;
  v &= 0x3F;` compiles to `sub; and; ld c,a` (the dead-store rule removes the
  first store), `v = (0x3F - v) & 0x3F` to `sub; ld b,a; and; ld c,a` — stock
  SDCC 2.2.1's accumulator packing (`packRegsForAccUse2`) never covers `-`.
- **Arguments are evaluated right to left**, so writing a lookup inline in a
  call (`DrawTextField(X[t][f], Y[t][f], W[t][f], gbc ? 3 : 2)`) orders and
  spills the temporaries differently from computing them into locals first;
  the ROM has both.
- **Per-type tables are 2-D arrays**: `InstrFieldX[type][field]` with
  `extern UBYTE InstrFieldX[][18]` gives the ROM's `__muluchar` (`type*18`,
  computed once and reused across tables) plus a row pointer; LSDj's tables
  are 67 bytes (the NOISE row has 13 fields), so they are declared, not
  defined, in the C. SDCC's runtime helpers (`__muluchar`, `__divuint`, …)
  resolve to our names for those GBDK routines (`MulU8`, `DivU16`, …).
- **Plain `char` locals are signed**, and LSDj's source uses them: reading a
  byte into a `char` and testing or masking it compiles to a copy first
  (`ld b,c; ld a,b; bit 5,a` — the implicit conversion), which `UBYTE` does
  not produce. `DrawInstrTableField`'s `char v` is the first confirmed case;
  the same copy shows in several parked functions.
- **Constants above 127 are `int`s**: `v & 0xF0` feeding another operation
  is done in 16 bits (`and a,$f0; ld b,$00`), which is why
  `FileUpgradeRemapBits` builds its result in a 16-bit temporary. Assigned
  straight back to a byte (`t = v & 0xF0`) it stays 8-bit.
- **Calling through a function pointer** needs a K&R pointer type:
  `void (*fn)()`. SDCC 2.2.1 counts the `void` of `void (*fn)(void)` as a
  parameter and rejects the call ("to few parameters"). Callers passing
  `void f(void)` functions compile the same either way.
- **Loop invariants are hoisted** (stock SDCC; LSDj built without
  `--noinvariant`): `FileForEachInstrType` copies its function pointer to a
  stack temporary before the loop, which `--noinvariant` would not.
- **Inline assembly** is SDCC 2.2.1's `_asm ... _endasm;` (the `di`/`ei`
  around SRAM access); the MBC registers are variables (`rRAMG = 0x0A`,
  `rRAMB = 3`), like `rROMB0`.
- **Missing `return`s are real**: `HasEnterableColLeft/Right` are declared to
  return a byte but only `break` out of their loop; callers read whatever
  the last call left in `e`. SDCC 2.2.1 compiles that without complaint.
- **A prototype may be declared only once per unit**: SDCC 2.2.1 reports a
  repeated identical prototype as `Duplicate symbol` (an error). A function
  declared in `lsdj.h` and defined in the unit is fine.
- With 2.95-3 no `nonbanked` is needed; with plain 2.95 every definition and
  prototype needs it.

## How to run it

```sh
# 1. Build the period compiler natively into ./toolchain (git-ignored).
#    Needs git, gcc with -m32 support, bison, flex, make.
tools/toolchain/build_sdcc221.sh
toolchain/sdcc-2.2.1-native/bin/sdcc --version    # -> SDCC : mcs51/gbz80/z80/avr 2.2.1 (UNIX)

# 2. Compile C and compare against the ROM.
make match                                                     # all of c/ (cached)
tools/match/verify_all.py /tmp/wip.c                           # a work-in-progress batch
tools/match/match_function.py --c FILE --target NAME --diff    # one function, aligned diff
tools/match/match_function.py --c FILE --target NAME --show-asm --dump out.bin
```

### The native build, and why

GBDK 2.95-3 only ever shipped as Win32 binaries, but its compiler is SDCC
2.2.1 — the version in the GBDK 2.95-2 source release
([juju.nz/src/juju/gbdk-releases](https://juju.nz/src/juju/gbdk-releases), tag
`gbdk-2.95-2`). `build_sdcc221.sh` builds it 32-bit with the source's own
`_NO_GC` switch and a handful of host-portability fixes (modern flex, headers
the 2000-era code got implicitly); nothing in the code generator changes.
Checked on every unit Wine could compile: identical `.asm` (down to the stock
peephole comments), identical objects and listings from `as-gbz80`, identical
linked images from `link-gbz80`.

The Win32 `sdcc.exe` is linked with an old Boehm GC, and under Wine it frees
live objects once a unit allocates enough: some `.globl` names come out as
garbage bytes, and from ~30 functions on it dies with NULL-pointer faults
(exit 5; `eip` inside the GC's `free` and elsewhere) whose onset depends on
the heap layout — adding 600 unused prototypes crashes a unit, 1500 do not.
Without the GC (whose allocations are zeroed — SDCC relies on that, so
`_NO_GC` maps them to `calloc`) the native build has none of this, and
compiles a 46-function unit in ~10 ms. The harness uses it whenever it is
present (`LSDJ_USE_WINE=1` forces the Win32 tools, which still work for
small units: `tools/toolchain/fetch_gbdk295.sh`, `tools/toolchain/sdcc295`).

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
- Wine's session services inherit a client's stdout/stderr: capturing its
  output through a pipe blocks until they exit (forever with a persistent
  `wineserver`), so the harness writes tool output to files.

## How the match harness works

`tools/match/match_function.py`:

```
file.c --[sdcc -mgbz80 -S]--> file.asm           (stock rules)
       --[tools/match/peep.py: lsdj.peep, then accumulator tracking]--> file.asm
       --[as-gbz80 -l -o]--> file.o + file.lst
--target NAME|BB:AAAA:
       --[link-gbz80 -i -b_CODE=<addr> -b_HOME=<addr> -g_<sym>=<addr> ...]--> file.ihx
       -> bytes between ___<func>_start / ___<func>_end, diffed against the ROM
without --target (leaf functions only, no relocations):
       -> diff vs ROM@offset   (--offset)  or scan the whole ROM (--search)
```

Every extern is resolved from GBDK's `libc/gb/sfr.s` register names,
`include/hardware.inc` (`rROMB0` …), `constants/ram.inc` and `src/bank*.sym`
(a note lists callees that live in other ROM banks); `--define NAME=ADDR` adds
or overrides one, and a header extern the unit never references needn't
resolve. A unit may define several functions: each is linked at its own
address, and calls between them are *isolated* (the neighbours' definitions
renamed away, so the calls link to their ROM addresses). On a mismatch it
prints the first differing bytes and the listing lines around them; `--diff`
prints both disassemblies aligned, `--dump` writes the linked bytes.

`tools/match/verify_all.py` (`make match`) runs every function defined under
`c/` the same way, caching compiler output by source/rules/header hash and
linked bytes by address and extern values under `build/match-cache/`; given
file arguments (work in progress may live outside `c/`) it checks just those
and prints where each mismatch starts. `tools/match/shapes.py` lists the
not-yet-matched C functions by size or groups them by instruction shape.

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

## Parked functions

Functions tried and set aside because no source spelling reproduced them yet;
each note says what differs. Revisit when another function shows the same
shape.

| Function | Where | What differs |
|----------|-------|--------------|
| `SetInstrFlags54` | `$03:$4B5E` | the `ld b,c; ld a,b` copy is a `char` local (as DrawInstrTableField); then `and a,$f8; ld c,a ... or a,c`: the AND result must stay in c, ours packs it into a (the or-a,a bug shape) |
| `SetInstrLen2` | `$03:$441A` | synth clone, 19-byte frame: widened (UWORD) copies of i/so spilled, long `\| 0xA000`; not LICM (FileForEachInstrType proves hoisting on) |
| `FileUpgradeV3Pulse` | `$1C:$628B` | same `ld b,c; ld a,b; and; or #n; ld c,a` tail as V3Kit |
| `FileUpgradeV3Kit` | `$1C:$62D3` | with `(char)v` casts all but one byte matches: the tail's `and a,$f9` result stays in a for `\| 1` in the ROM, ours stores it to b (stock packing refuses a literal operand) |
| `FileUpgradeV0Kit` | `$1C:$6134` | all 5 byte locals + a 16-bit temp on the stack (frame 7); ours keeps 2 in registers, 8-bit clear (~0x20 folds) |
| `FileUpgradeV2` | `$1C:$622D` | only an `ld hl,sp+0` reload after `or a,a; jp z` (&& chain on a named local): ROM keeps hl there, but reloads in switch chains |
| `FileCheckByte6973` | `$1C:$6973` | pointer param copied to a local (frame 3), deref via de; ours via bc |
| `ClearTableRow` | `$1F:$5183` | pointer param copied to b,c before the call, then incremented in place on the stack after it; p++ / post-increment / a local copy don't give that |
| `GetInstrFlags1F5` | `$1F:$40CA` | `v = f() & 0x80; if (v)` (ROM: and; ld c,a; or a,c; jp z) makes SDCC 2.2.1 assert (gen.c 3215, genAnd single-bit + ifx); no crash-free spelling found yet |
| `MarkPhraseDataUsed` | `$1F:$5F20` | frame 3 with i at sp+0 and p at sp+1, call first, p copied to b,c for the push; ours keeps i in c (call-first) or evaluates the table first |
| `GrooveDrawField` | `$1D:$40AE` | `(wGrooveRow & 2) ? 0 : 1`: stock SDCC copies the global (`ld c,(hl); ld a,c; bit 1,a`), the ROM tests it directly (`ld a,(hl); bit`, 34 global sites vs 7 copies); opPreservesA-accepts-literals variant breaks 10 matched functions |
| `IsBookmarked` | `$07:$6320` | ROM spills i (sp+2) and the pointer (sp+0), then pushes the pointer through a b/c copy (low byte in b): a real conversion temporary at the call, yet the sum itself is pointer arithmetic (i fetched straight into de); char*/UWORD pointers, casts and CSE'd forms tried |
| `RemoveBookmark` | `$07:$63B5` | same spill shape as IsBookmarked without the copy: i at sp+2, pointer at sp+0, the read byte in c; ours keeps i in c |

## What's still unknown

1. **Build options.** Every function so far matches with and without
   `--noinvariant` / `--noinduction`; `SetInstrLen2` (`$03:$441A`, the synth
   clone) recomputes a loop-invariant address every iteration, which stock
   SDCC hoists — either LSDj's build disabled loop-invariant motion or the
   source spelled it differently. It is parked until one or the other shows.
2. **The complete compiler model.** An accumulator-tracking pass plus 68
   rules so far; most new functions need nothing new, but each new idiom can
   surface a rule. One function so far (`SetInstrFlags54`, `$03:$4B5E`)
   points at a register-allocation difference no rule can express: stock
   SDCC keeps the result of an `&` in `a` for the `|` that follows and then
   clobbers it (wrong code), LSDj's compiler keeps it in `c`. Not a fixed
   bug: `InstrTableNew` (`$03:$436D`) ships exactly that miscompilation
   (`and a,$d0; ld a,c; or a,a`), so it is a source-shape question. The reload-after-ALU and
   dead-store families are probably one codegen mechanism (tracking what `a`
   holds) that we model textually; the overlapping-pointer exception rests on
   one site.
   Spill choices were the other open front: in four loops of bank `$07`
   (`IsBookmarked`, `RemoveBookmark`, `SongFindNextRow`/`SongFindPrevRow`)
   LSDj's compiler spills the loop counter where our first C spilled the
   other value. For the two row finders the source repeats the expression
   instead of naming it (see Writing C: spill victims); the bookmark pair is
   still open. 2.2.1 spills the least `used` live range, a use counting
   `2^depth + 1`, and a use in a block that leaves the loop by `return` counts
   1, which is what decides these (`--dumpregassign` shows it). GBDK 2.96a's
   SDCC 2.3.1 is not LSDj's compiler either: it has the 16-bit compare
   (`gb_cmp16`) but saves `bc` around each call instead of in the prologue,
   which the ROM never does.
3. **Flags.** The default `lcc` compile is `sdcc -mgbz80 --c1mode` after a
   separate `sdcpp` pass (`-DGB=1 -DGAMEBOY=1 -DINT_16_BITS …`); nothing so far
   needed more.
4. **Whole translation units.** Per-function matching links one function at
   its address. Reproducing LSDj's original object/link layout (function
   order within a bank, string literals and `const` tables after their
   functions, as `DrawTileHexCell`'s table shows) is only needed to build
   banks from C wholesale.
5. **Building whole banks from C.** Solved per function (below); building a
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
- [x] Shared header, cached batch verification, multi-function units
      (`c/bank03/InstrScreen.c`: 34 functions).
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
