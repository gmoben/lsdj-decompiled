# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95-3** (19 Aug 2000) → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker, small memory model — **plus an accumulator-tracking optimisation stock SDCC lacks** (the "modified GBDK"), modelled here as an accumulator-tracking pass plus 79 peephole rules (below) |
| Can we run it today? | **Yes, natively.** SDCC 2.2.1 + `sdcpp`/`as-gbz80`/`link-gbz80` build from the GBDK 2.95-2 sources (`tools/toolchain/build_sdcc221.sh`, ~15 s) and emit **byte-identical** asm, objects, listings and images to GBDK 2.95-3's Win32 binaries — which still run under Wine (`fetch_gbdk295.sh`), but crash on large units (below) |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock output does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95-3 lacks |
| Matched real LSDj functions? | **Yes — 549**, in twelve banks (the list: [`c/matched.json`](../c/matched.json)): a message renderer, a 15-case `switch`, a sparse `switch`, the SRAM accessors, and the instrument screen's field accessors and draw helpers. `make match` re-verifies them all (cached: only changed sources reach the compiler), and **their bytes in the ROM build come from the compiled C** (below) |
| Is all of LSDj C? | **No.** ~130 functions (7% of the instructions) are hand-written assembly — the interrupt handlers, sound tick, envelope/pitch and tile-loader code in bank 0, and the sequencer core in bank `$02` (`tools/match/code_kind.py`). Those stay asm; the ~830 others (every editor) are Track 2 targets |
| Go/no-go on Track 2? | **Go, with the rule set still growing.** Every function tried has matched (each new one has cost a few rules so far), the C feeds the ROM build with `make compare` as the gate, and the hand-written asm (7%) is mapped and excluded |

## Matched functions

| Source | Functions | What they exercise |
|--------|-----------|--------------------|
| [`c/bank00/DrawFields.c`](../c/bank00/DrawFields.c) | 7 functions: tempo, channel header, duty and pan fields | `switch` chains with `?:` returns, a register pair plus a constant, a block-local initialiser |
| [`c/bank00/GbdkLib.c`](../c/bank00/GbdkLib.c) | `StringLength` (GBDK's `strlen`) | GBDK library C under the stock rules (`@stock-rules`) |
| [`c/bank00/Helpers.c`](../c/bank00/Helpers.c) | 7 functions: bank 0's shared C helpers | kit bank probes through a `volatile` MBC register, a word compare, the clipboard cell accessors, sign extension into an `int` array with a 32-bit `\|=`, a re-typed pointer argument |
| [`c/bank00/InstrByte.c`](../c/bank00/InstrByte.c) | `WriteInstrByte`, `ReadInstrByte`, `ReadCurInstrByte` | byte args, pointer local, 16-bit shift loop, asserts (one compound), both forms of pointer + byte |
| [`c/bank00/Main.c`](../c/bank00/Main.c) | `InitAndLoadSong`, `main` | the boot `switch`, an endless frame loop with `continue`, an `\|\|` chain, a second absolute `sfr` name for a plain global, `?:` into globals, `FarCallArg` with a plain function pointer |
| [`c/bank00/ReadSeqByte.c`](../c/bank00/ReadSeqByte.c) | `ReadSeqByte` | `?:` on bit 7 of a call result |
| [`c/bank00/ScreenDrawDispatch.c`](../c/bank00/ScreenDrawDispatch.c) | 5 functions: the screen dispatchers | 15-case `switch` → `jp hl` tables, bank switching, cross-bank calls, case bodies out of value order, an `&&` chain |
| [`c/bank00/Sprites.c`](../c/bank00/Sprites.c) | 10 functions: the sprite helpers | literal-pointer stores, a register pair plus a constant added in place, `v == *p` |
| [`c/bank00/Sync.c`](../c/bank00/Sync.c) | 17 functions: sync settings, transport and clock, the overload message, unchecked sequence/chain reads | literal bank switches around cross-bank calls, a message string in another bank, an early `return` under `&&`, nested `else if` with early returns |
| [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) | `DrawMessageBar` (198 B) | globals, `sfr` HRAM stores, two loops, calls |
| [`c/bank01/SongAlloc.c`](../c/bank01/SongAlloc.c) | 13 functions: song allocation bitmaps, 16-byte row copies | a bit test on the chain bitmap (`1 << (chain & 7)`, whose shift count dies to `ld b,#1`) |
| [`c/bank01/UiHelpers.c`](../c/bank01/UiHelpers.c) | 23 functions: small UI helpers, the cartridge test and SRAM test, the play/work clocks, instrument names, project settings, the screen palettes, the top bar and command help | `?:` over string literals, a `char` local (its dead `ld b,c` copy), a glyph table, a file-scope `const` table flushed ahead of the next function's strings, a `volatile` scratch byte so `++` is not folded into the store, `v = f(); v++` reusing the call register, a 12-case `switch` chain, a `switch` whose `default:` comes first and `goto`s into a later body, a global read as a word for a 32-bit `<`, a `UBYTE * volatile` loop pointer (kept on the stack, its `+ 1` through a register pair), `char` booleans stored and then tested, a stack-vs-stack `==` whose operand order avoids SDCC's `cp a` miscompile |
| [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) | `ShowOutOfMemory`, `ShowTapAddChain` | one-line wrappers, 16-bit argument |
| [`c/bank03/InstrScreen.c`](../c/bank03/InstrScreen.c) | 63 functions of the instrument screen | field get/set/step helpers, `FarCall` in both address forms, SRAM literal addresses, a string literal placed after its function, `hl` kept across a `!=` compare |
| [`c/bank1c/FileScreen.c`](../c/bank1c/FileScreen.c) | 36 functions: file screen | field highlights from column tables, a `do … while (--i)` clear (CSE shares `i - 1`), the serial receive wait, `?:` returns; DrawFileTextField's frame loops (bounds inline, see Writing C: hoisting depth) |
| [`c/bank1c/FileUpgrade.c`](../c/bank1c/FileUpgrade.c) | 37 functions: the song-format upgrades | per-type instrument callbacks through a function pointer (`FileForEachInstrType`), the upgrade steps' field fix-ups, a 16-bit `\|` forced by an `int` constant |
| [`c/bank1d/SynthScreen.c`](../c/bank1d/SynthScreen.c) | 51 functions: synth and groove screens, the per-wave parameter curves and their loader | a `switch` through a jump table, SRAM param/groove accessors, a `?:` over `long` constants (> 32767), the cache walker; SynthFillWaveRange's `*buf++ = ((UWORD)i * b >> 4) + a` (the 16-bit product through MulU16, the sum operand order per branch), RenderSynthWave's curve buffers as real array symbols (`wSynthCurveSeg3[wSynthRenderIdx]` is the `ld a,#<sym; add a,c` form a cast literal would not give), SynthReadParam's `1 << (x & 7)` mask; SynthLoadParams reads the 16 parameter bytes through `volatile UWORD` locals (`ReadSongByte(1, p++)`: the old value fetched into bc, the slot incremented in place, a plain `p` pushed straight from the stack), inline call results as arguments (a named result would hold c through the whole function), the last pair into two locals so the `+ 0x0F` adds come out in the ROM's order, and the render destination as a pointer cast of the sum; BuildOverwritePrompt's frame (the ASCII restore at the end of the middle-column branch, see Writing C: hoisting depth) |
| [`c/bank1e/Gfx.c`](../c/bank1e/Gfx.c) | 2 functions: VRAM helpers | `UWORD` loop addresses compared against `(UWORD)0x9800` (constants above 32768 are `long`), a `while (STAT_REG & 2)` wait |
| [`c/bank1f/InstrHelpers.c`](../c/bank1f/InstrHelpers.c) | 69 functions: bank $1F helpers | the used-table/phrase/chain scans and counters (one template, four limits), SRAM resets bracketed by `_asm di _endasm;` and MBC register writes, a function that returns `e` by accident (no `return`); RemapCmdTableRefs scans three SRAM ranges through `volatile UWORD` pointers (`*(UBYTE *)p` reads via de, in-place increments, `p != (UWORD)0xB880` byte-wise), `from == *(UBYTE *)q` (the global on the left), a `volatile UBYTE` counter and a `volatile` remapped byte for the instrument loop |
| [`c/bank04/PhraseScreen.c`](../c/bank04/PhraseScreen.c) | 39 functions: phrase screen, its row loader, the four A+direction cell editors, instrument entry and name draw, prelisten | far-call wrappers (`FarCallArg2V`), the phrase instrument reader and the backward search for the instrument in effect (a return through `e`, `track_e`), `?:` returns, a `volatile UWORD` SRAM offset (no CSE of its casts: each push reloads it), `if (a != X && b != Y) return;` chains (the ROM's uncollapsed `jp z,L; jp done; L:`), a name copy through `char` casts, a `UWORD addr = (UWORD)p` copy (a named spill slot the allocator evicts instead of the value), a `wSongCol == 3` test the ROM shares between an `if` and a later `\|\|` (one CSE temp without a spill slot, so the value spills instead) |
| [`c/bank05/TableScreen.c`](../c/bank05/TableScreen.c) | 28 functions: table screen (draw, step reader, play markers) and screen map (draw, enter) | table step accessors through column pointers held in `UWORD` globals, the map moves, materialized `<=` tests, a two-case `switch`, six row-buffer column pointers (`t += (UWORD)0xBC80` keeps the add 16-bit), a `switch` with `default:` between `case 0` and an empty `case 0xFF`, the loop counter reused for the cursor x so the column index lands in b |
| [`c/bank06/Screens.c`](../c/bank06/Screens.c) | 45 functions: chain, wave and word screens, the chain cell and sample-name draws, the per-frame status panel, the chain screen draw, phrase open/clone | `while (i != wWaveSelEnd)` (global right: the kept `jr nz` trampoline), `(UWORD)y != 0x80`, an early `return` under `==` (SDCC's `jp nz,L; jp end; L:`), the chain row buffers through `ReadSongBytes`, `?:` over string literals as an argument, string literals declared as the ROM's `Txt*` symbols where the unit's data block sits elsewhere, `(UWORD)t < 0x78` (a 16-bit `add hl` compare off a just-stored temp), `if (n != 0)` keeping the reload of a pointer-read value, a far call by literal address (`FarCall(5, 0x747D)` pushes `ld hl,#nnnn`), `(char)` casts on far-call arguments (their temps take c, so the function-pointer temp lands in b/c reversed), eight SRAM row pointers where the two bank-2 ones are *recomputed* (CSE copies them from the first pair's spill slots; writing `s3 = s1` adds a use and keeps `s1` in bc), the wave copy's nibble pairs read as `ReadWaveSample(i) << 4 \| ReadWaveSample(i + 1)` (a named temp would stay live across the second call) |
| [`c/bank06/DrawTileHexCell.c`](../c/bank06/DrawTileHexCell.c) | `DrawTileHexCell` (130 B) | consecutive updates of a stack local, sign test, `-t`, pointer table |
| [`c/bank07/KbdLink.c`](../c/bank07/KbdLink.c) | 11 functions: keyboard octave, the link queue and its scancode dispatcher, keyboard notes, sync rate, a hex digit, the keyboard instrument name/state and calibration screen | a multiply kept as a `MulU16` call, `di`/`ei` around a 16-bit store, an empty case listed first; `UBYTE * volatile` pointers to `&wKbdChanState[1]`/`[2]` (a single-definition pointer local is otherwise rematerialised) and `wKbdChanState[3] = 0` through the array (the ROM's `ld de,$cb99; ld a,0; ld [de],a`); the name copy as direct stores, `wSampleNoteString[i] = wNameEditBuf[i] ? wNameEditBuf[i] : ' '` and `wSampleNoteString[1] = SongHexDigit(…)` (the address temps live across the call and share one spill slot; naming the address in a pointer local keeps the counter in c instead of on the stack); ProcessKbdNote's `armed = wSongCol == 3 \|\| !FarCallArgByte(...)` as one expression (the `==` temp is what the far-call pointer evicts from c, so the pointer lands b/c reversed), `wKbdHeldNote != note` with the global on the left; ProcessLinkQueue's 28-case `switch` on a stack byte (the ROM reloads it for every compare), case bodies in the ROM's order with `case 0xC0: if (!(mod & 2)) goto note;` falling into `case 0x40`, `wKbdChanState[1]` (never the Pu2 symbol), and FarCallArg called with two arguments (`FarCallArg0`, aliased) |
| [`c/bank07/SongScreen.c`](../c/bank07/SongScreen.c) | 30 functions: song screen, its row palettes and play markers, the chain clone, bookmarks | page-up/down scroll arithmetic, bookmark add/toggle in SRAM, `WriteBookmarkByte`'s pointer-range asserts (16-bit compares: `ld hl,#-lit; add hl,de`), a `!=` sprite loop; the row-number column's `(row & 4) ? 1 : 2` (the int-promoted copy of `row` is stored to the variable's own stack slot and the register tested) and its second loop's `if ((BYTE)row & 4)` (a signed cast copy lands in b: `ld b,c; ld a,b; bit 2,a`); SongRowPalettes reuses one local for the row offset and the cursor flag (`cur = wSongScroll + y - 2; shadow = cur & (UBYTE)4;` … `cur = i == wSongCol`) -- the long-lived `cur` is what makes the allocator spill the bookmark flag instead of the counter, and `(UBYTE)4` avoids the int-promoted copy; `i == wSongCol` (register left, global right) is the `jr nz` boolean, `wSongCol == i` the `jp nz` one; SongUpdatePlayMarker's sprite index is written `i + 4` at each use (a CSE'd temp; naming it keeps `i` in c where the ROM has it on the stack); SongCloneChain's two pointers into the $C532 map are `wClonePhraseMap + 16` and `(UBYTE *)wClonePhraseMap + 16` (the cast keeps the second sum from being CSE'd into the first), `if (!(wClonePhraseMap[j] - ph))` for the materialised difference test, the `ph != 0xFF` deep-clone body first with the $FF write as the else branch, and the transpose read reusing `ph` |
| [`c/bank07/SongCutCopy.c`](../c/bank07/SongCutCopy.c) | `CutCopySongSelection` (545 B) | its own unit: SongCopyColumn prototyped with two UWORDs (the ROM casts both pointer arguments; SongScreen.c defines it with pointers), `UBYTE * volatile` song pointers (reloaded at every use; the plain pointer would inherit the shifted offset's register pair), a `char` visibility flag, `x = (UWORD)p` as a cast (a plain copy is propagated away), `0xC0 - t` through a UWORD local, `isCut && !wSelLeft && wSelRight == 4`, the CUT/COPIED CHAINS literals after the function |
| [`c/bank07/SongPaste.c`](../c/bank07/SongPaste.c) | `PasteSongSelection` (415 B) | its own unit: `extern volatile UBYTE wSongRow` (the row-shifting loop recomputes `wSongRow + h` where a plain global shares the test's value), SongWriteSramBytes prototyped with UWORD source and destination (both pointers cast, the source through a reversed `b/c` pair), `(UWORD)0xC0 - (wSongRow + h)` for the 16-bit count, `(UWORD)p` compared against `(UWORD)0xB290`/`0xB590` (the CSE'd cast shares one spill slot with the clipboard byte), `if (!(... & 0xF0)) continue;` for the scroll step |
| [`c/bank07/SongSelection.c`](../c/bank07/SongSelection.c) | `SongEditSelection` (667 B) | its own unit: SortBytePair prototyped with `char *` (the `&UBYTE` locals go through cast temps: the first spilled below the frame's variables, the second in bc), the clamp/erase loops written as expressions (`while (wSongCol < (x1 == 4 ? 4 : x1 + 1))`, a four-way `\|\|` of `<`/`>=` tests each materialised into a spilled boolean temp: a temp defined in a loop is live to the loop's end), the 32-byte CGB attribute table (`const`) after the function |
| [`c/bank1b/ProjectScreen.c`](../c/bank1b/ProjectScreen.c) | 37 functions: project and help screens | property writes to SRAM, a busy-wait on a global, `(UWORD)x == 9` (a 16-bit compare: one `jp nz` per byte through one trampoline), `?:` string arguments, a NULL-terminated pointer table; DrawProjTextField, the same frame as DrawFileTextField with the field draw first |
| [`c/bank1b/DrawSyncField.c`](../c/bank1b/DrawSyncField.c) | `DrawSyncField` (262 B) | sparse `switch` (`cp` chain), if/else, a pointer local chosen by a condition, 4-arg calls |
| [`c/bank1b/DrawFontColorFields.c`](../c/bank1b/DrawFontColorFields.c) | `DrawFontColorFields` (107 B) | `const` 2-D name tables (SDCC 2.2.1 drops a NUL inside a literal and emits a unit's const data after its first function, so the tables behind it in the ROM make this its own unit), an sfr pushed with `ldh` |
| [`c/bank1f/DrawChanStatus.c`](../c/bank1f/DrawChanStatus.c) | `DrawChanStatus` (324 B) | the song screen's MUTE/L/R channel cell; two `const` CGB attribute rows behind it in the ROM make it its own unit, a static pointer into a static flag array ($C9CE/$C9D2) |

Shared prototypes, variables and the SRAM layout live in
[`c/include/lsdj.h`](../c/include/lsdj.h); a signature is part of the match, so
it is settled there once.

```sh
make match                      # every c/bankNN/*.c linked at its ROM address and compared
python3 tools/match/test_peep.py  # comparison truth values and HL/SP-relative destinations
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

### Synth renderer and field redraw

`SynthRenderWaveA` (367 B), `SynthRenderWaveB` (403 B), `SynthComposeWave`
(882 B), `SynthRenderWaveType` (581 B), `DrawSynthNumber` (739 B),
`BuildOverwritePrompt` (476 B) and `SynthOverwriteWaves` (110 B) match in
`c/bank1d/SynthScreen.c`.

- The two waveform mixers repeat `wSynthSignalType == 2` in their tests.
  SDCC shares an unnamed comparison temporary; storing it in a named
  boolean spills that boolean and adds a byte to the frame. Plain pointer
  and word locals match once that extra spill location is removed.
- Samples widen through `(WORD)*tbl` before storage in an unsigned word,
  and the four-bit logical shift is explicitly sign-extended with `0xF000`.
  `vol = param & mask; vol >>= 1;` must remain two statements.
- The dispatcher uses real bank-$1E array symbols (`SynthSeedTableA`–`F`),
  not literal pointer bases: SDCC preserves an immediate symbol as the left
  operand of addition. A single `UWORD p` holds the offset and then the
  banked address, cast to `char *` for `LoadSynthShape`.
- Composition uses signed word bounds and samples, but widens the window
  control through `UWORD`. The shared test is `hi == lo`; FOLD uses
  `v >= hi + 1`, preserving the ROM's hoisted comparison bound. The new
  `wSynthCurveAmp`/`wSynthCurveLimit` symbols document `$CA94`/`$CAA4`.
- The redraw's SRAM cursor is a plain `UWORD`, not a pointer. The two `--`
  literals are emitted after the function at `$68A5` and `$68A8`.
  `SynthOverwriteWaves` opens the confirmation dialog; its old quoted
  comment had incorrectly classified the routine as data.

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
| Redundant flag ops | `or a,X` / `or a,a` → `or a,X`; `ld r,a` / `xor a,a` / `or a,r` → `ld r,a` / `or a,r` (r a register: `if operandIsReg %1`; after `ld (hl),a` see `track_a`) | 0 | 575, 41 |
| Register reload | `ld r,a` / `ld a,r` → `ld r,a` (r = b, c, d, e, h, l; **not** `(hl)`); `ld c,e` … `ld e,c` (a returned value) even across `add sp,N` | 2 (both with a jump label between) + 1 (below) | 69, 34 |
| Reload of a stored value | `ld (hl),a`; `ld a,(hl)` → reload dropped, whatever `a` held (an ALU result, a stack copy through `(hl+)`, …); **kept** after a pointer read (`ld a,(de)` / `ld a,(bc)`: all 43 reloads in the ROM) | 0 | 31+ |
| Dead stores | `ld X,a`; *a-only ops*; `ld X,a` → first store dropped (X a register or `(hl)`, also when the first store is followed by its reload) | 0 | (small; e.g. `and 3; add a,a; ld c,a` ×2) |
| Constant reuse | `ld a,N` / `push af; inc sp` / `ld a,N` → reload dropped, also across `ldh (n),a` + `ld hl,nn; push hl`; `xor a` / `ld hl,sp+N` / `ld a,#0` → reload dropped | 0 | 10, 5, 2 |
| Compare chains | a sparse `switch` re-reads its global before every `cp` in stock output; LSDj loads it once (one rule per chain position, up to twelve, so the pattern always carries the variable). SDCC's `==` trampoline `jp cc,T; jr F; T: jp X; F:` → `jp cc,X`, every byte's `jp cc,T` of a multi-byte compare included (the only survivor in the ROM is inside GBDK's stock-compiled `gprintf`) | 0, 1 (library) | 72 |
| Kept across a test | `ld a,r` / `bit n,a` / `jp cc` / `ld a,r` → reload dropped; `ld hl,#x` / `ld a,(hl)` / `cp a,#n` / `jp z,F` / *(dead label)* / `ld hl,#x` → reload dropped (a `!=` test; stock SDCC's Rule 5 leaves the trampoline's label behind, which makes its codegen forget `hl`); `xor a` / `ld a,#0` → `xor a` | 0, 1 (a real jump target in between), 0 | 68, 7, 6 |
| Register tracking (`track_a`, below) | a reload of what `a` already holds: `ld a,r` / `cp` / `jp cc` / *(dead label)* / `ld a,r` (an `\|\|` chain) → reload dropped; a stored stack local reread: `ld hl,sp+N` / `ld (hl),r` / [`add sp,M`] / `ld a,(hl)` → `ld a,r`; `ld (hl),a` / `ld de,#T` / `ld l,(hl)` → `ld l,a` (indexing a table by the value just stored) | 0, 0, 0 | 35, 25 + 141, 32 |
| More dead stores | `ld r,#n` / `ld r,X` → `ld r,X`; `ld r,a` / … / `ld r,(hl)` → `ld r,(hl)`; `ld (hl),e` / … / `ld (hl),a` → `ld (hl),a` (nothing touching hl between); a discarded sign extension `rla` / `sbc a,a` / `ld r,a` / `ld a,X` / `and a,#n` → store dropped; the `== 0` test of a fresh `&`: `and a,#n` / `ld r,a` / `or a,a` / `jp nz` → `and a,#n` / `jp nz`; after *ALU* `a,r` / `ld (hl),a` the `ld a,(hl)` reload is dropped too | 0, 0, 0 (1 `jp z` / `if (t)` site keeps it), 0 | —, 3, 12, 69 |
| 16-bit compare with a constant (`gb_cmp16`, below) | `xor a,a` / `ld a,lo` / `sbc a,#lo` / `ld a,hi` / `sbc a,#hi` / `jp cc` → `ld hl,#-lit` / `add hl,de` (or `bc` when the value already sits there) / `jp` on the inverted carry | 0 (its 5 `sbc` chains are 32-bit) | 28 |
| `ld a,a` | the MOVA SDCC leaves after reading an sfr for a compare → removed | 0 (the one `ld a,a` in the ROM is palette data) | — |

Besides SDCC's `if labelRefCount %N k`, a rule may say `if operandIsReg %N`
(the variable bound to a register rather than `(hl)`); `peep.py` implements
both.

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
A store overwritten by any other `ld r,...` goes too: no unread `ld r,a`
dies to a constant load (`IsChainUsed`'s shift count) or to a `ld r,(hl)`
(`WriteWaveSample`'s `pos + 1`, whose register the next temp reloads) in the
ROM's real code -- the five apparent survivors are text data disassembled
as code. Stores killed by `pop bc` or across a call or a branch survive, so
the pass stops there.
`dead_mem_stores` does the same for a byte stored through `hl` and
rewritten by the next `ld (hl),...` before anything touches `hl`: SDCC
spills a call result to its variable, `track_a` turns the reads into
`ld a,e`, and the variable's next assignment overwrites the slot
(`v = ReadSongByte(1, p); v |= v << 4` in FileUpgradeV4); no such pair
exists in the ROM. `gb_cmp16` also re-seats `hl` after the compare it rewrites: stock SDCC
leaves `hl` at the operand's high byte and may continue from there
(`dec hl; ld a,(hl+)`), while the add form clobbers it, so the ROM
reloads `ld hl,sp+0` (DrawBlocksUsed).
After `ld (hl),e; ld a,e` the byte at hl is `a` too, so a table index
loaded from it is `ld l,a` (CountFileBlocks' `ld l,a; ld h,#0`).
A byte read through `de`/`bc` is remembered as "the byte at (de)": its
copies are forgotten once the pair is rewritten, so `ld a,(de); ld c,a;
ld e,(hl); inc hl; ld d,(hl); ld a,c; ld (de),a` keeps the reload
(SynthFilterPass, FileFormatBuf, 2/2) while `ld a,(de); ld b,a; cp a,#n;
jp z; ld a,b` drops it (DrawPhraseCellField).
`track_a` also copies a byte still held in a register
instead of reloading it (`ld (hl),e; add sp,3; ld c,e`, 23 ROM sites) and
drops the reload into the same register (`ld (hl),e; ld e,(hl)` never
occurs: FileCalcCompressedSize's result).

`inline_asm_hl` reloads hl after an `_asm` block: stock genInline leaves
its pair cache alone, so a stack byte addressed right after `_asm di
_endasm` is reached with `inc hl`, where LSDj's compiler emitted
`ld hl,sp+N` (RemapSeqChains' `i = 0` after the di).

One hl reload is rewritten: a 16-bit local tested for zero (`ld a,(hl+);
or a,(hl); jp nz`) and then assigned has stock SDCC reload `ld hl,sp+N`
for the store; the ROM steps back with `dec hl` (GetInstrEnvBytes'
`if (total == 0) total = 1`). The passes see the jump targets as they
are once the textual rules have run: a label the rules left unreferenced
no longer ends a block.

`track_a`'s register knowledge is pairwise: `ld c,a; ld b,c` makes `b`
equal `a` only through `c`, so `ld c,a; ld b,c; ld a,b` loses the reload
(DrawSpeedGlyph) while `ld c,a; ld b,c; ld c,#0; ld a,b` keeps it
(FindFreeBlock's `<< 9`).

A register holding half of a 16-bit call result (`ld c,d; ld b,e`) does not
set up that equality: the ROM's one reread of a byte just stored from a
register (`ld (hl),b; ld a,(hl)`, OpenNameEditor's StringLength result)
stores such a half, while the 46 sites that reuse the register stored a
byte value. `dead_stores` also drops a call result copied to its register
(`call; ld c,e`) and never read: SDCC leaves the copy when the cast that
follows reads `e` itself (CloseNameEditor's `row * 3`), and the ROM has no
such copy before its register is rewritten. A rule drops the `ld hl,#g`
reload after `ld hl,#g; ld a,(hl); cp r; jp z` when only a dead label
intervenes (the `cp a,#n` form was already covered): stock SDCC's label
spills its hl knowledge, LSDj's compiler kept it, while the `==` shape
that leaves `jp nz; jr; L1: jp; L2:` between compare and body reloads in
the ROM too (14 sites each way).

`track_a` also knows the byte at `hl` after `ld (hl),r` (r = `b`..`e`): a
reread becomes `ld a,r` and a test `ld a,r; or a,r` (183 and 36 such sites
in the ROM's compiled code; one site rereads, after storing the low byte of
a 16-bit value), which is how `EraseFile` and `FileUpgradeV21` test a value
they just stored. A load `ld r,(hl)` sets up the same equality: the
ROM never rereads the byte after one (171 `ld a,r`, 0 `ld a,(hl)`, 0
`or a,(hl)` tests), while stock `genCast` does to sign-extend a spilled
`char` (`ld c,(hl); ld a,(hl); rla`, `LoadSynthShape`). One exception: a
byte incremented or decremented in place and tested straight away (`ld
a,(hl); add a,#0xFF; ld c,a; ld (hl),c`, how `while (--i)` compiles) is
reread for the test, `xor a,a; or a,(hl)` (`SortBookmarks`; no ROM site of
that shape tests the register). SDCC's pre-decrement yields the variable
itself rather than the new value.

After `ld (hl),a` a test of the stored byte is `or a,a` (169 ROM sites: the
value from an ALU op or a merge label; never `or a,(hl)` alone), except that a
value read through a pointer is reread, `xor a,a; or a,(hl)` (27 sites, each
after `ld a,(de)` / `ld a,(bc)`), the same split as the reload row above
(`SetColInstrument`'s `wInstrIsKit = instr != 0x40; if (wInstrIsKit)`).

`track_e` does the same for `e`, which carries call results and return
values: after `ld c,e` (a result kept in `c`) a later `ld e,c` (returning
it) is dropped while neither register changed and no jump target
intervened. Of the 740 `ld r,e` copies in the ROM's compiled code, no
redundant `ld e,r` follows one (`FindCursorInstr` returns its value so).
`track_d` is the same knowledge for `d`: a pointer kept in `bc` and fetched
twice (`ld e,b; ld d,c` for a read, again for the store that follows) loses
both halves of the second fetch in the ROM (`ProcessLinkQueue` clearing
`wKbdChanState[1]`).
`track_const` does it for constants: `ld r,#n` (r = `b`..`e`) is dropped
while r already holds n, so the high byte of a widened index stays 0 for
the next one (`ReadWordCell`). None of the 1571 `ld r,#n` in the ROM's
compiled code reloads a constant its register still holds. `track_de` is
the 16-bit case: `ld de,#X` is dropped while `de` still holds X. SDCC's
`fetchLitPair` remembers the literal last loaded into a pair, but the `de`
half is compiled out in 2.2.1 (`IS_GB && pairId == PAIR_DE && 0`), so a
global array's base is reloaded between `a[0] = x` and the `ld hl,#1; add
hl,de` of `a[1] = y`; LSDj's compiler kept the first load (`LoadFile`'s
`wFileSramProps` stores), and the ROM has no such pair within a basic
block while `d`/`e` are untouched.

`track_hram` (run before `track_a`) reuses an HRAM byte read with `ldh`.
Stock SDCC 2.2.1 rereads an `sfr` at every use: its CSE never substitutes a
register for a direct-space global (`isGlobalInNearSpace`, an 8051 optimisation
applied to every port). LSDj's compiler reused a value read before a
conditional jump, a loop condition's `hSoundCh < 4` serving the body's three
array indexes in `SyncTickLSDJ`, but reread within one block
(`HandleInputCombos`, 3 sites); outside bank `$02`'s sound engine, which goes
both ways, no site contradicts that. An HRAM store, a call or a jump target
ends the reuse.
A textual rule covers the pair case: `ld b,l; ld c,h; ld l,b; ld h,c` loses
the copy back (none in the ROM; `FileReadCompressedBlock` pushes `hl` right
after copying it).

One family is code generation rather than a peephole: LSDj's compiler
compares a 16-bit value with a constant by adding the negated constant
(`p < 0xAFF0` is `ld hl,#0x5010; add hl,de; jp c,skip`), the Game Boy
special case later SDCC releases have in `genCmp`, where SDCC 2.2.1
subtracts byte by byte. `gb_cmp16` rewrites the stock form before the rules
run: a stack operand is fetched into `de`, a value already in `bc` or `de`
is used in place, and a stack slot `hl` already points at (a call result
just stored through it, TapTempoStep's `sum < 0x100`) is fetched from
there -- also one byte past it, stepping back first (DrawChainCell's
`(UWORD)t < 0x78` after a two-byte store) -- the following hl-relative use
reloaded with `ld hl,sp+N` as the add clobbered hl.

The same pass now handles a **stored unsigned comparison** from a stack
operand or register pair: stock `sbc` followed by `ld a,0; rla` becomes
`add hl,pair; ld a,0; rla; xor a,1`. A scan of all reached instruction
boundaries in the program banks finds one such add/materialize/invert site
(`BuildOverwritePrompt`, `$1D:$6B8E`) and zero stock subtract/materialize
forms (stack, global, or register operands). This is the same carry
inversion as the branch form, with the truth value made explicit.
Zero thresholds are left alone: adding zero cannot provide the inverse
borrow. The pass also restores HL when its fetch starts one byte above the
stack operand (`dec hl`), including relative stores after materialization.
`tools/match/test_peep.py` checks both branch polarities and the actual
destination of these stores with a small instruction interpreter, plus all
65,536 input values at five representative thresholds. Global boolean
materialization is deliberately left to stock code until its HL handling
is established.

Most of these are one mechanism — the code generator knowing what `a` holds
— so besides the textual rules `peep.py` runs `track_a` over the result: it
follows the registers and constants `a` equals (`ld a,c`, `ld c,a`,
`ld a,#5`, `xor a`), drops a later load of the same value (and `xor a,a`
before `or a,r` when `a` already equals `r`), and forgets at anything that
changes `a` or the source (a shift of another register, `srl b`, changes
neither), at calls and jumps, and at every label that
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

Not all of GBDK's library in the ROM is asm: libc's C routines were compiled
by GBDK's build, with the stock rules (`strlen` is `StringLength`, matched
from gbdk-2.95-2's own `libc/strlen.c`). The heuristic also counts as C some
asm that reads stack arguments (`DrawByte`, `DrawString`, `GetTilemapAddr`,
`DrawTileRun`, the division helpers); those poll `rSTAT` or use `jr` freely.

## Writing C that matches (what we learned)

- **Bytes stored with `ldh`** (HRAM/IO) are `extern volatile sfr UBYTE NAME;`
  (GBDK 2.95's `USE_SFR_FOR_REG` style). `*(UBYTE *)0xFFxx = v` compiles to
  `ld de,#0xFFxx; ld a,v; ld (de),a` instead.
- **SDCC 2.2.1 crashes** on `if (*(UBYTE *)0xFFxx)` (2.3.1 segfaults too).
  Bytes LSDj *tests* through `ld hl,$FFxx; or a,(hl)` (`hGameboyType`) are
  plain `extern` variables placed there at link time.
- **MBC registers are variables**: `rROMB0 = n` gives the ROM's
  `ld hl,#0x2000; ld (hl),#n`; a literal-pointer store goes through `de`.
  LSDj's bank switch is `hCurrentBank = n; rROMB0 = n`. `rROMB0` is `volatile`:
  with no call between two switches (`KitExists`, which pages a kit bank in
  and straight back out) SDCC otherwise drops the first store as dead.
- **`++s` on a pointer parameter, `s++` on a local**: the post-increment's
  old-value copy (`rOp := s`) survives CSE for a true symbol (the temp is
  "cheaper"), so a parameter's `s++` compiles to `ld hl,#1; add hl,bc` and
  a store, while `++s` (or `s += 1`) is the in-place `inc (hl); jr nz; inc
  hl; inc (hl)` -- DrawNameEditArrows walks its string with `++s`. A
  local's copy is replaced by the local, so `p++` already increments in
  place there (FileUpgradeV7).
- **Bank 3 declares `ReadSongByte`'s address a pointer** (`#define
  SONG_BYTE_PTR` before `lsdj.h`): CloneSynth pushes a spilled pointer sum
  straight from its slot (`ld a,(hl+); ld h,(hl); ld l,a; push hl`), where
  the `UWORD` prototype would copy it into `bc` first; the other banks'
  matches need the `UWORD` form. A pointer plus `(UWORD)(s << 4)` keeps the
  widening inside the loop (a CAST is never a loop invariant), which is why
  only the byte shift is hoisted there.
- **`switch (b = f())` and a function-pointer compare**: DecompressBlock
  switches on the assignment's value (the call result in `c`) while the
  local `b` keeps a copy and is reused as the run counter (`while (b--)`);
  `wBlockSinkPtr == FlushBlockToSram` compares the pointer global with an
  address (`AOP_IMMD`: `ld a,#lo; cp a,(hl)`), where a numeric literal
  would load the global first.
- **Too few arguments compile**: SDCC 2.2.1 accepts a call with fewer
  arguments than the prototype, so `DrawFormat("INSTRS....")` compiles
  against `void DrawFormat(char *fmt, UWORD v)` (CleanInstrData); a K&R
  `void DrawFormat();` would not promote a byte argument (`...` neither).
- **Spelling a scan from the top**: `wColPhrase[i - 1]` twice and then `i--`
  share one `i - 1` temp through CSE, which is why RemapPhraseRefs stores
  `i` only after the test (`ld (hl),b`); `i--` first packs into `inc (hl)`.
  `from == *p` (the stack parameter on the left) is the compare that keeps
  the pointer read in `a`; `*p == from` hits the `cp a,a` miscompile.
- **Call arguments are turned into code right to left** (`geniCodeParms`
  recurses on the right operand first), so a call nested in the *last*
  argument runs before the side effects of the earlier ones:
  `WriteSongByte(0, q++, ReadSongByte(0, p++))` reads, then bumps `q`, and
  the read's result holds `c` across the `q++` (whose old-value pair comes
  out reversed, `ld b,(hl); inc hl; ld c,(hl)` ... `ld l,b; ld h,c; push
  hl`) and spills to one shared slot (FileUpgradeV19, three times). Written
  as `v = ReadSongByte(...); WriteSongByte(0, q++, v)` the local is spilled
  once for good and the later pairs are `bc`.
- **Which side of `==`/`!=` a global sits on shows**: `gencjneshort` puts a
  register, direct or literal *right* operand through `cp; jp nz,L`, which
  SDCC's own rule 5 then folds with the `jp` after it into `jp z,X`; a global
  (`AOP_HL` on the GB) or stack *right* operand takes the pointer branch,
  `cp; jr nz,L; jp X; L:`, which nothing folds. The ROM keeps 12 such
  trampolines (all `x == global`, `x == stack`): `while (i != wWaveSelEnd)`
  in `PasteWaveSelection`, `CloneInstrTable`'s field test. `global == reg`
  is the folded form. (The `!=` double trampoline, `jr nz,L; jr M; L: jp X;
  M:`, is folded by lsdj.peep either way.)
- **Literal types** (`constVal`): hex and octal literals are *unsigned*,
  decimal ones signed; a value under 256 is a `short`, which is **one byte**
  in this port (`gbz80_port` sizes: char 1, short 1, int 2, long 4), so `y
  != 0x80` compares bytes and `pos + 1` is an 8-bit add; a value above
  32768 is a `long`. The ROM's 16-bit `!=` against a small constant
  (`WaveInitSprites`, `ReadWaveSample`'s loop bounds) is `(UWORD)y != 0x80`
  -- the zero-extended copy spills in the first of two identical loops and
  sits in `bc` in the second.
- **Sign extension follows the destination type**: `genCast` extends by
  the type cast *to*, so a `char` stored into an `int` element is
  `rla; sbc a,a` and into a `UWORD` is `ld b,#0` (`wSynthShapeBuf` is
  `int[]`). A hex constant above `0x7FFF` is an *unsigned* `long`:
  `wSynthShapeBuf[i] |= 0xFF00` widens the `int` to 32 bits with a zero
  upper word, as the ROM does. `*(UWORD *)0x4000 == 0x4060` loads the
  word into the reversed `b`/`c` pair and compares it byte by byte.
- **A register pair plus a constant, constant second**: `(UBYTE *)offset +
  0xDD00` is added in place (`ld a,c; add a,#0x00; ld c,a; ld a,b; adc
  a,#0xDD; ld b,a`, 25 such adds in the ROM) because the cast leaves `offset`
  in a register pair; `wOamBuffer + offset` adds the address first (`ld
  a,#0x00; add a,c`), and `offset + 0xDD00` on a stack operand goes through
  `ld hl,#0xDD00; add hl,de` (`SetOAMByte`).
- **`array + n` is computed at run time**, even as a call argument (`ld
  de,#_a; ld hl,#n; add hl,de`), and `&a[n]` likewise; only the bare array
  name is an immediate. An address the ROM pushes as `ld hl,#addr` is a
  symbol of its own or a literal: `SongReadRow` fills the 64-byte song window
  in 16-byte chunks at `wSongReadBuf`, `(UBYTE *)0xC47D`, `0xC48D`, `0xC49D`.
- **One global, two views**: `hGameboyType` is read everywhere as a plain
  global (`ld hl,#0xFF90; or a,(hl)`), but `main` stores it with `ldh`, so
  `main`'s file saw it as an sfr. SDCC's absolute form gives that file its
  own name without a linker symbol: `sfr hGameboyTypeIo = 0xFF90;`.
- **A multiply by a constant stays a `MulU16` call** only when the constant
  is not a literal while the tree is built: `decorateType` moves a literal
  left operand of `*` to the right, and `geniCodeMultiply` turns a power of two
  there into a shift. The ROM's `MulU16(0x800, x)` is `r = 0x800; r *= x;`
  (constant propagation puts the literal back afterwards; `SetSyncSendRate`).
- **An empty case listed first** (`case 0x27: break;`) puts its `jp end` right
  after the compare chain, where the stock rule `jp z,L1; jp L2; L1:` →
  `jp nz,L2` inverts the last compare (`KbdPollScancode`).
- **A re-typed pointer argument is copied first**: `ShowMessage` passes
  its `char *` on as `(UBYTE *)text`, which moves it through `bc` before
  the push; passed as-is it goes straight from `hl` (a `const` in
  `strcpy`'s prototype does not cause the copy).
- **Empty `case N: break;`** produce their own `jp end` trampolines in source
  order — the ROM has them, so spell the empty cases out.
- **16-bit shifts need the promotion**: `(unsigned int)instr << 4` is the
  `rl c / rl b` loop; `instr << 4` alone is shifted in 8 bits. Likewise
  `p + field` and `p + (unsigned int)field` are different code (hl/de add vs
  the byte extended into bc first); the ROM uses both.
- **Zero tests**: `x == 0` on a stack byte is `ld a,(hl); or a,a`, `!x` is
  `xor a; or a,(hl)`; on a pointer target `!*p` is direct and `*p == 0` goes
  through a trampoline.
- **Advancing a pointer parameter**: `src = src + 1;` increments it in place
  on the stack (`inc (hl); jr nz; inc hl; inc (hl)`), `src++` goes through
  a register temporary and costs a register (`CopySong16`).
- **SRAM addresses are integers**: LSDj's `ReadSongByte` / `WriteSongByte`
  take the address as a `UWORD` (`lsdj.h`). An integer argument is pushed
  as it is; a pointer is converted first, which costs a copy through a
  register pair when it sits on the stack (`IsBookmarked`'s reversed `b`/`c`
  copy). So address variables are mostly `UWORD`s: `UWORD p =
  (UWORD)(SramPhraseFx + (unsigned int)row);` (the cast keeps the sum in
  `bc`, where assigning to a pointer packs it straight into `p`'s slot),
  `p += (UWORD)phrase << 4; ReadSongByte(2, p)`. A comparison of such a
  `UWORD` with a constant above `0x7FFF` is 32-bit (the constant is a
  `long`), as in `FileFindSlotEnd`; `(UWORD)0xB000` keeps it 16-bit.
  `(UWORD)` of a literal-pointer macro inside a comparison crashes SDCC
  2.2.1; spell the constant.
- **Addresses held in integers**: `extern UWORD wTableCellAPtr;` copied
  with `p = (UBYTE *)wTableCellAPtr;` goes byte by byte through `a`
  (`genCast`); a pointer-typed global copies as `ld a,(hl+); ld e,(hl)`
  (`genAssign`'s two-byte case). The ROM has both (`TableReadCmd`).
- **A comparison kept as a value**: `ok = v <= 0x13; if (!ok) …` stores the
  0/1 (`ld a,#0; rla`) and its negation before testing it; the same test
  written inside the `if` compiles to a plain `cp` / `jp` (`DrawCmdLetter`).
- **Signed globals**: a signed compare that reads the global straight into
  `xor #0x80` (no copy) means the global itself was a plain `char` in that
  source file (`extern char wPhraseCol;` for `PhraseCheckEditContext`); a
  `(char)` cast copies it through a register first. The clone commands
  (`CloneInstrAtCursor`, `CloneTableAtCursor`) pass `(char)wPhraseRow`: the
  ROM loads the row into a register before pushing it, as a conversion does,
  so their file saw `wPhraseRow` as a `char` too. The other face of it: a
  `char` global passed to a `UBYTE` parameter or incremented and tested is
  copied through `c` (`wGrooveRow` in `PasteGrooveSelection`, which also
  settles the parked `GrooveDrawField`).
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
  (`FAR_FN(fn)` in `lsdj.h`). FarCallArg takes a function pointer as well:
  `InitAndLoadSong` passes `LoadProjectSettings` plainly (`ld bc,#fn`), the
  other call sites through `FAR_FN`. A cast to another pointer type, `banked`
  or `nonbanked` all fold away.
- **String literals** are emitted right after the function that uses them, as
  in the ROM (`DrawInstrByte11`'s `"--"` at `$03:$49E4`). Precisely, SDCC
  flushes its constant segment after each function it compiles, in the order
  things were put there: `const` data defined at file scope since the last
  flush first (`const UBYTE SpeedGlyphTiles[]` just above `DrawSpeedModeName`
  lands between its code and its strings, where the ROM has it), then the
  function's literals as `decorateType` numbers them, in source order except
  that a block-local initialiser's literal is numbered while the declaration
  is parsed, ahead of the rest (`{ char *s = "LR"; ... }` inside one case of
  `DrawFieldValue` puts "LR" before case 0's "--"). SDCC 2.2.1 has no
  `code` keyword.
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
- **How far an invariant is hoisted depends on stale dataflow.** Loops are
  processed inner to outer, and an invariant moved into an inner loop's
  preheader is moved again only if its definition is in that preheader's
  `inDefs` -- which `computeDataFlow` computed once, before the loop pass,
  iterating only until the *available expressions* stop changing. With two
  passes (the common case) a definition from an inner-loop block other than
  the back-edge block has not yet reached the inner preheader, so it stays
  there; one in the back-edge block (the end of the body) has, and leaves
  both loops. A `local := temp` copy before the loop (`bottom = y + h;`)
  forces a third pass (`ifKilledInBlock` kills such a copy once the temp's
  own definition flows around the back edge), after which everything leaves
  both loops. The three frame drawers rest on this: `BuildOverwritePrompt`
  restores the ASCII map at the end of its middle-column branch (not the end
  of the body), and `DrawFileTextField`/`DrawProjTextField` write their bounds
  inline (`i < x + w`, `j == y + h - 1`) with no named copies, which leaves
  `x + w - 1`, the edge tests and the restore in the inner preheader as in the
  ROM. Their `==` operands come out swapped (`i == x` compiles with `x` on the
  left). Instrumenting `SDCCloop.c`/`SDCCdflow.c` in a debug build shows
  each decision (`LOOPDBG` in `/tmp/opencode/sdcc-dbg-src`).
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
the 2000-era code got implicitly; the undocumented `/L1`..`/L4` label-optimiser
switches, which the release's option parser cannot reach, are accepted for
experiments); nothing in the code generator changes.
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

The bytes the unit places right after a function -- the constant segment
SDCC flushes there, its string literals and `const` data, up to the next
function -- are compared with the ROM too: the code alone checks only their
addresses, so a wrong last string would otherwise pass.

A source file that says `@stock-rules` (in a comment) is GBDK library code:
GBDK's own build compiled it with SDCC's stock peephole rules, so the harness
skips `lsdj.peep` and the tracking passes for that file
([`c/bank00/GbdkLib.c`](../c/bank00/GbdkLib.c), whose `strlen` keeps an
`or a,a` LSDj's rules would drop).

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
| `FileUpgradeV3Pulse` | `$1C:$628B` | same `ld b,c; ld a,b; and; or #n; ld c,a` tail as V3Kit |
| `FileUpgradeV3Kit` | `$1C:$62D3` | with `(char)v` casts all but one byte matches: the tail's `and a,$f9` result stays in a for `\| 1` in the ROM, ours stores it to b (stock packing refuses a literal operand) |
| `FileUpgradeV0Kit` | `$1C:$6134` | all 5 byte locals + a 16-bit temp on the stack (frame 7); ours keeps 2 in registers, 8-bit clear (~0x20 folds) |
| `FileUpgradeV2` | `$1C:$622D` | only an `ld hl,sp+0` reload after `or a,a; jp z` (&& chain on a named local): ROM keeps hl there, but reloads in switch chains |
| `GetInstrFlags1F5` | `$1F:$40CA` | `v = f() & 0x80; if (v)` (ROM: and; ld c,a; or a,c; jp z) makes SDCC 2.2.1 assert (gen.c 3215, genAnd single-bit + ifx); no crash-free spelling found yet |
| `RemoveBookmark` | `$07:$63B5` | the ROM spills i (sp+2) and keeps each call result in c; ours keeps i in c. A UWORD p removes the conversion copy but not that; declaration order, an early return and a named result don't change it |
| `CopySynthParam` | `$06:$493F` | ROM keeps s = wSynth >> 4 in c, packs ChainCalcParamAddr's result straight into p (sp+0) and stores a copy of s to sp+2 before the mask; UWORD/pointer p and three mask spellings tried |
| `ChainWriteWaveB` | `$06:$4F42` | reverses the selection; ROM spills the first ChainReadPhraseNote result (sp+2) and keeps the second in b, where SDCC pushes the second straight from e and keeps the first in b |
| `PlotEnvCurve` | `$1B:$7200` | matches except register naming: the ROM keeps every temporary in b (the end pointer as b/c, low byte in b), as if c were busy for the whole function; ours uses c. The $DA00 buffer is addressed as a literal pointer ((char *)0xDA00) |
| `SynthWriteWaveCol` | `$1D:$5F29` | all but the register pair: the ROM loads the post-incremented wSynthRenderDst into c/b and pushes bc, ours gets b/c (c busy at allocation per --dumpregassign, though no live temp shows); needs `ld l,a` (track_a) and the genXor double-store rule, both in |
| `CheckRomChecksum` | `$00:$1FD5` | all but the end matches: the ROM tests (UBYTE)sum == lo with the cast in c and SDCC's cp a,a miscompile (so that test always passes), and compares sum >> 8 with hi in 16 bits with the shift spilled; ours keeps the cast in a and compares correctly |
| `DedupPhrases` | `$1F:$5BDE` | in its nested loop SDCC 2.2.1 extends the inner loop's temporaries to the loop end (SDCClrange.c operandLUse: hasIncomingDefs), so `wPhraseDedupScratch = IsPhraseUsedFar(j); if (...)` keeps the call result in a spill slot; LSDj's compiler stores it straight into the global (packRegsForAssign), frame 2 vs 4 |
| `DedupChains` | `$1F:$5C78` | same nested-loop live range as DedupPhrases |
| `PhraseCellFlagB` | `$04:$597D` | register allocation: the ROM keeps the instrument in c and spills r (first assigned in both branches); ours spills the instrument from its definition. PhraseCellFlagA/C (r = 0 at the top) match |
| `PhraseCellFlagD` | `$04:$5AC7` | same as PhraseCellFlagB; a separate j = i + 0x10 keeps i in c but spills j |
| `GfxCopyVramRegion` | `$1E:$7A4B` | `WriteVramByteSafe(dst++, wVramCopyScratch = GfxReadVramByte(src++))` gives the right order and both reversed pairs but a 2-byte frame: SDCC's `=` hands the push the call result (no spill location); the ROM re-reads the global, so its temp spilled into it. Both `src++`/`dst++` old-value pairs are reversed (`ld b,(hl); inc hl; ld c,(hl)`): c is busy at both, so a byte temp lives across the second with the call result spilled *into* wVramCopyScratch (one-byte frame, `ld (hl),e`, re-read for the push); SDCC's `=` hands the push the call result, which has no spill location (frame 2), and the two-statement form frees c |
| `DrawFreedTables` | `$1F:$5D18` | nested loops: the ROM keeps IsTableFree(j)'s result in c to the inner loop's end (j++ goes through b) yet packs FindDupTable's result straight into wPhraseDedupScratch5 (`ld (hl),e`); SDCC 2.2.1 extends both call results' live ranges (hasIncomingDefs), spilling one to a new slot (frame 7 vs 6) |
| `FindDupChain` | `$1F:$5644` | the first `SramChainPhrases + oa` is computed into a spill slot and copied byte-wise into pa (frame 11) while the second goes through bc; both scans keep their byte values in b/c with one i on the stack (block-scoped locals get their own slots here) |
| `DrawPhraseCellValue` | `$1F:$6B3B` | the `hGameboyType ? 3 : 2` temp sits in b and the width read spills into wDrawTextScratch (re-read for the push); SDCC spills the ?: temp and keeps the width in c (frame 3 vs 2) |
| `SynthReadWaveSram` | `$1D:$5F90` | shape matches (xor, two sign-extended stores through a 2-byte slot, p++) but the ROM keeps i in c for the whole loop and spills v and the int temps; SDCC spills i for the 2-register cast temp (`int t`, `register`, `volatile v` and statement splits tried) |
| `EnableSerialSync` | `$1C:$7310` | the inner loop's y is spilled in the ROM (every local is: x, y, p, the bound boolean, the p+y temp); SDCC keeps y in b since it is an operand of the ic whose allocation spills (CopySelection is the same shape with the boolean in c) |
| `CopySelection` | `$1C:$71CD` | see EnableSerialSync: the ROM spills y where SDCC keeps it in b |
| `CountFileBlocks` | `$1C:$48D4` | `(*wFileBlockCountPtr)++` on the global pointer: the ROM reads the pointer global for both the read and the write (frame 7, the read value spilled into the 32-bit compare's slot); SDCC copies the pointer into a spill slot for the write (packRegsForOneuse refuses across the intervening pointer get); UWORD-with-cast and volatile forms differ more |
| `FileUpgradeClearRange` | `$1C:$65B9` | all but `v` (the transpose being rescaled): the ROM keeps it on the stack with every inner-loop temp spilt, SDCC gives it c; `(UBYTE *)0xDA00 + (UWORD)t` reproduces the reversed-pair index and literal far-call addresses the `ld hl,#fn; push hl` |
| `FindFreeBlock` | `$1C:$415A` | UWORD p/end with `(p - end) & 0x8000` (or int p/end) gives the ROM's spilt signed difference and `bit 7` test, and UWORD p the plain `push hl` of the SRAM address; what remains is one allocation: the ROM spills `end` and keeps the ReadSongByte result in c, SDCC keeps end in bc and spills the result (leastUsedLR prefers the smaller symbol on a tie) |
| `NameEditorInput` | `$1B:$53E8` | all but one assertion: before storing the letter the ROM computes wNameEditBuf + 1 byte-wise into b/c and tests it with the signed add form (`ld e,b; xor a,#0x80; ld d,a; ld hl,#0x8000; add hl,de; jp nc`, i.e. `(int)(wNameEditBuf + 1) > -1` -> DebugTrap); stock SDCC emits the unsigned sbc chain for that spelling and spills an `int t` local, and gb_cmp16 has no signed variant yet |
| `DrawKitInstr` | `$1F:$44DD` | all but the HALF/FULL test: the ROM keeps `ReadCurInstrByte(2) & 0x40` in b and tests it (`ld b,e; ld a,b; and a,#0x40; ld b,a; or a,b; jp z`, the GetInstrFlags1F5 shape); `if (x & 0x40)` gives `bit 6,a`, a stack local the spilt form, and `t &= 0x40; if (t)` the genAnd internal error (gen.c 3110) |
| `ClearOamBuffer` | `$01:$4B5E` | `for (p = 0x8000; p != 0x8030; p++)` with a `volatile UWORD p` (the unsigned-long literal compare's four-byte temp and the in-place increment match), a pointer-parameter cast per call and the second loop's `((UBYTE)p & 2) ? 0x60 : 0` inline; the ROM spills each loop's volatile-read temp (the pushed pointer copy, the `(UBYTE)p` byte) to the stack where SDCC keeps them in bc/c (19 rows); `/tmp/opencode/c/b013best.c` |
| `UiClearSram` | `$01:$45D1` | matches end to end with `UBYTE * volatile p` except the "W-x" word loop's `*p += i < 10 ? '0' : '7'`: the ROM spills the `*p` read to the stack (frame 7) while SDCC keeps it in c; the pointer copy, the `?:` in c and the sum all agree |
| `ChainPasteCells` | `$06:$6F0D` | matches with `volatile` x/y, `char` booleans and gotos except the WriteChainPhrase address: the ROM stores `fn = 0x48C5` to a late spill slot and copies it into a function-pointer temp (no constant folding, slot below the temp); SDCC folds the literal into `ld hl,#0x48C5` unless fn is volatile, which puts its slot above the temp |
| `FindDupPhrase` | `$1F:$549C` | four 16-byte SRAM compare loops over two phrases; matches end to end with `volatile UWORD` pointers (`x = *(UBYTE *)p; p++;` is the ROM's fetch-then-in-place increment), a `volatile UBYTE` counter, block-scoped `x, y` per loop and the section bases as `(UWORD)(UBYTE *)...` casts, except that the ROM's first loop keeps the p byte in b and the q byte in c (the later loops c/b as SDCC does): one register swap; `/tmp/opencode/c/fdp_best.c` |
| `FindDupTable` | `$1F:$573C` | the same shape as FindDupPhrase over the table rows ($B680 + offset), the same first-loop register swap |

## What's still unknown

1. **Build options.** Default loop optimisations: invariant motion is on
   (`FileForEachInstrType`), and the code that once looked like it was
   compiled without it (`CloneSynth`, the three frame drawers) is stock
   SDCC 2.2.1's one-level hoisting from stale dataflow (see Writing C).
2. **The complete compiler model.** An accumulator-tracking pass plus 79
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
