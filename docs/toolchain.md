# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95-3** (19 Aug 2000) → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker, small memory model — **plus an accumulator-tracking optimisation stock SDCC lacks** (the "modified GBDK"), modelled here as an accumulator-tracking pass plus 86 peephole rules (below), **and one iCode difference**: no SSA copy when a call's result is assigned to a global (patched into the native build, see *Writing C*) |
| Can we run it today? | **Yes, natively.** SDCC 2.2.1 + `sdcpp`/`as-gbz80`/`link-gbz80` build from the GBDK 2.95-2 sources (`tools/toolchain/build_sdcc221.sh`, ~15 s) and emit **byte-identical** asm, objects, listings and images to GBDK 2.95-3's Win32 binaries — which still run under Wine (`fetch_gbdk295.sh`), but crash on large units (below) |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock output does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95-3 lacks |
| Matched real LSDj functions? | **Yes — 671**, in 13 banks (the list: [`c/matched.json`](../c/matched.json)): a message renderer, a 15-case `switch`, a sparse `switch`, the SRAM accessors, and the instrument screen's field accessors and draw helpers. `make match` re-verifies them all (cached: only changed sources reach the compiler), and **their bytes in the ROM build come from the compiled C** (below) |
| Is all of LSDj C? | **No.** ~200 functions (~15 KB, 10% of the code with the GBDK library) are hand-written assembly — the interrupt handlers, sound tick, envelope/pitch and tile-loader code in bank 0, and the sequencer core in bank `$02` (`tools/match/code_kind.py`). Those stay asm; the C is ~142.5 KB, 64% of it matched |
| Go/no-go on Track 2? | **Go, with the rule set still growing.** Every function tried has matched (each new one has cost a few rules so far), the C feeds the ROM build with `make compare` as the gate, and the hand-written asm is mapped and excluded |

## Matched functions

| Source | Functions | What they exercise |
|--------|-----------|--------------------|
| [`c/bank00/DrawFields.c`](../c/bank00/DrawFields.c) | 7 functions: tempo, channel header, duty and pan fields | `switch` chains with `?:` returns, a register pair plus a constant, a block-local initialiser |
| [`c/bank00/FarCall.c`](../c/bank00/FarCall.c) | 5 functions: the far calls passing arguments or returning the callee's byte (FarCallByte, FarCallArgByte, FarCallArg2V, FarCallArg2, FarCallArg3) | the callee called through a function pointer cast from a `UWORD` address (`r = ((UBYTE (*)(UBYTE, UBYTE))fn)(a, b)`): the cast is the copy into b/c, landing reversed (`ld b,(hl); inc hl; ld c,(hl)`) because `old` takes c first and is then spilled for it; a function-pointer parameter is called straight from its stack slot. lsdj.h declares FarCallByte and FarCallArg2V with function-pointer parameters for their callers, so FarCall.c defines `FARCALL_C` to drop those two prototypes. SDCC's return label after the `jp (hl)` is the `.resume` local |
| [`c/bank00/GbdkLib.c`](../c/bank00/GbdkLib.c) | 4 functions: GBDK's `abs`, the console `printf` and its number printer (`Abs16`, `DrawFormat`, `DrawNumber`), `strlen` (`StringLength`) | GBDK library C under the stock rules (`@stock-rules`), the 2.95-2 sources as they stand: `abs`'s `num < 0` and printn's `(int)u < 0` are the signed add form against 0 (`gb_cmp16` still runs for this unit), printf's `...` through GBDK's gbz80 `stdarg.h` macros (lsdj.h's fixed-argument DrawFormat prototype hides behind `GBDKLIB_C`), printn's digits a string literal emitted right after it (`DigitChars`); 2.95-2 hands both an emitter, the ROM's call putchar (DrawStringIndexed) and puts (DrawString) directly |
| [`c/bank00/Helpers.c`](../c/bank00/Helpers.c) | 7 functions: bank 0's shared C helpers | kit bank probes through a `volatile` MBC register, a word compare, the clipboard cell accessors, sign extension into an `int` array with a 32-bit `\|=`, a re-typed pointer argument |
| [`c/bank00/InstrByte.c`](../c/bank00/InstrByte.c) | `WriteInstrByte`, `ReadInstrByte`, `ReadCurInstrByte` | byte args, pointer local, 16-bit shift loop, asserts (one compound), both forms of pointer + byte |
| [`c/bank00/Main.c`](../c/bank00/Main.c) | `InitAndLoadSong`, `ProcessInput`, `main` | the boot `switch`, an endless frame loop with `continue`, an `\|\|` chain, a second absolute `sfr` name for a plain global, `?:` into globals, `FarCallArg` with a plain function pointer; ProcessInput's `do ... while (--wLinkQueueCount && buttons == 0xFF)`, `if (buttons) wKbdHeldNote = 0; else return 1;` (the return laid out after), `if (ok = HandleButtonEvent())` (the call result tested in b, copied to c), `wKeyDelay == wKeyDelayTimer` / `wButtons == buttons` (the operand to load into `a` on the right), the transpose stored and then pushed as `wSongTranspose` (an assignment's value pushes the right operand again), `char wChainRow` (copied through b to be pushed), and SndGetChainPhrase declared with its real arguments under `SND_DIRECT` |
| [`c/bank00/ReadSeqByte.c`](../c/bank00/ReadSeqByte.c) | `ReadSeqByte` | `?:` on bit 7 of a call result |
| [`c/bank00/ScreenDrawDispatch.c`](../c/bank00/ScreenDrawDispatch.c) | 6 functions: the screen dispatchers, the B combos (mute, pan, solo and their release) | 15-case `switch` → `jp hl` tables, bank switching, cross-bank calls, case bodies out of value order, an `&&` chain; HandleInputCombos' `wChanOutput[wSongCol] &= 3` after `hSoundCh = wSongCol` (the address from the stored value, the operand reread), `wSongCol == hSoundCh` in the loops (the global left: `cp a,(hl)` on the loop test's `a`), `!(wKbdActionCh == 0xFF) ? wKbdActionCh : wSongCol` (both booleans materialised), a two-case `switch` for B+LEFT/RIGHT (compares sorted, bodies in source order) and the saved pan in its own local (`v = wChanOutputSoloSaved[hSoundCh]; if (v) ... = v`) |
| [`c/bank00/Sprites.c`](../c/bank00/Sprites.c) | 10 functions: the sprite helpers | literal-pointer stores, a register pair plus a constant added in place, `v == *p` |
| [`c/bank00/Sync.c`](../c/bank00/Sync.c) | 18 functions: sync settings, transport and clock, the overload message, unchecked sequence/chain reads, the cartridge test's ROM checksum | literal bank switches around cross-bank calls, a message string in another bank, an early `return` under `&&`, nested `else if` with early returns; CheckRomChecksum: `(UBYTE *)0x2000` as a hoisted cast temp (SDCC folds no literal-to-pointer cast), `return (sum & 0xFF) == lo && (sum >> 8) == hi;` in value context (genAndOp evaluates both), the `& 0xFF` narrowed to a byte in c and then the `cp a,a` miscompile, and a `volatile UBYTE hi` so the final test widens `hi` again instead of reusing the subtraction's cast (CSE) |
| [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) | `DrawMessageBar` (198 B) | globals, `sfr` HRAM stores, two loops, calls |
| [`c/bank01/SongAlloc.c`](../c/bank01/SongAlloc.c) | 14 functions: song allocation bitmaps, 16-byte row copies | a bit test on the chain bitmap (`1 << (chain & 7)`, whose shift count dies to `ld b,#1`) |
| [`c/bank01/UiHelpers.c`](../c/bank01/UiHelpers.c) | 27 functions: small UI helpers, the cartridge test and SRAM test, the play/work clocks, instrument names, project settings, the screen palettes, the top bar and command help, the crash entry and the crash screen | `?:` over string literals, a `char` local (its dead `ld b,c` copy), a glyph table, a file-scope `const` table flushed ahead of the next function's strings, a `volatile` scratch byte so `++` is not folded into the store, `v = f(); v++` reusing the call register, a 12-case `switch` chain, a `switch` whose `default:` comes first and `goto`s into a later body, a global read as a word for a 32-bit `<`, a `UBYTE * volatile` loop pointer (kept on the stack, its `+ 1` through a register pair), `char` booleans stored and then tested, a stack-vs-stack `==` whose operand order avoids SDCC's `cp a` miscompile; DrawCrashScreen's `LCDC_REG &= ~2` (the int-promoted copy lands in c: `ld c,a`, never read; `0xFD` or `0xFDU` gives none), its strings as literals laid out right after it (the ROM's `Txt*`), `for (i = 0; i < 10; i++)` over an `int` (the signed add form, `#0x7FF6`) and a `while (1);` after which SDCC still emits the epilogue (4 dead bytes); ClearOamBuffer (a plain `UWORD p` compared as a long and cast for every use -- the cast copies spill themselves since p is their operand; `WriteVramByteSafeP` takes the pointer; the WAV sprites through a shadow-OAM struct pointer, `ld de,#addr; ld (de),a` stores); UiClearSram (a plain `UBYTE *p` for the same reason: its copies and the word loop's `*p` read spill themselves, `memcpy((void *)p, ...)` copies it into bc for the push, a volatile byte for the third saved property) |
| [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) | `ShowOutOfMemory`, `ShowTapAddChain` | one-line wrappers, 16-bit argument |
| [`c/bank02/Sound.c`](../c/bank02/Sound.c) | 45 functions of the sound engine's C: FX commands E (its note-on half, the per-channel envelope restarts, the release's direction), K, O, P, R, T, Z (the repeat and its phrase and table-column entry points), a delayed K's slot firing, the speech player's step, the synth's frame stepping, live mode's chain end (a queued stop or song row) and its queued starts, the noise channel's chord step, the table MayBe hop, Z's random range, E's release speed, the note kill, the WAV level step, the play range and live mode's queued starts and stops, the START and SELECT+START play toggles and the current screen's playback start, a channel's start from its next song row and the song's start at the cursor (each chain walked back past empty rows; from another screen's chain row, each chain starts at that row modulo its length), R's retrig firing, P with PITCH on STEP (an immediate pitch change), F on the wave channel (a synth's wave frame, a kit's sample positions), the noise channel's per-tick P slide and vibrato, the engine tick, the chain-phrase lookup | a pointer local indexed with constants (`q = wChanLiveQueued; if (q[1] && !wChanActive[1])`: `q[1]` is a 16-bit `ld hl,#1; add hl,de` into a stack temp where `wChanActive[1]` is a direct address; SndLiveStartQueued, SndNoiChordStep), SndSpeechStep's `n = wChanNote[2] = v + 1` (the store's address loaded before the sum) and its `rRAMG = 0x0A; x = *p; rRAMG = 0;` (the MBC registers are `volatile` in lsdj.h, or SDCC drops the first store as dead), Z's value as one expression over `wChanCmdVal[slot]` (the address kept in a stack slot, the digits read through it once), R's `n = hFxValue & 0x0F; n = MulU8(n, wChanCmdRate[hSoundCh] + 1)` (the ROM computes the first argument first; arguments are evaluated right to left), StartScreenPlayback's repeated `wSongCol` reads (the second one becomes `ld a,c`, see `track_a`), a `?:` assignment keeping its value in a register (`v = right ? a : b`), a mask compared before the call it is compared with (`(v & 0xF0) > Random16()`), `Random16` declared as returning a byte, a pointer sum assigned with no cast in between, a const table flushed after its function, a local copy of the volatile `hSoundCh` shared by later stores (`k = hSoundCh;`), `wSongRow == *p` (the global left of `==`), `wChainRow` a `char`, `hFxValue` read both as an sfr and as a plain global (`hFxValueMem`), `!=` against a global putting the not-equal branch first, `while (v && wWavLevel)`, StartSongPlayback's chain-length scan with both tests stored (`while (!(stop = wChainRow < n) && !(end = SndGetChainPhrase(*chain, n) == 0xFF)) n++`) and pointer locals for the channel's cells (`*active = 1`), FxCmd_F_Wav's `wWavSynthFrame = hFxValue + wWavSynthFrame` (an sfr on the right of `+` loads over the left operand in stock SDCC: `ld a,(hl); ldh a,(X); add a,a`), an if/else rather than an early `return` (the inner test then jumps straight to the end), `off & 0xFF` (the low byte through c, the AND's temp unpacked) and `hFxValueS`/`wWavLevelS`, the two bytes as signed globals so one cast widens them (a `(BYTE)` cast first leaves a byte temp in c), SndNoiPitchStep's `v & (UBYTE)3` (a plain `& 3` int-promotes `v` into a two-register temp, spilled to the stack while b and c hold the sign and the value) and the vibrato offset in its own local (`v` reused stays live over the test, pushing the test's temp into b) |
| [`c/bank02/SoundChan.c`](../c/bank02/SoundChan.c) | 3 functions: the sequencer's per-channel tick (SndUpdateChannel: the next phrase or chain row, then the row's B, D, G and H) and its step read (SndReadChanStep: the note or the PS/2 keyboard's, the groove less any D delay, the instrument, the transpose) and the phrase's H hop (SndPhraseHop: HFF stops the song, else after the high digit's count of hops to the low digit's row of this phrase or the chain's next) | the working pointers (`wPtrNoteSrc`, ...) as pointers, which Sound.c declares as bytes for SndAdvanceStatePtrs (one view per file); `*wPtrNoteCountdown > wChanDelayTicks[hSoundCh]` with the operand the ROM reads first on the left; a `switch (SndReadPhraseCmd())` whose case 8 body comes first in the source (the chain's last `jp z` folds into `jp nz`) and whose case 0 shares the switch's end with a falling-through case 7; B's and D's values in separate locals (the ROM gives them separate stack slots); `wSongCol == hSoundCh` (the global on the left); `if (0xF0 & v)` for H's register-held mask |
| [`c/bank03/InstrScreen.c`](../c/bank03/InstrScreen.c) | 65 functions of the instrument screen and its input | field get/set/step helpers, `FarCall` in both address forms, SRAM literal addresses, a string literal placed after its function, `hl` kept across a `!=` compare; InstrScreenInput (9.8 KB) is one `switch (wButtons)` (sorted compare chain, `case 0x10` tested through the CSE'd `wButtons == 0x10`, bodies in the ROM's order) whose A+direction cases switch on the type and then the field (jump tables for contiguous fields, compare chains otherwise; a field's body can come before a lower field's, so the labels follow the tables, not the layout): the kit's fields 9/11/13 are `case (int)9:` (int-typed constants: a 16-bit compare of a copy widened from `a`), `v = ReadCurInstrByte(n); v op= k;` where the ROM stores the read before using it and `v = ReadCurInstrByte(n) op k` where it does not, `v &= 0xFC; v |= 2;` (an OR with a literal never leaves its operand in `a`), the flag toggles' `v & 64` / `v & 128` (decimal: signed literals copy `v` into a register for the `bit` test), `v = (int)(char)v & (int)0xBF` (a 16-bit AND narrowed straight into the stack byte), block-scoped `kit` locals (two same-named block locals at one level become one symbol), and `FarCallByteU(0x1F, InstrEnvHasSecond)` for callees declared returning UBYTE; SetVibratoShape (a `char` parameter whose OR reads it through a cast placed between the mask and the OR, so the masked byte keeps c -- see Writing C: accumulator packing; the callees take a `char`: `WriteCurrentInstrByteC`, `FarCallArgWC`) |
| [`c/bank1c/FileScreen.c`](../c/bank1c/FileScreen.c) | 41 functions: file screen and the song compressor | CompressSong (the `cur == next` operand order, `wCompDst + 2` read twice through one CSE'd temporary, `wCompSrc & 0x100` / `wCompDst & 0x8000` as `bit` tests of the high bytes), field highlights from column tables, a `do … while (--i)` clear (CSE shares `i - 1`), the serial receive wait, `?:` returns; DrawFileTextField's frame loops (bounds inline, see Writing C: hoisting depth); FindFreeBlock (the `?:` over two `long` literals copied into a `UWORD end`, the signed-difference loop `while ((p - end) & 0x8000)`, the call result unnamed so `end` is the spill victim, `+ (UWORD)0xA000` to keep the block address 16-bit, both range asserts reading back through the pointers) |
| [`c/bank1c/FileUpgrade.c`](../c/bank1c/FileUpgrade.c) | 43 functions: the song-format upgrades | per-type instrument callbacks through a function pointer (`FileForEachInstrType`), the upgrade steps' field fix-ups, a 16-bit `\|` forced by an `int` constant, FileUpgradeV11's aliases (`ReadSongBytesW` takes the UWORD SRAM cursor, `ReadInstrByteS` returns `char` so its result keeps a slot of its own; FileUpgradeV18's `v = types[i] == 3` gets the same copy from the comparison's `char` result) |
| [`c/bank1d/SynthScreen.c`](../c/bank1d/SynthScreen.c) | 55 functions: synth and groove screens with both screens' input handlers, the per-wave parameter curves and their loader, the LOWP/HIGHP filter | a `switch` through a jump table, SRAM param/groove accessors, a `?:` over `long` constants (> 32767), the cache walker; SynthFillWaveRange's `*buf++ = ((UWORD)i * b >> 4) + a` (the 16-bit product through MulU16, the sum operand order per branch), RenderSynthWave's curve buffers as real array symbols (`wSynthCurveSeg3[wSynthRenderIdx]` is the `ld a,#<sym; add a,c` form a cast literal would not give), SynthReadParam's `1 << (x & 7)` mask; SynthLoadSignalWave's `p = wSynthScreen; p <<= 4; p += (UWORD)0xAF00;` (shifted and added in p's slot; a bare `0xAF00` is a `long`) with a signed `char v` so the second nibble's cast copy, not the loop counter, takes the stack (see Writing C: an instruction's own operands); SynthLoadParams reads the 16 parameter bytes through `volatile UWORD` locals (`ReadSongByte(1, p++)`: the old value fetched into bc, the slot incremented in place, a plain `p` pushed straight from the stack), inline call results as arguments (a named result would hold c through the whole function), the last pair into two locals so the `+ 0x0F` adds come out in the ROM's order, and the render destination as a pointer cast of the sum; BuildOverwritePrompt's frame (the ASCII restore at the end of the middle-column branch, see Writing C: hoisting depth); GrooveScreenInput (`char wGrooveRow` against a decimal `16`, which needs no conversion where `0x10` copies it through `c`; `while (top != bot)` with the operand SDCC loads into `a` on the right; `end = bot; end++;` for `ld c,(hl); inc c`; `wSelBottom = wSelTop + 1` reusing `a`; `switch ((char)wMapCol)`); SynthScreenInput (`if (wButtons != 0x10) wDrawStep = 0;` before the `switch (wButtons)`, whose case 0x10 test CSE takes from that boolean in `c` -- see `switch_chain`; nested `switch (wSynthCursor)` jump tables whose case bodies follow the source order, fall-through chains for the cursor moves, a `default:` written first where its body comes first); SynthFilterLowHigh (the resampling filter: two scratch words serve as slot pointer and slot value in the loop's first half and as value and doubled index in its second -- `*(UWORD *)wSynthFilterPtr` through a `UBYTE *` global so the pointer's cast copy is spilt into the global itself and reloaded from it after MulU16; the index as a **block-scope `extern UBYTE`**, which this compiler treats as a register-equivalent local whose spill location is the global: the shift's temporary is spilt into $CB3A, both accumulator pointers are one 2-use temporary each, and the index is fetched from the global with `ld l,(hl)` -- a file-scope global would be copied per use (near-space globals are never replaced by CSE); `(ub - 0x100) & 0x8000` for the stored-then-tested sign of a temporary where `(int)(ub - 0x100) < 0` folds into an unsigned compare) |
| [`c/bank1e/Gfx.c`](../c/bank1e/Gfx.c) | 5 functions: the top row's column labels, the font loader, VRAM helpers | `UWORD` loop addresses compared against `(UWORD)0x9800` (constants above 32768 are `long`), a `while (STAT_REG & 2)` wait; ScreenPreDraw's `switch (wCurrentScreen)` with its bodies in source order (1, 3, 5, 2, 12, 4); LoadFontTiles pushes its two spilled DMG blocks through `(UBYTE *)(UWORD)` cast temps (the ROM's `ld c,(hl); inc hl; ld b,(hl); push bc`); GfxCopyVramRegion (`WriteVramByteSafeC(dst++, wVramCopyScratch)`: the char value's cast copy of the scratch global is the pointer copy's register occupant) |
| [`c/bank1f/InstrHelpers.c`](../c/bank1f/InstrHelpers.c) | 82 functions: bank $1F helpers, the instrument screens' field draws and the word screen's input | the used-table/phrase/chain scans and counters (one template, four limits), SRAM resets bracketed by `_asm di _endasm;` and MBC register writes, a function that returns `e` by accident (no `return`); RemapCmdTableRefs scans three SRAM ranges through `volatile UWORD` pointers (`*(UBYTE *)p` reads via de, in-place increments, `p != (UWORD)0xB880` byte-wise), `from == *(UBYTE *)q` (the global on the left), a `volatile UBYTE` counter and a `volatile` remapped byte for the instrument loop; FreeUnusedInstrs (CLEAN INSTR DATA's instrument pass) calls `FarCallArg2P(4, FAR_FN(ReadPhraseInstr), ph, row)` (the literal through two cast temps), builds the parameter address in place (`addr = i; addr <<= 4; addr \|= 0xB000; addr += 0x80;`), clamps `UBYTE n` with `if (n & 0x80)` (`bit 7`) and keeps `wColInstrument[k - 1] == i`, which SDCC compiles to `cp a,a` (always true); DrawPhraseSelection's loop bounds are `?:` expressions (`x < (right == 5 ? 5 : right + 1)`: the test and the `+ 1` hoisted into stack temporaries, the choice made in b each pass), its ring test an `\|\|` chain over hoisted and per-row booleans, and the cursor column read into a local for the first comparison only (the ROM's `ld c,(hl)` before `sbc a,c`); CutCopyPhraseSelection (a 70-byte frame: every call result and boolean in its own slot) is a five-case `switch` whose case 0 can `goto` the case 2 label, a `UBYTE cutInstr = cut;` copy (CSE reads the copy for the message choice), and far calls short one argument (`FarCallArg2P(4, FAR_FN(PhraseReadNote), y)`); WordScreenInput (a `switch (wButtons)` whose case bodies run in source order, `(UWORD)` on every SRAM literal above `0x7FFF` -- a bare `0xBDD0` makes the address arithmetic 32-bit --, `p++` arguments, CSE sharing `(UWORD)wSpeechCursorRow` between the `<< 5` and the `* 3`); DrawKitInstr (`UBYTE v, flags`: byte 5 in a slot of its own, `DrawStringIndexed((char)v)` for the volume digit's copy through b, `0x40 & ReadCurInstrByte(2)` for the SPEED test); FindDupPhrase/FindDupChain/FindDupTable (the duplicate scanners: `UWORD t` declared last for the first row address, `p = (UBYTE *)t`, the other rows cast from `oa + (UWORD)base` temporaries, `if (*p++ != *q++) return 0;` for the byte loops); DedupPhrases/DedupChains/DedupTables (the DE-DUPE passes: plain `UBYTE i, j` counters, `i = i + 1`, the inner call result and the FindDup result stored to their scratch globals before the test, `DrawString("OK")` -- the shape only LSDj's compiler gives, see *Writing C*: `global = call()`); HighlightPhraseCell (the cursor/selection colouring: `char x`, the width stored and passed as a comma expression in the argument, see *Writing C*) |
| [`c/bank04/PhraseScreen.c`](../c/bank04/PhraseScreen.c) | 44 functions: phrase screen and its draw, its row loader, the four A+direction cell editors, instrument entry and name draw, prelisten, the screen's input | DrawPhraseScreen (file-level `const` attribute rows defined just before it, which SDCC flushes after its code and ahead of its string literals; a `switch (ins)` whose `== 0x40` test CSE reuses later as a stored boolean; `KitIndexToBank((char)t & 0x3F)` for the second sample's copy through `c`; a nested `?:` for the attribute row; `!wPhraseCol` and `(char)wPhraseRow`), far-call wrappers (`FarCallArg2V`), the phrase instrument reader and the backward search for the instrument in effect (a return through `e`, `track_e`), `?:` returns, a `volatile UWORD` SRAM offset (no CSE of its casts: each push reloads it), `if (a != X && b != Y) return;` chains (the ROM's uncollapsed `jp z,L; jp done; L:`), a name copy through `char` casts, a `UWORD addr = (UWORD)p` copy (a named spill slot the allocator evicts instead of the value), a `wSongCol == 3` test the ROM shares between an `if` and a later `\|\|` (one CSE temp without a spill slot, so the value spills instead); PhraseScreenInput (8.6 KB) is ChainScreenInput's `if` chain over the five columns, its A+direction blocks walking the rows with a per-column `switch` (sorted `case` tests, bodies in source order, the value column's per-command clamps), the command letter stepped in a `do`/`while` over `PhraseStepOnes[(UWORD)wSongCol * 20 + cmd]` (MulU16 and a 16-bit index; `wSongCol * 20` alone is 8-bit) and the kit sample nibbles scanned past empty names: `wButtons == 0x10` is written twice (a named `isA` is the spill victim when the long-lived `wCursorMode == 4` boolean wants its register; the CSE'd test keeps b), `(UBYTE)(char)wPhraseRow >= 0x10` (an unsigned test of the register copy), `(char)wChainRow` far-call arguments, a `char chainRow` that keeps `chainRow + 1` across SndGetChainPhrase for the store after it, `wPhraseCol == 0` (not `!wPhraseCol`), `wInstrument = instr; if (wInstrument == 0x40)` (the stored byte tested in `a`), PhraseEditPlace declared with the phrase and row its caller pushes but never reads; PastePhraseSelection (the 27-byte frame of a paste: locals declared in the ROM's slot order `char x; UBYTE y; ... UBYTE c4, c3;`, `switch (wSelRight - wSelLeft) { default: return; case 2: ... }` with the default first -- a single-case switch with the default last is inverted into one `jp nz` --, a nested switch on wSelLeft for the command pair's shift, `for (x = wSelRight - 1; !(x < (char)wSelLeft); x--)` for the signed column loop with its materialised test, `switch (x)` for the five column pastes (its range checks are the two stored booleans before the jump table), `wPhraseNoteIsKitA = CursorInstrIsKit()` -- the call-result temporary LSDj's compiler keeps, spilt into the global --, and `if ((char)wPhraseRow == 0x10)` for the row wrap (the cast copies the incremented row into c)) |
| [`c/bank05/TableScreen.c`](../c/bank05/TableScreen.c) | 32 functions: table screen (draw, step reader, play markers, cut/copy/paste, selection, input) and screen map (draw, enter) | table step accessors through column pointers held in `UWORD` globals, the map moves, materialized `<=` tests, a two-case `switch`, six row-buffer column pointers (`t += (UWORD)0xBC80` keeps the add 16-bit), a `switch` with `default:` between `case 0` and an empty `case 0xFF`, the loop counter reused for the cursor x so the column index lands in b; CutCopyTableSelection's six-column `switch` (a jump table whose range test, like `x == 6`, is hoisted out of the row loop as a stored boolean; each case's read result gets its own spill slot) behind `if (x == 6 && y) continue;`; TablePasteCells is the same loop and `switch` over clipboard reads, with `UBYTE swap = 0, fromPhrase = 0` initializers and ChainPasteCells' `(char)wTableRow & 0x0F` wrap; DrawTableSelection sorts its rectangle with SortBytePair on address-taken locals and reads `bottom = (char)wTableRow` / `right = (char)right + 1` through b and c; TableScreenInput (5.7 KB) is one `switch (wButtons)` whose bodies run 0x43, 0x10, the directions, A+directions, B+directions, A+B, select+A, start, select+directions, B, select+B -- its `case 0x10` test is the CSE'd `wButtons != 0x10` boolean from the top (`or a,c`), each A+direction a row loop with a `switch (wTableCol)` per cell (command letters stepped with `wPendingCmds[y]++` / `while (TableInputFlag(...))`), `(char)wTableRow` copies again, `v = TableReadEnv(y); v += 0x10;` (the call's byte stored straight into v), and the groove lookup's addresses as UWORD sums with `(UWORD)wTableRow` (a real cast, which CSE keeps across the call; the pointer form's index is an assignment, dropped at calls) in block-scoped `a, b` |
| [`c/bank05/FieldHelp.c`](../c/bank05/FieldHelp.c) | `DrawInstrFieldHelp` (872 B) | a `switch` on the instrument type around jump-table `switch`es on the field; the help strings shared by several types are stored by constant address (byte by byte) and the rest by symbol (`ld de,#sym`), so the C spells the first group as `((char *)0x6D25)`; `== 0` on the PLAY field's ReadInstrByte (the `ld a,c; or a,a` compare, where `!x` is toBoolean's `xor a,a; or a,c`); the final `if (s)` reload is `ifx_word_hl` |
| [`c/bank06/Screens.c`](../c/bank06/Screens.c) | 53 functions: chain, wave and word screens (the chain screen's input too), the chain cell and sample-name draws, the per-frame status panel, the chain screen draw, phrase open/clone | `while (i != wWaveSelEnd)` (global right: the kept `jr nz` trampoline), `(UWORD)y != 0x80`, an early `return` under `==` (SDCC's `jp nz,L; jp end; L:`), the chain row buffers through `ReadSongBytes`, `?:` over string literals as an argument, string literals declared as the ROM's `Txt*` symbols where the unit's data block sits elsewhere, `(UWORD)t < 0x78` (a 16-bit `add hl` compare off a just-stored temp), `if (n != 0)` keeping the reload of a pointer-read value, a far call by literal address (`FarCall(5, 0x747D)` pushes `ld hl,#nnnn`), `(char)` casts on far-call arguments (their temps take c, so the function-pointer temp lands in b/c reversed), eight SRAM row pointers where the two bank-2 ones are *recomputed* (CSE copies them from the first pair's spill slots; writing `s3 = s1` adds a use and keeps `s1` in bc), the wave copy's nibble pairs read as `ReadWaveSample(i) << 4 \| ReadWaveSample(i + 1)` (a named temp would stay live across the second call); CutCopyChainSelection's per-column work is a `switch (x)` (both case tests hoisted out of the row loop as stored booleans, the bodies laid out after the tests; an `if`/`else if` puts the first body between them); ChainPasteCells is the same `switch` with `FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), ...)` (the literal copied between two cast temps, which a `(UWORD)` argument folds away) and `wChainRow = (char)wChainRow & 0x0F` (the row read into c first); DrawChainSelection is DrawPhraseSelection's twin over the two chain columns (`hGameboyType ? x : 0` as the ring's palette); WaveScreenInput is a twenty-case `switch (wButtons)` returning 1/0 per case, its bodies in source order ($43 first) and the cursor wrap `(char)wWaveCursor & 0x1F`; DrawWaveColumn passes its tiles as nested `?:` arguments (the outer temp stored, an inner one copied over it) and tests a `char` copy of the column (`t = col; if (t & 8)`, the copy in b); ChainScreenInput (4.4 KB) is an `if` chain on wButtons whose four A+direction blocks walk the selection with SortBytePair'd bounds and a two-case `switch (x)` per cell; `wCursorMode == 4` is written at each use (CSE keeps one stored boolean, a temporary below the named locals), wChainCol and wChainRow are read through `(char)` copies (char locals `cx`/`cy` kept in c/b across the selection test; `(char)wChainRow` arguments reverse the far calls' function-pointer pair), `++x`/`--x` in the B+LEFT/RIGHT channel loops (an `inc (hl)`; `x++` keeps the old value), `wChain = GetCursorChain(); if (wChain & 0x80)` as two statements, `v >= 0xEF` for the 0xFE clamp laid out first, and `wSelLeft != wChainCol` (the global to load into `a` on the right); LockCurrentSynth (`s = wSynth; s >>= 4;` so the address spills before s, a named `r |= 1 << (s & 7)`, and a frame byte that only receives s -- `volatile UBYTE m; m = s;` declared before p for the slot order) |
| [`c/bank06/DrawTileHexCell.c`](../c/bank06/DrawTileHexCell.c) | `DrawTileHexCell` (130 B) | consecutive updates of a stack local, sign test, `-t`, pointer table |
| [`c/bank07/KbdLink.c`](../c/bank07/KbdLink.c) | 11 functions: keyboard octave, the link queue and its scancode dispatcher, keyboard notes, sync rate, a hex digit, the keyboard instrument name/state and calibration screen | a multiply kept as a `MulU16` call, `di`/`ei` around a 16-bit store, an empty case listed first; `UBYTE * volatile` pointers to `&wKbdChanState[1]`/`[2]` (a single-definition pointer local is otherwise rematerialised) and `wKbdChanState[3] = 0` through the array (the ROM's `ld de,$cb99; ld a,0; ld [de],a`); the name copy as direct stores, `wSampleNoteString[i] = wNameEditBuf[i] ? wNameEditBuf[i] : ' '` and `wSampleNoteString[1] = SongHexDigit(…)` (the address temps live across the call and share one spill slot; naming the address in a pointer local keeps the counter in c instead of on the stack); ProcessKbdNote's `armed = wSongCol == 3 \|\| !FarCallArgByte(...)` as one expression (the `==` temp is what the far-call pointer evicts from c, so the pointer lands b/c reversed), `wKbdHeldNote != note` with the global on the left; ProcessLinkQueue's 28-case `switch` on a stack byte (the ROM reloads it for every compare), case bodies in the ROM's order with `case 0xC0: if (!(mod & 2)) goto note;` falling into `case 0x40`, `wKbdChanState[1]` (never the Pu2 symbol), and FarCallArg called with two arguments (`FarCallArg0`, aliased) |
| [`c/bank07/SongScreen.c`](../c/bank07/SongScreen.c) | 31 functions: song screen, its row palettes and play markers, the chain clone, bookmarks | page-up/down scroll arithmetic, bookmark add/toggle in SRAM, `WriteBookmarkByte`'s pointer-range asserts (16-bit compares: `ld hl,#-lit; add hl,de`), a `!=` sprite loop; the row-number column's `(row & 4) ? 1 : 2` (the int-promoted copy of `row` is stored to the variable's own stack slot and the register tested) and its second loop's `if ((BYTE)row & 4)` (a signed cast copy lands in b: `ld b,c; ld a,b; bit 2,a`); SongRowPalettes reuses one local for the row offset and the cursor flag (`cur = wSongScroll + y - 2; shadow = cur & (UBYTE)4;` … `cur = i == wSongCol`) -- the long-lived `cur` is what makes the allocator spill the bookmark flag instead of the counter, and `(UBYTE)4` avoids the int-promoted copy; `i == wSongCol` (register left, global right) is the `jr nz` boolean, `wSongCol == i` the `jp nz` one; SongUpdatePlayMarker's sprite index is written `i + 4` at each use (a CSE'd temp; naming it keeps `i` in c where the ROM has it on the stack); SongCloneChain's two pointers into the $C532 map are `wClonePhraseMap + 16` and `(UBYTE *)wClonePhraseMap + 16` (the cast keeps the second sum from being CSE'd into the first), `if (!(wClonePhraseMap[j] - ph))` for the materialised difference test, the `ph != 0xFF` deep-clone body first with the $FF write as the else branch, and the transpose read reusing `ph` |
| [`c/bank07/SongInput.c`](../c/bank07/SongInput.c) | `SongScreenInput` (3011 B) | its own unit (SongCopyColumn with UWORD parameters, as in SongCutCopy.c): an if/else chain on wButtons whose `wButtons == 0x10` / `== 0x20` / `wCursorMode == 4` tests CSE keeps as stored booleans; `UBYTE *p = (UBYTE *)0xB290 + ...` (a pointer add straight into p, where the UWORD sum and a cast go through bc) and `p += wSongCol` (a byte operand, no cast temporary); `(char)wChainRow` for the reversed `ld b,lo; ld c,hi` FAR_FN pair; comparison operands written the way round SDCC loads them (`wSelLeft != wSongCol`, `top != wSongScroll`) |
| [`c/bank07/SongCutCopy.c`](../c/bank07/SongCutCopy.c) | `CutCopySongSelection` (545 B) | its own unit: SongCopyColumn prototyped with two UWORDs (the ROM casts both pointer arguments; SongScreen.c defines it with pointers), `UBYTE * volatile` song pointers (reloaded at every use; the plain pointer would inherit the shifted offset's register pair), a `char` visibility flag, `x = (UWORD)p` as a cast (a plain copy is propagated away), `0xC0 - t` through a UWORD local, `isCut && !wSelLeft && wSelRight == 4`, the CUT/COPIED CHAINS literals after the function |
| [`c/bank07/SongPaste.c`](../c/bank07/SongPaste.c) | `PasteSongSelection` (415 B) | its own unit: `extern volatile UBYTE wSongRow` (the row-shifting loop recomputes `wSongRow + h` where a plain global shares the test's value), SongWriteSramBytes prototyped with UWORD source and destination (both pointers cast, the source through a reversed `b/c` pair), `(UWORD)0xC0 - (wSongRow + h)` for the 16-bit count, `(UWORD)p` compared against `(UWORD)0xB290`/`0xB590` (the CSE'd cast shares one spill slot with the clipboard byte), `if (!(... & 0xF0)) continue;` for the scroll step |
| [`c/bank07/SongSelection.c`](../c/bank07/SongSelection.c) | `SongEditSelection` (667 B) | its own unit: SortBytePair prototyped with `char *` (the `&UBYTE` locals go through cast temps: the first spilled below the frame's variables, the second in bc), the clamp/erase loops written as expressions (`while (wSongCol < (x1 == 4 ? 4 : x1 + 1))`, a four-way `\|\|` of `<`/`>=` tests each materialised into a spilled boolean temp: a temp defined in a loop is live to the loop's end), the 32-byte CGB attribute table (`const`) after the function |
| [`c/bank1b/ProjectScreen.c`](../c/bank1b/ProjectScreen.c) | 42 functions: project and help screens, the name editor, the envelope graph | property writes to SRAM, a busy-wait on a global, `(UWORD)x == 9` (a 16-bit compare: one `jp nz` per byte through one trampoline), `?:` string arguments, a NULL-terminated pointer table; DrawProjTextField, the same frame as DrawFileTextField with the field draw first ; ComputeEnvelope's three segment loops write `vol != vol2` (the ROM loads the target first) and shift by 3; ProjectScreenInput (the `switch (wButtons)` case 0x10 shares the `wTapTempoCount` test's boolean; `goto`s into other cases' blocks for the menu's left/right and the reset path -- the ROM's tail merging is source-level; `_asm ei/di _endasm` pairs; `wSyncMode != 0` for genCmpEq's `ld a,(hl); or a,a` where `if (wSyncMode)` is toBoolean's `xor a,a; or a,(hl)`); NameEditorInput (`wNameEditBuf` is `char[]`, so its RAM assertion `(int)(wNameEditBuf + 1) > -1` compares signed; the keyboard is `char AlnumChars[][11]`; `wKbdLetterPos = wKbdLetterPos + 1` stores the terminator's `+ 1` kept in c where `++` is `inc (hl)`); PlotEnvCurve (the graph buffer as a `char *`: the loop store's (char)y is hoisted and holds c until the column address evicts it, the compare at x reuses that signed copy -- see Writing C: a hoisted cast) |
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
| Constant reuse | `ld a,N` / `push af; inc sp` / `ld a,N` → reload dropped, also across `ldh (n),a` + `ld hl,nn; push hl`; `xor a` / `ld hl,sp+N` / `ld a,#0` → reload dropped; an sfr pushed twice, `ldh a,(X)` / `push af; inc sp` / `ldh a,(X)` → reload dropped (SndZPhrase) | 0 | 10, 5, 2, 1 |
| Compare chains | a sparse `switch` re-reads its global before every `cp` in stock output; LSDj loads it once, and an int-typed case widens the copy in `a` (`ld c,a` for `ld hl,#G; ld c,(hl)`, InstrScreenInput's kit chains) (one rule per chain position, up to twelve, so the pattern always carries the variable; `switch_chain` in peep.py drops what the rules leave of a longer chain, and after a case that tests a CSE'd boolean (`xor a,a; or a,c; jp nz`) reloads only `a`, as the ROM's three such chains do, WaveScreenInput's twenty `wButtons` cases). SDCC's `==` trampoline `jp cc,T; jr F; T: jp X; F:` → `jp cc,X`, every byte's `jp cc,T` of a multi-byte compare included (the only survivor in the ROM is inside GBDK's stock-compiled `gprintf`) | 0, 1 (library) | 72 |
| Kept across a test | `ld a,r` / `bit n,a` / `jp cc` / `ld a,r` → reload dropped; `ld hl,#x` / `ld a,(hl)` / `cp a,#n` / `jp z,F` / *(dead label)* / `ld hl,#x` → reload dropped (a `!=` test; stock SDCC's Rule 5 leaves the trampoline's label behind, which makes its codegen forget `hl`) unless a constant is stored there next (`ld (hl),#n`: ProcessInput's `wLiveStartCol = 0xFF`, the ROM's one store after such a test; `inc`/`dec (hl)` keep hl), also after `or a,a` (`x != 0`); `xor a` / `ld a,#0` → `xor a` | 0, 1 (a real jump target in between), 0; 0 for `or a,a` | 68, 7, 6; 1 for `or a,a` (ProjectScreenInput; 2 more in hand-written asm) |
| Register tracking (`track_a`, below) | a reload of what `a` already holds: `ld a,r` / `cp` / `jp cc` / *(dead label)* / `ld a,r` (an `\|\|` chain) → reload dropped; a stored stack local reread: `ld hl,sp+N` / `ld (hl),r` / [`add sp,M`] / `ld a,(hl)` → `ld a,r`; `ld (hl),a` / `ld de,#T` / `ld l,(hl)` → `ld l,a` (indexing a table by the value just stored) | 0, 0, 0 | 35, 25 + 141, 32 |
| More dead stores | `ld r,#n` / `ld r,X` → `ld r,X`; `ld r,a` / … / `ld r,(hl)` → `ld r,(hl)`; `ld (hl),e` / … / `ld (hl),a` → `ld (hl),a` (nothing touching hl between); a discarded sign extension `rla` / `sbc a,a` / `ld r,a` / `ld a,X` / `and a,#n` → store dropped; the `== 0` test of a fresh `&`: `and a,#n` / `ld r,a` / `or a,a` / `jp nz` → `and a,#n` / `jp nz`; after *ALU* `a,r` / `ld (hl),a` the `ld a,(hl)` reload is dropped too; `ld a,#n` / `ldh a,X` → `ldh a,X` (genCmp's `x > n` against an sfr, FxCmd_E_NoteOn's switch bound) | 0, 0, 0 (1 `jp z` / `if (t)` site keeps it), 0, 0 (no two loads of `a` in a row anywhere) | —, 3, 12, 69, 1 |
| 16-bit compare with a constant (`gb_cmp16`, below) | `xor a,a` / `ld a,lo` / `sbc a,#lo` / `ld a,hi` / `sbc a,#hi` / `jp cc` → `ld hl,#-lit` / `add hl,de` (or `bc` when the value already sits there) / `jp` on the inverted carry | 0 (its 5 `sbc` chains are 32-bit) | 28 |
| `ld a,a` | the MOVA SDCC leaves after reading an sfr for a compare → removed | 0 (the one `ld a,a` in the ROM is palette data) | — |
| Dead `a` load before a stack read | `ld a,X` / `ld hl,sp+N` / `ld a,(hl)` → first load dropped: `gencjneshort` fetches a stack right operand into `a` over an `a`-packed left (`ld a,(de)` / `ld a,c`, then the `cp a,a` miscompile in FreeUnusedInstrs and CheckRomChecksum) | 0 (any `ld a,X`, after `ld hl,sp+N` or `ld hl,nn`) | — (neither `cp a,a` site has the load) |
| `if (!(x & n))` trampoline | `and a,#n` / `jr nz,L` / `jp M` / `L: jp X` / `M:` (genAnd's jmpTrueOrFalse for a jump on true) → `and a,#n` / `jp nz,X` | 0 (the one `jr nz,+3; jp; jp` left in the ROM follows a `cp (hl)`: CloneInstrTable's trampoline) | 12 `and a,n; jp nz` |

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
exists in the ROM. `chained_bitwise` is the one exception to the `pop bc`/call/branch
limit: LSDj's compiler kept `x & n` in `a` when the next operation used
it with a literal (`and a,#0xF9; or a,#0x01`, 14 such pairs in the ROM,
never `and a,#n; ld r,a; or a,#m`), where stock SDCC gives the
temporary a register (opPreservesA wants a symbol on the other side).
The pass drops that `ld r,a` between two literal `and`/`or`/`xor` when r
is dead on every path, following jumps and calls (`b`/`c` survive a
call, `d`/`e` do not): FileUpgradeV3Pulse/V3Kit's `((char)v & 0xF9) |
1`. The opposite case is not modelled: with the AND result as the
*right* operand of an OR with a variable, stock packs it into `a` and
genOr emits `ld a,(hl); or a,a` (wrong code), where the ROM keeps it in
a register (parked SetVibratoShape). `gb_cmp16` also re-seats `hl` after the compare it rewrites: stock SDCC
leaves `hl` at the operand's high byte and may continue from there
(`dec hl; ld a,(hl+)`), while the add form clobbers it, so the ROM
reloads `ld hl,sp+0` (DrawBlocksUsed), or `ld hl,#G` for a global operand
(CompressSong's `wCompDst < 0x7FFF` before `wCompDst + 1`).
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
`ld hl,sp+N` (RemapSeqChains' `i = 0` after the di). The walk back to
the last `ld hl,sp+N` passes the fall-through of a conditional jump
(FreeUnusedInstrs' `i < 0x40` loop test right before its `di`).

One hl reload is rewritten: a 16-bit local tested for zero (`ld a,(hl+);
or a,(hl); jp nz`) and then assigned has stock SDCC reload `ld hl,sp+N`
for the store; the ROM steps back with `dec hl` (GetInstrEnvBytes'
`if (total == 0) total = 1`). The same bytes from genIfx (`if (w)`, no
compare) go the other way, and `ifx_word_hl` handles them before the rules
run: stock SDCC still knows hl after the test and steps back, while LSDj's
compiler reloaded it (DrawInstrFieldHelp's `if (s) ShowMessage(s)`, the
ROM's one such site; no `dec hl` follows a word test elsewhere). The SDCC
comment naming the code generator (`; genIfx` / `; genCmpEq`) tells the two
apart. The passes see the jump targets as they
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
follows reads `e` itself (CommitNameEdit's `row * 3`), and the ROM has no
such copy before its register is rewritten. The source of the dead store
is any side-effect-free load (`a`, a register, `(hl)`, `(bc)`, `(de)`, a
literal): the ROM keeps no dead `ld r,X` of any kind -- of its 2,411
`ld r,(hl)`, 1,367 `ld r,#n` and 2,234 `ld r,r2` none is rewritten before
it is read -- while SDCC's 16-bit `&` narrowed to a byte loads a dead high
byte (`inc hl; ld c,(hl)`: FileUpgradeV0Kit's `a &= ~(UWORD)0x20`).
`dead_hl_steps` then drops the `inc hl`/`dec hl` left stepping to it when
hl is rewritten (`ld hl,`, `lda hl,`, `pop hl`) before any read: of the
ROM's 3,312 hl steps none precedes an unread rewrite. A rule drops the `ld hl,#g`
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
`char` (`ld c,(hl); ld a,(hl); rla`, `LoadSynthShape`). A store through
`de` or `bc` leaves the equality standing (3 ROM sites copy the register
after one, none rereads): `StartScreenPlayback`'s `wChanActive[wSongCol] =
1; wPlayStartChan = wSongCol;` is `ld c,(hl)`, the indexed `ld (de),a`, then
`ld a,c` for the second read while hl still points at `wSongCol`; the
`hSoundCh = wSongCol` after it, with hl moved on, reloads. A conditional
jump ends the equality when hl points at a global: `FxCmd_E_Pu1`'s
`wEnvSpeedPu1 = v; if (v) wEnvCounterPu1 = wEnvSpeedPu1;` rereads it (`ld
(hl),c; xor a,a; or a,c; jp z; ld a,(hl)`, 3 sites, one per channel), while
a stack slot keeps it (PhraseScreenInput's call result: `ld (hl),e; ...; jp
c; ld a,e`). One exception: a
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

`track_hram` (run before `track_a`) skips an `ldh` reload of an HRAM byte
a register still holds. Stock SDCC 2.2.1 rereads an `sfr` at every use: its
CSE never substitutes a register for a direct-space global
(`isGlobalInNearSpace`, an 8051 optimisation applied to every port).
LSDj's compiler skipped loading the byte into a register that already held
it -- `ldh a,(X); ld r,a` goes when r holds X, within a block or across a
jump (`HandleInputCombos`' two `hSoundCh` indexes in one block,
`SyncTickLSDJ`'s loop body, `SndUpdateChannel`'s D across `if (d)`) -- but
never copied it over from another register: a reload into b while c holds
the byte stays (the `+=` target in `SndUpdateChannel` and
`HandleInputCombos`). `a` itself serves a reread in straight-line code (the
sign extension of a widened sfr: stock genCast reads `hFxValueS` again for
the `rla`, `FxCmd_F_Wav`; no compiled code in the ROM rereads a byte `a`
holds before a jump) and across an ordering test (`SyncTickLSDJ`'s
`hSoundCh < 4`: `cp #4; jp nc`); after an equality test the byte is reread
(`cp; jp nz; ldh a,(X)`: `SndUpdateChannel`, `FxCmd_P_Step`,
`SndRetrigNote`). The pair goes when `a` dies a few
instructions on, past ones that leave it alone (`ld de,#x; ld l,c; ld h,#0;
add hl,de; ld a,l`: the address of an index, `StartSongPlayback`). An HRAM
store, a call or a jump target ends the reuse. A store through a pointer
loaded from memory (`ld e,(hl); inc hl; ld d,(hl); ld (de),a`) ends it only
for a byte read before a conditional jump (the `+=` in `SndUpdateChannel`'s
D), not for one read in the same block (`StartSongPlayback`'s `*active = 1`
between two `hSoundCh` indexes); a store to an array element (`#arr + c` in
de) ends neither. `track_a` treats the
byte at hl the same way: after an equality test `a` no longer stands for it
(`SndPhraseHop`'s `*wPtrPhraseRow = wSeqNoteLatch; if (wSeqNoteLatch)
AdvanceCurChanCursors(wSeqNoteLatch);` reloads it, 2 sites, none reusing it;
PhraseScreenInput's `if (k)` on a stack byte, 7 sites), but after a bit test
of a stack byte it does (InstrScreenInput's kit flag toggles, 4 sites: `ld
(hl),e; ld c,e; ld a,c; bit 6,a; jp nz; or a,#0x40`). `a` also stands for the
byte once it copied a register that copied it (`ld c,(hl)` then `ld a,c`).
A textual rule covers the pair case: `ld b,l; ld c,h; ld l,b; ld h,c` loses
the copy back (none in the ROM; `CompressIsDefaultWave` pushes `hl` right
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
`(UWORD)t < 0x78` after a two-byte store), or one byte below it, stepping
up (WordScreenInput's `p >= 0xBDD0` right after the `i < 0x10` loop test)
-- the following hl-relative use reloaded with `ld hl,sp+N` as the add
clobbered hl.

**Signed** compares go the same way on the operand with its sign bit
flipped. Stock SDCC flips both sign bits into `e`/`d` and subtracts (`ld
a,hi; xor a,#0x80; ld e,a; ld d,#K.hi^0x80; ld a,lo; sbc a,#K.lo; ld a,e;
sbc a,d`, the constant first for `x > K`); LSDj's compiler built `d:e` as
`x ^ 0x8000` and added `-(K ^ 0x8000)`: `ld e,lo; ld a,hi; xor a,#0x80; ld
d,a; ld hl,#0x7FF6; add hl,de; jp c` is DrawCrashScreen's `i < 10`, and
with the constant first the add compares with `K + 1` and keeps its jump
(`FxCmd_F_Wav`'s `step >= 16` / `step < -16`: `#0x7FF0`, `#0x8010`). Against
0 stock SDCC tests bit 7 of the high byte (after genCmpLt's or genCmpGt's
comment); LSDj's code keeps that (SynthRenderWaveA's `(WORD)samp < 0`), but
GBDK's library build used the add form there too (`abs`'s `num < 0` and
printn's `(int)u < 0`: `ld hl,#0x8000`), so `gb_cmp16(zero=True)` runs for
`@stock-rules` units. After a rewrite the stack byte stock SDCC left hl on
is reloaded at the next use: a negation's after its carry clear and before
the zero it subtracts from (`xor a,a; ld hl,sp+4; ld a,$00; sbc a,[hl]`),
a shift's after its `or a,a`. The ROM has 6 signed adds (Abs16, DrawNumber,
DrawCrashScreen, FxCmd_F_Wav twice, NameEditorInput).

The same pass now handles a **stored unsigned comparison** from a stack
operand or register pair: stock `sbc` followed by `ld a,0; rla` becomes
`add hl,pair; ld a,0; rla; xor a,1`. A scan of all reached instruction
boundaries in the program banks finds one such add/materialize/invert site
(`BuildOverwritePrompt`, `$1D:$6B8E`) and zero stock subtract/materialize
forms (stack, global, or register operands). This is the same carry
inversion as the branch form, with the truth value made explicit.
Zero thresholds are left alone: adding zero cannot provide the inverse
borrow. The pass also restores HL when its fetch starts one byte above the
stack operand (`dec hl`), including relative stores after materialization,
and reloads a global operand's address when the branch's fall-through goes
on through HL.
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

The bundled SDCC is a **CVS snapshot of 1 August 2000** (`gen.c 1.44`, CVS
tag `gbdk-295-2`), and SDCC's full history survives in a git mirror
(<https://git.eris-associates.co.uk/active/sdcc>, Gitea: the per-file
commit lists are a few API pages deep). Every z80/allocator fix committed
*after* that date is a bug this compiler still has, and the diffs show the
exact buggy code -- reading them is faster than inferring a quirk from
behaviour. Confirmed that way so far: the `deassignLRs` register hand-over
loop `for (i = 0; i < max(sym->nRegs, result->nRegs); i++)` (fixed
2000-09-09 as "Fixed Big BUG in register allocator": a dying 2-register
symbol gives its first register to a 1-register result and never frees the
second), CSE ignoring casts (fixed 2001-04-22/25: the "pointer compares
with its element's sign" bullet below), loop-invariant hoisting of
expressions over `volatile` operands (fixed 2001-03-17), nested loops that
share a last block overwriting each other's induction set (2001-10-27), and
loop induction being off for the z80 ports (switched on 2001-08-09).

## What is C and what is asm

`tools/match/code_kind.py` tags every named function by compiler fingerprints
(SDCC addresses locals/arguments with `ld hl,sp+N` / `add sp,N` and branches
with `jp cc`; hand-written code uses `jr` freely and `add a,l; ld l,a` address
arithmetic; SDCC 2.2.1 never emits an absolute `ld a,[nn]` / `ld [nn],a`,
`[hl-]` or `reti` -- none of the 575 matched functions has one). It also
counts what matching C already owns and the GBDK/SDCC library routines
(by their GBDK sources), and measures in bytes. As of 575 matched functions:

| | matched | C | ambiguous leaf | asm | library | tiny | C bytes matched | asm + library share |
|---|--:|--:|--:|--:|--:|--:|--:|--:|
| bank `$00` | 54 | 17 | 9 | 70 | 31 | 13 | 59% | 37% |
| bank `$02` (sound engine) | 0 | 28 | 26 | 90 | 0 | 5 | 0% | 52% |
| editor banks `$01`, `$03`-`$07`, `$1B`-`$1F` | 521 | 54 | 0 | 40 | 0 | 9 | 68% | 2% |
| **all** | **575** | **99** | **35** | **200** | **31** | **27** | **64%** (90,980 of ~142,500) | **10%** |

The counts are labels: a big function's case bodies (the 13 `InstrParam*`
inside `InstrScreenInput`) and an indirect call's return point (the
`FarCall*Resume` labels) count separately, so the bytes are the measure.

The asm is where you'd expect it: `TimerHandler` and the sound tick, the
pitch/vibrato/envelope steppers, the tile loader, `ReadJoypad`, the OAM DMA
stub, GBDK library routines, and bank `$02`'s note/period/sequencer core
(`NoteToPeriod`, `WriteChannelFreq`, …). Those are kept as asm, pret-style.

Not all of GBDK's library in the ROM is asm: libc's C routines were compiled
by GBDK's build, with the stock rules (`strlen` is `StringLength`, matched
from gbdk-2.95-2's own `libc/strlen.c`). Some asm reads stack arguments like C
does; the absolute-address signal catches `DrawByte`, `DrawString` and
`GetTilemapAddr`, and `DrawTileRun` / `WriteVramByteSafe` (which branch with
`jr` and poll `rSTAT`) are listed by hand. Three functions whose `.sym`
comment began with a quoted string had been carved as data, since a leading
`"` marks a data symbol: `DrawCrashScreen`, `CopyInstrument`,
`DrawVerifyPanel` (all SDCC code, now disassembled).

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
  `geniCodeCast` folds no cast of a literal *to a pointer*, so in a loop
  `*(UBYTE *)0x2000 = bank` hoists the cast into a temporary that spills to
  a stack slot (`ld hl,sp+2; ld e,(hl); inc hl; ld d,(hl)`: CheckRomChecksum's
  bank loop).
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
- **Undeclared calls lose their underscore**: a function called before any
  declaration in the unit (defined further down, no prototype) is emitted as
  `call Name`, not `call _Name`, which the link then cannot resolve
  (WaveScreenInput calling CopyWaveSelection); declare it first.
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
- **A pointer compares with its element type's sign**: the `(int)` of
  `(int)(wNameEditBuf + 1) > -1` is gone by the iCode (`iTemp > 0x80000000
  {_near * char}`), and genCmpGt/genCmpLt take the sign from both operands'
  specs -- a pointer's is its element's. Over a `UBYTE` buffer the test is
  unsigned (`xor a,a; ld a,#0xFF; sbc a,c; ...`, never true); over a `char`
  one it is signed, which gb_cmp16 turns into the ROM's add form
  (NameEditorInput's RAM assertion; `wNameEditBuf` holds a name, and
  `KbdLink.c` already declared it `char`). `(int)gp` on a `UBYTE *` global
  keeps the cast and compares signed.
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
  An address passed to two such calls can also be written out in both
  (`ReadSongByte(0, SramBookmarks + i)` ... `WriteBookmarkByte(SramBookmarks
  + i, 0xFF)`): CSE makes it one converted temporary with one slot, whose
  pressure spills the loop counter as the ROM does (RemoveBookmark). A
  named `UBYTE *p` gets its own slot beside the conversion's, and a `UWORD
  p` leaves the counter in c.
  `(UWORD)` of a literal-pointer macro inside a comparison crashes SDCC
  2.2.1; spell the constant.
- **Addresses held in integers**: `extern UWORD wTableCellAPtr;` copied
  with `p = (UBYTE *)wTableCellAPtr;` goes byte by byte through `a`
  (`genCast`); a pointer-typed global copies as `ld a,(hl+); ld e,(hl)`
  (`genAssign`'s two-byte case). The ROM has both (`TableReadCmd`).
- **A comparison kept as a value**: `ok = v <= 0x13; if (!ok) …` stores the
  0/1 (`ld a,#0; rla`) and its negation before testing it; the same test
  written inside the `if` compiles to a plain `cp` / `jp` (`DrawCmdLetter`).
- **A masked byte tested in a register: the literal first.** `if (x & 0x80)`
  hands the AND to its `if`: ralloc marks the result a condition (REG_CND)
  when the right operand is a literal and the result dies at the `if`, so
  genAnd emits `bit 7,a` for one bit, `and a,#n; jr nz` for several, and
  for some single-bit cases an internal error (gen.c 3215). `if (0x80 &
  x)` has no literal on the right, so the AND gets a register and the `if`
  tests it: `and a,#0x80; ld c,a; or a,c; jp z` (toBoolean's `xor a,a; or
  a,c`, folded by lsdj.peep rule 2) -- `DrawKitLoopMode`'s ATK test,
  `DrawKitInstr`'s SPEED test (in `b`), `SndPhraseHop`'s `0xF0 & v`; genAnd
  moves the literal to the right itself. `(x & 0x80) != 0` gives `or a,a`
  instead, and a cast or a named temporary is packed away.
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
- **A register leaked by narrowing**: when a 2-byte temporary dies at the
  iCode that defines a 1-byte one, `deassignLRs` hands its registers over in
  a loop that runs to the larger count, so the second register is copied into
  the byte's unused slot and never freed. `(int)(char)x & 0xBF` (SDCC narrows
  the AND back to a byte) leaves `b` taken for the rest of the function and
  every later temporary spills (InstrScreenInput's kit columns); `v =
  (int)(char)v & (int)0xBF` narrows straight into the stack byte and frees
  both.
- **Same-named block locals are one variable**: `{ UBYTE kit; ... }` in two
  sibling blocks at the same level is a single symbol with one live range
  spanning both, so it spills; give them different names or keep them apart.
- **Spill victims**: when registers run out, SDCC 2.2.1 spills a *named*
  local (it has a spill location) before any unnamed temporary, and among
  named ones the least used (a use inside a loop counts 3, one in a block
  that leaves the loop counts 1). So a value the ROM keeps in a register
  while a named local goes to the stack was an expression in the source:
  `if (IsBookmarked(i + wSongRow)) wSongRow = i + wSongRow;` (one CSE'd
  temporary; `SongFindNextRow`), `ColorField(…, c && hGameboyType)`
  (`ChainDrawCellLo`), `wButtons == 0x10` written twice
  (`PhraseScreenInput`). A cast copy of a global (`(char)wChainRow`) has the
  global itself as its spill location, so it goes first and costs nothing:
  as a far call's last argument it gives `c` up to the function-pointer
  temporary (the reversed `ld b,lo; ld c,hi` pair) and is pushed straight
  from the global.
- **Accumulator packing needs the use to be the very next iCode**:
  `packRegsForAccUse2` walks a one-byte result's uses and packs it into `a`
  only while each use is `ic->next` and `opPreservesA` accepts it (`+`,
  bitwise, `=`, `==` or a pointer get with the other operand a symbol).
  `v |= t & 0xF8` packs the AND (the ROM's `ld c,a` then `or a,c` is the
  unpacked form; packed, genOr emits a wrong `or a,a`). An implicit
  conversion of a binary operation's *left* operand is emitted after both
  operands, so a signed `char v` parameter puts `v`'s unsigned copy between
  the AND and the OR: the AND keeps `c`, the copy is packed into `a`, and
  SDCC 2.2.1 rejects only definition/prototype sign mismatches, not `UBYTE`
  arguments to a `char` parameter -- the callees then need `char`
  prototypes (`WriteCurrentInstrByteC`, `FarCallArgWC`; SetVibratoShape).
- **A hoisted cast copy is the invisible register**: a sign-changing copy
  of a parameter inside a loop (`((char *)buf)[i] = y` needs `(char)y`) is
  loop-invariant, so `loopInvariants` hoists it into the preheader, where it
  takes a register for the whole loop although no instruction shows it;
  its spill location is the parameter itself, so when a later 2-byte
  temporary evicts it (the address as singles `[b c]`) every use silently
  reads the parameter's slot. CSE, blind to casts in this snapshot, then
  substitutes that signed copy for `y` in `*p < y`, which is why the ROM's
  last compare is the signed `xor a,#0x80` form (PlotEnvCurve).
- **A symbol spilt later held its register earlier**: `isspilt` is per
  symbol, so a local the allocator evicts half-way through a function is
  generated as a stack variable *everywhere* -- yet in the allocator's
  bookkeeping it occupied its register from its definition to the eviction,
  and that invisible occupancy decided where the earlier temporaries went.
  DedupTables' and DedupPhrases' outer call result sits in `b` because
  a plain `UBYTE i` held `c` until the inner loop's compare evicted it, and
  `j` is stored straight to the stack at `j = i + 1` because the inner
  call's result temporary evicted it later; the code never shows either in
  a register (`volatile i` puts the result in `c`).
- **A string literal argument changes the loops around it**: `"OK"` is a
  `_code *`, `DrawString`'s parameter a generic `char *`, so the call copies
  the address into a generic-pointer temporary; CSE's `findCheaperOp`
  rewrites the push to the raw address (neither temporary has a spill
  location), the copy is dead, and `killDeadCode`'s change reruns
  `computeDataFlow`. Reaching definitions converge only as far as the CSE
  expression sets do (`change` is measured on `outExprs`), and the extra
  passes let an inner loop's definitions flow round the outer back edge
  into the inner preheader: `hasIncomingDefs` then extends every inner
  temporary to the loop end, call results included. Under stock SDCC that
  stops the `global = call()` fold and spills them, and only `char *s; s =
  "OK"; DrawString(s);` (the copy gets `s` as its spill location, nothing
  is dead, the passes stay at two) came close; with LSDj's compiler (no SSA
  copy of a call result, above) the stretched temporaries are exactly the
  ROM's and the bare `DrawString("OK")` matches (`DedupChains`,
  `DedupTables`).
- **An instruction's own operands can't be its spill victims**:
  `computeSpillable` drops the instruction's `uses` from the candidates, so
  when the registers run out at `t = v & 0xF0` (c = the loop counter, b = v,
  handed over from the call result it was computed from) the counter is the
  only possible victim. Under an unsigned mask a signed `char v` first needs
  a sign-changing cast copy `t0 := v`; that copy has v as its spill location
  and is spilt at its definition (`spillThis`: the counter is used more), and
  the AND then uses t0, not v, so v -- less used than the counter -- goes to
  the stack and the counter keeps c. `UBYTE v` spills i (the ROM's opposite);
  `char v` gives the ROM's `ld b,(hl)` cast copy before the second nibble's
  `and a,#0x0F` (SynthLoadSignalWave).
- **A local the ROM keeps in its slot is `volatile`**: when every access to
  a variable is a stack access -- its first store too, made while its value
  still sits in a register for the next use -- a plain local is the wrong
  shape. CSE folds `r = k2; r |= !(kit == k2);` into one `r = k2 | ...`, r
  then starts where the registers are full and something else (the loop
  instrument in c) spills. `volatile UBYTE r` makes r a stack variable, so
  `r = k2` stays a store and k2 stays in b for the compare
  (PhraseCellFlagB/D).
- **A local read before it is written is spilt from the start**:
  `computeLiveRanges` checks every local's first use against its
  definitions (`W_LOCAL_NOINIT`, and `useDefLoopCheck` for a use that
  precedes the first definition in instruction order inside a loop) and
  marks the register equivalent `isspilt` before allocation runs. Such a
  local never occupies a register, yet unlike a `volatile` one it still
  takes part in CSE, loop-invariant hoisting and `packRegsForAssign`; a
  loop that reads a flag or a "previous value" at its head and assigns it
  at its tail, with no initialisation before the loop, gets exactly this
  shape (a stack variable whose reads come from the slot although a
  register was free).
- **Loop-head temporaries are extended, body temporaries are not**: a
  temporary's live range is stretched to the loop end only when its
  definition reaches the loop's preheader (`hasIncomingDefs`), and in a
  nested loop that happens through the *outer* back edge. `computeDataFlow`
  propagates reaching definitions one block generation per iteration and
  stops as soon as the expression sets stabilise -- two iterations in a
  simple nest -- so the inner loop's condition (defined in the loop-head
  block, one hop from the exit edge) reaches the preheader and is
  materialised (`ld a,#0; rla; ld c,a; or a,c`), while the body's call
  results do not and still fold into `global = call()`. A third iteration
  (more expressions, or a rerun after dead code or hoisting) extends the
  body temporaries too: every call result then needs a slot or a register
  and the folds break (the string-literal and inner-cast "chaos").
- **A split statement is an extra use**: the allocator spills a new value at
  its definition when every live range it would push out is used more
  (`leastUsed->used > sym->used`, uses weighted by loop depth). `s = wSynth;
  s >>= 4;` gives s a third use where `s = wSynth >> 4` has two, so the
  2-byte address after it (two uses) spills itself and s keeps c, the
  ROM's shape in LockCurrentSynth.
  The same rule spills a named local at its definition when it is used
  less than whatever holds the registers: `UWORD t; t = oa + 0xA000;
  p = (UBYTE *)t;` (t declared last, used once) writes the sum straight
  into t's slot -- the ROM's `ld a,l; ld d,h; ld hl,sp+0` store -- and the
  int-to-pointer cast copies it byte-wise (`genCast`, where a 2-byte `=`
  between stack operands would go through a and e), while the other rows'
  `p = (UBYTE *)(ob + 0xA000)` sums are bc temporaries cast into the
  pointer's slot. The byte loop is `if (*p++ != *q++) return 0;`: the
  post-increment copies are packed away and the two bytes stay
  location-less temporaries, so neither is a spill candidate -- a named
  `x = *p` would be the least-used victim at `y`'s definition. `i` holds
  c into the first loop, hence `b` for the first byte there and `c` in the
  later loops (FindDupPhrase/Chain/Table).
- **A cast copy of a global is an occupant that costs nothing**: a
  `char` parameter fed a `UBYTE` global makes a same-size cast copy whose
  spill location is the global itself (`geniCodeCast`), and arguments are
  evaluated right to left, so the copy for the *last* argument is born
  before an earlier argument's `dst++` pointer copy. It takes c, the
  pointer copy's second register evicts it into the global (the only
  candidate with a location once the counter is spilt) and the pair comes
  out `[b c]` with the push still reading the global -- the ROM's
  GfxCopyVramRegion, where a `UBYTE` value argument gives the pair `[c b]`
  and `push bc` (`WriteVramByteSafeC`).
- **A global declared `extern` inside the function is a register
  variable spilt into the global**: `allocVariables` gives a block-scope
  `extern` the global's storage but leaves its level, so the compiler makes
  it a register-equivalent local (`reqv`) whose spill location is the global
  -- no SSA temporary on assignment, its reads replaced by the defining
  temporary (a file-scope global is in near space, which `findCheaperOp`
  never replaces), and the value only reaches memory when the allocator
  spills it. SynthFilterLowHigh's doubled index: `wSynthFilterIdx = pos <<
  1` folds the shift into the temporary, both `wSynthBufA + wSynthFilterIdx`
  and `wSynthBufB + wSynthFilterIdx` become one 2-use pointer each, and the
  index is written to $CB3A once (`add a,a; ld hl,#g; ld (hl),a`) and fetched
  from it (`ld l,(hl)`); the file-scope spelling copies the global per use
  into c. `repair_globls` declares such symbols so they link. Likewise a
  `UBYTE *` global dereferenced as `*(UWORD *)p` (the same function's slot
  pointer): the pointer-to-pointer cast copy is spilt into the global and
  reloaded from it after a call, where `*p` on a `UWORD *` global copies the
  pointer to a stack slot before the call.
- **A call result with a slot of its own**: `v = f()` makes v the call
  temporary's spill location (`updateSpillLocation` in SDCCcse.c), so a
  temporary that a loop keeps alive and the allocator spills is written
  straight into v's slot. When the ROM stores the result to a separate slot
  and then copies it (`ld hl,sp+N; ld (hl),e` … `ld a,e; ld hl,sp+M;
  ld (hl),a`), a cast sat between the call and the assignment: that file
  prototyped the callee as returning `char` (`ReadInstrByteS`, aliased to
  ReadInstrByte, in FileUpgradeV11). A `char` local instead changes every
  other use of it.
  The slot also decides who spills: a temporary *with* a spill location is
  spilt at its own definition when the live ranges it would push out are
  used more (`spillThis`), one without must evict somebody. In
  FindFreeBlock `v = ReadSongByte(4, p); if (v > 0x1F)` spills the result
  into v while `end` keeps bc; the unnamed `if (ReadSongByte(4, p) > 0x1F)`
  evicts `end` (the less used named local) and the result keeps c, the
  ROM's shape.
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
  in the ROM (`DrawWaveSpeed`'s `"--"` at `$03:$49E4`). Precisely, SDCC
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
  call (`ColorField(X[t][f], Y[t][f], W[t][f], gbc ? 3 : 2)`) orders and
  spills the temporaries differently from computing them into locals first;
  the ROM has both.
- **Per-type tables are 2-D arrays**: `InstrFieldX[type][field]` with
  `extern UBYTE InstrFieldX[][18]` gives the ROM's `__muluchar` (`type*18`,
  computed once and reused across tables) plus a row pointer; LSDj's tables
  are 67 bytes (the NOISE row has 13 fields), so they are declared, not
  defined, in the C. SDCC's runtime helpers (`__muluchar`, `__divuint`, …)
  resolve to our names for those GBDK routines (`MulU8`, `DivU16`, …).
  The name keyboard is one too (`char AlnumChars[][11]`, NameEditorInput's
  `AlnumChars[row][col]`): over a flat `char[]` the row-then-column read
  cannot be spelled -- `(a + n)[i]` and `*(a + n + i)` do not compile
  ("Array or pointer required for '[]' operation", "Pointer required"),
  `(&a[n])[i]` doubles `i` -- except through a cast, `((char *)a + n)[i]`;
  `a[1]` as an argument is the immediate `a + 11`. The clipboard is one
  too, `UBYTE wClipboardBuf[][42]` (6 columns of 42 cells): the link
  transfer's `&wClipboardBuf[x][y]` written inside the inner loop is
  hoisted to the ROM's `MulU8` before it, and gives x the inner-loop use
  counts that make y, not x, the spill victim (SendLinkClipboard,
  ReceiveLinkClipboard).
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
  left). The instrumented compiler shows each decision:
  `tools/match/trace_alloc.py FILE.c FUNC --loops --hid` (see *Tools for
  the grind* below).
- **Dead code changes live ranges.** `killDeadCode` removing anything (a
  post-increment's unused old-value copy, the temp a literal store to a
  non-volatile global leaves behind) makes SDCC 2.2.1 rerun
  `computeDataFlow` after the loop pass; the extra propagation lets a
  nested loop's body definitions reach the inner preheader, so
  `hasIncomingDefs` extends their temporaries to the loop end: an
  assignment such as `wDedupPhraseUsed = IsPhraseUsedFar(j)` then keeps
  its call-result temporary instead of folding into the global, and that
  temporary's eviction of `j` is what puts `j` on the stack in
  `DedupPhrases` (next bullet).
- **`global = call()` keeps the call's temporary -- LSDj's compiler
  differs from stock here.** Stock 2.2.1's `geniCodeAssign` copies every
  value assigned to a non-volatile global through a fresh temporary
  ("SSA"); the allocator's `packRegsForAssign` folds the copy away again
  unless the live range was stretched (above). LSDj's compiler made no such
  copy for a function call's result: the result temporary is assigned to
  the global directly, with the global as its spill location. Every other
  function compiles identically either way; `DedupPhrases`, `DedupChains`
  and `DedupTables` only match without the copy (with it, two temporaries
  per call survive the stretching, each wanting a stack slot). The native
  build carries this as a source patch (`tools/toolchain/build_sdcc221.sh`,
  "LSDj's compiler"); the Win32 `sdcc.exe` is stock and so is 2 bytes off
  on those three. Dropping the copy for *every* assignment is wrong: ten
  functions that read the global back (`LoadSyncSetting`...) then keep the
  value in a register where the ROM reloads it.
- **A value stored to a global and passed in the same call is a comma
  expression in the argument**: `ColorField(x, row + 2, (wDrawTextScratch =
  PhraseCellWidth[col], wDrawTextScratch), hGameboyType ? 3 : 2)`
  (HighlightPhraseCell). The arguments are evaluated right to left, so the
  `?:` comes first and its temporary -- no spill location -- keeps its
  register when the width's `col` reload evicts `x` instead; the store
  then happens in argument position, and the comma's second operand reads
  the global back for the push (a bare `f(g = e)` pushes the value's
  temporary, a separate store statement evaluates before the `?:`).
- **Inline assembly** is SDCC 2.2.1's `_asm ... _endasm;` (the `di`/`ei`
  around SRAM access); the MBC registers are variables (`rRAMG = 0x0A`,
  `rRAMB = 3`), like `rROMB0`.
- **Missing `return`s are real**: `HasEnterableColLeft/Right` are declared to
  return a byte but only `break` out of their loop; callers read whatever
  the last call left in `e`. SDCC 2.2.1 compiles that without complaint.
- **A far-call target's own bank declares it for real**: `lsdj.h` declares bank 2's
  far-call targets as `void f(void)`, the type the banked-call routines take;
  bank 2's units define `SOUND_BANK` before the include and skip those
  declarations (`SndGetChainPhrase` returns a byte from two arguments).
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

### Tools for the grind

`DFV=2 tools/match/trace_alloc.py FILE.c FUNC --dfv` prints, for each block
whose expression set a dataflow pass changed, the old and new sets (result
temporary, operation, operands). A third `computeDataFlow` pass -- the one
that extends the inner loop's body temporaries and breaks the `global =
call()` folds -- happens whenever an `=`-copy of a symbol with a reaching
definition is available at a loop head: `ifKilledInBlock` drops it on the
back edge in the second pass, the head's set shrinks, and the third pass
carries the reaching definitions one block generation further. Pointer
arithmetic on a `UBYTE *` with a byte index makes such a copy
(`geniCodeMultiply` by the element size casts the index to the literal's
1-byte `unsigned short`), and so do `p := sum` and `j := 0` for plain
locals; `volatile` operands stay out of the expression sets.

`match_function.py --func NAME --target NAME` trims the compiled unit to
that one function before the peephole passes and the assembler. `make
match` / `verify_all.py` run the whole unit (so the literal pool that
follows a function in the ROM is checked there); the rule matcher
(`peep.apply`) is incremental -- a rule re-attempts only the positions
whose window a replacement changed, the `labelRefCount` and label-dropping
rules the whole function when a label or jump changed -- and gives the
reference full-scan matcher's output (`apply_full`, checked by
test_peep.py) in 0.5 s where the full scan took three minutes on a
17,000-line unit, so a tool change rebuilds the 666 functions in about a
minute instead of twenty.

- `match_function.py --score` -- one line: how many rows of the aligned
  disassemblies differ, and how many once local addresses are ignored (jump
  targets inside the function and pointers to its literals move whenever the
  code grows or shrinks). The quick way to compare two attempts.
- `shapes.py --list [--bank BB] [--no-parked]` -- the unmatched C functions
  by size, with their kind, `P` for parked and the `.sym` note;
  `code_kind.py` prints the per-bank totals and the share of C bytes matched.
- `tools/show_func.py NAME [--bare] [-n 0]` -- a function's disassembly; one
  that matching C owns is disassembled from the ROM.
- `tools/rom_strings.py BB:AAAA|NAME [COUNT]` -- the NUL-terminated strings
  at an address (a function's literals follow its code).
- `tools/match/rom_grep.py PATTERN [--list] [--kinds] [--tally]` -- every
  occurrence of a byte pattern in the code banks, with the function it falls
  in and whether matching C owns it: the ROM-wide evidence a new rule needs.
- `tools/match/rom_bigrams.py --build`, then `rom_bigrams.py FUNC ATTEMPT.c`
  -- the adjacent-instruction pairs (numbers and labels abstracted) of every
  function matched from C, cached in `build/rom_bigrams.json`, and the pairs
  an attempt's linked disassembly has that the ROM has nowhere. A missing
  peephole rule shows up as such a pair (the dead `ld r,(hl)` after a
  narrowed 16-bit `&` did); an attempt whose pairs are all ROM-known differs
  by allocation alone.
- `tools/match/trace_alloc.py FILE.c FUNC [--hid] [--loops] [--alloc]` -- why
  a value landed in a register or a slot: every iTemp's live range and home,
  the dataflow passes, the hoisted invariants, which last uses were extended
  to a loop end (hasIncomingDefs), and the allocator's own trace, which
  names what it allocates, spills and frees (`in loop on result iTemp14
  (seq24)`, `victim iTemp11`, `alloced b`). It runs an instrumented copy of
  the compiler built by `tools/toolchain/build_sdcc221.sh --debug` (same
  code; verified identical output on four modules); its work directory
  stays in `build/trace_alloc/`. That build also takes experiment knobs:
  `BUSYC=lo-hi` / `BUSYB=lo-hi` keep register c / b busy for iCode seq
  lo..hi, `SPILLSYM=iTemp4,iTemp9` spills those symbols at their
  definition (what a use before the definition or a `volatile` would do,
  without touching the code elsewhere); `LSDJ_NATIVE_BIN=toolchain/
  sdcc-2.2.1-debug/bin BUSYC=82-82 match_function.py ...` compiles an
  attempt under them.
- `tools/match/perturb.py FILE.c FUNC` -- searches those knobs (every
  symbol spilt at its definition, c or b held over every short range of
  sequence numbers, then pairs of the best) and lists the perturbations
  that bring the attempt closer to the ROM, best first. A 0 names the
  allocator *state* the ROM's compile had where ours differs -- the fact
  to explain with C -- or with the compiler: `DedupPhrases` with plain
  counters needed b held from `j = i + 1` through the inner loop's test,
  which is what the call-result temporary of LSDj's compiler does (*Writing
  C*: `global = call()`); SynthFilterBandPass needs c held at one
  instruction (the doubled index's definition), SynthWriteWaveCol its
  counter spilt at its definition and c held through the body.
- The debug build has more knobs for such hypotheses, all environment
  variables read by the patched compiler: `KEYOF=name:K` forces a global
  symbol's operand key (`KEYDBG=1` prints each first assignment; keys are
  reset per function in 2.2.1, so a collision with a temporary's key --
  `isOperandEqual` compares keys -- does not happen naturally),
  `NOEXTEND=iTemp9` stops one temporary's loop-end extension,
  `DEASSIGN_STRICT`, `CND_ALLOC`, `HANDOVER_NONE`, `ALLOC_BFIRST`,
  `NO_POSITION`, `IFX_LATE` each change one allocator rule (all six break
  matched functions or change nothing: the allocator is stock). Run a
  candidate over everything with `LSDJ_NATIVE_BIN=toolchain/sdcc-2.2.1-debug/bin
  KNOB=1 tools/match/verify_all.py` -- and mind that `verify_all`'s cache
  key does not see environment knobs: bump `NATIVE_REV` in
  `match_function.py` (or clear `build/match-cache/`) first.
- Parked attempts live in `c/parked/NAME.c` (outside `make match`), next to
  their note in `c/parked.txt`.

When the ROM has a value in a *different register or slot* than the C gives,
the allocator's rules decide it, and they are few (`src/z80/ralloc.c` of the
snapshot; every post-August-2000 fix in SDCC's history is a bug still in it,
see "Compiler versions checked"). The checklist, in the order it pays to try:

1. **Where does the register pressure come from?** A symbol spilt *later*
   still held its register at every earlier allocation (`isspilt` is per
   symbol, so the code shows it in memory everywhere): a plain local that
   the ROM only ever touches in its slot may be exactly that. The ROM's
   choice of `b` over `c` for a temporary says `c` was owned at that point.
2. **Who is excluded from eviction?** `computeSpillable` drops the current
   instruction's operands, so a value that is an operand of the instruction
   that runs out of registers is never the victim -- the *other* live symbol
   is, or (if nothing is spillable) the new result spills itself. A
   sign-changing cast copy moves the use away from the original (`char v`).
3. **What has a spill location?** Named locals and cast copies of named
   symbols do (a copy's location is its source: free to evict), call results
   assigned to a named local or non-volatile global do (`updateSpillLocation`),
   bare temporaries don't -- and `spillThis` prefers spilling a new value
   with a location when every live range it would displace is used more.
4. **Is it the hand-over?** A dying operand gives its registers to the
   result defined in the same instruction (no `allocReg`, so the "c first"
   order doesn't apply); a dying 2-register operand into a 1-register result
   leaks the second register for the rest of the function.
5. **Did something get hoisted or extended?** A loop-invariant cast copy of
   a parameter is hoisted into the preheader and owns a register for the
   whole loop without a visible instruction; inner-loop temporaries are
   extended to the loop end when their definitions reach the inner preheader
   (which depends on how many dataflow passes ran -- a dead iCode killed
   anywhere in the function adds passes).
6. **Did CSE substitute or refuse?** A near-space global's read is never
   replaced by its temporary; a cast copy is (CSE ignores casts here); a
   literal base folds an expression into a stack result where a symbol base
   does not (`packRegsForAssign`'s one-symbol-operand rule).
7. **Is the accumulator packing chain intact?** A one-byte result is packed
   into `a` only while each use is the very next iCode and preserves `a`
   (`opPreservesA`); an implicit conversion of a binary operation's *left*
   operand is emitted after both operands and breaks the chain.

## How matched C enters the build

`make match` (`tools/match/verify_all.py --write build/c`) compiles every
`c/bankNN/*.c`, links each function at its ROM address, and on a match writes
the bytes to `build/c/bankNN/Name.bin`, the function's RGBDS translation to
`build/c/bankNN/Name.lines` (`tools/match/to_rgbds.py`: its code plus the
literals and const data the compiler emits after it, each row with its ROM
address), and records the function in the tracked manifest `c/matched.json`
(bank, address, length, source). The carve (`tools/carve_clean.py` →
`tools/disasm.py --clines`) then renders each such range from the
translation instead of decoding the ROM: the compiler's own code with
symbolic operands (`ld hl, wCurrentScreen`, `call DrawString`,
`jp nz, .notWave`). A branch goes to the `.sym` label at its target (the
disassembly named every jump target), written as a local reference in its
own scope and `Parent.local` outside it; SDCC's labels nothing jumps to (its
fall-through blocks) are dropped, so no `00102$`-style number reaches the
source. The few targets the `.sym` lacks are named from what is there: a
switch's jump table after the variable it switches on (`.soundChCases`,
else `.jumpTable`) and a string literal after its text (`.strSaveSong`).
Names the asm side lacks become guarded `DEF`s, and every `.sym` label
inside the range is printed as before (`tools/name_targets.py` names the
asm side's own leftovers the same way: jump tables, strings, pointer-table
entries). With the compiler built
(`tools/toolchain/build_sdcc221.sh`), `src/main.asm` depends on that output:
change a C file and `make compare` recompiles it, re-carves, and fails if it
no longer matches. Without it the same code ranges `INCBIN` the ROM itself,
so `compare` works everywhere (`make listing` disassembles those slices back
inline).

This is the pret loop for this project: asm functions are replaced by
matching C one at a time while `make compare` stays green.

## Parked functions

Functions tried and set aside because no source spelling reproduced them yet;
each note says what differs. Revisit when another function shows the same
shape.

| Function | Where | What differs |
|----------|-------|--------------|
| `SynthFilterBandPass` | `$1D:$5BBD` | the BANDP filter: halve the wave an octave per cutoff doubling (`*(UBYTE *)wSynthBandDst = (wSynthWaveBuf[k++] + wSynthWaveBuf[k++]) >> 1` over a shortened `n`, `volatile UBYTE k` so k stays in its slot and the two copies take b then c), then the LOWP/HIGHP scatter core in a `for (;;)` whose head computes pos/frac/sample and, at `pos == 0x20`, runs the normalise loop and returns (`return`, not `break`: the loop exit's `jp` stays a trampoline). c/parked/SynthFilterBandPass.c is 10 rows off: in the normalise loop the BufB word pushed to DivU16 comes out [b c] (`ld l,b; ld h,c; push hl`) where the ROM has [c b] (`push bc`). With the debug compiler's BUSYC=82-82 (register c held when `next2 = i << 1` is allocated, so next2 takes b and is the word's second victim) the function is byte-identical: the ROM has a one-register occupant of c born in `wSynthBandDst = (UWORD *)(wSynthWaveBuf + SynthWrapIndex32(i))` and gone by the BufB read, which no spelling tried creates (288 head forms, 36 normalise forms, 128 wrap-call forms, all 512 volatile subsets, block-scope externs for the pointer and value: /tmp/opencode/csw/grid_*) |
| `SynthWriteWaveCol` | `$1D:$5F29` | `for (i = 0; i < 0x20; i += 2) { wWaveEditX = wSynthWaveBuf[i] << 4; wWaveEditX = (wSynthWaveBuf[i + 1] + wWaveEditX) ^ 0xFF; WriteSongByte(3, (UWORD)wSynthRenderDst++, wWaveEditX); }` (c/parked/SynthWriteWaveCol.c) is 10 rows off: ours keeps i in c and takes b for the body temporaries, the ROM has i on the stack from its definition, every body temporary in b and the pointer pair c b. perturb.py: with register c held from the function's first instruction to the body's last byte temporary (BUSYC=3-17) it is byte-identical, and nothing else is -- the occupant is there before i is defined, which no statement can do; 216 index spellings, 1,144 declaration/store forms, 300 prefix/extra-local forms, every NOSSA_OPS variant: no change. Not the SSA-copy difference; something else in LSDj's compiler |
| `CountFileBlocks` | `$1C:$48D4` | 16 bytes with `/tmp/opencode/cfb/e1.c`: `t = *wFileBlockCountPtr + 1; *wFileBlockCountPtr = t;` as two statements (the write's pointer copy then follows its use and packRegsForOneuse folds it: the ROM re-reads the pointer global for the store); `while (p != 0xA200)` is the 32-bit compare. Left: the ROM spills the `*ptr` read into the dead long's slot (`ld hl,sp+0; ld (hl),a; ld a,(hl); add a,#1; ld c,a`) where ours keeps it in c -- p holds [c b] in the allocator's books until that read evicts it, so the ROM must have had both registers non-spillable there; volatile p/v, char t, `(*p)++`, pointer-arithmetic and statement-order variants tried. Same family: the `*ptr` read spills itself (nothing spillable) although p, which holds bc through the loop in the allocator's books, is neither spilt nor an operand there |
| `FileUpgradeClearRange` | `$1C:$65B9` | really the chain transposes' rescale: mark the chains the sequence uses in $DA00, then for each used chain's 16 steps `v = FarCallArg2(6, 0x56AA, i, j); v = v / 12; v <<= 4; FarCallArg3(6, 0x5655, i, j, v);` (ChainReadTrans/SetChainTranspose by literal address). Plain C now (c/parked/FileUpgradeClearRange.c: `UBYTE i, j; UBYTE *p; UBYTE t; char v;`, `p = (UBYTE *)0xDA00 + (UWORD)t`, `while (!(j == 0x10))` -- the `!` materialises the boolean, which is spilt into a slot and tested in a as the ROM does) is 12 rows off: ours keeps v in c through the inner loop, the ROM has v and every temporary of the three statements in memory. perturb.py: register b held from the second loop's first pointer (seq 26) to the end (BUSYB=18-62) makes it byte-identical -- the ROM leaked b there, as deassignLRs' hand-over does when a two-register operand dies into a one-register result, but no such instruction exists at that point (p is a two-register pointer, spilt); `UBYTE * volatile p`, `volatile UBYTE *p`, volatile v/done forms do not create it |
| `DrawInstrumentScreen` | `$03:$4DE7` | c/parked/DrawInstrumentScreen.c (compile with --define FarCallArgW=0x17BB --define FarCallW=0x179A: FarCallArg/FarCall taking a UWORD target) is 8 rows off: ClaimInstrSlot called by address, `UWORD fn` set by a switch whose cases run 0, 1, 3, 2, the name loop's `UBYTE c` local, no off/y locals: `GetTilemapAddr(InstrFieldX[type][0], InstrFieldY[type][0])` and `[type][1]` -- the `type * 18` of the 2-D index is one CSE'd temporary and the row's `+ 1` becomes `inc de` -- locals in slot order i, type. Left: the two spill slots come out swapped (the ROM spills the `type * 18` temporary first, at its definition, then the row value; ours spills the row value first and the multiply later). perturb.py: register b held from the multiply call to the first GetTilemapAddr's column read (BUSYB=47-52) makes it byte-identical -- type evicted by the multiply result, that result and the row value spilt in turn, the column in c. `char type`, `volatile type`, `if ((type = ...) >= 4)`, (UBYTE) casts on the arguments, the palette stores reordered, `MulU8()` spelled out, `18 * type`: no change |
| `WordEditorInput` | `$06:$431D` | 11 rows (`--define wWordClipPtrB=0xC939`: $C939 is a byte there and a pointer in the cut): a twenty-case `switch (wButtons)`, the clipboard cells re-read through CSE (`WriteWordCell(wWordClipCell1[0]) ... DrawHexByte(wWordClipCell1[0])` stays in c), `v = wWordClipCell0[0]; v--;`, a `volatile UBYTE v` so the cut's first two reads land in b; the cut's last two reads go through a local the ROM keeps in c and ours in b (y still holds c until well after, in ours) |

## What's still unknown

1. **Build options.** Default loop optimisations: invariant motion is on
   (`FileForEachInstrType`), and the code that once looked like it was
   compiled without it (`CloneSynth`, the three frame drawers) is stock
   SDCC 2.2.1's one-level hoisting from stale dataflow (see Writing C).
2. **The complete compiler model.** An accumulator-tracking pass plus 86
   rules so far; most new functions need nothing new, but each new idiom can
   surface a rule. One function so far (`SetVibratoShape`, `$03:$4B5E`)
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
- [x] Matched C feeds the ROM build (first as `INCBIN`s of the compiled bytes,
      now as its RGBDS translation; a ROM slice without the compiler;
      `make compare` is the gate).
- [x] A larger, branchier function: `DrawSyncField` (262 B).
- [x] Shared header, cached batch verification, multi-function units
      (`c/bank03/InstrScreen.c`: 34 functions).
- [x] A screen input handler (300+ instructions): Wave, Word, Groove, Synth,
      Project and Song screens matched.
- [ ] Grow the C tree bank by bank (editor code first; the asm functions
      stay asm).

### Toward a moddable fork

The goal past matching is a fork that adds features, so code has to be able
to move. The milestone, pret's "shiftable", is met as far as the shift test
reaches: every program bank, its contents moved by 16 bytes, runs the same as
the original.

The checklist:

- [x] Translate matched C to RGBDS source: `tools/match/to_rgbds.py --check`
      assembles every matched function (code, literals and const data) with
      rgbasm/rgblink at its address -- all 575 byte-identical.
- [x] Build from that source instead of the INCBINs: `make match` writes
      each function's translation to `build/c/bankNN/Name.lines` and the
      carve renders it in place (the `.sym` labels inside each function
      kept, the literals and const data the C emits owned by it);
      `make compare` still the gate.
- [x] Symbolic code operands: every ROM address a `call`/`jp`/`ld rr` uses
      prints as a label, in the asm carve (`rom_pointers()` in
      tools/disasm.py: banked-call targets by their bank, bank 0's 22
      trampolines by the tracked `$2000` write, 37 switch jump tables and
      strings inside data blocks with labels of their own, the `jr cc` onto
      its own `$ff` operand — a `rst $38` assert — as `@ + 1`) and in the C
      (`rom_operands()` in tools/match/to_rgbds.py: the three
      `FarCall(5, 0x747D)`; FieldHelp's ten strings, which the C has to store
      byte-wise as numbers to match, are `LOW()`/`HIGH()` of the names its
      `#define`s give them). What stays a number is a constant — kit-format
      addresses (`$4000`, `$4052`), `gb_cmp16`'s add form (`p < 0xB290` →
      `ld hl, $4D70`): `tools/carve_clean.py --pointer-notes`.
- [x] Addresses loaded a byte at a time: SDCC's `Table[i]`
      (`ld a, LOW(PhraseCellWidth) / add a, c / … / ld a, HIGH(…) / adc a, $00`,
      or `adc a, b` for a word index), a banked call's target pushed as two
      bytes (`ld c, LOW(InstrHReadCursorCellAt) / ld b, HIGH(…) / push bc`, also
      through register copies, or loaded before a `jp` to a switch's shared
      `push bc / … / call FarCall` tail), a function pointer stored a byte at
      a time (SDCC spilling a banked call's target to the stack,
      `ld [hl], LOW(SndGetChainPhrase) / inc hl / ld [hl], HIGH(…)`, also via
      registers; the `wBlockSinkPtr` handler), and an address plus a constant
      (`wStringSrc = AsciiCharMap - $20` as `ld a, LOW(AsciiCharMap) /
      add a, $e0 / … / adc a, $ff`).
- [x] Pointer tables as `dw Label`: a `.sym` tag `[N] dw` (`[N] dw @$BB` for
      one pointing into another bank) makes the carve print the N words as
      labels — CommandHelpTable, FxCommandTable, HexCellPrefixPtrs,
      HelpMenuPtrs, HelpPagePtrs. The interrupt and `rst $38` vectors are
      named code now (they sat inside `Unused` fill symbols as data, their
      `jp VBlankHandler` a raw number).
- [x] A shift test: `tools/shift_test.py` (PyBoy, in `.venv/`) builds the ROM
      with N bytes of `$FF` padding inserted in a bank (`--bank`, `--all`) or
      in every bank at once (`--together`) — taken back from each bank's
      trailing fill — and runs it next to the unshifted build on the same
      SRAM (`.emu/lsdj_song.sav` if present) and input: a tour recorded on
      the reference (it finds its way round the screen map by search over
      save states: play the song; every screen with cursor moves, an edit and
      a play; the four instrument types; the file screen's load, play and
      save through the name editor) then seeded random input. It compares
      the screen and the sound registers every frame, and SRAM at the end. A
      shifted build is not cycle-identical — a pointer into moved data
      crosses a page at another step (`inc [hl] / jr nz / inc hl / inc [hl]`)
      — so a difference counts once it lasts 30 frames. Each run is a child
      process under a watchdog (PyBoy never returns from a CPU stopped for
      good). On a divergence it reports where an `$FF` (`rst $38`: crash,
      assert, `DebugTrap`) ran and the call chain; `--bisect` narrows down
      the stale target. Bank 0 shifts at `$0391`, past the page-aligned
      tables at `$0200`-`$0390` that SndCalcVibOffset reaches with
      `ld d, $02` / `ld h, $03`, which a fork keeps in place.
      Passing: every bank alone (16 bytes) and all at once (1, 3, 5, 7, 9, 11,
      16, 32 bytes), CGB and DMG, seeds 1-15. Odd sizes matter: a stale
      target can land on harmless code at one size and on a `ret` at
      another. It proves what the input reaches, no more.
- [ ] Behaviour tests on tools/emu for builds that no longer match the ROM:
      what a fork runs instead of `make compare`.

A function that won't match doesn't block this: it stays asm, which is just
as editable once the carve is symbolic, or gets functionally equivalent C
checked by the behaviour tests.

## References

- GBDK binaries: <https://sourceforge.net/projects/gbdk/files/> (`gbdk-win32/2.95-3/`, `gbdk/2.96/`)
- GBDK releases with SDCC source per tag: <https://juju.nz/src/juju/gbdk-releases>
- GBDK 2.95 mirror (with binaries): <https://github.com/rbong/gbdk> (`gbdk-2.95/`)
- SDCC: <https://sdcc.sourceforge.net/>
- Matching-decomp methodology (C via original compiler): pret `pokeemerald`.
