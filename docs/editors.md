# Editors / screens — architecture and carving plan

LSDj's UI is a set of **screens** (editors) navigated with SELECT+direction.
This documents how the screen system is wired and how each editor was carved
out of the ROM. (All screens are located, carved and named — status table at
the end.)

## Control-flow spine (confirmed)

```
Entry $0100 -> Start $0150 (init) -> main $1306 (InitAndLoadSong $0DDB, ...) -> main.mainLoop $1377
  main.mainLoop: when wRedrawPending ($C56C) says a frame is due ->
    ProcessInput $0ED6: drain the button queue (GetNextButtons); per event
      HandleButtonEvent $2B8D -> HandleInputCombos + DispatchScreenInput
      ($2A71, jp table $2A86 by wCurScreen -> the screen's input handler)
    ScreenUpdateDispatch $264B (screens 1-5), then a bank-1 UI tail
      (UpdatePlayClock, UpdateWorkClock, UiShowMessage)
  otherwise, while wSynthRenderActive ($C9F1): one RenderSynthWave step
      (bank $1D) per pass, 16 steps
  ScreenDrawDispatch $254F (via RedrawScreen): jp ($2563 + 3*wCurScreen)
      -> the active screen's draw routine (selects its bank, calls it)
```

Key mechanisms:
- **Bank switching**: write the bank number to HRAM shadow `hCurBank`
  (`$FF8E`) *and* the MBC5 register `$2000`. Editors live in the switchable
  window `$4000`-`$7FFF`.
- **Banked calls**: bank 0 sets the bank then `call`s the `$4000`-`$7FFF`
  routine; results come back in `e`/`de` (SDCC convention). Inline stubs (the
  `DrawDispatch_<Screen>` entries, `CrashTrampoline` `$0061`) and the far-call
  helpers `FarCall`/`FarCallArg`/`FarCallByte` wrap this. (In the generated
  asm, bank 0's inline-stub call targets still print as raw `$4000`-`$7FFF`
  addresses — 79 sites; the debugger resolves them at runtime.)
- **Song access**: editors read/write the working song via `ReadSongByte`
  ($14D1) and SRAM banking — see [`data_model.md`](data_model.md).
- **UI text**: a **string table lives in bank 1** (e.g. `$6F7E` =
  "LITTLE SOUND DJ V9.4.0 © 2000 JOHAN KOTLINSKI", plus "TOO BUSY!",
  "A: RUN …"). `ShowMessage` (`$0DBC`) copies a string into `wStrBuf`
  (`$C6A9`) and triggers the on-screen message; bank 1's `DrawMessageBar`
  (`$74F7`) renders that buffer onto the top tilemap row. (`DrawMessageBar` is
  also the first function matched in C: `c/bank01/DrawMessageBar.c`.)
- **Config**: `LoadSyncSetting` ($1CFF) pulls song properties (e.g. sync) from
  SRAM into WRAM mirrors (`wSyncMode` `$CB66`, `wSyncMode2` `$CB67`).

## Screen dispatch — the editor index (bank 0)

The current screen is `wCurScreen` = **`$C401`** (0–14).
`ScreenDrawDispatch` (`$254F`) does `hl = $2563 + 3*screen; jp hl` into a
15-entry `jp` table (`ScreenDrawTable`, `$2563`). Each target is a stub that
selects the screen's ROM bank (`$FF8E` + `$2000`) and `call`s its draw routine:

| `$C401` | Draw routine | Screen | Evidence |
|---------|--------------|--------|----------|
| 0 | — | (none) | blank slot |
| 1 | `$04:$4CE5` | **Phrase** | note/instr/cmd/val grid |
| 2 | `$1D:$41DA` | **Groove** | reads `grooves` |
| 3 | `$06:$5947` | **Chain** | reads `chain_transposes`, 2 columns |
| 4 | `$07:$4574` | **Song** | reads `sequence` (`$1290`), 4 channels |
| 5 | `$05:$4762` | **Table** | reads `table_envelopes` |
| 6 | `$03:$4DE7` | **Instrument** | reads `instruments` 119x; **live `$C401`=6 on Instrument** |
| 7 | — | (none) | blank slot |
| 8 | `$06:$4F18` | **Wave** | reads `wave_frames`; copy/paste ("CANCEL SELECT") |
| 9 | `$1B:$5C17` | **Project** | `DrawProjScreen` ✓ (sync/clone/look/clock) |
| 10 | `$1D:$68AB` | **Synth** | reads softsynth params (`synth`) |
| 12 | `$06:$41A2` | **Word / Speech** | reads `words`; draws "WORD" |
| 13 | `$1C:$57BB` | **File** | "BLOCKS USED", "LOSE %u HOURS OF UNSAVED WORK?" |
| 14 | `$1B:$4A62` | **Help** | bank `$1B` help text (`DrawHelpScreen`) |

All 12 of LSDj's screens are identified — a complete, coherent set. Each
screen's draw routine is located by `bank:addr`; the indices are named in
[`../constants/screens.inc`](../constants/screens.inc) and the bank-0 dispatch
stubs in `src/bank00.sym` (`DrawDispatch_<Screen>`).

Input has its own index: `DispatchScreenInput` (`$2A71`) jumps through a
15-entry table at `$2A86` to each screen's input handler (resolved from the
stubs' bank + call target):

| `$C401` | Input handler | | `$C401` | Input handler |
|---------|---------------|-|---------|---------------|
| 1 Phrase | `PhraseScreenInput` `$04:$5BA6` | | 8 Wave | `WaveScreenInput` `$06:$507F` |
| 2 Groove | `GrooveScreenInput` `$1D:$4451` | | 9 Project | `ProjScreenInput` `$1B:$5DBF` |
| 3 Chain | `ChainScreenInput` `$06:$5BD0` | | 10 Synth | `SynthScreenInput` `$1D:$7138` |
| 4 Song | `SongScreenInput` `$07:$4B3B` | | 12 Word | `WordEditorInput` `$06:$431D` |
| 5 Table | `TableScreenInput` `$05:$4B5D` | | 13 File | `FileScreenInput` `$1C:$5B0C` |
| 6 Instrument | `InstrScreenInput` `$03:$541F` | | 14 Help | `HelpScreenInput` `$1B:$4BC3` |
| 7 (name editor) | `NameEditorInput` `$1B:$53E8` | | 0, 11 | — |

Matching `DispatchScreenInput` in C (`c/bank00/ScreenDrawDispatch.c`) settled
the handlers' names: five of them had been named after a first action (a cursor
highlight, a field draw) and three after `Edit`.
Index 7 has no draw routine but does have an input handler: the on-screen
name keyboard used by the Project/File screens.

A **second** dispatch, `ScreenUpdateDispatch` (`$264B`), runs each frame for
screens **1–5 only** (the active music editors) via its own 5-entry table
(`$268D`) — a per-frame **visual refresh** (redraws the playing rows), distinct
from the (interleaved) input-edit code. So `$C401` drives parallel *draw* and
*update* dispatches. The per-screen update handlers:

| Screen | Update handler | | Screen | Update handler |
|--------|----------------|-|--------|----------------|
| Phrase (1) | `PhraseScreenUpdate` `$1F:$648F` | | Song (4) | `SongScreenUpdate` `$07:$47AA` |
| Groove (2) | `GrooveScreenUpdate` `$1D:$4215` | | Table (5) | `TableScreenUpdate` `$05:$4967` |
| Chain (3) | `ChainScreenUpdate` `$06:$5A62` | | | |

Before most screen draws, the dispatch prologue calls `ScreenPreDraw`
(`$1E:$4000`). Runtime variables are collected in
[`../constants/ram.inc`](../constants/ram.inc).

**Carved banks: 13 of 64** — `$00`-`$07` and `$1B`-`$1F`, i.e. **every program
bank**. The remaining `INCBIN` banks are the 21 sample kits (`$08`-`$1A`,
`$20`-`$21`) and 30 empty banks (`$22`-`$3F`) — both correctly left as raw data.

## The screens (LSDj manual)

The [LSDj v9.2.6 manual](https://www.littlesounddj.com/lsd/latest/documentation/LSDj_9_2_6.pdf)
(chapter 2; the closest to 9.4.0) describes every screen and field; its
Appendix C has the SRAM map and the instrument byte layouts.
Song, Chain, Phrase, Instrument, Table, Groove, Synth (Wave), Word/Speech,
Project, and File — each edits the corresponding song structure in
[`data_model.md`](data_model.md). Each screen has a **draw** routine (paints the
grid from song data into VRAM/the OAM-shadow) and an **input/edit** routine
(applies button presses to the song structure).

## Carving method (per editor — as used; all editor banks are done)

1. **Locate.** Find the screen-dispatch table (the index mapping the current
   screen to its handler bank:addr) — reached from the frame processor. Then
   follow the bank trampoline to the handler.
2. **Carve the bank.** Create `src/bankNN.sym`, then `make compare`:
   `tools/carve_clean.py` regenerates every bank that has a `.sym` (coverage
   trace → code/data split → `tools/disasm.py`) and `tools/gen_main.py`
   INCLUDEs it. Data tables the trace can't classify go in `carve_clean.py`'s
   `MANUAL` dict.
3. **Name.** Add `addr Name` lines to `src/bankNN.sym` for the editor's draw/
   input routines and the song fields they touch; `make compare` again.
4. **Document.** Record behaviour here and in `functionality.md`.

Banks without a `.sym` stay `INCBIN` (the sample kits get labeled sections
from the ROM itself). `make compare` is the invariant throughout.

## Bank → role (from `bank_map.md`)

- Engine/UI code: banks `$00`-`$07`, `$1B`-`$1F` (editors are here).
- Bank `$01`: UI **string table** (+ code).
- Sample kits: `$08`-`$1A`, `$20`-`$21`. Empty: `$22`-`$3F`.

## Project editor

The Project screen holds whole-song settings. Its **value strings and field-draw
helpers live in bank `$1B`** (`src/bank1b.sym`):

| Field | Value source | Strings (bank `$1B`) |
|-------|--------------|----------------------|
| TEMPO | song `$3FB4` (BPM) | numeric |
| TRANSPOSE | `wSongTranspose` (song property) | numeric |
| **SYNC** | `wSyncMode` `$CB66` (song `$3FBD`) | `TxtSyncOff`…`TxtSyncAnOut` `$59B7` (six 7-byte strings) — see [`sync.md`](sync.md) |
| └ sub-setting | PS/2 DELAY / TICKS/STEP | `TxtSyncPs2` `$59E1` / `$59ED` |
| CLONE | deep/slim | `DEEP`/`SLIM` (`CloneModeNames` `$5D4F`, `CloneModeNames2` `$6A5C`) |
| LOOK | font (`wFont`) + colorset (`wColorSet`) | `ColorSetNames` `$5716` (5-char names, indexed `wColorSet*5`) |
| KEY DELAY/REPEAT | settings | numeric |
| PRELISTEN | flag | on/off |
| clock | total/work time | `PLAY %d:` / `WORKED %d:` (`TxtClockLabels` `$5C03`) |
| menu | — | `LOAD/SAVE SONG`, `RESET MEMORY` (`$5D59`), `RESET CARTRIDGE?` (`TxtResetCartridge` `$6A66`) |

Confirmed routines (bank `$1B`):

- **`DrawProjScreen` (`$5C17`)** — the Project draw routine (dispatch index
  9). Paints each field at a fixed tilemap address via helpers, e.g. the
  LOAD/SAVE-SONG vs RESET-MEMORY menu line, then the SYNC value, then song
  properties read with `ReadSongByte`.
- **`DrawSyncField` (`$58B1`-`$59B6`)** — reads `wSyncMode`, selects the mode
  string via a sparse `switch` (`cp` chain over `0,1,3,5,6,7`), draws it to
  VRAM `$9885`, then the sub-setting label + `wSyncMode2` (or a blank) at
  `$98A0`. Its second half was once labelled as a separate `DrawSyncModeField`;
  matching it in C (`c/bank1b/DrawSyncField.c`) showed it is one function.
- **`DrawSyncSubSetting` (`$5B7A`)** — the PS/2 DELAY / TICKS/STEP value beside
  SYNC. Other field helpers: `ProjDrawTempo` (`$5853`, commit a queued VRAM
  write), `DrawFontColorFields` (`$569C`, the FONT and COLOR SET rows), and
  bank 0's `DrawHexByte` (`$2E33`, numbers).

The Project cursor row is `wProjCursor` (`$C9A4`). (`$C566`-`$C569` were once
taken for cursor state; they are the BG-attribute draw palettes
`wShadowDigitPal`/`wShadowPal`/`wSelectDigitPal`/`wSelectPal`.) The field
*labels* (TEMPO, SYNC, …) are tiles, not ASCII. The Project input handler is
the index-9 entry of `DispatchScreenInput` (`ProjScreenInput`, `$5DBF`).

## Status

Every screen is **located, carved and named** in the source tree (byte-for-
byte): draw routines below, input handlers in the input-index table above.

| Screen | idx | Draw routine | Edits | Carved | Draw label |
|--------|-----|--------------|-------|--------|-----------|
| Instrument | 6 | `$03:$4DE7` | `instruments` `$3080` | ✓ `bank03.asm` | `DrawInstrScreen` |
| Chain | 3 | `$06:$5947` | `chain_*` `$2080`/`$2880` | ✓ `bank06.asm` | `DrawChainScreen` |
| Song | 4 | `$07:$4574` | `sequence` `$1290` | ✓ `bank07.asm` | `DrawSongScreen` |
| Phrase | 1 | `$04:$4CE5` | notes/instr/fx | ✓ `bank04.asm` | `DrawPhraseScreen` |
| Table | 5 | `$05:$4762` | `table_*` | ✓ `bank05.asm` | `DrawTableScreen` |
| Groove | 2 | `$1D:$41DA` | `grooves` `$1090` | ✓ `bank1d.asm` | `DrawGrooveScreen` |
| Wave | 8 | `$06:$4F18` | `wave_frames` `$6000` | ✓ `bank06.asm` | `DrawWaveScreen` |
| Synth | 10 | `$1D:$68AB` | `synth` `$3EB2` | ✓ `bank1d.asm` | `DrawSynthScreen` |
| Word/Speech | 12 | `$06:$41A2` | `words` `$1890` | ✓ `bank06.asm` | `DrawWordScreen` |
| Project | 9 | `$1B:$5C17` | properties `$3FB2` | ✓ `bank1b.asm` | `DrawProjScreen` (+sync) |
| File | 13 | `$1C:$57BB` | SRAM filesystem | ✓ `bank1c.asm` | `DrawFileScreen` |
| Help | 14 | `$1B:$4A62` | (text) | ✓ `bank1b.asm` | `DrawHelpScreen` |

All 13 program banks (`$00`-`$07`, `$1B`-`$1F`) are carved; the only `INCBIN`s
left are the 21 sample kits (`$08`-`$1A`, `$20`-`$21`, emitted as labeled
per-kit/per-sample sections) and the 30 empty banks (`$22`-`$3F`).

The whole screen system is named: the spine (`main.mainLoop`, `ProcessInput`,
`HandleButtonEvent`, `DispatchScreenInput`, `ScreenDrawDispatch` +
`ScreenDrawTable`, `ScreenUpdateDispatch` + `ScreenUpdateDispatch.jumpTable`,
`RedrawScreen`), every draw/update/input handler, the per-field helpers, and
the sync subsystem ([`sync.md`](sync.md)).
