# The period toolchain

The compiler LSDj was built with, rebuilt and checked: it reproduces LSDj's
code byte for byte, which is what the matching C decompilation rests on (see
[`AGENTS.md`](../AGENTS.md) → *Strategy*).

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95-3** (19 Aug 2000) → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker, small memory model — **plus an accumulator-tracking optimisation stock SDCC lacks** (the "modified GBDK"), modelled here as an accumulator-tracking pass plus 86 peephole rules (below), **and one iCode difference**: no SSA copy when a call's result is assigned to a global (patched into the native build, see *Writing C*); six functions need a per-function allocator directive on top (`lsdj-alloc`, below) |
| Can we run it today? | **Yes, natively.** SDCC 2.2.1 + `sdcpp`/`as-gbz80`/`link-gbz80` build from the GBDK 2.95-2 sources (`tools/toolchain/build_sdcc221.sh`, ~15 s) and emit **byte-identical** asm, objects, listings and images to GBDK 2.95-3's Win32 binaries — which still run under Wine (`fetch_gbdk295.sh`), but crash on large units (below) |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock output does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95-3 lacks |
| Matched real LSDj functions? | **Yes — 678**, in 13 banks (the list: [`c/matched.json`](../c/matched.json)): a message renderer, a 15-case `switch`, a sparse `switch`, the SRAM accessors, and the instrument screen's field accessors and draw helpers. `make match` re-verifies them all (cached: only changed sources reach the compiler), and **their bytes in the ROM build come from the compiled C** (below) |
| Is all of LSDj C? | **No.** 226 functions (15.5 KB, 10% of the code with the GBDK library) are hand-written assembly — the interrupt handlers, sound tick, envelope/pitch and tile-loader code in bank 0, and the sequencer core in bank `$02` (`tools/match/code_kind.py`). Those stay asm; the C is ~141.7 KB, all of it matched (678 functions, six of them through an `lsdj-alloc` directive) |
| Is the matching C done? | **Yes.** Every C function matches (678, 141,700 bytes, 86 peephole rules, six of the functions through an `lsdj-alloc` directive); the C feeds the ROM build with `make compare` as the gate, and the hand-written asm is mapped and excluded |

The C tree itself -- every module, what it holds and the C constructs it
exercises, what is C and what is asm, how C that matches is written, the
parked functions -- is in [`matching_c.md`](matching_c.md).

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
| Constant reuse | `ld a,N` / `push af; inc sp` / `ld a,N` → reload dropped, also across `ldh (n),a` + `ld hl,nn; push hl`; `xor a` / `ld hl,sp+N` / `ld a,#0` → reload dropped; an sfr pushed twice, `ldh a,(X)` / `push af; inc sp` / `ldh a,(X)` → reload dropped (SoundZPhrase); a literal stored through hl (`ld (hl),#n`, a global `g = n`) leaves `a` standing for n and nothing else: the next `ld a,N` stays unless N is that literal (ResetWorkState's `wWordClipboardCell0[0] = 1` after six `g = 0` stores reloads; SoundTogglePlayback's `hSoundChannel = 0` after `wSongPlaying = 0` reuses the 0) | 0 | 10, 5, 2, 1; 1 kept, 1 reused |
| Compare chains | a sparse `switch` re-reads its global before every `cp` in stock output; LSDj loads it once, and an int-typed case widens the copy in `a` (`ld c,a` for `ld hl,#G; ld c,(hl)`, InstrumentScreenInput's kit chains) (one rule per chain position, up to twelve, so the pattern always carries the variable; `switch_chain` in peep.py drops what the rules leave of a longer chain, and after a case that tests a CSE'd boolean (`xor a,a; or a,c; jp nz`) reloads only `a`, as the ROM's three such chains do, WaveScreenInput's twenty `wButtons` cases). SDCC's `==` trampoline `jp cc,T; jr F; T: jp X; F:` → `jp cc,X`, every byte's `jp cc,T` of a multi-byte compare included (the only survivor in the ROM is inside GBDK's stock-compiled `gprintf`) | 0, 1 (library) | 72 |
| Kept across a test | `ld a,r` / `bit n,a` / `jp cc` / `ld a,r` → reload dropped; `ld hl,#x` / `ld a,(hl)` / `cp a,#n` / `jp z,F` / *(dead label)* / `ld hl,#x` → reload dropped (a `!=` test; stock SDCC's Rule 5 leaves the trampoline's label behind, which makes its codegen forget `hl`) unless a constant is stored there next (`ld (hl),#n`: ProcessInput's `wLiveStartColumn = 0xFF`, the ROM's one store after such a test; `increment`/`dec (hl)` keep hl), also after `or a,a` (`x != 0`); `xor a` / `ld a,#0` → `xor a` | 0, 1 (a real jump target in between), 0; 0 for `or a,a` | 68, 7, 6; 1 for `or a,a` (ProjectScreenInput; 2 more in hand-written asm) |
| Register tracking (`track_a`, below) | a reload of what `a` already holds: `ld a,r` / `cp` / `jp cc` / *(dead label)* / `ld a,r` (an `\|\|` chain) → reload dropped; a stored stack local reread: `ld hl,sp+N` / `ld (hl),r` / [`add sp,M`] / `ld a,(hl)` → `ld a,r`; `ld (hl),a` / `ld de,#T` / `ld l,(hl)` → `ld l,a` (indexing a table by the value just stored) | 0, 0, 0 | 35, 25 + 141, 32 |
| More dead stores | `ld r,#n` / `ld r,X` → `ld r,X`; `ld r,a` / … / `ld r,(hl)` → `ld r,(hl)`; `ld (hl),e` / … / `ld (hl),a` → `ld (hl),a` (nothing touching hl between); a discarded sign extension `rla` / `sbc a,a` / `ld r,a` / `ld a,X` / `and a,#n` → store dropped; the `== 0` test of a fresh `&`: `and a,#n` / `ld r,a` / `or a,a` / `jp nz` → `and a,#n` / `jp nz`; after *ALU* `a,r` / `ld (hl),a` the `ld a,(hl)` reload is dropped too; `ld a,#n` / `ldh a,X` → `ldh a,X` (genCmp's `x > n` against an sfr, Command_E_NoteOn's switch bound) | 0, 0, 0 (1 `jp z` / `if (t)` site keeps it), 0, 0 (no two loads of `a` in a row anywhere) | —, 3, 12, 69, 1 |
| 16-bit compare with a constant (`gb_cmp16`, below) | `xor a,a` / `ld a,lo` / `sbc a,#lo` / `ld a,hi` / `sbc a,#hi` / `jp cc` → `ld hl,#-lit` / `add hl,de` (or `bc` when the value already sits there) / `jp` on the inverted carry | 0 (its 5 `sbc` chains are 32-bit) | 28 |
| `ld a,a` | the MOVA SDCC leaves after reading an sfr for a compare → removed | 0 (the one `ld a,a` in the ROM is palette data) | — |
| Dead `a` load before a stack read | `ld a,X` / `ld hl,sp+N` / `ld a,(hl)` → first load dropped: `gencjneshort` fetches a stack right operand into `a` over an `a`-packed left (`ld a,(de)` / `ld a,c`, then the `cp a,a` miscompile in FreeUnusedInstruments and CheckRomChecksum) | 0 (any `ld a,X`, after `ld hl,sp+N` or `ld hl,nn`) | — (neither `cp a,a` site has the load) |
| `if (!(x & n))` trampoline | `and a,#n` / `jr nz,L` / `jp M` / `L: jp X` / `M:` (genAnd's jmpTrueOrFalse for a jump on true) → `and a,#n` / `jp nz,X` | 0 (the one `jr nz,+3; jp; jp` left in the ROM follows a `cp (hl)`: CloneInstrumentTable's trampoline) | 12 `and a,n; jp nz` |

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
`DrawTileHexCell` added 10, `ReadInstrumentByte` 2, `DrawSyncField` 11 (ten of
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
(`WriteWaveSample`'s `position + 1`, whose register the next temp reloads) in the
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
(CompressSong's `wCompressDestination < 0x7FFF` before `wCompressDestination + 1`), also when the
operand was a global byte promoted into a pair (`ld c,(hl); ld b,#0`) and
the code goes on to touch that byte (WordScreenInput's
`(UWORD)wSpeechRow < 13` before `wSpeechRow++`).
After `ld (hl),e; ld a,e` the byte at hl is `a` too, so a table index
loaded from it is `ld l,a` (CountFileBlocks' `ld l,a; ld h,#0`).
A byte read through `de`/`bc` is remembered as "the byte at (de)": its
copies are forgotten once the pair is rewritten, so `ld a,(de); ld c,a;
ld e,(hl); inc hl; ld d,(hl); ld a,c; ld (de),a` keeps the reload
(Resync2SynthWave, FileFormatBuf, 2/2) while `ld a,(de); ld b,a; cp a,#n;
jp z; ld a,b` drops it (UnhighlightPhraseCell).
`track_a` also copies a byte still held in a register
instead of reloading it (`ld (hl),e; add sp,3; ld c,e`, 23 ROM sites) and
drops the reload into the same register (`ld (hl),e; ld e,(hl)` never
occurs: FileCalculateCompressedSize's result).

`inline_asm_hl` reloads hl after an `_asm` block: stock genInline leaves
its pair cache alone, so a stack byte addressed right after `_asm di
_endasm` is reached with `inc hl`, where LSDj's compiler emitted
`ld hl,sp+N` (RemapSequenceChains' `i = 0` after the di). The walk back to
the last `ld hl,sp+N` passes the fall-through of a conditional jump
(FreeUnusedInstruments' `i < 0x40` loop test right before its `di`).

One hl reload is rewritten: a 16-bit local tested for zero (`ld a,(hl+);
or a,(hl); jp nz`) and then assigned has stock SDCC reload `ld hl,sp+N`
for the store; the ROM steps back with `dec hl` (GetInstrumentEnvelopeBytes'
`if (total == 0) total = 1`). The same bytes from genIfx (`if (w)`, no
compare) go the other way, and `ifx_word_hl` handles them before the rules
run: stock SDCC still knows hl after the test and steps back, while LSDj's
compiler reloaded it (ShowInstrumentFieldHelp's `if (s) ShowMessage(s)`, the
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
after one, none rereads): `SoundStartScreenPlayback`'s `wChannelActive[wSongColumn] =
1; wPlayStartChannel = wSongColumn;` is `ld c,(hl)`, the indexed `ld (de),a`, then
`ld a,c` for the second read while hl still points at `wSongColumn`; the
`hSoundChannel = wSongColumn` after it, with hl moved on, reloads. A conditional
jump ends the equality when hl points at a global: `Command_E_Pu1`'s
`wEnvelopeSpeedPu1 = v; if (v) wEnvelopeCounterPu1 = wEnvelopeSpeedPu1;` rereads it (`ld
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
(`SetColumnInstrument`'s `wInstrumentIsKit = instrument != 0x40; if (wInstrumentIsKit)`).

`track_e` does the same for `e`, which carries call results and return
values: after `ld c,e` (a result kept in `c`) a later `ld e,c` (returning
it) is dropped while neither register changed and no jump target
intervened. Of the 740 `ld r,e` copies in the ROM's compiled code, no
redundant `ld e,r` follows one (`FindCursorInstrument` returns its value so).
`track_d` is the same knowledge for `d`: a pointer kept in `bc` and fetched
twice (`ld e,b; ld d,c` for a read, again for the store that follows) loses
both halves of the second fetch in the ROM (`ProcessLinkQueue` clearing
`wKeyboardChannelState[1]`).
`track_const` does it for constants: `ld r,#n` (r = `b`..`e`) is dropped
while r already holds n, so the high byte of a widened index stays 0 for
the next one (`ReadWordCell`). None of the 1571 `ld r,#n` in the ROM's
compiled code reloads a constant its register still holds. `track_de` is
the 16-bit case: `ld de,#X` is dropped while `de` still holds X. SDCC's
`fetchLitPair` remembers the literal last loaded into a pair, but the `de`
half is compiled out in 2.2.1 (`IS_GB && pairId == PAIR_DE && 0`), so a
global array's base is reloaded between `a[0] = x` and the `ld hl,#1; add
hl,de` of `a[1] = y`; LSDj's compiler kept the first load (`LoadFile`'s
`wFileSramProperties` stores), and the ROM has no such pair within a basic
block while `d`/`e` are untouched.

`track_hram` (run before `track_a`) skips an `ldh` reload of an HRAM byte
a register still holds. Stock SDCC 2.2.1 rereads an `sfr` at every use: its
CSE never substitutes a register for a direct-space global
(`isGlobalInNearSpace`, an 8051 optimisation applied to every port).
LSDj's compiler skipped loading the byte into a register that already held
it -- `ldh a,(X); ld r,a` goes when r holds X, within a block or across a
jump (`HandleInputCombos`' two `hSoundChannel` indexes in one block,
`SyncTickLsdj`'s loop body, `SoundUpdateChannel`'s D across `if (d)`) -- but
never copied it over from another register: a reload into b while c holds
the byte stays (the `+=` target in `SoundUpdateChannel` and
`HandleInputCombos`). `a` itself serves a reread in straight-line code (the
sign extension of a widened sfr: stock genCast reads `hCommandValueS` again for
the `rla`, `Command_F_Wav`; no compiled code in the ROM rereads a byte `a`
holds before a jump) and across an ordering test (`SyncTickLsdj`'s
`hSoundChannel < 4`: `cp #4; jp nc`); after an equality test the byte is reread
(`cp; jp nz; ldh a,(X)`: `SoundUpdateChannel`, `Command_P_Step`,
`SoundRetrigNote`). The pair goes when `a` dies a few
instructions on, past ones that leave it alone (`ld de,#x; ld l,c; ld h,#0;
add hl,de; ld a,l`: the address of an index, `SoundStartSongPlayback`). An HRAM
store, a call or a jump target ends the reuse. A store through a pointer
loaded from memory (`ld e,(hl); inc hl; ld d,(hl); ld (de),a`) ends it only
for a byte read before a conditional jump (the `+=` in `SoundUpdateChannel`'s
D), not for one read in the same block (`SoundStartSongPlayback`'s `*active = 1`
between two `hSoundChannel` indexes); a store to an array element (`#arr + c` in
de) ends neither. `track_a` treats the
byte at hl the same way: after an equality test `a` no longer stands for it
(`SoundPhraseHop`'s `*wPointerPhraseRow = wSequenceNoteLatch; if (wSequenceNoteLatch)
AdvanceCurrentChannelCursors(wSequenceNoteLatch);` reloads it, 2 sites, none reusing it;
PhraseScreenInput's `if (k)` on a stack byte, 7 sites), but after a bit test
of a stack byte it does (InstrumentScreenInput's kit flag toggles, 4 sites: `ld
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
there -- also one byte past it, stepping back first (DrawNoteName's
`(UWORD)t < 0x78` after a two-byte store), or one byte below it, stepping
up (SpeechInstrumentInput's `p >= 0xBDD0` right after the `cell < 0x10` loop test)
-- the following hl-relative use reloaded with `ld hl,sp+N` as the add
clobbered hl.

**Signed** compares go the same way on the operand with its sign bit
flipped. Stock SDCC flips both sign bits into `e`/`d` and subtracts (`ld
a,hi; xor a,#0x80; ld e,a; ld d,#K.hi^0x80; ld a,lo; sbc a,#K.lo; ld a,e;
sbc a,d`, the constant first for `x > K`); LSDj's compiler built `d:e` as
`x ^ 0x8000` and added `-(K ^ 0x8000)`: `ld e,lo; ld a,hi; xor a,#0x80; ld
d,a; ld hl,#0x7FF6; add hl,de; jp c` is DrawCrashScreen's `i < 10`, and
with the constant first the add compares with `K + 1` and keeps its jump
(`Command_F_Wav`'s `step >= 16` / `step < -16`: `#0x7FF0`, `#0x8010`). Against
0 stock SDCC tests bit 7 of the high byte (after genCmpLt's or genCmpGt's
comment); LSDj's code keeps that (RenderSynthWaveA's `(WORD)samp < 0`), but
GBDK's library build used the add form there too (`abs`'s `number < 0` and
printn's `(int)u < 0`: `ld hl,#0x8000`), so `gb_cmp16(zero=True)` runs for
`@stock-rules` units. After a rewrite the stack byte stock SDCC left hl on
is reloaded at the next use: a negation's after its carry clear and before
the zero it subtracts from (`xor a,a; ld hl,sp+4; ld a,$00; sbc a,[hl]`),
a shift's after its `or a,a`. The ROM has 6 signed adds (abs, printn,
DrawCrashScreen, Command_F_Wav twice, NameEditorInput).

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
  without touching the code elsewhere), `SPILLAT=iTemp0:94` evicts a
  symbol from its register when the walk reaches that sequence number
  (what a phantom allocation there would do), and the hypothesis switches
  listed in the script (`PAIR_SPILL_FIRST`, `PAIR_SPILL_FIRST_LOADS`,
  `PAIR_NORMALIZE`, `HOLD_CALLRES`, `HOLD_PAIR_HI`,
  `HANDOVER_SPILT`, `DEASSIGN_STRICT`, `CND_ALLOC`, `HANDOVER_NONE`,
  `ALLOC_BFIRST`, `NO_POSITION`, `IFX_LATE`); `LSDJ_NATIVE_BIN=toolchain/
  sdcc-2.2.1-debug/bin BUSYC=82-82 match_function.py ...` compiles an
  attempt under them.
- `tools/match/perturb.py FILE.c FUNC` -- searches those knobs (every
  symbol spilt at its definition, c or b held over every short range of
  sequence numbers, then pairs of the best) and lists the perturbations
  that bring the attempt closer to the ROM, best first. A 0 names the
  allocator *state* the ROM's compile had where ours differs -- the fact
  to explain with C -- or with the compiler: `DeduplicatePhrases` with plain
  counters needed b held from `j = i + 1` through the inner loop's test,
  which is what the call-result temporary of LSDj's compiler does (*Writing
  C*: `global = call()`); ResyncSynthWave needs c held at one
  instruction (the doubled index's definition), WriteSynthWaveColumn its
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

## Allocator directives (`lsdj-alloc`)

Six functions allocate registers as if one register were held by an
occupant no symbol accounts for, or one symbol were unspillable at one
instruction -- windows that the debug build's experiment knobs pin exactly
(each function is byte-identical with the window applied and with nothing
else; see "The phantom register" below for the search). Rather than leave
them as asm, their C carries the window as a comment the matcher passes to
the compiler:

```c
/* lsdj-alloc(WordScreenInput): BUSYC=1-93 */
UBYTE WordScreenInput(void)
```

`tools/match/match_function.py` collects every `lsdj-alloc(Func): ...`
comment of a unit into `LSDJ_ALLOC="Func:KIND=spec ..."` for the compile,
and the native compiler (`tools/toolchain/build_sdcc221.sh`, "per-function
allocator directives" in `z80/ralloc.c`) applies each directive to the named
function only, at the point in `serialRegAssign` where the stock allocator
takes registers from the live ranges ending at an instruction:

| Directive | Effect |
|-----------|--------|
| `BUSYC=lo-hi`, `BUSYB=lo-hi` | register c / b is held for iCode sequence numbers lo..hi (the numbers of `--dumprassgn` / `trace_alloc.py`) |
| `SPILLSYM=sym` | the symbol is spilt at its definition |
| `SPILLAT=sym:seq` | the symbol is evicted from its registers when the walk reaches seq |
| `NOSPILLAT=sym:seq` | the symbol is kept out of the spill candidates at seq |

| Function | Directive | What it reproduces |
|----------|-----------|--------------------|
| `WriteSynthWaveColumn` ($1D:$5F29) | `BUSYC=1-17` | c held from the first instruction to the loop body's last byte temporary: i takes b and is evicted by the body, the pointer pair comes out as bc |
| `ResyncSynthWave` ($1D:$5BBD) | `BUSYC=82-82` | c held when `next2 = i << 1` is allocated, so the BufB word pushed to DivU16 comes out as bc |
| `WordScreenInput` ($06:$431D) | `BUSYC=1-93` | c held from the first instruction through the cut's second ReadWordCell result: the first two results take b, y and v never hold a register |
| `DrawInstrumentScreen` ($03:$4DE7) | `BUSYB=47-52` | b held from the `type * 18` multiply to the first SetVramCursor's column read: type evicted by the multiply's result, the result and the row value spilt in turn |
| `FileUpgradeClearRange` ($1C:$65B9) | `BUSYB=26-62` | b held from the second loop's pointer cast to the end: v and every temporary of the inner loop's three statements in memory |
| `CountFileBlocks` ($1C:$48D4) | `NOSPILLAT=iTemp0:27 SPILLAT=iTemp0:28` | at the count's read nothing is spillable (p keeps bc and the read spills itself into the dead long's slot); p is evicted at the `+ 1` that follows |

The directives are an admission, not a model: the windows begin at the
function start, at a pair's or a call result's death or at no visible event,
and end before a two-register request or a call (details below). What they
stand for in LSDj's compiler is open; candidates that fit the shape --
heap-layout-dependent state in a 2000-era build (the GC-free `_NO_GC`
build's uninitialised bit vectors, which our own native build had until
`bitVectResize` was made to zero-fill) -- would make them unreproducible by
rule. The matcher treats a directive like any other source text: it is part
of the unit's cache key, applies to one function, and leaves the other 672
untouched (`make match` verifies all 678).

## What's still unknown

1. **Build options.** Default loop optimisations: invariant motion is on
   (`FileForEachInstrumentType`), and the code that once looked like it was
   compiled without it (`CloneSynth`, the three frame drawers) is stock
   SDCC 2.2.1's one-level hoisting from stale dataflow (see
   [`matching_c.md`](matching_c.md) → *Writing C that matches*).
2. **The complete compiler model.** An accumulator-tracking pass plus 86
   rules so far; most new functions need nothing new, but each new idiom can
   surface a rule. One function so far (`SetVibratoShape`, `$03:$4B5E`)
   points at a register-allocation difference no rule can express: stock
   SDCC keeps the result of an `&` in `a` for the `|` that follows and then
   clobbers it (wrong code), LSDj's compiler keeps it in `c`. Not a fixed
   bug: `AllocateInstrumentTable` (`$03:$436D`) ships exactly that miscompilation
   (`and a,$d0; ld a,c; or a,a`), so it is a source-shape question. The reload-after-ALU and
   dead-store families are probably one codegen mechanism (tracking what `a`
   holds) that we model textually; the overlapping-pointer exception rests on
   one site.
   Spill choices were the other open front: in four loops of bank `$07`
   (`IsBookmarked`, `RemoveBookmark`, `FindNextSongRow`/`FindPreviousSongRow`)
   LSDj's compiler spills the loop counter where our first C spilled the
   other value. For the two row finders the source repeats the expression
   instead of naming it ([`matching_c.md`](matching_c.md), *spill victims*); the bookmark pair is
   still open. 2.2.1 spills the least `used` live range, a use counting
   `2^depth + 1`, and a use in a block that leaves the loop by `return` counts
   1, which is what decides these (`--dumpregassign` shows it). GBDK 2.96a's
   SDCC 2.3.1 is not LSDj's compiler either: it has the 16-bit compare
   (`gb_cmp16`) but saves `bc` around each call instead of in the prologue,
   which the ROM never does.
   **The phantom register.** Six functions (matched through `lsdj-alloc`
   directives, above) share one residue: the ROM allocates as if a
   register were held by an occupant no symbol accounts for, and the debug
   build's BUSYC/BUSYB knobs pin each window (the function is
   byte-identical with the register held over it, and nothing else makes
   it so): WriteSynthWaveColumn c from the first
   instruction to the loop body's last byte temporary (seq 3..17, released
   before the pointer pair at 21); WordScreenInput c from the first
   instruction through the cut's second ReadWordCell result (1..93,
   released before the third read's call at 99); ResyncSynthWave c from
   the WrapSynthIndex32 result's death (79) to the BufB word's pair request
   (90); DrawInstrumentScreen b from the far-call pair's death (11) or any
   later point up to the `type * 18` multiply (47) through the column read
   (52), released before the pair at 56; FileUpgradeClearRange b from the
   second loop's cast pair's death (26) to the function's end;
   CountFileBlocks both registers unspillable at the `*ptr` read. The
   windows end right before a two-register request or a call, and begin at
   the function start, at a pair's or a call result's death, or at no
   visible event. Not the hand-over leak (WriteSynthWaveColumn and
   WordScreenInput have no register pairs before their windows), not a
   carry-over from the previous function in the file (`freeAllRegs` runs
   after every function and the predecessors leak nothing), not a key
   collision, not dead-code-block liveness (no `noPath` blocks). Rules
   tried in the debug build and rejected on the matched set: spill before
   pairing (`PAIR_SPILL_FIRST`: fits WriteSynthWaveColumn and ResyncSynthWave
   exactly but gives [c b] where 11 of 80 matched functions keep stock's
   [b c], e.g. PreviousInstrumentPitchMode; restricted to pairs defined by a load
   from memory, `PAIR_SPILL_FIRST_LOADS`, still 3 of 80: SaveFile,
   SpeechInstrumentInput, TableScreenUpdate's pointer read stays [b c]; the
   codegen-side swap `PAIR_NORMALIZE` leaves the hand-over copies stale),
   holding a call result's register or a
   dying pair's second register until the next pair request
   (`HOLD_CALLRES`, `HOLD_PAIR_HI`: 11-12 of 60 broken, no parked function
   improved), handing a dying operand's registers to a spilt result
   (`HANDOVER_SPILT`: 41 of 60 broken), plus the six earlier switches.
3. **Flags.** The default `lcc` compile is `sdcc -mgbz80 --c1mode` after a
   separate `sdcpp` pass (`-DGB=1 -DGAMEBOY=1 -DINT_16_BITS …`); nothing so far
   needed more.
4. **LSDj's source files.** Each module compiles as one unit and each of
   its functions is linked at its own ROM address, so the build has LSDj's
   layout without LSDj's link: the function order within a bank, and the
   string literals and `const` tables SDCC emits after a function
   (`DrawTileHexCell`'s table) at their addresses. The original files are
   not recovered and nothing needs them: the modules are named for what they
   hold, and 18 of the 65 are one contiguous run of ROM; the rest interleave
   with asm, data and other modules. Only building a bank from C wholesale
   with GBDK's linker would need the original cut, and the asm and the data
   in its assembler's syntax besides.

## Next steps

- [x] Link step in the harness; `DrawMessageBar` matched.
- [x] Validate on harder functions: a `switch` (`DispatchScreenDraw`), 16-bit
      math + locals + asserts (`ReadInstrumentByte`), the register-reload exception
      (`DrawTileHexCell`) — all matched; rule set 6 → 28.
- [x] `make match` regression suite; `code_kind.py` C/asm map.
- [x] Matched C feeds the ROM build (first as `INCBIN`s of the compiled bytes,
      now as its RGBDS translation; a ROM slice without the compiler;
      `make compare` is the gate).
- [x] A larger, branchier function: `DrawSyncField` (262 B).
- [x] Shared header, cached batch verification, multi-function units
      (`c/bank03/InstrumentScreen.c`: 34 functions).
- [x] A screen input handler (300+ instructions): Wave, Word, Groove, Synth,
      Project and Song screens matched.
- [x] Grow the C tree bank by bank (editor code first; the asm functions
      stay asm): every C function in the ROM is matched (678, 141,700 bytes),
      six of them through an `lsdj-alloc` directive (above). The 226 asm
      functions (the interrupt handlers, the sound driver's core, the tile
      loader, the mixer kernel) stay asm.

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
      bytes (`ld c, LOW(ErasePhraseCursor) / ld b, HIGH(…) / push bc`, also
      through register copies, or loaded before a `jp` to a switch's shared
      `push bc / … / call FarCall` tail), a function pointer stored a byte at
      a time (SDCC spilling a banked call's target to the stack,
      `ld [hl], LOW(SoundGetChainPhrase) / inc hl / ld [hl], HIGH(…)`, also via
      registers; the `wBlockSinkPointer` handler), and an address plus a constant
      (`wStringSource = AsciiCharacterMap - $20` as `ld a, LOW(AsciiCharacterMap) /
      add a, $e0 / … / adc a, $ff`).
- [x] Pointer tables as `dw Label`: a `.sym` tag `[N] dw` (`[N] dw @$BB` for
      one pointing into another bank) makes the carve print the N words as
      labels — CommandHelpPointers, CommandHandlers, HexCellPrefixPointers,
      HelpMenuPointers, HelpPagePointers. The interrupt and `rst $38` vectors are
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
      save through the name editor) then seeded random input. Every frame it
      compares the screen and the sound: the sound registers a program can
      read back (volumes, envelopes, duty, sweep, the noise channel, panning,
      the on/off bits), the wave RAM, and whenever the frame's audio differs
      the period each of PU1, PU2 and WAV plays. Those 11 bits are write-only
      (they read back `$FF`/`$BF`), so they come from PyBoy's save state: the
      APU's fields come early in it, a sentinel written to each register
      finds them (a PyBoy that moves them fails there), and the save stops
      once it has them. SRAM is compared at the end. A shifted build is not
      cycle-identical — a pointer into moved data crosses a page at another
      step (`inc [hl] / jr nz / inc hl / inc [hl]`) — so a difference counts
      once it lasts 30 frames, and values written a few cycles apart sound
      the same without sampling the same: the periods decide, not the
      samples. Each run is a child
      process under a watchdog (PyBoy never returns from a CPU stopped for
      good). On a divergence it reports where an `$FF` (`rst $38`: crash,
      assert, `DebugTrap`) ran and the call chain; `--bisect` narrows down
      the stale target. Bank 0 shifts at `$0391`, past the page-aligned
      tables at `$0200`-`$0390` that SoundCalculateVibratoOffset reaches with
      `ld d, $02` / `ld h, $03`, sections pinned at `$0200` and `$0300`.
      Passing: every bank alone (16 bytes) and all at once (1, 3, 5, 7, 9, 11,
      16, 32 bytes), CGB and DMG, seeds 1-15 (the DMG 1-5). Odd sizes matter: a stale
      target can land on harmless code at one size and on a `ret` at
      another. It proves what the input reaches, no more.
- [x] Behaviour tests for builds that no longer match the ROM, what a fork
      runs instead of `make compare`: `make behaviour`
      (`tools/behaviour_test.py`) runs the build next to LSDj 9.4.0 itself in
      the same lockstep, tour and seeded input, as a CGB and as a DMG
      (`--seeds 1-5`, `--build` for any ROM). Checked on builds changed on
      purpose: a string the project screen shows (`TxtSyncLsdj`: the screen,
      frame 3431), the low bit of every note period (`NoteFrequencyTable`:
      the period PU1/WAV plays, frame 183, as the song starts), an `rst $38`
      at `DrawSyncField`'s entry (found, with the call chain). A string the
      input never shows (`TxtSyncOff`: the song syncs to LSDJ) passes, as it
      should.

A function that won't match doesn't block this: it stays asm, which is just
as editable once the carve is symbolic, or gets functionally equivalent C
checked by the behaviour tests.

## References

- GBDK binaries: <https://sourceforge.net/projects/gbdk/files/> (`gbdk-win32/2.95-3/`, `gbdk/2.96/`)
- GBDK releases with SDCC source per tag: <https://juju.nz/src/juju/gbdk-releases>
- GBDK 2.95 mirror (with binaries): <https://github.com/rbong/gbdk> (`gbdk-2.95/`)
- SDCC: <https://sdcc.sourceforge.net/>
- Matching-decomp methodology (C via original compiler): pret `pokeemerald`.
