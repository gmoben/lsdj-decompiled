# LSDj data model — the song format

How LSDj represents a song in memory: Chains, Phrases, Notes, Instruments,
Synths, Tables, Grooves, Words (speech), the sequence, and song properties.

The **working song** is a 32 KiB structure. At runtime it lives in **SRAM**
(`$A000`-`$BFFF`, battery-backed) as four 8 KiB banks; on a `.sav` it is the
first 32 KiB (see [`save_format.md`](save_format.md)). Runtime playback state
(cursors, tick counters, e.g. `$C578` tempo, `$C8F4` tick — see
[`functionality.md`](functionality.md)) lives separately in WRAM.

Layout is the canonical LSDj song format (cross-checked with
[liblsdj](https://github.com/stijnfrishert/libLSDJ) and
[pylsdj](https://github.com/alexras/pylsdj)); offsets validate against the
reserved-region markers at `$1030`, `$1FBA`, `$2000`, `$5FE0`, `$7FF2`. The
sample-kit location was confirmed directly in this ROM (magic `60 40` + names).

## How a song fits together (tree)

```
Sequence (song)            256 steps x 4 channels (PU1, PU2, WAV, NOI)
  -> Chain                 up to 128 chains; a list of phrases + transposes
       -> Phrase           up to 255 phrases; 16 steps of note/instr/fx/val
            -> Note        pitch
            -> Instrument  one of 64; how the note sounds
                 -> Table  one of 32; per-tick fx/transpose/volume macro
                 -> Synth   one of 16 softsynths -> Wave frames (wavetable)
```

## Song memory map (offsets within the 32 KiB buffer)

| Offset | Size | Field | Feature |
|--------|------|-------|---------|
| `$0000` | `$0FF0` | `phrase_notes` [255][16] | **Notes** in every phrase |
| `$0FF0` | `$0040` | `bookmarks` [64] | screen bookmarks |
| `$1030` | `$0060` | reserved (96) | |
| `$1090` | `$0200` | `grooves` [32][16] | **Grooves** |
| `$1290` | `$0400` | `song` [256][4] | **Sequence**: chain # per channel per step |
| `$1690` | `$0200` | `table_envelopes` [32][16] | **Tables** (envelope/amp column) |
| `$1890` | `$0540` | `words` [42][16][2] | **Speech** allophones (SPEECH instr) |
| `$1DD0` | `$00A8` | `word_names` [42][4] | speech word names |
| `$1E78` | `$0002` | `mem_init_flag_1` (`'rb'`) | init/format marker |
| `$1E7A` | `$0140` | `instrument_names` [64][5] | **Instrument** names |
| `$1FBA` | `$0046` | reserved (70) | |
| `$2000` | `$0020` | reserved (32) | (SRAM bank 1 start) |
| `$2020` | `$0020` | `table_alloc_table` [32] | which **Tables** are in use |
| `$2040` | `$0040` | `instr_alloc_table` [64] | which **Instruments** are in use |
| `$2080` | `$0800` | `chain_phrases` [128][16] | **Chains**: phrase # per step |
| `$2880` | `$0800` | `chain_transposes` [128][16] | **Chains**: transpose per step |
| `$3080` | `$0400` | `instruments` [64][16] | **Instrument** params (16 B each) |
| `$3480` | `$0200` | `table_transposes` [32][16] | **Tables** (transpose column) |
| `$3680` | `$0400` | `table_cmd1` (fx[32][16], then val[32][16] at `$3880`) | **Tables** (command column 1) |
| `$3A80` | `$0400` | `table_cmd2` (fx[32][16], then val[32][16] at `$3C80`) | **Tables** (command column 2) |
| `$3E80` | `$0002` | `mem_init_flag_2` (`'rb'`) | init marker |
| `$3E82` | `$0020` | `phrase_alloc_table` [256b] | which **Phrases** are in use |
| `$3EA2` | `$0010` | `chain_alloc_table` [128b] | which **Chains** are in use |
| `$3EB2` | `$0100` | `softsynth_params` [16][16] | **Synths** (16 B each) |
| `$3FB2` | ~`$4E` | work time, tempo, transpose, sync, font, colour set, … (below) | **Song properties** |
| `$4000` | `$0FF0` | `phrase_fx` [255][16] | **Phrase** command column |
| `$4FF0` | `$0FF0` | `phrase_fx_val` [255][16] | **Phrase** command values |
| `$5FE0` | `$0020` | reserved (32) | |
| `$6000` | `$1000` | `wave_frames` [16][16][32] | **Synth** wavetables (nibbles) |
| `$7000` | `$0FF0` | `phrase_instruments` [255][16] | **Phrase** instrument column |
| `$7FF0` | `$0010` | `mem_init_flag_3` (`'rb'`), version | format version (`$7FFF`) |

### Song properties (bank 1, `$BFB2`)

The manual's Appendix C, checked against the code (`tools/gen_sym.py`'s
`SRAM_MAP`, `c/include/lsdj.h`'s `SRAM_X`/`SramX`):

| Song offset | Name | Holds |
|-------------|------|-------|
| `$3FB2` | `SramPropertyWorkTime` (`SramProperties`, the block) | work time: hours, minutes |
| `$3FB4` | `SramPropertyTempo` | tempo |
| `$3FB5` | `SramPropertyTranspose` | the transpose (`wSongTranspose`; the manual's "tune setting") |
| `$3FB6` | `SramPropertyTotalTime` | total time: days, hours, minutes, and a fourth byte kept with them across a load |
| `$3FBA` | `SramPropertyKeyDelay` | key delay |
| `$3FBB` | `SramPropertyKeyRepeat` | key repeat |
| `$3FBC` | `SramPropertyFont` | font (`wFont`) |
| `$3FBD` | `SramPropertySyncMode` | sync mode |
| `$3FBE` | `SramPropertyColorSet` | colour set |
| `$3FBF` | `SramPropertySyncParameter` | sync parameter (e.g. analog in ticks/step) |
| `$3FC0` | `SramPropertyCloneMode` | clone mode (0 deep, 1 slim) |
| `$3FC1` | `SramPropertyFileChanged` | the top bar's `!` once the song is edited |
| `$3FC2` | `SramPropertyPowerSave` | power save |
| `$3FC3` | `SramPropertyPrelisten` | prelisten |
| `$3FC4` | -- | two bytes (the manual: "synths locked?") |
| `$3FC6` | `SramPropertyColumnInstruments` | the last instrument per channel (`wColumnInstrument`) |
| `$3FCA` | -- | a copy of the work time (`MigrateTempoProperties` writes it, `FileCalculateCompressedSize` subtracts it) |
| `$3FCC` | `SramPropertyTempoMode` | tempo mode (`wTempoMode`) |

## Structures

- **Sequence / "song"** (`$1290`): 256 steps; each step has a chain number for
  PU1, PU2, WAV, NOI (`$FF` = empty). This is the top-level arrangement.
- **Chain** (`$2080`/`$2880`): 16 steps, each a phrase number + a transpose.
- **Phrase** (`$0000` notes, `$7000` instruments, `$4000`/`$4FF0` fx+val): 16
  steps; columns = note, instrument, command, command value (matches the Phrase
  screen).
- **Instrument** (`$3080`, 16 bytes): byte 0 low 2 bits = type (0=pulse 1=wave
  2=kit 3=noise). Full per-type field/bit offsets are in
  [`constants/song.inc`](../constants/song.inc), validated against this ROM's
  `.sav`. Note 9.x differs from pylsdj's older
  layout: **pulse** has a 3-stage envelope (`$1`/`$9`/`$a`), command-rate (`$8`)
  and finetune (`$b`) where pylsdj saw padding. Shared bytes: `$6`=`[table_on:1][table:4]`,
  `$7`=output/pan (+duty for pulse).
- **Table** (`$1690` env, `$3480` transpose, `$3680`/`$3A80` two command cols):
  16-step macros run per tick; referenced by instruments.
- **Synth (softsynth)** (`$3EB2`, 16 bytes): waveform, filter type/cutoff/Q,
  distortion, phase, start/end sound params. Renders into `wave_frames`
  (`$6000`): 16 synths × 16 waves × 32 nibble frames.
- **Groove** (`$1090`): 16-step tick patterns controlling playback speed.
- **Words / Speech** (`$1890` + names `$1DD0`): 42 words of allophones for the
  SPEECH instrument.
- **Allocation tables**: bitmaps/byte-tables marking which chains/phrases/
  instruments/tables are in use — drives "next free slot" and memory display.
- **FX commands** (phrase & table command columns): `A C D E F G H K L M O P R
  S T V W Z` (+ Arduinoboy `N X Q Y`).

## SRAM banking (runtime access)

The 32 KiB song = SRAM banks 0-3, each mapped at `$A000`-`$BFFF`:

| Song offset | SRAM bank | CPU address |
|-------------|-----------|-------------|
| `$0000`-`$1FFF` | 0 | `$A000` + offset |
| `$2000`-`$3FFF` | 1 | `$A000` + (offset-`$2000`) |
| `$4000`-`$5FFF` | 2 | `$A000` + (offset-`$4000`) |
| `$6000`-`$7FFF` | 3 | `$A000` + (offset-`$6000`) |

So e.g. `chain_phrases` (`$2080`) is read at `$A080` after selecting SRAM bank 1
(MBC5 RAM-bank register `$4000`-`$5FFF`). Bank 0's 5 `ld [$4000], a` writes are
these SRAM-bank selects. Symbolic offsets live in
[`../constants/song.inc`](../constants/song.inc).

## Runtime playback cursors (engine RAM ↔ this map)

The bank `$02` sound engine walks the song tree once per channel; its per-channel
state lives in the `$C1xx` WRAM page (see [`../constants/ram.inc`](../constants/ram.inc)).
Verified against a live RAM dump cross-checked with this map:

| RAM var | Addr | Holds | Indexes |
|---------|------|-------|---------|
| `wChannelSongRow` | `$C200` | current song row | `song` (`$1290`), top of the hierarchy |
| `wChannelChain` | `$C170` | current chain # | `= song[songRow][ch]` |
| `wChannelChainRow` | `$C17C` | row in chain (0-15) | into `chain_phrases`/`chain_transposes` |
| `wChannelPhrase` | `$C168` | current phrase # | `= chain_phrases[chain][chainRow]` |
| `wChannelPhraseRow` | `$C16C` | note step in phrase (0-15) | into the phrase columns |
| `wPhraseCommandPointer` | `$C110` | 16-bit SRAM cursor | `phrase_notes`(b0)/`phrase_fx`(b2) |
| `wPhraseCommandValuePointer` | `$C128` | 16-bit SRAM cursor | `phrase_fx_val` (`$AFF0`+) |
| `wChannelInstrumentPointer` | `$C140` | 16-bit SRAM cursor | `phrase_instruments` (`$B000`+) |

The bank `$02` walkers: `SoundAdvanceSongRow` (`$48D2`) reloads `wChannelChain` from
`song` when a chain ends (with LSDj's loop-back scan); `SoundReadSongChain` (`$5EAD`)
reads `song[wChannelSongRow][ch]`; `SoundReadChainPhrase` (`$5E7C`) computes `wChannelPhrase`
from `chain_phrases[wChannelChain*16 + wChannelChainRow]` in SRAM bank 1.

**Pitch.** Notes read from `phrase_notes` are turned into GB periods by `SoundNoteToPeriod`
(bank `$02` `$49B7`): `wChannelFrequency[ch] (16-bit, $C0F4) = wNotePeriods[note + wNoteTranspose - 1]`.
`wNotePeriods` (`$CF28`) is a 16-bit GB-period table built in WRAM at runtime (so it
tracks master tuning), and `SoundWriteChannelFrequency` writes `wChannelFrequency` to `rAUDxLOW/HIGH`
each tick. Verified against a live dump: `wChannelFrequency` = `$0706/$0672/$0511` for PU1/PU2/WAV,
each value present in the `$CF28` table.

**Instruments.** A phrase step's instrument number (read via `wChannelInstrumentPointer` from
`phrase_instruments`, `$7000`) selects a 16-byte record in `instruments` (`$3080`).
On note-on the engine copies that record verbatim into `wCurrentInstrument` (`$C0D0`),
then `SoundSetupChannel` distributes its bytes to the `rAUDx` register shadows
(`$C2D8`+) per channel type. (The per-byte field layout is type-specific — see
pylsdj/liblsdj for the bitfields.)

## Sources / confidence

- Structure & offsets: pylsdj `bread_spec`, liblsdj — **documented**, offsets
  cross-validated against reserved markers **and a live engine RAM dump** (the
  `$C1xx` playback cursors above resolve to these tables exactly).
- Sample kits at banks `$08`-`$1A`,`$20`-`$21`: **confirmed in this ROM**.
- The routines that read/write these structures are named throughout the
  disassembly (e.g. `ReadSongByte`, the bank `$02` walkers above, the per-screen
  editors), and the working-song structures themselves are exported as debugger
  symbols by `tools/gen_sym.py` (37 `Sram*` names: `SramPhraseNotes`,
  `SramGrooves`, `SramSequence`, `SramInstruments`, …) so SRAM accesses resolve
  by name in Emulicious/BGB/SameBoy.
