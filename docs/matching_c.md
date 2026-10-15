# Matching C

The C tree under `c/` matches LSDj byte for byte: every module, what it
holds and the C constructs it exercises, how to tell C from hand-written asm,
how C that matches is written, and the functions parked for a later attempt.
The compiler, the match harness and the build are described in
[`toolchain.md`](toolchain.md).

## Matched functions

| Source | Functions | What they exercise |
|--------|-----------|--------------------|
| [`c/bank00/DrawFields.c`](../c/bank00/DrawFields.c) | 7 functions: tempo, channel header, duty and pan fields | `switch` chains with `?:` returns, a register pair plus a constant, a block-local initialiser |
| [`c/bank00/FarCall.c`](../c/bank00/FarCall.c) | 5 functions: the far calls passing arguments or returning the callee's byte (FarCallByte, FarCallArgumentByte, FarCallArgument2V, FarCallArgument2, FarCallArgument3) | the callee called through a function pointer cast from a `UWORD` address (`r = ((UBYTE (*)(UBYTE, UBYTE))function)(a, b)`): the cast is the copy into b/c, landing reversed (`ld b,(hl); inc hl; ld c,(hl)`) because `old` takes c first and is then spilled for it; a function-pointer parameter is called straight from its stack slot. lsdj.h declares FarCallByte and FarCallArgument2V with function-pointer parameters for their callers, so FarCall.c defines `FARCALL_C` to drop those two prototypes. SDCC's return label after the `jp (hl)` is the `.resume` local |
| [`c/bank00/GbdkLib.c`](../c/bank00/GbdkLib.c) | 4 functions: GBDK's `abs`, the console `printf` and its number printer (`abs`, `printf`, `printn`), `strlen` (`StringLength`) | GBDK library C under the stock rules (`@stock-rules`), the 2.95-2 sources as they stand: `abs`'s `number < 0` and printn's `(int)u < 0` are the signed add form against 0 (`gb_cmp16` still runs for this unit), printf's `...` through GBDK's gbz80 `stdarg.h` macros (lsdj.h's fixed-argument printf prototype hides behind `GBDKLIB_C`), printn's digits a string literal emitted right after it (`DigitCharacters`); 2.95-2 hands both an emitter, the ROM's call putchar (DrawStringIndexed) and puts (DrawString) directly |
| [`c/bank00/Helpers.c`](../c/bank00/Helpers.c) | 7 functions: bank 0's shared C helpers | kit bank probes through a `volatile` MBC register, a word compare, the clipboard cell accessors, sign extension into an `int` array with a 32-bit `\|=`, a re-typed pointer argument |
| [`c/bank00/InstrumentByte.c`](../c/bank00/InstrumentByte.c) | `WriteInstrumentByte`, `ReadInstrumentByte`, `ReadCurrentInstrumentByte` | byte args, pointer local, 16-bit shift loop, asserts (one compound), both forms of pointer + byte |
| [`c/bank00/Main.c`](../c/bank00/Main.c) | `InitializeAndLoadSong`, `ProcessInput`, `main` | the boot `switch`, an endless frame loop with `continue`, an `\|\|` chain, a second absolute `sfr` name for a plain global, `?:` into globals, `FarCallArgument` with a plain function pointer; ProcessInput's `do ... while (--wLinkQueueCount && buttons == 0xFF)`, `if (buttons) wKeyboardHeldNote = 0; else return 1;` (the return laid out after), `if (ok = HandleButtonEvent())` (the call result tested in b, copied to c), `wKeyDelay == wKeyDelayTimer` / `wButtons == buttons` (the operand to load into `a` on the right), the transpose stored and then pushed as `wSongTranspose` (an assignment's value pushes the right operand again), `char wChainRow` (copied through b to be pushed), and SoundGetChainPhrase declared with its real arguments under `SND_DIRECT` |
| [`c/bank00/ReadSequenceByte.c`](../c/bank00/ReadSequenceByte.c) | `ReadSequenceByte` | `?:` on bit 7 of a call result |
| [`c/bank00/ScreenDispatch.c`](../c/bank00/ScreenDispatch.c) | 6 functions: the screen dispatchers, the B combos (mute, pan, solo and their release) | 15-case `switch` → `jp hl` tables, bank switching, cross-bank calls, case bodies out of value order, an `&&` chain; HandleInputCombos' `wChannelOutput[wSongColumn] &= 3` after `hSoundChannel = wSongColumn` (the address from the stored value, the operand reread), `wSongColumn == hSoundChannel` in the loops (the global left: `cp a,(hl)` on the loop test's `a`), `!(wKeyboardActionChannel == 0xFF) ? wKeyboardActionChannel : wSongColumn` (both booleans materialised), a two-case `switch` for B+LEFT/RIGHT (compares sorted, bodies in source order) and the saved pan in its own local (`v = wChannelOutputSoloSaved[hSoundChannel]; if (v) ... = v`) |
| [`c/bank00/Sprites.c`](../c/bank00/Sprites.c) | 10 functions: the sprite helpers | literal-pointer stores, a register pair plus a constant added in place, `v == *p` |
| [`c/bank00/Sync.c`](../c/bank00/Sync.c) | 18 functions: sync settings, transport and clock, the overload message, unchecked sequence/chain reads, the cartridge test's ROM checksum | literal bank switches around cross-bank calls, a message string in another bank, an early `return` under `&&`, nested `else if` with early returns; CheckRomChecksum: `(UBYTE *)0x2000` as a hoisted cast temp (SDCC folds no literal-to-pointer cast), `return (sum & 0xFF) == low && (sum >> 8) == high;` in value context (genAndOp evaluates both), the `& 0xFF` narrowed to a byte in c and then the `cp a,a` miscompile, and a `volatile UBYTE high` so the final test widens `high` again instead of reusing the subtraction's cast (CSE) |
| [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) | `DrawMessageBar` (198 B) | globals, `sfr` HRAM stores, two loops, calls |
| [`c/bank01/SongAllocation.c`](../c/bank01/SongAllocation.c) | 14 functions: song allocation bitmaps, 16-byte row copies | a bit test on the chain bitmap (`1 << (chain & 7)`, whose shift count dies to `ld b,#1`) |
| [`c/bank01/CartridgeTest.c`](../c/bank01/CartridgeTest.c) | 5 functions: the boot CARTRIDGE TEST screen (the ROM checksum and the SRAM self-test), the working song's SRAM format, the 'jk' marker check | a `UBYTE * volatile` loop pointer (kept on the stack, its `+ 1` through a register pair), `char` booleans stored and then tested, a stack-vs-stack `==` whose operand order avoids SDCC's `cp a` miscompile; FormatSramSong (a plain `UBYTE *p` for the same reason: its copies and the word loop's `*p` read spill themselves, `memcpy((void *)p, ...)` copies it into bc for the push, a volatile byte for the third saved property) |
| [`c/bank01/InstrumentNames.c`](../c/bank01/InstrumentNames.c) | 2 functions: an instrument's five-letter name copied to the name editor and written back | byte loops over the name buffer |
| [`c/bank01/Display.c`](../c/bank01/Display.c) | 7 functions: the CGB/DMG tables, the colour set and the screens' palette maps, the OAM and BG map clears, a screen's setup | `v = f(); v++` reusing the call register, a 12-case `switch` chain; ClearOamBuffer (a plain `UWORD p` compared as a long and cast for every use -- the cast copies spill themselves since p is their operand; `WriteVramByteSafeP` takes the pointer; the WAV sprites through a shadow-OAM struct pointer, `ld de,#addr; ld (de),a` stores) |
| [`c/bank01/Clocks.c`](../c/bank01/Clocks.c) | 2 functions: the PLAY clock and the WORKED time | a `volatile` scratch byte so `++` is not folded into the store |
| [`c/bank01/FieldNames.c`](../c/bank01/FieldNames.c) | 3 functions: the speed mode and on/off names, the speed glyph | `?:` over string literals, a `char` local (its dead `ld b,c` copy), a glyph table, a file-scope `const` table flushed ahead of the next function's strings |
| [`c/bank01/TopBar.c`](../c/bank01/TopBar.c) | 4 functions: the top status bar: the timed message or the screen title, a command's help line | a `switch` whose `default:` comes first and `goto`s into a later body, a global read as a word for a 32-bit `<` |
| [`c/bank01/EditorState.c`](../c/bank01/EditorState.c) | 2 functions: the selection and clipboard state reset, the song's project settings loaded | plain stores and SRAM property reads |
| [`c/bank01/CrashScreen.c`](../c/bank01/CrashScreen.c) | 2 functions: the crash entry and the DONT PANIC screen (the bank, the stack pointer, ten stack words) | DrawCrashScreen's `LCDC_REG &= ~2` (the int-promoted copy lands in c: `ld c,a`, never read; `0xFD` or `0xFDU` gives none), its strings as literals laid out right after it (the ROM's `Txt*`), `for (entry = 0; entry < 10; entry++)` over an `int` (the signed add form, `#0x7FF6`) and a `while (1);` after which SDCC still emits the epilogue (4 dead bytes) |
| [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) | `ShowOutOfMemory`, `ShowTapAddChain` | one-line wrappers, 16-bit argument |
| [`c/bank02/Sound.c`](../c/bank02/Sound.c) | 5 functions: the engine tick, the state pointers' advance, a chain's phrase lookup, the synth's frame stepping and the speech player's step | SoundSpeechStep's `n = wChannelNote[2] = v + 1` (the store's address loaded before the sum) and its `rRAMG = 0x0A; x = *p; rRAMG = 0;` (the MBC registers are `volatile` in lsdj.h, or SDCC drops the first store as dead) |
| [`c/bank02/Commands.c`](../c/bank02/Commands.c) | 25 functions: the phrase and table commands' C half: E's envelope restarts, K, O, P, R, T, Z and the table's maybe-hop, the noise channel's pitch and chord steps, a delayed command's slot | Command_F_Wav's `wWavSynthFrame = hCommandValue + wWavSynthFrame` (an sfr on the right of `+` loads over the left operand in stock SDCC: `ld a,(hl); ldh a,(X); add a,a`), an if/else rather than an early `return` (the inner test then jumps straight to the end), `off & 0xFF` (the low byte through c, the AND's temp unpacked) and `hCommandValueS`/`wWavLevelS`, the two bytes as signed globals so one cast widens them (a `(BYTE)` cast first leaves a byte temp in c), SoundNoiPitchStep's `v & (UBYTE)3` (a plain `& 3` int-promotes `v` into a two-register temp, spilled to the stack while b and c hold the sign and the value) and the vibrato offset in its own local (`v` reused stays live over the test, pushing the test's temp into b); Z's value as one expression over `wChannelCommandValue[slot]` (the address kept in a stack slot, the digits read through it once), R's `n = hCommandValue & 0x0F; n = MulU8(n, wChannelCommandRate[hSoundChannel] + 1)` (the ROM computes the first argument first; arguments are evaluated right to left) |
| [`c/bank02/Playback.c`](../c/bank02/Playback.c) | 15 functions: starting and stopping the song or a screen's playback, the play range, live mode's queued starts and stops and its chain end | a pointer local indexed with constants (`q = wChannelLiveQueued; if (q[1] && !wChannelActive[1])`: `q[1]` is a 16-bit `ld hl,#1; add hl,de` into a stack temp where `wChannelActive[1]` is a direct address; SoundLiveStartQueued, SoundNoiChordStep), SoundStartScreenPlayback's repeated `wSongColumn` reads (the second one becomes `ld a,c`, see `track_a`), a `?:` assignment keeping its value in a register (`v = right ? a : b`), a mask compared before the call it is compared with (`(v & 0xF0) > randw()`), `randw` declared as returning a byte, a pointer sum assigned with no cast in between, a const table flushed after its function, a local copy of the volatile `hSoundChannel` shared by later stores (`k = hSoundChannel;`), `wSongRow == *p` (the global left of `==`), `wChainRow` a `char`, `hCommandValue` read both as an sfr and as a plain global (`hCommandValueMemory`), `!=` against a global putting the not-equal branch first, `while (v && wWavLevel)`, SoundStartSongPlayback's chain-length scan with both tests stored (`while (!(stop = wChainRow < n) && !(end = SoundGetChainPhrase(*chain, n) == 0xFF)) n++`) and pointer locals for the channel's cells (`*active = 1`) |
| [`c/bank02/SoundChannel.c`](../c/bank02/SoundChannel.c) | 3 functions: the sequencer's per-channel tick (SoundUpdateChannel: the next phrase or chain row, then the row's B, D, G and H) and its step read (SoundReadChannelStep: the note or the PS/2 keyboard's, the groove less any D delay, the instrument, the transpose) and the phrase's H hop (SoundPhraseHop: HFF stops the song, else after the high digit's count of hops to the low digit's row of this phrase or the chain's next) | the working pointers (`wPointerNoteSource`, ...) as pointers, which Sound.c declares as bytes for SoundAdvanceStatePointers (one view per file); `*wPointerNoteCountdown > wChannelDelayTicks[hSoundChannel]` with the operand the ROM reads first on the left; a `switch (SoundReadPhraseCommand())` whose case 8 body comes first in the source (the chain's last `jp z` folds into `jp nz`) and whose case 0 shares the switch's end with a falling-through case 7; B's and D's values in separate locals (the ROM gives them separate stack slots); `wSongColumn == hSoundChannel` (the global on the left); `if (0xF0 & v)` for H's register-held mask |
| [`c/bank03/InstrumentScreen.c`](../c/bank03/InstrumentScreen.c) | 66 functions of the instrument screen and its input | field get/set/step helpers, `FarCall` in both address forms, SRAM literal addresses, a string literal placed after its function, `hl` kept across a `!=` compare; InstrumentScreenInput (9.8 KB) is one `switch (wButtons)` (sorted compare chain, `case 0x10` tested through the CSE'd `wButtons == 0x10`, bodies in the ROM's order) whose A+direction cases switch on the type and then the field (jump tables for contiguous fields, compare chains otherwise; a field's body can come before a lower field's, so the labels follow the tables, not the layout): the kit's fields 9/11/13 are `case (int)9:` (int-typed constants: a 16-bit compare of a copy widened from `a`), `v = ReadCurrentInstrumentByte(n); v op= k;` where the ROM stores the read before using it and `v = ReadCurrentInstrumentByte(n) op k` where it does not, `v &= 0xFC; v |= 2;` (an OR with a literal never leaves its operand in `a`), the flag toggles' `v & 64` / `v & 128` (decimal: signed literals copy `v` into a register for the `bit` test), `v = (int)(char)v & (int)0xBF` (a 16-bit AND narrowed straight into the stack byte), block-scoped `kit` locals (two same-named block locals at one level become one symbol), and `FarCallByteU(0x1F, InstrumentEnvelopeHasSecond)` for callees declared returning UBYTE; SetVibratoShape (a `char` parameter whose OR reads it through a cast placed between the mask and the OR, so the masked byte keeps c -- see Writing C: accumulator packing; the callees take a `char`: `WriteCurrentInstrumentByteC`, `FarCallArgumentWC`) |
| [`c/bank1c/FileScreen.c`](../c/bank1c/FileScreen.c) | 17 functions: the file screen: the list and its fields, the load, save and erase actions, the verify panel, the input | field highlights from column tables, a `do … while (--channel)` clear (CSE shares `channel - 1`); DrawFileTextField's frame loops (bounds inline, see Writing C: hoisting depth) |
| [`c/bank1c/FileSystem.c`](../c/bank1c/FileSystem.c) | 15 functions: SRAM's song files: the bank and format checks, the block allocation table, the slots' names and block counts | `?:` returns; FindFreeBlock (the `?:` over two `long` literals copied into a `UWORD end`, the signed-difference loop `while ((p - end) & 0x8000)`, the call result unnamed so `end` is the spill victim, `+ (UWORD)0xA000` to keep the block address 16-bit, both range asserts reading back through the pointers) |
| [`c/bank1c/Compression.c`](../c/bank1c/Compression.c) | 4 functions: the compressor and decompressor between the 32 KiB working song and SRAM's file blocks | CompressSong (the `current == next` operand order, `wCompressDestination + 2` read twice through one CSE'd temporary, `wCompressSource & 0x100` / `wCompressDestination & 0x8000` as `bit` tests of the high bytes) |
| [`c/bank1c/LinkClipboard.c`](../c/bank1c/LinkClipboard.c) | 4 functions: a copy's clipboard sent to and received from a linked Game Boy | the serial receive wait |
| [`c/bank1c/FileUpgrade.c`](../c/bank1c/FileUpgrade.c) | 46 functions: the song-format upgrades, the tempo properties' migration, the R command's rate fix of version 13 | per-type instrument callbacks through a function pointer (`FileForEachInstrumentType`), the upgrade steps' field fix-ups, a 16-bit `\|` forced by an `int` constant, FileUpgradeV11's aliases (`ReadSongBytesW` takes the UWORD SRAM cursor, `ReadInstrumentByteS` returns `char` so its result keeps a slot of its own; FileUpgradeV18's `v = types[instrument] == 3` gets the same copy from the comparison's `char` result) |
| [`c/bank1d/GrooveScreen.c`](../c/bank1d/GrooveScreen.c) | 15 functions: groove screen: steps and rows, cursor, update and step cache, the input, cut/copy/paste and selection | SRAM groove accessors, the cache walker; GrooveScreenInput (`char wGrooveRow` against a decimal `16`, which needs no conversion where `0x10` copies it through `c`; `while (top != bottom)` with the operand SDCC loads into `a` on the right; `end = bottom; end++;` for `ld c,(hl); inc c`; `wSelectionBottom = wSelectionTop + 1` reusing `a`; `switch ((char)wMapColumn)`) |
| [`c/bank1d/SynthRender.c`](../c/bank1d/SynthRender.c) | 14 functions: a synth's 16 wave frames rendered from its parameters: the curves' fill, the signal shaped by the filter, the volume, VSHIFT and LIMIT composition, the PHASE stage, each frame written back | FillSynthWaveRange's `*buffer++ = ((UWORD)step * b >> 4) + a` (the 16-bit product through MulU16, the sum operand order per branch), RenderSynthWave's curve buffers as real array symbols (`wSynthCurveVolume[wSynthRenderIndex]` is the `ld a,#<sym; add a,c` form a cast literal would not give); LoadSynthSignalWave's `p = wSynthScreen; p <<= 4; p += (UWORD)0xAF00;` (shifted and added in p's slot; a bare `0xAF00` is a `long`) with a signed `char v` so the second nibble's cast copy, not the loop counter, takes the stack (see Writing C: an instruction's own operands); LoadSynthParameters reads the 16 parameter bytes through `volatile UWORD` locals (`ReadSongByte(1, p++)`: the old value fetched into bc, the slot incremented in place, a plain `p` pushed straight from the stack), inline call results as arguments (a named result would hold c through the whole function), the last pair into two locals so the `+ 0x0F` adds come out in the ROM's order, and the render destination as a pointer cast of the sum; PinchOrWarpSynthWave (the resampling filter: two scratch words serve as slot pointer and slot value in the loop's first half and as value and doubled index in its second -- `*(UWORD *)wSynthPhasePointer` through a `UBYTE *` global so the pointer's cast copy is spilt into the global itself and reloaded from it after MulU16; the index as a **block-scope `extern UBYTE`**, which this compiler treats as a register-equivalent local whose spill location is the global: the shift's temporary is spilt into $CB3A, both accumulator pointers are one 2-use temporary each, and the index is fetched from the global with `ld l,(hl)` -- a file-scope global would be copied per use (near-space globals are never replaced by CSE); `(ub - 0x100) & 0x8000` for the stored-then-tested sign of a temporary where `(int)(ub - 0x100) < 0` folds into an unsigned compare) |
| [`c/bank1d/SynthScreen.c`](../c/bank1d/SynthScreen.c) | 28 functions: the synth screen: its fields, the cursor, the lock and the overwrite prompt, the parameters read, written and stepped, the input | SRAM param accessors, a `switch` through a jump table, a `?:` over `long` constants (> 32767); IsSynthLocked's `1 << (x & 7)` mask; BuildOverwritePrompt's frame (the ASCII restore at the end of the middle-column branch, see Writing C: hoisting depth); SynthScreenInput (`if (wButtons != 0x10) wDrawStep = 0;` before the `switch (wButtons)`, whose case 0x10 test CSE takes from that boolean in `c` -- see `switch_chain`; nested `switch (wSynthCursor)` jump tables whose case bodies follow the source order, fall-through chains for the cursor moves, a `default:` written first where its body comes first) |
| [`c/bank1e/Graphics.c`](../c/bank1e/Graphics.c) | 5 functions: the top row's column labels, the font loader, VRAM helpers | `UWORD` loop addresses compared against `(UWORD)0x9800` (constants above 32768 are `long`), a `while (STAT_REG & 2)` wait; DrawScreenColumnLabels's `switch (wCurrentScreen)` with its bodies in source order (1, 3, 5, 2, 12, 4); LoadFontTiles pushes its two spilled DMG blocks through `(UBYTE *)(UWORD)` cast temps (the ROM's `ld c,(hl); inc hl; ld b,(hl); push bc`); CopyVramTile (`WriteVramByteSafeC(destination++, wVramCopyScratch)`: the char value's cast copy of the scratch global is the pointer copy's register occupant) |
| [`c/bank1f/InstrumentFields.c`](../c/bank1f/InstrumentFields.c) | 12 functions: the instrument screen's bank-$1F half, each instrument type's fields (pulse, wave, kit, noise), the envelope and table fields | DrawKitInstrument (`UBYTE v, flags`: byte 5 in a slot of its own, `DrawStringIndexed((char)v)` for the volume digit's copy through b, `0x40 & ReadCurrentInstrumentByte(2)` for the SPEED test) |
| [`c/bank1f/Allocation.c`](../c/bank1f/Allocation.c) | 8 functions: chains' phrases written, instruments claimed and allocated, free tables found, song blocks tested for data | plain C |
| [`c/bank1f/CleanSongData.c`](../c/bank1f/CleanSongData.c) | 21 functions: the project screen's two clean-ups: unused instruments, tables and synths freed, duplicate phrases, chains and tables folded and their references remapped | SRAM resets bracketed by `_asm di _endasm;` and MBC register writes; RemapCommandTableRefs scans three SRAM ranges through `volatile UWORD` pointers (`*(UBYTE *)p` reads via de, in-place increments, `p != (UWORD)0xB880` byte-wise), `from == *(UBYTE *)q` (the global on the left), a `volatile UBYTE` counter and a `volatile` remapped byte for the instrument loop; FreeUnusedInstruments (CLEAN INSTR DATA's instrument pass) calls `FarCallArgument2P(4, FAR_FN(ReadPhraseInstrument), phrase, row)` (the literal through two cast temps), builds the parameter address in place (`address = instrument; address <<= 4; address \|= 0xB000; address += 0x80;`), clamps `UBYTE n` with `if (n & 0x80)` (`bit 7`) and keeps `wColumnInstrument[column - 1] == instrument`, which SDCC compiles to `cp a,a` (always true); FindDuplicatePhrase/FindDuplicateChain/FindDuplicateTable (the duplicate scanners: `UWORD t` declared last for the first row address, `p = (UBYTE *)t`, the other rows cast from `oa + (UWORD)base` temporaries, `if (*p++ != *q++) return 0;` for the byte loops); DeduplicatePhrases/DeduplicateChains/DeduplicateTables (the DE-DUPE passes: plain `UBYTE phrase, candidate` counters (`chain`, `table` in the others), `phrase = phrase + 1`, the inner call result and the FindDup result stored to their scratch globals before the test, `DrawString("OK")` -- the shape only LSDj's compiler gives, see *Writing C*: `global = call()`) |
| [`c/bank1f/UsageScan.c`](../c/bank1f/UsageScan.c) | 16 functions: which tables, phrases and chains the song uses: the marks, the walks that set them, the counts | the used-table/phrase/chain scans and counters (one template, four limits) |
| [`c/bank1f/SynthReset.c`](../c/bank1f/SynthReset.c) | 7 functions: which synths' wave frames differ from the default, and the frames and parameters reset | plain C |
| [`c/bank1f/PhraseScreen.c`](../c/bank1f/PhraseScreen.c) | 10 functions: the phrase screen's bank-$1F half, its update handler, the channel moves, cut/copy and the cursor and selection highlights | DrawPhraseSelection's loop bounds are `?:` expressions (`column < (right == 5 ? 5 : right + 1)`: the test and the `+ 1` hoisted into stack temporaries, the choice made in b each pass), its ring test an `\|\|` chain over hoisted and per-row booleans, and the cursor column read into a local for the first comparison only (the ROM's `ld c,(hl)` before `sbc a,c`); CutCopyPhraseSelection (a 70-byte frame: every call result and boolean in its own slot) is a five-case `switch` whose case 0 can `goto` the case 2 label, a `UBYTE cutInstrument = cut;` copy (CSE reads the copy for the message choice), and far calls short one argument (`FarCallArgument2P(4, FAR_FN(ReadPhraseNote), row)`); HighlightPhraseCell (the cursor/selection colouring: `char x`, the width stored and passed as a comma expression in the argument, see *Writing C*); a function that returns `e` by accident (no `return`) (EnterPhraseRight/Left) |
| [`c/bank1f/SpeechInstrumentScreen.c`](../c/bank1f/SpeechInstrumentScreen.c) | 7 functions: the speech instrument screen, the word list, its cursor and its input | SpeechInstrumentInput (a `switch (wButtons)` whose case bodies run in source order, `(UWORD)` on every SRAM literal above `0x7FFF` -- a bare `0xBDD0` makes the address arithmetic 32-bit --, `p++` arguments, CSE sharing `(UWORD)wSpeechRow` between the `<< 5` and the `* 3`) |
| [`c/bank04/PhraseScreen.c`](../c/bank04/PhraseScreen.c) | 44 functions: phrase screen and its draw, its row loader, the four A+direction cell editors, instrument entry and name draw, prelisten, the screen's input | DrawPhraseScreen (file-level `const` attribute rows defined just before it, which SDCC flushes after its code and ahead of its string literals; a `switch (instrument)` whose `== 0x40` test CSE reuses later as a stored boolean; `KitIndexToBank((char)t & 0x3F)` for the second sample's copy through `c`; a nested `?:` for the attribute row; `!wPhraseColumn` and `(char)wPhraseRow`), far-call wrappers (`FarCallArgument2V`), the phrase instrument reader and the backward search for the instrument in effect (a return through `e`, `track_e`), `?:` returns, a `volatile UWORD` SRAM offset (no CSE of its casts: each push reloads it), `if (a != X && b != Y) return;` chains (the ROM's uncollapsed `jp z,L; jp done; L:`), a name copy through `char` casts, a `UWORD address = (UWORD)p` copy (a named spill slot the allocator evicts instead of the value), a `wSongColumn == 3` test the ROM shares between an `if` and a later `\|\|` (one CSE temp without a spill slot, so the value spills instead); PhraseScreenInput (8.6 KB) is ChainScreenInput's `if` chain over the five columns, its A+direction blocks walking the rows with a per-column `switch` (sorted `case` tests, bodies in source order, the value column's per-command clamps), the command letter stepped in a `do`/`while` over `PhraseStepOnes[(UWORD)wSongColumn * 20 + command]` (MulU16 and a 16-bit index; `wSongColumn * 20` alone is 8-bit) and the kit sample nibbles scanned past empty names: `wButtons == 0x10` is written twice (a named `isA` is the spill victim when the long-lived `wCursorMode == 4` boolean wants its register; the CSE'd test keeps b), `(UBYTE)(char)wPhraseRow >= 0x10` (an unsigned test of the register copy), `(char)wChainRow` far-call arguments, a `char chainRow` that keeps `chainRow + 1` across SoundGetChainPhrase for the store after it, `wPhraseColumn == 0` (not `!wPhraseColumn`), `wInstrument = instrument; if (wInstrument == 0x40)` (the stored byte tested in `a`), PlacePhraseCell declared with the phrase and row its caller pushes but never reads; PastePhraseSelection (the 27-byte frame of a paste: locals declared in the ROM's slot order `char column; UBYTE row; ... UBYTE c4, c3;`, `switch (wSelectionRight - wSelectionLeft) { default: return; case 2: ... }` with the default first -- a single-case switch with the default last is inverted into one `jp nz` --, a nested switch on wSelectionLeft for the command pair's shift, `for (column = wSelectionRight - 1; !(column < (char)wSelectionLeft); column--)` for the signed column loop with its materialised test, `switch (column)` for the five column pastes (its range checks are the two stored booleans before the jump table), `wPhraseNoteIsKitA = CursorInstrumentIsKit()` -- the call-result temporary LSDj's compiler keeps, spilt into the global --, and `if ((char)wPhraseRow == 0x10)` for the row wrap (the cast copies the incremented row into c)) |
| [`c/bank05/TableScreen.c`](../c/bank05/TableScreen.c) | 32 functions: table screen (draw, step reader, play markers, cut/copy/paste, selection, input) and screen map (draw, enter) | table step accessors through column pointers held in `UWORD` globals, the map moves, materialized `<=` tests, a two-case `switch`, six row-buffer column pointers (`t += (UWORD)0xBC80` keeps the add 16-bit), a `switch` with `default:` between `case 0` and an empty `case 0xFF`, the loop counter reused for the cursor x so the column index lands in b; CutCopyTableSelection's six-column `switch` (a jump table whose range test, like `column == 6`, is hoisted out of the row loop as a stored boolean; each case's read result gets its own spill slot) behind `if (column == 6 && row) continue;`; PasteTableCells is the same loop and `switch` over clipboard reads, with `UBYTE swap = 0, fromPhrase = 0` initializers and PasteChainCells' `(char)wTableRow & 0x0F` wrap; DrawTableSelection sorts its rectangle with SortBytePair on address-taken locals and reads `bottom = (char)wTableRow` / `right = (char)right + 1` through b and c; TableScreenInput (5.7 KB) is one `switch (wButtons)` whose bodies run 0x43, 0x10, the directions, A+directions, B+directions, A+B, select+A, start, select+directions, B, select+B -- its `case 0x10` test is the CSE'd `wButtons != 0x10` boolean from the top (`or a,c`), each A+direction a row loop with a `switch (wTableColumn)` per cell (command letters stepped with `wPendingCommands[row]++` / `while (TableSkipsCommand(...))`), `(char)wTableRow` copies again, `v = ReadTableEnvelope(row); v += 0x10;` (the call's byte stored straight into v), and the groove lookup's addresses as UWORD sums with `(UWORD)wTableRow` (a real cast, which CSE keeps across the call; the pointer form's index is an assignment, dropped at calls) in block-scoped `a, b` |
| [`c/bank05/FieldHelp.c`](../c/bank05/FieldHelp.c) | `ShowInstrumentFieldHelp` (872 B) | a `switch` on the instrument type around jump-table `switch`es on the field; the help strings shared by several types are stored by constant address (byte by byte) and the rest by symbol (`ld de,#sym`), so the C spells the first group as `((char *)0x6D25)`; `== 0` on the PLAY field's ReadInstrumentByte (the `ld a,c; or a,a` compare, where `!x` is toBoolean's `xor a,a; or a,c`); the final `if (s)` reload is `ifx_word_hl` |
| [`c/bank06/WordScreen.c`](../c/bank06/WordScreen.c) | 9 functions: word screen (screen 12): a word's allophones and lengths, the allophone kit and names, the cursor and the input | an early `return` under `==` (SDCC's `jp nz,L; jp end; L:`) |
| [`c/bank06/WaveScreen.c`](../c/bank06/WaveScreen.c) | 21 functions: wave screen: the graph and its columns, the cursor and selection sprites, the range edits (reverse, flip, change, line), copy/paste, the synth lock | `while (position != wWaveSelectionEnd)` (global right: the kept `jr nz` trampoline), `(UWORD)y != 0x80`, the wave copy's nibble pairs read as `ReadWaveSample(position) << 4 \| ReadWaveSample(position + 1)` (a named temp would stay live across the second call); WaveScreenInput is a twenty-case `switch (wButtons)` returning 1/0 per case, its bodies in source order ($43 first) and the cursor wrap `(char)wWaveCursor & 0x1F`; DrawWaveColumn passes its tiles as nested `?:` arguments (the outer temp stored, an inner one copied over it) and tests a `char` copy of the column (`t = column; if (t & 8)`, the copy in b); LockCurrentSynth (`s = wSynth; s >>= 4;` so the address spills before s, a named `r |= 1 << (s & 7)`, and a frame byte that only receives s -- `volatile UBYTE m; m = s;` declared before p for the slot order) |
| [`c/bank06/ChainScreen.c`](../c/bank06/ChainScreen.c) | 17 functions: chain screen: rows and transposes, draw and update, cursor, phrase open and clone, the input, cut/copy/paste and selection | the chain row buffers through `ReadSongBytes`, a far call by literal address (`FarCall(5, 0x747D)` pushes `ld hl,#nnnn`), `(char)` casts on far-call arguments (their temps take c, so the function-pointer temp lands in b/c reversed), eight SRAM row pointers where the two bank-2 ones are *recomputed* (CSE copies them from the first pair's spill slots; writing `s3 = s1` adds a use and keeps `s1` in bc); CutCopyChainSelection's per-column work is a `switch (column)` (both case tests hoisted out of the row loop as stored booleans, the bodies laid out after the tests; an `if`/`else if` puts the first body between them); PasteChainCells is the same `switch` with `FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), ...)` (the literal copied between two cast temps, which a `(UWORD)` argument folds away) and `wChainRow = (char)wChainRow & 0x0F` (the row read into c first); DrawChainSelection is DrawPhraseSelection's twin over the two chain columns (`hGameboyType ? column : 0` as the ring's palette); ChainScreenInput (4.4 KB) is an `if` chain on wButtons whose four A+direction blocks walk the selection with SortBytePair'd bounds and a two-case `switch (column)` per cell; `wCursorMode == 4` is written at each use (CSE keeps one stored boolean, a temporary below the named locals), wChainColumn and wChainRow are read through `(char)` copies (char locals `cursorColumn`/`cursorRow` kept in c/b across the selection test; `(char)wChainRow` arguments reverse the far calls' function-pointer pair), `++column`/`--column` in the B+LEFT/RIGHT channel loops (an `inc (hl)`; `column++` keeps the old value), `wChain = GetCursorChain(); if (wChain & 0x80)` as two statements, `v >= 0xEF` for the 0xFE clamp laid out first, and `wSelectionLeft != wChainColumn` (the global to load into `a` on the right) |
| [`c/bank06/StatusPanel.c`](../c/bank06/StatusPanel.c) | 7 functions: the per-frame status panel: each channel's playing note (a kit sample name or a note name), the sync status | `?:` over string literals as an argument, string literals declared as the ROM's `Txt*` symbols where the unit's data block sits elsewhere, `(UWORD)t < 0x78` (a 16-bit `add hl` compare off a just-stored temp), `if (n != 0)` keeping the reload of a pointer-read value |
| [`c/bank06/DrawTileHexCell.c`](../c/bank06/DrawTileHexCell.c) | `DrawTileHexCell` (130 B) | consecutive updates of a stack local, sign test, `-t`, pointer table |
| [`c/bank07/KeyboardLink.c`](../c/bank07/KeyboardLink.c) | 11 functions: keyboard octave, the link queue and its scancode dispatcher, keyboard notes, sync rate, a hex digit, the keyboard instrument name/state and calibration screen | a multiply kept as a `MulU16` call, `di`/`ei` around a 16-bit store, an empty case listed first; `UBYTE * volatile` pointers to `&wKeyboardChannelState[1]`/`[2]` (a single-definition pointer local is otherwise rematerialised) and `wKeyboardChannelState[3] = 0` through the array (the ROM's `ld de,$cb99; ld a,0; ld [de],a`); the name copy as direct stores, `wSampleNoteString[letter] = wNameEditBuffer[letter] ? wNameEditBuffer[letter] : ' '` and `wSampleNoteString[1] = HexDigitCharacter(…)` (the address temps live across the call and share one spill slot; naming the address in a pointer local keeps the counter in c instead of on the stack); ProcessKeyboardNote's `armed = wSongColumn == 3 \|\| !FarCallArgumentByte(...)` as one expression (the `==` temp is what the far-call pointer evicts from c, so the pointer lands b/c reversed), `wKeyboardHeldNote != note` with the global on the left; ProcessLinkQueue's 28-case `switch` on a stack byte (the ROM reloads it for every compare), case bodies in the ROM's order with `case 0xC0: if (!(modifier & 2)) goto note;` falling into `case 0x40`, `wKeyboardChannelState[1]` (never the Pu2 symbol), and FarCallArgument called with two arguments (`FarCallArgument0`, aliased) |
| [`c/bank07/SongScreen.c`](../c/bank07/SongScreen.c) | 31 functions: song screen, its row palettes and play markers, the chain clone, bookmarks | page-up/down scroll arithmetic, bookmark add/toggle in SRAM, `WriteBookmarkByte`'s pointer-range asserts (16-bit compares: `ld hl,#-lit; add hl,de`), a `!=` sprite loop; the row-number column's `(row & 4) ? 1 : 2` (the int-promoted copy of `row` is stored to the variable's own stack slot and the register tested) and its second loop's `if ((BYTE)row & 4)` (a signed cast copy lands in b: `ld b,c; ld a,b; bit 2,a`); SetSongRowPalettes reuses one local for the row offset and the cursor flag (`current = wSongScroll + y - 2; shadow = current & (UBYTE)4;` … `current = channel == wSongColumn`) -- the long-lived `current` is what makes the allocator spill the bookmark flag instead of the counter, and `(UBYTE)4` avoids the int-promoted copy; `channel == wSongColumn` (register left, global right) is the `jr nz` boolean, `wSongColumn == channel` the `jp nz` one; UpdateSongPlayMarker's sprite index is written `channel + 4` at each use (a CSE'd temp; naming it keeps `channel` in c where the ROM has it on the stack); CloneSongChain's two pointers into the $C532 map are `wClonePhraseMap + 16` and `(UBYTE *)wClonePhraseMap + 16` (the cast keeps the second sum from being CSE'd into the first), `if (!(wClonePhraseMap[j] - phrase))` for the materialised difference test, the `phrase != 0xFF` deep-clone body first with the $FF write as the else branch, and the transpose read reusing `phrase` |
| [`c/bank07/SongInput.c`](../c/bank07/SongInput.c) | `SongScreenInput` (3011 B) | its own unit (CopySongColumn with UWORD parameters, as in SongCutCopy.c): an if/else chain on wButtons whose `wButtons == 0x10` / `== 0x20` / `wCursorMode == 4` tests CSE keeps as stored booleans; `UBYTE *p = (UBYTE *)0xB290 + ...` (a pointer add straight into p, where the UWORD sum and a cast go through bc) and `p += wSongColumn` (a byte operand, no cast temporary); `(char)wChainRow` for the reversed `ld b,lo; ld c,hi` FAR_FN pair; comparison operands written the way round SDCC loads them (`wSelectionLeft != wSongColumn`, `top != wSongScroll`) |
| [`c/bank07/SongCutCopy.c`](../c/bank07/SongCutCopy.c) | `CutCopySongSelection` (545 B) | its own unit: CopySongColumn prototyped with two UWORDs (the ROM casts both pointer arguments; SongScreen.c defines it with pointers), `UBYTE * volatile` song pointers (reloaded at every use; the plain pointer would inherit the shifted offset's register pair), a `char` visibility flag, `x = (UWORD)p` as a cast (a plain copy is propagated away), `0xC0 - t` through a UWORD local, `isCut && !wSelectionLeft && wSelectionRight == 4`, the CUT/COPIED CHAINS literals after the function |
| [`c/bank07/SongPaste.c`](../c/bank07/SongPaste.c) | `PasteSongSelection` (415 B) | its own unit: `extern volatile UBYTE wSongRow` (the row-shifting loop recomputes `wSongRow + h` where a plain global shares the test's value), WriteSongSramBytes prototyped with UWORD source and destination (both pointers cast, the source through a reversed `b/c` pair), `(UWORD)0xC0 - (wSongRow + h)` for the 16-bit count, `(UWORD)p` compared against `(UWORD)0xB290`/`0xB590` (the CSE'd cast shares one spill slot with the clipboard byte), `if (!(... & 0xF0)) continue;` for the scroll step |
| [`c/bank07/SongSelection.c`](../c/bank07/SongSelection.c) | `EditSongSelection` (667 B) | its own unit: SortBytePair prototyped with `char *` (the `&UBYTE` locals go through cast temps: the first spilled below the frame's variables, the second in bc), the clamp/erase loops written as expressions (`while (wSongColumn < (x1 == 4 ? 4 : x1 + 1))`, a four-way `\|\|` of `<`/`>=` tests each materialised into a spilled boolean temp: a temp defined in a loop is live to the loop's end), the 32-byte CGB attribute table (`const`) after the function |
| [`c/bank1b/HelpScreen.c`](../c/bank1b/HelpScreen.c) | 11 functions: help screen: the menu, its entries and pages, the input | `(UWORD)x == 9` (a 16-bit compare: one `jp nz` per byte through one trampoline), a NULL-terminated pointer table |
| [`c/bank1b/NameEditor.c`](../c/bank1b/NameEditor.c) | 7 functions: the name editor: the keyboard cursor, the edited name field, commit and backspace, the input | DrawProjectTextField, the same frame as DrawFileTextField with the field draw first; NameEditorInput (`wNameEditBuffer` is `char[]`, so its RAM assertion `(int)(wNameEditBuffer + 1) > -1` compares signed; the keyboard is `char AlphanumericCharacters[][11]`; `wNameEditPosition = wNameEditPosition + 1` stores the terminator's `+ 1` kept in c where `++` is `inc (hl)`) |
| [`c/bank1b/ProjectScreen.c`](../c/bank1b/ProjectScreen.c) | 18 functions: project screen: the sync, tempo, key-repeat and prelisten fields, the cursor, the input, tap tempo | property writes to SRAM, a busy-wait on a global, `?:` string arguments; ProjectScreenInput (the `switch (wButtons)` case 0x10 shares the `wTapTempoCount` test's boolean; `goto`s into other cases' blocks for the menu's left/right and the reset path -- the ROM's tail merging is source-level; `_asm ei/di _endasm` pairs; `wSyncMode != 0` for genCmpEq's `ld a,(hl); or a,a` where `if (wSyncMode)` is toBoolean's `xor a,a; or a,(hl)`) |
| [`c/bank1b/KitMixer.c`](../c/bank1b/KitMixer.c) | `InitializeKitMixer` (190 B) | a code copy bounded by function addresses (`(UBYTE *)(UWORD)MixerKernel` up to `DrawHelpMenu`, the routine behind it), the copy loop's `destination = destination + 1` / `source = source + 1` beside the `*destination++` opcode stores (`inc c`, `inc e`) and the closing `ret` |
| [`c/bank1b/EnvelopeGraph.c`](../c/bank1b/EnvelopeGraph.c) | 5 functions: the instrument screen's envelope graph: the envelope bytes, the plotted curve and its sprites | ComputeEnvelope's three segment loops write `volume != volume2` (the ROM loads the target first) and shift by 3; PlotEnvelopeCurve (the graph buffer as a `char *`: the loop store's (char)y is hoisted and holds c until the column address evicts it, the compare at x reuses that signed copy -- see Writing C: a hoisted cast) |
| [`c/bank1b/DrawSyncField.c`](../c/bank1b/DrawSyncField.c) | `DrawSyncField` (262 B) | sparse `switch` (`cp` chain), if/else, a pointer local chosen by a condition, 4-arg calls |
| [`c/bank1b/DrawFontColorFields.c`](../c/bank1b/DrawFontColorFields.c) | `DrawFontColorFields` (107 B) | `const` 2-D name tables (SDCC 2.2.1 drops a NUL inside a literal and emits a unit's const data after its first function, so the tables behind it in the ROM make this its own unit), an sfr pushed with `ldh` |
| [`c/bank1f/DrawChannelStatus.c`](../c/bank1f/DrawChannelStatus.c) | `DrawChannelStatus` (324 B) and `DrawAllChannelStatus` (the four in turn) | the song screen's MUTE/L/R channel cell; two `const` CGB attribute rows behind it in the ROM make it its own unit, a static pointer into a static flag array ($C9CE/$C9D2) |

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

`RenderSynthWaveA` (367 B), `RenderSynthWaveB` (403 B), `ComposeSynthWave`
(882 B), `RenderSynthWaveType` (581 B), `DrawSynthNumber` (739 B),
`BuildOverwritePrompt` (476 B) and `OverwriteSynthWaves` (110 B) match in
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
  control through `UWORD`. The shared test is `high == low`; FOLD uses
  `v >= high + 1`, preserving the ROM's hoisted comparison bound. The new
  `wSynthCurveResonance`/`wSynthCurveLimit` symbols document `$CA94`/`$CAA4`.
- The redraw's SRAM cursor is a plain `UWORD`, not a pointer. The two `--`
  literals are emitted after the function at `$68A5` and `$68A8`.
  `OverwriteSynthWaves` opens the confirmation dialog; its old quoted
  comment had incorrectly classified the routine as data.

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
inside `InstrumentScreenInput`) and an indirect call's return point (the
`FarCall*Resume` labels) count separately, so the bytes are the measure.

The asm is where you'd expect it: `TimerHandler` and the sound tick, the
pitch/vibrato/envelope steppers, the tile loader, `ReadJoypad`, the OAM DMA
stub, GBDK library routines, and bank `$02`'s note/period/sequencer core
(`SoundNoteToPeriod`, `SoundWriteChannelFrequency`, …). Those are kept as asm, pret-style.

Not all of GBDK's library in the ROM is asm: libc's C routines were compiled
by GBDK's build, with the stock rules (`strlen` is `StringLength`, matched
from gbdk-2.95-2's own `libc/strlen.c`). Some asm reads stack arguments like C
does; the absolute-address signal catches `DrawByte`, `DrawString` and
`SetVramCursor`, and `DrawTileRun` / `WriteVramByteSafe` (which branch with
`jr` and poll `rSTAT`) are listed by hand. Three functions whose `.sym`
comment began with a quoted string had been carved as data, since a leading
`"` marks a data symbol: `DrawCrashScreen`, `CopyInstrument`,
`DrawVerifyPanel` (all SDCC code, now disassembled).

## Writing C that matches

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
  hl; inc (hl)` -- DrawHelpPage walks its string with `++s`. A
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
  `wBlockSinkPointer == FlushBlockToSram` compares the pointer global with an
  address (`AOP_IMMD`: `ld a,#lo; cp a,(hl)`), where a numeric literal
  would load the global first.
- **Undeclared calls lose their underscore**: a function called before any
  declaration in the unit (defined further down, no prototype) is emitted as
  `call Name`, not `call _Name`, which the link then cannot resolve
  (WaveScreenInput calling CopyWaveSelection); declare it first.
- **Too few arguments compile**: SDCC 2.2.1 accepts a call with fewer
  arguments than the prototype, so `printf("INSTRS....")` compiles
  against `void printf(char *fmt, UWORD v)` (CleanInstrumentData); a K&R
  `void printf();` would not promote a byte argument (`...` neither).
- **Spelling a scan from the top**: `wColumnPhrase[i - 1]` twice and then `i--`
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
  trampolines (all `x == global`, `x == stack`): `while (i != wWaveSelectionEnd)`
  in `PasteWaveSelection`, `CloneInstrumentTable`'s field test. `global == reg`
  is the folded form. (The `!=` double trampoline, `jr nz,L; jr M; L: jp X;
  M:`, is folded by lsdj.peep either way.)
- **Literal types** (`constVal`): hex and octal literals are *unsigned*,
  decimal ones signed; a value under 256 is a `short`, which is **one byte**
  in this port (`gbz80_port` sizes: char 1, short 1, int 2, long 4), so `y
  != 0x80` compares bytes and `pos + 1` is an 8-bit add; a value above
  32768 is a `long`. The ROM's 16-bit `!=` against a small constant
  (`InitializeWaveSprites`, `ReadWaveSample`'s loop bounds) is `(UWORD)y != 0x80`
  -- the zero-extended copy spills in the first of two identical loops and
  sits in `bc` in the second.
- **Named constants**: `lsdj.h` names the screens (`SCREEN_SONG` for
  `wCurrentScreen` and `wClipboardType`), the instrument types
  (`INSTRUMENT_KIT`, an instrument's byte 0; `INSTRUMENTS`, the 64, and
  `INSTRUMENT_SPEECH`, `$40`), an empty cell (`EMPTY`, `$FF`: no chain,
  phrase or instrument), the instrument screen's fields
  per type (`WAVE_FIELD_PLAY`, `KIT_FIELD_CLIP`: the manual's labels, checked
  against `ShowInstrumentFieldHelp` and `InstrumentFieldX`), an instrument's
  bytes (`INSTRUMENT_BYTE_TABLE`, `KIT_BYTE_OFFSET1`: the manual's Appendix C,
  which every field handler's reads and writes agree with), the sync modes
  (`SYNC_ANALOG_IN`; 2 and 4 are no mode), the synth screen's fields, a
  synth's parameter bytes and their values (`SYNTH_FIELD_Q_END`,
  `SYNTH_BYTE_PHASE_TYPE`, `SYNTH_PHASE_RESYNC2`: checked against
  `ShowSynthFieldHelp` and the field draws), the PROJECT screen's fields
  (`PROJECT_FIELD_CLEAN_SONG_DATA`), the play, tempo and font modes
  (`PLAY_SONG` -- the screen playback started from --, `TEMPO_6X`,
  `FONT_KIKO`), the phrase and table columns
  (`PHRASE_COLUMN_LEFT_NOTE`, a kit's first kit; `TABLE_COLUMN_VALUE2`), the cursor modes
  (`CURSOR_COPIED`: the clipboard holds something to paste), the channels (`CHANNEL_NOI`
  for `wSongColumn` and `hSoundChannel`), the commands (`COMMAND_Z`, in
  `CommandMnemonics`' order; `FileUpgrade.c` keeps numbers, which there mean
  an older format's commands) and the joypad bits (`J_A`, GBDK's
  names; held together, `(J_B | J_A)`, which SDCC folds to the one
  constant). A `#define` puts back the very tokens it names, so a name is
  only safe where it expands to the literal's spelling or to a value SDCC
  types the same: every site using these is verified by `make match`, the
  screens 11-14 once spelled in hex included. A background-map cell is
  `BG_MAP(x, y)` (`0x9800 + y * 32 + x`, folded to the address it was), the
  OAM shadow GBDK's sprite records (`OamShadow[3].prop = 0`, folded the same
  way), and the fixed addresses the code uses as numbers have names that are
  those numbers: `SRAM_START`/`SRAM_END`, `ROW_BUFFER_COLUMN(k)`,
  `DECOMPRESS_BUFFER`, a kit's `KIT_SAMPLE_BOUNDARIES`, `KIT_NAME`,
  `KIT_SAMPLES`. A song region in SRAM is `SRAM_X` as a number and `SramX`
  as its bytes (`((UBYTE *)SRAM_X)`, the literal pointer the code had), an
  address inside one `SRAM_X + n`; a site is named only where its bank is
  evident -- the bank argument of `ReadSongByte`/`WriteSongByte`, the
  function's `rRAMB = n`, or an address that starts a region in one bank
  alone -- because the same address is a different region in each bank.
  Bounds in comparisons stay numbers (an end of one region is the start of
  the next), and so do `FileUpgrade.c`'s, which are older formats'.
- **Sign extension follows the destination type**: `genCast` extends by
  the type cast *to*, so a `char` stored into an `int` element is
  `rla; sbc a,a` and into a `UWORD` is `ld b,#0` (`wSynthShapeBuffer` is
  `int[]`). A hex constant above `0x7FFF` is an *unsigned* `long`:
  `wSynthShapeBuffer[i] |= 0xFF00` widens the `int` to 32 bits with a zero
  upper word, as the ROM does. `*(UWORD *)0x4000 == 0x4060` loads the
  word into the reversed `b`/`c` pair and compares it byte by byte.
- **A register pair plus a constant, constant second**: `(UBYTE *)offset +
  0xDD00` is added in place (`ld a,c; add a,#0x00; ld c,a; ld a,b; adc
  a,#0xDD; ld b,a`, 25 such adds in the ROM) because the cast leaves `offset`
  in a register pair; `wOamBuffer + offset` adds the address first (`ld
  a,#0x00; add a,c`), and `offset + 0xDD00` on a stack operand goes through
  `ld hl,#0xDD00; add hl,de` (`SetOamByte`).
- **`array + n` is computed at run time**, even as a call argument (`ld
  de,#_a; ld hl,#n; add hl,de`), and `&a[n]` likewise; only the bare array
  name is an immediate. An address the ROM pushes as `ld hl,#addr` is a
  symbol of its own or a literal: `ReadSongRow` fills the 64-byte song window
  in 16-byte chunks at `wSongReadBuffer`, `(UBYTE *)0xC47D`, `0xC48D`, `0xC49D`.
- **A pointer compares with its element type's sign**: the `(int)` of
  `(int)(wNameEditBuffer + 1) > -1` is gone by the iCode (`iTemp > 0x80000000
  {_near * char}`), and genCmpGt/genCmpLt take the sign from both operands'
  specs -- a pointer's is its element's. Over a `UBYTE` buffer the test is
  unsigned (`xor a,a; ld a,#0xFF; sbc a,c; ...`, never true); over a `char`
  one it is signed, which gb_cmp16 turns into the ROM's add form
  (NameEditorInput's RAM assertion; `wNameEditBuffer` holds a name, and
  `KeyboardLink.c` already declared it `char`). `(int)gp` on a `UBYTE *` global
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
  `jp nz,L2` inverts the last compare (`KeyboardPollScancode`).
- **A re-typed pointer argument is copied first**: `ShowMessage` passes
  its `char *` on as `(UBYTE *)text`, which moves it through `bc` before
  the push; passed as-is it goes straight from `hl` (a `const` in
  `strcpy`'s prototype does not cause the copy).
- **Empty `case N: break;`** produce their own `jp end` trampolines in source
  order — the ROM has them, so spell the empty cases out.
- **16-bit shifts need the promotion**: `(unsigned int)instrument << 4` is the
  `rl c / rl b` loop; `instrument << 4` alone is shifted in 8 bits. Likewise
  `p + field` and `p + (unsigned int)field` are different code (hl/de add vs
  the byte extended into bc first); the ROM uses both.
- **Zero tests**: `x == 0` on a stack byte is `ld a,(hl); or a,a`, `!x` is
  `xor a; or a,(hl)`; on a pointer target `!*p` is direct and `*p == 0` goes
  through a trampoline.
- **Advancing a pointer parameter**: `source = source + 1;` increments it in place
  on the stack (`inc (hl); jr nz; inc hl; inc (hl)`), `source++` goes through
  a register temporary and costs a register (`CopySong16`).
- **SRAM addresses are integers**: LSDj's `ReadSongByte` / `WriteSongByte`
  take the address as a `UWORD` (`lsdj.h`). An integer argument is pushed
  as it is; a pointer is converted first, which costs a copy through a
  register pair when it sits on the stack (`IsBookmarked`'s reversed `b`/`c`
  copy). So address variables are mostly `UWORD`s: `UWORD p =
  (UWORD)(SramPhraseCommands + (unsigned int)row);` (the cast keeps the sum in
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
- **Addresses held in integers**: `extern UWORD wTableCellAPointer;` copied
  with `p = (UBYTE *)wTableCellAPointer;` goes byte by byte through `a`
  (`genCast`); a pointer-typed global copies as `ld a,(hl+); ld e,(hl)`
  (`genAssign`'s two-byte case). The ROM has both (`ReadTableCommand`).
- **A comparison kept as a value**: `ok = v <= 0x13; if (!ok) …` stores the
  0/1 (`ld a,#0; rla`) and its negation before testing it; the same test
  written inside the `if` compiles to a plain `cp` / `jp` (`DrawCommandLetter`).
- **A masked byte tested in a register: the literal first.** `if (x & 0x80)`
  hands the AND to its `if`: ralloc marks the result a condition (REG_CND)
  when the right operand is a literal and the result dies at the `if`, so
  genAnd emits `bit 7,a` for one bit, `and a,#n; jr nz` for several, and
  for some single-bit cases an internal error (gen.c 3215). `if (0x80 &
  x)` has no literal on the right, so the AND gets a register and the `if`
  tests it: `and a,#0x80; ld c,a; or a,c; jp z` (toBoolean's `xor a,a; or
  a,c`, folded by lsdj.peep rule 2) -- `DrawKitLoopMode`'s ATK test,
  `DrawKitInstrument`'s SPEED test (in `b`), `SoundPhraseHop`'s `0xF0 & v`; genAnd
  moves the literal to the right itself. `(x & 0x80) != 0` gives `or a,a`
  instead, and a cast or a named temporary is packed away.
- **Signed globals**: a signed compare that reads the global straight into
  `xor #0x80` (no copy) means the global itself was a plain `char` in that
  source file (`extern char wPhraseColumn;` for `CheckPhraseEditContext`); a
  `(char)` cast copies it through a register first. The clone commands
  (`CloneInstrumentAtCursor`, `CloneTableAtCursor`) pass `(char)wPhraseRow`: the
  ROM loads the row into a register before pushing it, as a conversion does,
  so their file saw `wPhraseRow` as a `char` too. The other face of it: a
  `char` global passed to a `UBYTE` parameter or incremented and tested is
  copied through `c` (`wGrooveRow` in `PasteGrooveSelection`, which also
  settles the parked `EraseGrooveCursor`).
- **A register leaked by narrowing**: when a 2-byte temporary dies at the
  iCode that defines a 1-byte one, `deassignLRs` hands its registers over in
  a loop that runs to the larger count, so the second register is copied into
  the byte's unused slot and never freed. `(int)(char)x & 0xBF` (SDCC narrows
  the AND back to a byte) leaves `b` taken for the rest of the function and
  every later temporary spills (InstrumentScreenInput's kit columns); `v =
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
  temporary; `FindNextSongRow`), `ColorField(…, c && hGameboyType)`
  (`EraseChainCursor`), `wButtons == 0x10` written twice
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
  prototypes (`WriteCurrentInstrumentByteC`, `FarCallArgumentWC`; SetVibratoShape).
- **A hoisted cast copy is the invisible register**: a sign-changing copy
  of a parameter inside a loop (`((char *)buf)[i] = y` needs `(char)y`) is
  loop-invariant, so `loopInvariants` hoists it into the preheader, where it
  takes a register for the whole loop although no instruction shows it;
  its spill location is the parameter itself, so when a later 2-byte
  temporary evicts it (the address as singles `[b c]`) every use silently
  reads the parameter's slot. CSE, blind to casts in this snapshot, then
  substitutes that signed copy for `y` in `*p < y`, which is why the ROM's
  last compare is the signed `xor a,#0x80` form (PlotEnvelopeCurve).
- **A symbol spilt later held its register earlier**: `isspilt` is per
  symbol, so a local the allocator evicts half-way through a function is
  generated as a stack variable *everywhere* -- yet in the allocator's
  bookkeeping it occupied its register from its definition to the eviction,
  and that invisible occupancy decided where the earlier temporaries went.
  DeduplicateTables' and DeduplicatePhrases' outer call result sits in `b` because
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
  ROM's and the bare `DrawString("OK")` matches (`DeduplicateChains`,
  `DeduplicateTables`).
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
  `and a,#0x0F` (LoadSynthSignalWave).
- **A local the ROM keeps in its slot is `volatile`**: when every access to
  a variable is a stack access -- its first store too, made while its value
  still sits in a register for the next use -- a plain local is the wrong
  shape. CSE folds `r = k2; r |= !(kit == k2);` into one `r = k2 | ...`, r
  then starts where the registers are full and something else (the loop
  instrument in c) spills. `volatile UBYTE r` makes r a stack variable, so
  `r = k2` stays a store and k2 stays in b for the compare
  (NextPhraseInstrument/D).
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
  later loops (FindDuplicatePhrase/Chain/Table).
- **An array's address after the array was indexed is `&arr[0]`**:
  `wPointerChainRow = wChannelChainRow` after `wChannelChainRow[3] = 0xFF` reuses the
  `&wChannelChainRow` temporary of the element store (CSE) and stores the
  address as two literal bytes (`ld (hl),#<arr; inc hl; ld (hl),#>arr`); the
  ROM loads it into de (`ld de,#arr; ld hl,#ptr; ld (hl),e; inc hl; ld
  (hl),d`, an address-of folded into the store), which `&wChannelChainRow[0]`
  gives -- its `+ 0` is a fresh temporary (ResetWorkState's nine pointers).
- **A cast copy of a global is an occupant that costs nothing**: a
  `char` parameter fed a `UBYTE` global makes a same-size cast copy whose
  spill location is the global itself (`geniCodeCast`), and arguments are
  evaluated right to left, so the copy for the *last* argument is born
  before an earlier argument's `destination++` pointer copy. It takes c, the
  pointer copy's second register evicts it into the global (the only
  candidate with a location once the counter is spilt) and the pair comes
  out `[b c]` with the push still reading the global -- the ROM's
  CopyVramTile, where a `UBYTE` value argument gives the pair `[c b]`
  and `push bc` (`WriteVramByteSafeC`).
- **A global declared `extern` inside the function is a register
  variable spilt into the global**: `allocVariables` gives a block-scope
  `extern` the global's storage but leaves its level, so the compiler makes
  it a register-equivalent local (`reqv`) whose spill location is the global
  -- no SSA temporary on assignment, its reads replaced by the defining
  temporary (a file-scope global is in near space, which `findCheaperOp`
  never replaces), and the value only reaches memory when the allocator
  spills it. PinchOrWarpSynthWave's doubled index: `wSynthPhaseIndex = pos <<
  1` folds the shift into the temporary, both `wSynthBufferA + wSynthPhaseIndex`
  and `wSynthBufferB + wSynthPhaseIndex` become one 2-use pointer each, and the
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
  prototyped the callee as returning `char` (`ReadInstrumentByteS`, aliased to
  ReadInstrumentByte, in FileUpgradeV11). A `char` local instead changes every
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
  (`MoveWordCursorDown`).
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
- **Banked calls** (`FarCall(bank, fn)`, `FarCallArgument(bank, fn, arg)`): passing
  the function loads it with `ld bc,#fn`; some call sites instead load it
  byte-wise (`ld c,#<fn; ld b,#>fn`), which is a cast through an integer
  (`FAR_FN(fn)` in `lsdj.h`). FarCallArgument takes a function pointer as well:
  `InitializeAndLoadSong` passes `LoadProjectSettings` plainly (`ld bc,#fn`), the
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
- **Per-type tables are 2-D arrays**: `InstrumentFieldX[type][field]` with
  `extern UBYTE InstrumentFieldX[][18]` gives the ROM's `__muluchar` (`type*18`,
  computed once and reused across tables) plus a row pointer; LSDj's tables
  are 67 bytes (the NOISE row has 13 fields), so they are declared, not
  defined, in the C. SDCC's runtime helpers (`__muluchar`, `__divuint`, …)
  resolve to our names for those GBDK routines (`MulU8`, `DivU16`, …).
  The name keyboard is one too (`char AlphanumericCharacters[][11]`, NameEditorInput's
  `AlphanumericCharacters[row][col]`): over a flat `char[]` the row-then-column read
  cannot be spelled -- `(a + n)[i]` and `*(a + n + i)` do not compile
  ("Array or pointer required for '[]' operation", "Pointer required"),
  `(&a[n])[i]` doubles `i` -- except through a cast, `((char *)a + n)[i]`;
  `a[1]` as an argument is the immediate `a + 11`. The clipboard is one
  too, `UBYTE wClipboardBuffer[][42]` (6 columns of 42 cells): the link
  transfer's `&wClipboardBuffer[x][y]` written inside the inner loop is
  hoisted to the ROM's `MulU8` before it, and gives x the inner-loop use
  counts that make y, not x, the spill victim (SendLinkClipboard,
  ReceiveLinkClipboard).
- **Plain `char` locals are signed**, and LSDj's source uses them: reading a
  byte into a `char` and testing or masking it compiles to a copy first
  (`ld b,c; ld a,b; bit 5,a` — the implicit conversion), which `UBYTE` does
  not produce. `DrawInstrumentTableField`'s `char v` is the first confirmed case;
  the same copy shows in several parked functions.
- **Constants above 127 are `int`s**: `v & 0xF0` feeding another operation
  is done in 16 bits (`and a,$f0; ld b,$00`), which is why
  `FileUpgradeRemapBits` builds its result in a 16-bit temporary. Assigned
  straight back to a byte (`t = v & 0xF0`) it stays 8-bit.
- **Calling through a function pointer** needs a K&R pointer type:
  `void (*function)()`. SDCC 2.2.1 counts the `void` of `void (*function)(void)` as a
  parameter and rejects the call ("to few parameters"). Callers passing
  `void f(void)` functions compile the same either way.
- **Loop invariants are hoisted** (stock SDCC; LSDj built without
  `--noinvariant`): `FileForEachInstrumentType` copies its function pointer to a
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
  of the body), and `DrawFileTextField`/`DrawProjectTextField` write their bounds
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
  assignment such as `wDeduplicatePhraseUsed = IsPhraseUsedFar(j)` then keeps
  its call-result temporary instead of folding into the global, and that
  temporary's eviction of `j` is what puts `j` on the stack in
  `DeduplicatePhrases` (next bullet).
- **`global = call()` keeps the call's temporary -- LSDj's compiler
  differs from stock here.** Stock 2.2.1's `geniCodeAssign` copies every
  value assigned to a non-volatile global through a fresh temporary
  ("SSA"); the allocator's `packRegsForAssign` folds the copy away again
  unless the live range was stretched (above). LSDj's compiler made no such
  copy for a function call's result: the result temporary is assigned to
  the global directly, with the global as its spill location. Every other
  function compiles identically either way; `DeduplicatePhrases`, `DeduplicateChains`
  and `DeduplicateTables` only match without the copy (with it, two temporaries
  per call survive the stretching, each wanting a stack slot). The native
  build carries this as a source patch (`tools/toolchain/build_sdcc221.sh`,
  "LSDj's compiler"); the Win32 `sdcc.exe` is stock and so is 2 bytes off
  on those three. Dropping the copy for *every* assignment is wrong: ten
  functions that read the global back (`SyncLoadSetting`...) then keep the
  value in a register where the ROM reloads it.
- **A value stored to a global and passed in the same call is a comma
  expression in the argument**: `ColorField(x, row + 2, (wDrawTextScratch =
  PhraseCellWidth[col], wDrawTextScratch), hGameboyType ? 3 : 2)`
  (HighlightPhraseCell). The arguments are evaluated right to left, so the
  `?:` comes first and its temporary -- no spill location -- keeps its
  register when the width's `column` reload evicts `x` instead; the store
  then happens in argument position, and the comma's second operand reads
  the global back for the push (a bare `f(g = e)` pushes the value's
  temporary, a separate store statement evaluates before the `?:`).
- **Inline assembly** is SDCC 2.2.1's `_asm ... _endasm;` (the `di`/`ei`
  around SRAM access); the MBC registers are variables (`rRAMG = 0x0A`,
  `rRAMB = 3`), like `rROMB0`.
- **Missing `return`s are real**: `EnterPhraseLeft/Right` are declared to
  return a byte but only `break` out of their loop; callers read whatever
  the last call left in `e`. SDCC 2.2.1 compiles that without complaint.
- **A far-call target's own bank declares it for real**: `lsdj.h` declares bank 2's
  far-call targets as `void f(void)`, the type the banked-call routines take;
  bank 2's units define `SOUND_BANK` before the include and skip those
  declarations (`SoundGetChainPhrase` returns a byte from two arguments).
- **A prototype may be declared only once per unit**: SDCC 2.2.1 reports a
  repeated identical prototype as `Duplicate symbol` (an error). A function
  declared in `lsdj.h` and defined in the unit is fine.
- With 2.95-3 no `nonbanked` is needed; with plain 2.95 every definition and
  prototype needs it.

## Parked functions

None. Every C function in the ROM matches. Six of them match only with a
register-allocation directive (`lsdj-alloc(...)`, [`toolchain.md`](toolchain.md) → *Allocator
directives*): the last
residue of LSDj's compiler that no source spelling and no modelled rule
reproduces.

