# LSDj 9.4.0 — functionality map

What the code actually *does*, as recovered so far. Bank 0 (the fixed,
non-banked `HOME` region) holds the timing-critical engine and dispatch; the
rest lives in banks `$01`-`$3F` reached via trampolines. Names here match the
labels in `src/bank00.sym` / `src/bank00.asm`.

Method: control-flow from the hardware entry points (reset, the 5 interrupt
vectors) plus hardware-register usage (the disassembly renders `$FFxx` as
`hardware.inc` names, so subsystems are greppable).

## Interrupt model

| Vector | Addr | Handler | Role |
|--------|------|---------|------|
| VBlank | `$40` | `VBlankHandler` (`$183A`) | reloads `rTIMA` from tempo `$C578`; can raise the timer IRQ (`set 2, [rIF]`); frame timing |
| LCD STAT | `$48` | — | unused (`reti`) |
| Timer | `$50` | `TimerHandler` (`$0391`) | **sound / playback engine tick**; exits via `TimerHandlerReturn` (`$0662`) |
| Serial | `$58` | `SerialHandler` (`$34BD`) | Game Boy **sync** receive: `rSB` → `$CBDD`, mode in `$CBDE` |
| Joypad | `$60` | — | unused (`reti`) |

The tempo system is the VBlank/Timer pair: each VBlank reloads `rTIMA` from a
WRAM tempo value and the timer overflow fires `TimerHandler`, which advances the
song. This is why playback is rock-steady regardless of frame workload.

## Boot / init — `Start` (`$0150`, via `jp` from `Entry`)

```
di                       ; no interrupts during setup
ld sp, $e000             ; stack at top of WRAM
clear WRAM $C000-$DFFF, HRAM $FF80-$FFFF
$CBD9 <- boot A          ; CGB/DMG hardware id (from boot ROM)
$CBDA <- boot B          ; AGB id
call DisableLCD          ; safe to touch VRAM
reset rSCY/rSCX/rSTAT/rWY ; clear scroll, LCD status, window
...
```

## Subsystems

- **Sound engine** — timer-driven, entry `TimerHandler` (`$0391`) with helpers
  around `$0662`/`$06A5`/`$066D`. Writes the audio registers (`rAUD1*`-`rAUD4*`,
  `rAUDVOL`/`rAUDTERM`/`rAUDENA`) and wave RAM (`rAUD3WAVE_*`). Reads a tick
  counter at `$C8F4` and an HRAM flag `$FF90`.
- **Video / LCD** — `DisableLCD` (`$349B`): waits for VBlank via `rLY` then
  clears `rLCDC` bit 7. `OAMDMARoutine` (`$34B2`): triggers OAM DMA from the
  shadow buffer at `$DD00`; copied into HRAM and called there (DMA requires
  HRAM execution).
- **Input** — `ReadJoypad` (`$18BF`): standard two-pass matrix read (d-pad then
  buttons via `rJOYP`), returns a combined button bitmask.
- **Sync (link cable)** — `SerialHandler` (`$34BD`) plus serial setup near
  `$01AB` (`rSC`/`rSB`). Supports LSDj's master/slave clock sync.
- **Song data access** — `ReadSongByte` (`$14D1`): an SDCC function
  `(bank, addr)` that `di`s, selects the SRAM bank (`$4000`), enables RAM
  (`$0A`→`$0000`), reads `[addr]`, then disables RAM. This is how the engine
  reads the working song out of SRAM banks 0-3. See
  [`data_model.md`](data_model.md) for the song structures it indexes and
  [`save_format.md`](save_format.md) for the `.sav` filesystem.
- **Banking / dispatch** — MBC5 ROM bank register at `$2000`. Bank 0 contains
  16 bank-switch sites; cross-bank calls use the trampoline pattern
  `ld a, N` / `ld [$2000], a` / `jp|call $4000-$7FFF`, e.g.
  `CallBank1_794B` (`$0061`) selects bank 1 and jumps to `$794B`. In-bank
  dispatch additionally uses 16 `jp hl` jump tables (SDCC `switch`).

## Provisional RAM / HRAM map

| Addr | Meaning |
|------|---------|
| `$C578` | timer reload (tempo); copied to `rTIMA` each VBlank |
| `$C8F4` | sound tick counter (decremented in `TimerHandler`) |
| `$CBD9` / `$CBDA` | boot hardware id (CGB/DMG, AGB) saved at init |
| `$CBDD` | last received sync byte; `$CBDE` sync mode |
| `$DD00` | OAM shadow buffer (OAM DMA source) |
| `$FF90` | HRAM flag tested by `TimerHandler` |

(WRAM addresses are not yet lifted into `constants/` symbols — that is the next
labelling pass.)

## Dispatch & embedded tables

Bank 0 mixes code with **pointer/jump tables** that the first-pass linear
disassembly currently shows as bogus instructions, e.g. near `$09FE` and `$0A84`
(arrays of 16-bit little-endian bank-0 addresses such as `$029D`, `$02B0`,
`$079C`) consumed by nearby `jp hl` dispatch. Marking these ranges as `--data`
and decoding the address arrays is the refinement that will turn the `jp hl`
sites into a readable call graph (and shrink the false "illegal opcode" counts).

## Next

- Mark bank 0's embedded jump tables as data; resolve `jp hl` dispatch targets.
- Follow the bank-switch trampolines to start carving the engines/editors in
  banks `$01`-`$07` and `$1B`-`$1F`.
- Locate the load/save/compress/format routines and the per-screen editors
  (chain/phrase/instrument/table/groove/word, plus filename **text input**).
- Lift the RAM/HRAM addresses above and the song offsets (`constants/song.inc`)
  into the disassembly via symbol substitution (mirrors the hardware registers).
