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

**What the *remaining* unnamed WRAM is — checked against the actual compiler.**
LSDj is SDCC-compiled C (GBDK 2.95 / SDCC 2.2.1, the period toolchain in
`toolchain/`). I compiled probe functions with that exact compiler
(`tools/match/match_function.py --show-asm`) to settle how it allocates storage:
**function locals go on the stack** — the codegen is a `lda sp,-N(sp)` frame with
`lda hl,N(sp)` (= `add sp,-N` / `ld hl, sp+N`) accesses, and the `_OVERLAY` area
is **empty**. LSDj's own code uses those exact stack-local patterns. Therefore:

> **Every fixed-address `ld hl, $cXXX` access is a real global or `static`
> variable, not transient compiler scratch.** The earlier guess that the
> draw-routine `$cXXX` bytes were "compiler locals" was *wrong* — the compiler
> check disproved it.

So the still-raw `$cXXX` operands are genuine variables worth naming; they're just
module-level state rather than per-frame globals: draw-module VRAM pointers and
cursors (`$c998` holds `$98xx`/`$9axx` tilemap addrs; `$c938`/`$c9a4`/`$c9e8` are
per-screen draw state), plus intricate subsystem state whose algorithm must be
reversed first to label it correctly (`$c4d4-$c4e3` pitch-mod sweep — its
accumulator/target relationships are subtler than they look; `$c8f5` sound flags;
`$cb67-$cb6b` MIDI sync). Name by tracing each variable's role (and, where a UI
action drives it, confirm with the SameBoy harness above).

### Subsystem-by-subsystem pass — and a refinement of "what's left"

DFS through whole subsystems (verified static + dynamic) named, beyond the
sound-struct/cursor work: `wRng`, `wFrameCounter(Hi)`, the serial **send clock**
(`wSyncSendAccum/Rate`) and **sync state machine** (`wSyncTick/Run/Handshake/
SendGate`), the **work-time clock** (`wWorkTimeAccum/Ticks` -> SRAM secs/mins),
the **oscilloscope** wave-display (`wOscActive/BankA-B/LoopA-B`), `wWaveCursor`,
`wWavMode`, `wFileScroll`, `wCompByte`, `wSongReadBuf`. Two structural findings:

1. **Overlay:** `$c4d4-$c4f9` is shared between the pitch-mod stepper (`c8f5`-gated)
   and the oscilloscope wave-display (`c8f6`-gated, bank00 `$03d8`) — same RAM,
   two mutually-exclusive features. Don't name the shared pointers for one use.
2. **The remaining ~1800 raw `$cXXX` refs are NOT mostly single-purpose globals.**
   They split three ways, and only the third is cleanly nameable:
   - *Shared work buffers* whose **address is passed** to helpers (e.g. `wSongReadBuf`
     -> `ReadSongBytes`, `wStrBuffer` -> `StrCopy`; `c431/c457/c405/c555` similar).
     General scratch, not a specific global — name generically at best.
   - *Transient draw/render scratch* — **zeroed between uses** (read `$c9e8`/`$c9ed`
     on the SYNTH screen: all `00`), i.e. SDCC static locals for one routine
     (`c9e8/c9ed` synth render, `c938` chain draw, `c9a4` proj draw). Naming needs
     the routine's algorithm reversed; the name is that routine's local, not a global.
   - *Subsystem state* (sync/oscilloscope/clock/file flags & counters) — the genuinely
     nameable globals, and where the verified names above came from.

   So "nameable globals remaining" is far smaller than 1800; target subsystem-state
   vars, recognise the scratch, and don't force names onto buffers/transients.

### bank 0 helper/runtime naming sweep

A later pass targeted the remaining auto-generated `L<hex>` entries in bank `$00`
(the SDCC-style helpers and runtime that the per-bank thematic pass left
unnamed): **74 → 0 — every function entry across all 13 carved banks now has a
behavioral name; no auto-generated `L<hex>`/`L<bank>_<hex>` labels remain.** Each
was verified by reading the code + confirming cross-bank callers (and, for the
tempo/sprite/tile-loader clusters, the running ROM), build byte-identical
throughout. Clusters named:

- **Sound engine**: per-channel pitch-slide/vibrato steppers, period interpolation,
  `SndWriteFreq{PU1,PU2,Wav}`, pitch-mod flags, the software-envelope ("zombie
  mode") volume engine (`FadeEnvStep`/`UpdateEnv*`), `LoadWaveRam`, `SndModWavFreq`.
- **SDCC/GBDK runtime**: the whole 8/16-bit signed+unsigned `Div`/`Mod` family +
  `DivStep`, `memcpy`, `StrCompare` (rgbasm reserves `strcmp`!), `joypad`,
  `delay`/`DelayLoop`, `Abs16`, `CallHL`.
- **Sync**: `DispatchSyncClock`, `SyncTick{LSDJ,Midi}`, `Sync{Start,Stop}Transport`,
  `SyncSet{Send,Recv}Mode`, `SyncApplyMode`, `SyncSend`/`ReceiveAnalogClock`
  (AN.OUT/AN.IN) — see sync.md.
- **Input**: `HandleButtonEvent` → `HandleInputCombos` + `DispatchScreenInput`
  (the per-event chain from the main loop).
- **Video/UI/OAM**: `DrawFormat` (printf, 23 callers) + `DrawNumber`, `DrawTempo`
  (+ `GetTempoTimerValue`, var `wTempoMode`), `DrawPulseDuty`, `ShowMessage`
  (47 callers), `SetOAMByte`, the marker-sprite `SetSprite*`/`ClearMarkerSprites`,
  `RedrawScreen`, `ResetScreenSprites`, `CopyToBgTilemap`, `ClearBgTilemap`,
  `SwitchToDoubleSpeed`.
- **Dynamic VRAM tile loader** (`$CBE1-$CBFC` LCD-off subsystem, confirmed at
  runtime to build a `$CBE6` request table of source pointers like bank-2 `$462A`):
  `LoadTilesToVram`, `SetTileLoadReq`, `CopyTileData` ( -> VRAM tile data
  `$9000+slot*16`), `ReinitDisplay`, `ResetTileLoader`. This also corrected the
  mislabeled `wSyncPosition` -> **`wTileLoadNow`** (`$CBDB` is the loader's
  immediate-copy flag, only ever 0/2; its `GetSyncPosition` getter is dead code).
- **Kit (sample bank) subsystem**: `KitIndexToBank` (kit index -> ROM bank, +8 with
  a +5 gap to skip the code banks `$1B-$1F`), `KitExists`, `ReadKitName`. This
  corrected a mislabel: `$0CFE` "FarCallByteAlt" is the kit-presence check (reads
  `[$4000:01]==$6040`), not a FarCall variant.
- **Sync / sound tail**: `SyncSeekSongPos`, `SndApplyWavVibrato`.
- **Final UI helpers** (glyphs decoded from VRAM): `DrawTileRun` (run of cursor `►`
  / blank / solid `█` tiles), `DrawStringTile20`, `AnyChanFlagSet`.
- New RAM vars from the same work: `wChanPitchModActive` (`$C3F9`), `wOamDirty`
  (`$CBD1`, which also corrected a wrong "NOI envelope" comment), `wTempoMode`
  (`$C52A`), `wTileLoadNow` (`$CBDB`), the tile-loader state (`wTileLoadSlot`/`Src`/
  `wTileSlotAlloc`/`wTileReqTable`, `$CBE1-6`), the power-save idle (`wIdleHalt`/
  `wIdleFrames`, `$C68B-C`), and the word-editor cursor (`wWordEditRow`/`Col`,
  `$C937-8`).

**Function labeling is 100%; RAM-var naming was then pushed to its verified limit.**
A var-naming pass added ~12 more (all byte-identical): `wDrawStep` (`$C405`,
multi-frame redraw step — *not* a buffer), `wTransposeDirty`, `wChanRetrigTimer`/
`Rate`, `wPlayMarkerTimer`, `wAnalogOutTimer`, `wSyncInPulses`/`wSyncInGate`,
`wSyncRxPos`, `wChanVibAccum`, `wFxNoiseNote`, `wChanNoteStep`, `wWordEditRow`/`Col`,
`wIdleHalt`/`wIdleFrames`, the tile-loader state, and the FX-slot/table step
countdowns (`wFxSlotCmdTimer`/`wFxSlotEnvTimer`, computed-addressed so doc-only).

The remaining unnamed var references are then **two provably-unnameable classes**,
confirmed by a dedicated bank-02 sound-engine pass + live emulator tracing:

- *Unexercised-feature state* — a memory diff across song playback showed **every
  sound var that changes is already named** (`wChanReadPtr`/`CmdValPtr`/`InstrPtr`,
  `wChanFreq`, `wChanOutNote`, …). The unnamed bank-02 vars (`$C347`/`$C34B`,
  `$C310`, `$C0D5`, `$C15C`, …) stay 0/static because the demo song doesn't use
  their features (FX commands, vibrato, tables); naming them needs constructed
  feature-rich song states, and several are computed-addressed (doc-only) anyway.
- *Multi-purpose scratch & address-passed buffers* — `$C431`/`$C457`/`$C555`/`$C938`-
  class work buffers reused across routines, with no single global purpose.

These are named only where ALL readers/writers (or live observation) pin down one
purpose; the rest are left raw rather than guessed.

### Emulator symbols: exporting the names back into the debugger

The names are only useful for live analysis if the emulator can see them. The
link step already emits `build/lsdj9_4_0.sym` (rgblink `-n`, the BGB / SameBoy /
Emulicious `BANK:ADDR Name` format) with **all 7151 routine symbols** — but
rgblink exports *only* section labels, **not `EQU` constants**, so every one of
our 230 `constants/ram.inc` WRAM/HRAM variables was missing and the debugger
showed raw addresses for memory accesses. (Confirmed: the link `.sym` has 0
symbols in the `$c000`–`$dfff`/`$ff80`+ range.)

`tools/gen_sym.py` closes the gap: it appends the `ram.inc` variables to the link
`.sym` (routines + `.L_xxxx` locals), tagging each RAM symbol with the correct
bank (`$c000`–`$cfff`/HRAM → `00`, `$d000`–`$dfff` WRAMX → `01`). `make sym`
enriches `build/lsdj9_4_0.sym` **in place** (idempotently — a prior RAM section is
stripped before re-appending), so there is a single comprehensive symbol file that
is already correct beside the built ROM, and then **deploys a copy beside each
other ROM you open** — repo-root `lsdj9_4_0.sym` and `.emu/lsdj_song.sym`. Those
copies are unavoidable: Emulicious/BGB/SameBoy auto-load a `.sym` only from *beside
the ROM, matching its basename*, and the root ROM (your supplied original) and the
daemon's `.emu/` copy aren't in `build/`. The result (7151 routines + 230 variables
= 7381 symbols) means the debugger now names both code *and* state, which is
exactly what makes the iterative diff-WRAM-while-driving-the-ROM loop above fast.
The generator only touches the `.sym`, so the ROM stays byte-for-byte identical.

### Driving out the debugger's `_LABEL_`/`_DATA_` auto-labels

Even with every routine + RAM var exported, Emulicious still shows
`_LABEL_bb_aaaa` for a **code** address that is a branch/call/pointer target with
no symbol, and `_DATA_bb_aaaa` for a **data** address referenced by an operand
with no symbol. Reaching "no auto-labels" means giving every *referenced* address
a symbol. The machinery:

- **Measure — `tools/label_gaps.py`.** Reproduces the classification statically
  (two-pass: index every bank's code/data region starts by recursive-descent from
  the known symbols, then flag each referenced-but-unnamed address as `LABEL` /
  `DATA` / ambiguous-bank `DATA?`). Immediate `ld rr,nn` loads only count when the
  target is a real region start, so loop-count constants aren't mistaken for
  pointers. The `DATA` count is reliable; the `LABEL` count is an over-approx
  (it also flags mid-named-function code pointers Emulicious would show as
  `Func+N`) — treat it as a worklist, not gospel. Baseline: ~175 `LABEL`, ~225
  `DATA` across the carved banks (kit + empty banks extra).
- **Understand — static first, then the emulator.** Read the referencing
  instruction (`--bank N` prints each gap with its ref sites) and the target
  bytes; when the role is unclear, drive the feature live (`tools/emu/song.py`)
  and/or set a read-watchpoint (ANALYSIS.md §6) to catch the consumer.
- **Two big gap classes, both byte-neutral to fix.** (1) *Mis-carved data tables*
  — a `dw`/`db` table the trace decoded as instructions; its bytes read as bogus
  operands, which the analyzer surfaces. Mark it data (`MANUAL[bank]` in
  `carve_clean.py`) + name it (`src/bankNN.sym`). (2) *Real data tables* referenced
  by `ld hl,TABLE` — just name them. Unreached code (`_LABEL_`) is re-seeded so it
  decodes as code + gets a name.
- **Render — `disasm.build_labels` now emits a user symbol at any row start**
  (code *or* data), so a table marked `db` still shows its label. (Previously
  labels only attached to instruction addresses, so a data label silently
  vanished the moment the region was marked data.)
- **Verify — `make compare` stays byte-identical** every step (code↔data
  re-rendering never changes bytes), then `make sym` re-deploys the symbols.

First application: bank `$02` `$43D8` is a 256-entry 16-bit exponential pitch
curve read by `ApplyPitchBend` (`FxCmd_P`) as `[$43D8 + 2*hFxValue]`; it decoded
as ~250 lines of garbage code and is now the `PitchBendTable` data block (bank
`$02` gaps 7→4 `LABEL`, 10→8 `DATA`). The remaining carved-bank tables are worked
the same way.

The **21 sample-kit banks** (`$08`–`$1A`, `$20`–`$21`) are done: `gen_main.py`
detects a kit by its `$6040` marker and emits a labeled section per kit instead of
one opaque `INCBIN`. All names are read from the ROM **at build time**, so a kit or
sample renamed with an external tool labels correctly: `Kit<NAME>_Ptrs` (`$4000`
offset table), `_Names` (`$4022`), `_KitName` (`$4052`), then the sample data split
at each pointer-table boundary into `Kit<NAME>_<SAMPLE>` using the 3-char sample
names (e.g. `KitTR808_BD/SD/CHH/…/CLP`). **312 symbols** (TR-606…TT-ZZ; the kit
name matches the INSTR editor's KIT selector). Collisions are disambiguated — a
duplicate kit name gets the bank appended, a duplicate sample name within a kit a
numeric suffix — so patched ROMs never produce duplicate labels. Now the sample
player's reads resolve to `Kit<NAME>_<SAMPLE>+off` rather than `_DATA_`.
Byte-identical (contiguous INCBINs). The empty banks (`$22`–`$3F`) are `$00`/`$FF`
fill and unreferenced.

**Naming standard.** Data-table labels are **intuitive/semantic**, reverse-engineered
from the using code — never `Lut<addr>`/`Table<addr>`. Determine what a table *is*
(what reads it, how it's indexed, where its result goes) before naming: e.g. the
recurring 16-byte ramp copied into `wave_frames` is `DefaultWaveFrame` (not
`Lut491A`); the 16 bytes copied to `synth_params $3EB2` are `DefaultSynthParams`;
an all-zero display string is `TxtBlankString`. If a table's purpose can't be
pinned after a reasonable look, leave it a documented gap rather than an
address-name (an address-name is worse than an honest gap). Cross-bank duplicates
need unique names (one link namespace); `make compare`'s assembler catches
collisions.

**Carved-bank tables — progress.** Worked every program bank with `label_gaps.py`
(refined so its `_DATA_` count is trustworthy: it skips low-page immediates —
`ld hl,$00xx` constants — and ROM *writes* `ld [nn],a/sp`, which are MBC5
bank/RAM control, not data). `_DATA_` fell from ~133 to **~14** (the residue is
mostly the analyzer decoding a *labeled* data table's bytes as code, plus a few
deferred tables below). Named intuitively (never `Lut<addr>`):
- sound `$02`: `PitchBendTable`, `VibDepthTable`/`VibRateTableA`/`VibRateTableB`
  (`FxCmd_V`), `EvenSpreadTable`; home `$00`: `BitMasks`, `FxMnemonics`.
- defaults copied into SRAM on init: `DefaultWaveFrame` (ramp → `wave_frames`,
  in banks `$01`/`$1C`/`$1F`), `DefaultSynthParams` (→ `synth_params $3EB2`),
  `DefaultInstrument`.
- display tables: `NoteNames` (chromatic), `PanNames`/`ChannelNames` (song),
  `SynthFilterNames`/`SynthWaveTypeNames`/`SineTable`/`SynthDispatchJumps` (synth),
  `WaveSourceNames`/`InstrOptionNames`/`InstrParamJumps` (instrument),
  `AlnumChars`/`TxtClockLabels`/`CloneModeNames`/`SyncModeNames` (project), and
  ~50 UI message strings (`TxtCopiedInstr`, `TxtOutOfMemory`, …; cross-bank
  duplicates get a bank/`2` suffix since the link is one namespace).
- **Deferred** (honest gap > address-name): bank `$04` `$50D8`/`$54E3` (phrase
  flag/config tables), `$07` `$662A` (a table added to sync var `$CB91`) and
  `$676A` (a mis-carved phantom), `$1E` `$42C8`/`$7550` — each needs the using
  algorithm reversed before it can be named truthfully.

The `_LABEL_` count is a loose worklist, not a metric: it over-counts `ld
hl,CodeAddr` pointers into named functions (Emulicious shows `Func+N`) and labeled
data tables whose bytes decode as instructions. Refining it is the next tooling
task.

**Dynamic driver — `tools/emu/song.py`.** A verified editing layer that also reads
and writes the working song directly in SRAM (`sram_read`/`sram_write` +
`data_model.md` offset map): named getters/setters (`row`, `seq`, `set_note`,
`set_fx`, `set_cell`), value codecs (`note_val`/`note_str`, `fx_val`/`fx_str`),
`set_instr_type`, and screen/cursor nav — enough to script song production and to
exercise any feature on demand. Caveat folded in: a phrase-note byte renders per
its row's instrument **type** (PULSE/WAVE tonal; KIT shows two sample-name columns
from the kit; NOISE its own scheme).

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
