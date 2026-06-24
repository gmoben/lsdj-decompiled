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
| Project | properties `$3FB2` | — | — | — |
| File (save/load) | SRAM filesystem | partial (`ReadSongByte`, SRAM access) | — | — |

Spine routines named so far: `MainLoop`, `Start`, `LoadSyncSetting`,
`ReadSongByte`, the interrupt handlers, and the sync subsystem
([`sync.md`](sync.md)). Next: find the screen-dispatch table, then carve the
hosting bank(s) and work down the table above.
