# LSDj saving / loading — the `.sav` filesystem

How LSDj stores up to 32 songs in the cartridge's 128 KiB battery SRAM, the
allocation/compression scheme, the sample-kit ROM format, and `.lsdsng` export.
(Container/compression per liblsdj/pylsdj — documented; kit layout confirmed in
this ROM. The specific ROM routines are noted where known, else TBD.)

## SRAM layout (128 KiB = the `.sav`)

| Offset | Size | Content |
|--------|------|---------|
| `$00000` | `$8000` | **Working memory song** (the 32 KiB live song; see [`data_model.md`](data_model.md)) |
| `$08000` | `$0200` | **Header block** (filesystem directory) |
| `$08200` | `$17E00` | **Block storage**: 191 × 512-byte blocks of compressed songs |

At runtime SRAM is paged into `$A000`-`$BFFF` (MBC5 RAM-bank register
`$4000`-`$5FFF`): banks 0-3 = the working song, banks 4-15 = header + blocks.

## Header block (`$8000`, the directory)

| Block offset | Size | Field |
|--------------|------|-------|
| `0` | `32×8` | project filenames (8 chars each) |
| `256` | `32` | project version bytes |
| `288` | `30` | reserved |
| `318` | `2` | **init check `'jk'`** — validates SRAM is LSDj-formatted |
| `320` | `1` | active/working project index (`$FF` = none) |
| `321` | `191` | **block allocation table** — block → project index (`$FF` = free) |

On boot LSDj checks the `'jk'` marker; if absent it formats SRAM. The working
song also carries `'rb'` init markers at `$1E78`/`$3E80`/`$7FF0` (see data model).

## Song compression

Each saved song is a chain of 512-byte blocks; the block allocation table says
which blocks belong to which project, and a block-switch command links them. The
byte stream (liblsdj algorithm):

| Marker | Meaning |
|--------|---------|
| `$C0 v n` | **RLE**: output byte `v`, `n` times |
| `$C0 $C0` | literal `$C0` |
| `$E0 $E0` | literal `$E0` |
| `$E0 $F0 n` | output the **default wavetable** `n` times (16 B each) |
| `$E0 $F1 n` | output the **default instrument** `n` times (16 B each) |
| `$E0 b` | **switch to block `b`** and continue decompressing |
| `$E0 $FF` | **end of file** |
| any other byte | literal |

Compression exploits that songs are mostly default instruments/waves and long
runs of `$00`/`$FF` (empty phrases/chains).

## Loading / saving (control flow)

- **Load**: select the song's first block, decompress through the block chain
  into the 32 KiB working song (SRAM banks 0-3), set the active-project index.
- **Save**: compress the working song, allocate free blocks (allocation table),
  write the chain, update filename/version/active index.
- These live in the **File screen** code (project screen → LOAD/SAVE). Bank 0
  performs the SRAM bank selects (`ld [$4000], a`); the file-management logic is
  reached via the bank trampolines — exact routines **TBD** (next pass).

## Sample-kit ROM format (confirmed in this ROM)

Kits occupy one 16 KiB ROM bank each, mapped to `$4000` when selected. Banks
**`$08`-`$1A` and `$20`-`$21`** (21 kits): TR-606/707/727/808/909, CR-78,
CR8000, DR-55, DR-110, DRUMUL, DMX, KR-55, LINNDR, FR-1, TOM, ACIEED, ANIMLZ,
AA-EH, EL-KK2, KK3-TH, TT-ZZ.

| Bank offset | Field |
|-------------|-------|
| `$0000` | magic `60 40` |
| `$0002` | 15 × sample end address (u16 LE, relative to `$4060`) |
| `$0022` | `00 00` |
| `$0024` | 15 × 3-char sample name (`-` padded) |
| `$0052` | 6-char kit name |
| `$005C` | force-loop flags |
| `$0060` | 4-bit PCM **sample data** (to `$3FA0` max) |

`SAMPLE_START_ADDRESS = $4060`. Banks `$22`-`$3F` are empty (`$FF`) — spare room
for user kits (added by LSDPatcher).

## `.lsdsng` (single-song export)

`name` (8 bytes) + `version` (1 byte) + the compressed song blocks (same codec,
without the SRAM directory). See [`gmolsdj`](../../gmolsdj) for tooling that
reads/writes these.

## Sources / confidence

- SRAM/header/compression: liblsdj, pylsdj, and the LSDj wiki — **documented**.
- Kit banks, names, and magic: **confirmed in this ROM**.
- ROM routines for load/save/compress/format: **not yet located** (TBD).
