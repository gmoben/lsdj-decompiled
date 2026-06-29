# Toward 100% reversal — methodology & coverage

The build already reproduces the ROM **100% byte-for-byte** (`make compare`).
"100% reversal" is the stronger goal: every byte classified as code or data,
all code disassembled with meaningful labels, all data structured.

## What counts

The 1 MiB ROM is 64 × 16 KiB banks:

- **13 code/data banks** — the actual program (`$00`-`$07`, `$1B`-`$1F`). **This
  is the reversal target, and all 13 are now carved into the source tree.**
- **21 sample-kit banks** (`$08`-`$1A`, `$20`-`$21`) — PCM data; identified and
  named (see `bank_map.md`), reversed as data.
- **30 empty banks** (`$22`-`$3F`) — `$FF` fill / spare.

## The coverage metric

`tools/coverage.py` measures **reachable code** via recursive-descent from every
control-transfer entry point:

- reset/interrupt vectors and `Entry`;
- **bank-switch trampolines** (`ld a,N; ld [$2000],a; call/jp $4000-$7FFF`)
  scanned ROM-wide → per-bank entry addresses;
- the **dispatch tables** (screen draw/update) and every hand-named symbol
  (`src/*.sym`);
- a **bank-aware global trace** (`global_trace`): state is `(cpu, pending_bank)`;
  a window byte's bank is implied by its physical offset, and `call/jp
  $4000-$7FFF` resolve against the pending bank set by the most recent `$2000`
  write — so the trace follows control flow **through** bank switches.
 - it follows direct branches/calls, **jump tables** (`jp hl` over a `jp`-list or
   a 2-byte address array), and LSDj's two **farcall helpers** `FarCall` (`$179A`,
   caller pushes `addr` then `bank`) and `FarCallArg` (`$17BB`, same but also
   passes one byte argument). The helper switches bank and calls; every call
   site's `(bank, addr)` pair is extracted as an entry point.
- conservative throughout (jump-table targets must be in range and start on a
  legal opcode), so the figure is a sound lower bound.

### Current coverage

Run `tools/coverage.py`. As of writing, **74%** of the code/data banks trace as
reachable code:

| bank | code% | | bank | code% |
|------|-------|-|------|-------|
| `$00` | 86% | | `$1B` | 58% |
| `$03` | 86% | | `$1C` | 76% |
| `$04` | 93% | | `$1D` | 89% |
| `$05` | 76% | | `$1E` | 13% |
| `$06` | 85% | | `$1F` | 84% |
| `$07` | 71% | | `$01`/`$02` | 51% / 96% |

That 74% is **of all code/data bank bytes**; ~25% of those banks is genuinely
data. `tools/coverage.py` classifies every unreached byte and prints the split —
of the 54007 unreached bytes: **26 KB `$00`/`$FF` fill + 8 KB text/strings + 19 KB
graphics/data** (`$1E` is ~90% tile data, hence 13%), leaving just **616 bytes**
("~8 short functions") that are plausibly unmodelled code. So **code coverage
excluding data is 99.6%** — the reachable code is effectively fully traced; the
remaining percentage points are data, not blind spots.

The lower banks are legitimately data-heavy: `$1B`/`$1C` (Project/File/Help) are
string-heavy, and `$01`/`$1E` carry large zero-padded data tables. Progression:
per-bank 42% → jump tables 56% → bank-aware trace 60% → farcall 68% →
computed-farcall tables 71% → second farcall helper (`FarCallArg`) 72% →
fill-stop (honest) 64% → split-load farcall addresses 72% → jp-hl null-guard
tables 73% → **convergent dispatch 74%**.

Those last three model the instrument editor / sound engine dispatch and lift
bank `$1F` 12→**84%** and `$02` 78→**96%**:
- **split-load addresses**: farcall targets passed as `ld c,lo; ld b,hi` (the
  compiler's form), not `ld bc, imm16` — the per-instrument-type handlers.
- **null-guard jp-hl tables**: an addr-array `jp hl` whose `2a 66 6f` read is
  separated from the `jp hl` by an `or a,h; ret z` null check (the sound command
  table at `$02:$47A2`).
- **convergent dispatch**: several `ld bc,addr; jp X` paths sharing one
  `push bc; FarCall` at `X` — the extractor now keeps every candidate that feeds
  a push/jp, not just the last.

Together they confirm end-to-end that the multi-hop dispatch is fully static and
resolvable — exactly as the live-RAM analysis predicted (no RAM-indirect dispatch).

The last step *lowered* the number on purpose. `rst $38` ($FF) and `nop` ($00)
were not flow-terminators, so any stray computed target landing in padding made
the tracer walk the whole run as bogus "code" (`rst $38`/`nop`), inflating the
metric and keeping the carve from rendering the padding as `db`. Stopping on a
clear fill run (two `$FF`, or eight `$00` -- real alignment is never that long)
both corrects the metric and cleans ~12k lines of fill noise to `db`. Note the
disassembly never *hides* code: carve marks bytes `db` only when they are **both**
unreached and data-like, so a real function after a padding gap still decodes as
code even though the tracer stops short of it.

`$1F` (now 78%) is the instrument subsystem and carves as ~93% code (8977
instruction lines vs 146 `db`). A static dispatch audit found the dispatch is
**entirely ROM-based, not RAM-indirect**: bank `$1F` has only two real `jp hl`
sites and both are ROM jump-tables (`$401B`, `$679D`); there are **no** RAM-fed
`jp hl` and **no** `push addr; ret` trampolines. The reach is a **multi-hop static**
path: the master screen dispatch (`ScreenDrawDispatch` → jp-table `$2563` →
`DrawDispatch_<screen>`) bank-switches into a per-screen handler (the instrument
editor lives in bank `$03` at `DrawInstrumentScreen $4DE7`, reached via an inline
`ld a,N; ld [$2000],a; call` trampoline), which then `FarCall`s into bank-`$1F`
per-instrument-type handlers. The last link was invisible only because those
handler addresses are built with a **split** byte-load (`ld c,lo; ld b,hi`) rather
than `ld bc, imm16`; teaching the extractor that pattern reached the lot
(`$7108`, `$5413`, `$4A65`, `$7097` …). The remaining unreached runs are data:
`$77F5` is the "COPIED WORD"/"CUT WORD" string pool, and others interleave short
label strings ("ATK", "ON", "0.5") with code, so
forcing data ranges would risk *hiding* code — the conservative carve is correct.

The coverage tracer resolves the **computed-address farcall**: when a
`call $179A` pushes an immediate `bank` but reads the `addr` from a pointer
table (`ld a,[hl+]; ld h,[hl]; ld l,a`), it reads the table from the *caller's*
bank and validates targets in the *callee's* bank.

### No RAM-indirect dispatch (confirmed with live RAM)

The early hypothesis that low-coverage banks hid behind *RAM-indirect* dispatch
was **wrong**, and this is now confirmed dynamically, not just statically. Two
WRAM dumps (BGB) — one idle on the instrument editor, one while a song plays —
were checked with `tools/annotate_ram.py`:

- They validated the corrected screen map (`wCurrentScreen` `$C401` = 6 →
  Instrument idle, 4 → Song playing).
- The sound engine's per-channel state (`$C0E0–$C200`, struct-of-arrays over the
  4 channels) holds 16-bit pointers that are all **SRAM song-data** addresses
  (`$A0xx`/`$B0xx`), never ROM code addresses.
- A whole-WRAM scan for code-pointer arrays (aligned, distinct, stable `$4000–
  $7FFF` words) found **none** — every candidate was a text/tile display buffer
  or a data curve (e.g. the monotonic ramp at `$CD38`).

So both subsystems use **static code dispatch + RAM *data* pointers**. The
remaining unreached code is reachable through multi-hop *static* dispatch that the
tracer doesn't fully model — there is no RAM-indirect indirection to chase.

### Sound engine: note-on path (named) and open leads

The per-tick engine (`ProcessChannel`, bank `$02`) reaches note-on via a write of
the new chain number into `wChanChain[ch]` (`$4DD2`), then channel setup:

- **`SetupChannelSound` (`$5934`)** — selects the instrument's SRAM bank, copies
  its parameters into the staging buffer **`wSoundParams` (`$C0D0`)**, then
  dispatches by `hSoundCh` to `SetupSoundPU1/PU2/Wave/Noise`, each of which
  distributes the staged bytes into the `rAUDx` register shadows (`wChanEnv`
  `$C2D8`, `wChanLenDuty` `$C2DC`, `wChanFreqHi` `$C2E0`, `wPU1Sweep` `$C2E4`)
  and triggers the voice. The note→GB-period value arrives **pre-staged** in
  `wSoundParams`; it is computed upstream.

**Resolved — `ReadChainPhrase` (`$5E7C`) and a `wChanNote` correction.** This reads
one byte from a working-song SRAM table (bank 1, base `$A080`) at `idx*16 + step`,
where `step = [$C17C]` (pointer low-byte `$7C` from `InitSoundStatePtrs`), and
stores the result into per-channel `$C168`. Mapping SRAM bank 1 `$A080` → `.sav`
offset `0x2080` identifies the table as **`chain_phrases`** (the authoritative
liblsdj `song_offsets.h` and our [`data_model.md`](data_model.md) agree). So the
index `[$C170+ch]` is a **chain number**, not a note — my earlier `wChanNote $C170`
label was wrong; it is **`wChanChain`**. A live-dump cross-check nails it: in
`songplaying.ram`, `wChanChain` = `01 05 04 00` = exactly song-row 0 of
`chain_assignments`, and `wChanPhrase` (`$C168`) = `chain_phrases[chain][chainRow]`
for every channel (ch1: chain `05`, row `01` → phrase `0C` ✓). The per-channel
read pointers match too (ch1 `$C110` = `$A0C5` = phrase `0C`×16 + step 5, and
`$C16C` = that step). `$5E7C` is therefore the chain→phrase sequencer step:
`wChanPhrase = chain_phrases[wChanChain*16 + wChanChainRow]`.

> SRAM reference: the 128 KB `dumps/lsdj9_4_0.sav` is a full 16-bank SRAM image;
> SRAM bank *N* address `$Axxx` maps to `.sav` offset `N*0x2000 + (xxx-0x000)`.
> No fresh capture is needed for static song data — it already resolved this lead.

### RAM labels: validating against live dumps

The `$C0xx`-`$C2xx` sound-engine RAM and the `$C8xx` channel pointers were
labelled from live RAM dumps (`dumps/*.ram`) cross-checked with the working-song
SRAM map: a name is only committed once the dumped value matches what the SRAM
structure predicts. For example `wChanSongRow`=`02` selects `chain_assignments[2]`,
which equals `wChanChain`=`01 05 04 00` across all four channels, and the read
pointers and phrase/chain rows line up the same way. This pins the whole sequencer
cursor set with high confidence.

The UI/editor RAM (`$C3xx`-`$C5xx`) can't be validated this way -- its values are
transient and screen-dependent -- so those names rest on engine-code usage instead
(e.g. `$C36B` is confirmed as the command value by the code that writes it from the
command-value cursor).

### RAM labels: dynamic verification (SameBoy harness)

UI/editor RAM that can't be pinned from static dumps is now verified *dynamically*
by driving the ROM in SameBoy (`tools/emu/`, cookbook in `ANALYSIS.md`): change
one thing in the game, diff WRAM, and read the on-screen result.

- **`tools/emu/nav.py`** is a verified screen-graph pathfinder. The SELECT+dir map
  is a 5x3 grid whose top/bottom rows are *column-spanning stateful hubs*
  (PROJECT/SYNTH/WAVE on top, GROOVE on the bottom span all columns and remember
  the column you entered from), so a flat `(screen,dir)->screen` graph is wrong;
  `goto` localises then routes via the unique row-0 screens, reaching any editor
  deterministically. (NB: SELECT+LEFT on SONG toggles LIVE mode -- a real side
  effect, never a no-op.)
- **diffstate-on-action** found e.g. `wLiveMode $C528`: SELECT+LEFT on SONG flips
  it `0<->1` and only that byte toggles+reverts; the title and nav-map letter
  switch SONG<->LIVE (confirmed by screenshot).
- **screenshots** map a value to meaning: one INSTR shot enumerates the
  `wInstrField` rows (NAME/TYPE/ENV/WAVE/OUTPUT/LENGTH/SWEEP/PITCH/TRANSP/PU2 TSP/
  FINETUNE/CMD-RATE/TABLE). `Read` the PNG directly.
- Engine-code reading still settles sound names: `wFxChannel $C527` (channel the
  command/FX/table engine is processing, set from `hSoundCh`; some FX OR in
  bit2/3 flags) and `wWavLevel $C8F3` (WAV ch-2 output level -> `rAUD3LEVEL`).

**What most of the *remaining* unnamed WRAM is.** LSDj is SDCC-compiled C, so
beyond the genuine globals a large share of the still-raw `$cXXX` operands are
**compiler scratch**, not hand-named variables. Two flavours:
- *per-routine locals* at fixed addresses, written/read only inside one
  `*DrawHex`/`*DrawStr`/`Draw*Screen`/`Synth*` function (SDCC also uses real stack
  locals via `add sp,-N`), often a 16-bit VRAM pointer holding `$98xx`/`$9axx`
  tilemap addresses -- e.g. `$c938` (Chain draw), `$c9a4` (Proj sync draw),
  `$c998` (File/Proj draw ptr), `$c9e8` (Synth render), `$c431`, `$c405`;
- *shared draw-helper scratch* used by **every** draw routine -- e.g. the
  `$c7a9/$c7aa` text-draw cursor/pointer (50+ refs) that `UiDrawStr` checks and
  the flush routine resets. This one is a real global and nameable with a deeper
  trace of the draw pipeline; the per-routine locals should be recognised *as
  locals* rather than force-named.

**Meaningful globals still worth verified names** (do the driving action, don't
guess): per-frame sound flags such as `$c8f5` (gates vibrato/wave-trigger/freq
writes; bit7 set on trigger, bit1 tested -- verify by observing during real
playback), MIDI/clock-sync runtime state around `wSyncMode $cb66` (`$cb67`-`$cb6b`
-- verify by changing SYNC), and the `$c7a9/$c7aa` draw cursor above.

## Path to 100%

1. **Code/data split (cleaner carves).** The unreached remainder in a bank is
   data or not-yet-reached code. `tools/carve_clean.py` marks an unreached run as
   `db` when it is illegal-opcode-dense, `$00`/`$FF` fill, a long printable run, or
   — since code coverage is ~99.6% — has **control-flow-opcode density** too low
   to be code (≤0.07 vs ~0.10-0.15 for real SM83; catches tile graphics and
   pointer tables that decode as legal opcodes). Two guards keep code intact: a
   run starting with an SDCC prologue is never data, and `func_brackets()` excludes
   mid-run `push bc; … pop bc; ret` functions — so a not-yet-reached function
   embedded in data still reads as code. This split bank `$1E` (~90% tile data)
   from ~12 k garbage instruction lines down to ~2.6 k.
2. **More computed dispatch.** Keep modelling indirect dispatch (e.g. the rest of
   `$1F`).
  3. **Naming — every function has a behavioral name.** Every function entry
     (call- *and* jp-reached) has a real global name; auto-generated **global**
     `L*` labels are gone, and so are the `<Screen>Func`/`Proc` "purpose TBD"
     placeholders — **all ~280 were given behavior-based names** derived by reading
     each function (its dominant RAM/reg access, callees, return value, structure).
     Internal jr/jp targets are RGBDS **local** labels (`.L_xxxx`) scoped to their
     function. Names that retain a `_XXXX` address suffix (e.g. `InstrGetField5_4B4C`)
     are accurate behavioral names where the stem repeats and the suffix only
     disambiguates.
  4. **Re-review pass (correctness).** After labelling, every bank's theme was
     re-verified against the actual entry/dispatch code. This caught and fixed
     several *systematic* mislabels from the earlier heuristic pass:
     - bank `$1C` "FileSave*" cluster was actually the **song-format upgrader**
       (`FileUpgradeSong`/`FileUpgradeV0..V21`, on-screen "UPGRADING…"), not save.
     - bank `$03`: 20 `InstrGetFieldN` were **setters** (read-modify-write) → `InstrSetFieldN`.
     - "incidental access" mislabels (the same class as the old `54D7`): a bulk
       SRAM clear named for a `$a000` base → `ClearSramWindow`; `CompressSong`
       mis-placed on the previous routine's `ret` (off-by-one) → moved to `$441F`.
     Verified-correct themes: sound engine (`$02`), Phrase/Table/Chain/Song
     editors, the `$1F` used-data scanner, the `$06` channel scope. The build
     stayed byte-for-byte through every rename.
  5. **Track the number.** Re-run `tools/coverage.py` after each push.

Throughout, the invariant is non-negotiable: **`make compare` stays green.**
