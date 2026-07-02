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
- These live in the **File screen** code, in **bank `$1C`** (`DrawFileScreen`
  `$57BB`) alongside the song (de)compressor.

### Decompressor — located in bank `$1C` (the LOAD path)

| Routine | Addr | Role |
|---------|------|------|
| `DecompressCommand` | `$46E1` | read a byte and act: `$E0` (escape→literal), `$F0` (default-wave run), `$F1` (default-instrument run), `$FF` (end), else literal |
| `ReadCompressedByte` | `$7402` | fetch the next stream byte via the SRAM block pointer (`$C97B`/`$C97C`) |
| `WriteSongByte` | `$74E0` | write a decompressed byte into the working song |
| `EmitDefaultWave` | `$753E` | `$F0` run: emit a default wavetable frame |
| `EmitDefaultInstrument` | `$758E` | `$F1` run: emit a default instrument |

The output pointer (`$C97D`/`$C97E`) is filled until it reaches `$8000` (the
32 KiB song size). `DecompressSpecial` (`$46E1`) is the `$E0`-escape sub-handler
(`$F0`/`$F1`/`$FF`/literal-`$E0`); the main loop also handles `$C0` RLE and
literals.

### Compressor — located in bank `$1C` (the SAVE path)

| Routine | Addr | Role |
|---------|------|------|
| `CompressSong` | `$441F` | compress the 32 KiB working song into blocks; inits `$C97D=0`, reads the song from `$A000` (`$441E` is the previous routine's `ret`) |
| `WriteCompressedByte` | `$715B` | emit one byte to the compressed output stream |
| `ReadWorkingSongByte` | `$711F` | read a byte of the working song (compressor input) |

The compressor confirms the codec **exactly matches the table above** (the
liblsdj model): it emits `$C0 $C0` / `$E0 $E0` (literal escapes), `$C0 v n`
(RLE), `$E0 $F0 n` (default-wave run), `$E0 $F1 n` (default-instrument run), and
`$E0 $FF` (end). Compress (`$441E`+) and decompress (`$45xx`/`$46xx`) sit next to
each other in bank `$1C` and share the loop tail at `$464F`.

## Song-format upgrade (migration on load) — bank `$1C`

Saved songs carry a format-version byte at working-song offset `$bfff` (byte 3
of the synth-params/`'rb'` area). After a song is loaded, `FileUpgradeSong`
(`$6FC7`) migrates it forward to the current version:

- read the stored version `v` from `$bfff`; if `v >= $16` (22) it's current — skip.
- otherwise draw "UPGRADING..." and, for `n = v .. $15`, run `FileUpgradeV<n>`
  (a per-version data patch), then stamp the version byte to `$16`.

Each `FileUpgradeV<n>` (`$61D6`-`$6F73`) applies that version's data change —
mostly instrument-field fixups via `FileForEachInstrType` (`$60F5`: iterate the
64 instruments whose type byte-0 matches a value and invoke a callback) plus
`FileReadInstrByte`/`FileWriteInstrByte` (`$60CC`/`$60E0`). Version `$14` needs
no change (no step). This is how old `.sav`/`.lsdsng` files keep working across
LSDj releases.

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
without the SRAM directory). See external tooling (e.g. liblsdj/lsdsng) that
reads/writes these.

## Sources / confidence

- SRAM/header/compression: liblsdj, pylsdj, and the LSDj wiki — **documented**.
- Kit banks, names, and magic: **confirmed in this ROM**.
- ROM routines: compressor/decompressor and the **song-format upgrader**
  (`FileUpgradeSong $6FC7`) are located in bank `$1C` (above). Block
  allocation-table management routines: not yet fully traced (TBD).
