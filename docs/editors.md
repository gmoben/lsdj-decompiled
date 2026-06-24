# Editors / screens — architecture and carving plan

LSDj's UI is a set of **screens** (editors) navigated with SELECT+direction.
This documents how the screen system is wired and the systematic method for
carving each editor out of the ROM. (Spine confirmed in bank 0; per-editor
routines are being located/named iteratively — status table at the end.)

## Control-flow spine (confirmed)

```
Entry $0100 -> Start $0150 (init) -> ... -> MainLoop $1377
  MainLoop: poll ready flags ($C56C, $C9F1); when a frame is due ->
    process-frame (L13A5): L0ED6 (frame/queue processor) + bookkeeping
      L0ED6 -> banked handlers (bank set via $FF8E shadow + $2000 register)
  ScreenDrawDispatch $254F: jp ($2563 + 3*wCurrentScreen) -> the active
      screen's draw routine (selects its bank, calls it) -- see index below
```

Key mechanisms:
- **Bank switching**: write the bank number to HRAM shadow `$FF8E` *and* the
  MBC5 register `$2000`. Editors live in the switchable window `$4000`-`$7FFF`.
- **Banked calls**: bank 0 sets the bank then `call`s the `$4000`-`$7FFF`
  routine; results come back in `e`/`de` (SDCC convention). Trampolines like
  `CallBank1_794B` ($0061) wrap this.
- **Song access**: editors read/write the working song via `ReadSongByte`
  ($14D1) and SRAM banking — see [`data_model.md`](data_model.md).
- **UI text**: a **string table lives in bank 1** (e.g. `$6F7E` =
  "LITTLE SOUND DJ V9.4.0 © 2000 JOHAN KOTLINSKI", plus "TOO BUSY!",
  "A: RUN …"). `L0DBC` copies strings into the display buffer (`$C6A9`).
- **Config**: `LoadSyncSetting` ($1CFF) pulls song properties (e.g. sync) from
  SRAM into WRAM mirrors (`$CB66`…).

## Screen dispatch — the editor index (bank 0)

The current screen is `wCurrentScreen` = **`$C401`** (0–14). Each frame,
`ScreenDrawDispatch` (`$254F`) does `hl = $2563 + 3*screen; jp hl` into a
15-entry `jp` table (`ScreenDrawTable`, `$2563`). Each target is a stub that
selects the screen's ROM bank (`$FF8E` + `$2000`) and `call`s its draw routine:

| `$C401` | Draw routine | Screen |
|---------|--------------|--------|
| 0 | — | (none) |
| 1 | `$04:$4CE5` | TBD |
| 2 | `$1D:$41DA` | TBD |
| 3 | `$06:$5947` | TBD |
| 4 | `$07:$4574` | TBD |
| 5 | `$05:$4762` | TBD |
| 6 | `$03:$4DE7` | TBD |
| 7 | — | (none) |
| 8 | `$06:$4F18` | TBD |
| 9 | `$1B:$5C17` | **Project** (`DrawProjectScreen`) ✓ |
| 10 | `$1D:$68AB` | TBD |
| 12 | `$06:$41A2` | TBD |
| 13 | `$1C:$57BB` | TBD |
| 14 | `$1B:$4A62` | Help? (bank `$1B` holds the help text) |

This table is the master index for carving editors: each screen's draw routine
is now located by `bank:addr`. Identifying each (and its paired input handler)
is a bounded task — disassemble that bank/address and see which song structure
and VRAM region it touches. A parallel input-dispatch (keyed on `$C401`) routes
button presses to the active screen's edit handler.

## The screens (LSDj manual)

Song, Chain, Phrase, Instrument, Table, Groove, Synth (Wave), Word/Speech,
Project, and File — each edits the corresponding song structure in
[`data_model.md`](data_model.md). Each screen has a **draw** routine (paints the
grid from song data into VRAM/the OAM-shadow) and an **input/edit** routine
(applies button presses to the song structure).

## Systematic carving method (per editor)

1. **Locate.** Find the screen-dispatch table (the index mapping the current
   screen to its handler bank:addr) — reached from the frame processor. Then
   follow the bank trampoline to the handler.
2. **Carve the bank.** `tools/disasm.py --bank N --symbols src/bankNN.sym
   --data <tables> -o src/bankNN.asm`, then `tools/gen_main.py`, then
   `make compare` (must stay green). Mark string/data tables with `--data`.
3. **Name.** Add `addr Name` lines to `src/bankNN.sym` for the editor's draw/
   input routines and the song fields they touch; regenerate; `make compare`.
4. **Document.** Record behaviour here and in `functionality.md`.

`tools/gen_main.py` makes step 2 systematic: any bank with a `src/bankNN.asm`
is INCLUDEd; the rest stay INCBIN. `make compare` is the invariant throughout.

## Bank → role (from `bank_map.md`)

- Engine/UI code: banks `$00`-`$07`, `$1B`-`$1F` (editors are here).
- Bank `$01`: UI **string table** (+ code).
- Sample kits: `$08`-`$1A`, `$20`-`$21`. Empty: `$22`-`$3F`.

## Project editor (in progress)

The Project screen holds whole-song settings. Its **value strings and field-draw
helpers live in bank `$1B`** (now carved → `src/bank1b.asm`, `src/bank1b.sym`):

| Field | Value source | Strings (bank `$1B`) |
|-------|--------------|----------------------|
| TEMPO | song `$3FB4` (BPM) | numeric |
| TRANSPOSE | song property | numeric |
| **SYNC** | `$CB66` (song `$3FBD`) | `$59B7` table — see [`sync.md`](sync.md) |
| └ sub-setting | PS/2 DELAY / TICKS/STEP | `$59E1` / `$59ED` |
| CLONE | deep/slim | `DEEP`/`SLIM` (`$5D4F`, `$6A5C`) |
| LOOK | font + colorset | colorset names `$5707` (Y2K/ZERO/KIKO/…/GOOD) |
| KEY DELAY/REPEAT | settings | numeric |
| PRELISTEN | flag | on/off |
| clock | total/work time | `PLAY %d:` / `WORKED %d:` (`$5C03`) |
| menu | — | `LOAD/SAVE SONG`, `RESET MEMORY` (`$5D59`), `RESET CARTRIDGE?` (`$6A66`) |

Confirmed routines (bank `$1B`):

- **`DrawProjectScreen` (`$5C17`)** — the Project draw routine (dispatch index
  9). Paints each field at a fixed tilemap address via helpers, e.g. the
  LOAD/SAVE-SONG vs RESET-MEMORY menu line, then the SYNC value, then song
  properties read with `ReadSongByte`.
- **`DrawSyncField` (`$58B1`)** — reads `$CB66`, selects the mode string via an
  explicit `cp` chain (`0,1,3,5,6,7`), draws it to VRAM `$9885`.
- **`DrawSyncSubSetting` (`$5B7A`)** — the PS/2 DELAY / TICKS/STEP value beside
  SYNC. Other field-draw helpers: `$5853`, `$569C` (TBD), `$2E33` (numbers).

The Project cursor/selection state is `$C566`/`$C567`. The field *labels*
(TEMPO, SYNC, …) are tiles, not ASCII. Remaining: the Project **input handler**
(edits a field on A+CURSOR — the input-dispatch counterpart of
`ScreenDrawDispatch`) and naming the remaining field helpers. Bank `$1B` is a
**first-pass carve** (SYNC/clock/clone/reset tables marked as data; the help-text
block and some graphics/jump tables still show as byte-safe code).

## Status

| Screen / editor | Edits | Located | Carved | Named |
|-----------------|-------|---------|--------|-------|
| Song (sequence) | `$1290` | — | — | — |
| Chain | `$2080`/`$2880` | — | — | — |
| Phrase | notes/instr/fx | — | — | — |
| Instrument | `$3080` | — | — | — |
| Table | `$1690`/`$3480`/`$3680` | — | — | — |
| Groove | `$1090` | — | — | — |
| Synth (Wave) | `$3EB2`/`$6000` | — | — | — |
| Word / Speech | `$1890` | — | — | — |
| Project | properties `$3FB2` | `$1B:$5C17` (idx 9) | draw carved | `DrawProjectScreen`, `DrawSyncField`, `DrawSyncSubSetting` |
| File (save/load) | SRAM filesystem | bank `$1B` (text) + `ReadSongByte` | first pass | — |

Spine routines named: `MainLoop`, `Start`, `LoadSyncSetting`, `ReadSongByte`,
`ScreenDrawDispatch` (+ `ScreenDrawTable`), the interrupt handlers, and the sync
subsystem ([`sync.md`](sync.md)). The **screen-dispatch index is now mapped**
(above); next: identify each `bank:addr` draw routine's screen, then carve them
and the matching input handlers, working down the table.
