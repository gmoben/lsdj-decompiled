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
| VBlank | `$40` | `VBlankHandler` (`$183A`) | reloads `rTIMA` from `wTimerReload` (`$C578`, derived from the tempo); can raise the timer IRQ (`set 2, [rIF]`); frame timing |
| LCD STAT | `$48` | — | unused (`reti`) |
| Timer | `$50` | `TimerHandler` (`$0391`) | **sound / playback engine tick**; exits via `TimerHandlerReturn` (`$0662`) |
| Serial | `$58` | `SerialHandler` (`$34BD`) | Game Boy **sync** receive: `rSB` → `wSyncReceiveByte` (`$CBDD`), state machine in `wSyncState` (`$CBDE`) |
| Joypad | `$60` | — | unused (`reti`) |

The tempo system is the VBlank/Timer pair: each VBlank reloads `rTIMA` from a
WRAM tempo value and the timer overflow fires `TimerHandler`, which advances the
song. This is why playback is rock-steady regardless of frame workload.

## Boot / init — `Start` (`$0150`, via `jp` from `Entry`)

```
di                       ; no interrupts during setup
ld sp, $e000             ; stack at top of WRAM
clear WRAM $C000-$DFFF, HRAM $FF80-$FFFF
wBootHardwareId  ($CBD9) <- boot A  ; CGB/DMG hardware id (from boot ROM)
wBootAgbId ($CBDA) <- boot B  ; AGB id
call display_off         ; GBDK library routine; safe to touch VRAM
reset rSCY/rSCX/rSTAT/rWY ; clear scroll, LCD status, window
...
```

## Subsystems

- **Sound engine** — timer-driven, entry `TimerHandler` (`$0391`), exiting via
  `TimerHandlerReturn` (`$0662`); `SoundModulateWavFrequency` (`$066D`) applies WAV-channel
  frequency modulation. Writes the audio registers (`rAUD1*`-`rAUD4*`,
  `rAUDVOL`/`rAUDTERM`/`rAUDENA`) and wave RAM (`rAUD3WAVE_*`). Reads the tick
  counter `wSoundTick` (`$C8F4`) and `hGameboyType` (`$FF90`). The same handler
  streams **kit samples**: each tick an active kit voice copies 16 bytes from its
  banked ROM read pointer into `hWavUploadBuffer` (→ WAV RAM), looping or stopping
  at the loop end (`wKitVoicesActive`, `wKit1*`/`wKit2*` in `constants/ram.inc`).
  The sequencer/FX/instrument side of the engine lives in bank `$02` — see
  [`reversal.md`](reversal.md) and [`data_model.md`](data_model.md).
  - *Per-channel pitch / vibrato / frequency* (bank 0): each frame
    `SndStepPitchVib{PU1,PU2,Wav,Noi}` (`$19A6`/`$1A26`/`$1AA7`/`$1CBA`) advance
    the pitch slide (`wChannelPitchRate`→`wChannelPitchCurrent`, clamped to
    `wChannelPitchTarget`) and the vibrato phase, the latter via the shared waveform
    evaluator `SoundCalculateVibratoOffset` (`$1986`, ctrl table `$0200` + dispatch `$0300`).
    `SndWriteFreq{PU1,PU2,Wav}` (`$1BA7`/`$1BF6`/`$1C3D`) then fold note +
    transpose + finetune + vibrato + pitch into a 16-bit period via
    `SoundInterpolatePeriod` (`$1B28`, period-table lookup with fractional interpolation)
    and write `wChannelFrequency` + `rAUD{1,2,3}LOW/HIGH/LEN`.
    `SndUpdatePitchModFlag{PU1,PU2,Wav}` (`$1CA8`/`$1C96`/`$1C84`, dispatched by
    `SoundUpdatePitchModulationFlagCurrent` `$1C7C`) cache a "pitch is modulating" flag at
    `$C3F9`-`$C3FB`.
  - *Work-time clock* — `TickWorkTimeClock` (`$1815`) advances `wWorkTimeAccumulator`
    (`$C69D`), wrapping at `$0E00` into `wWorkTimeTicks` (drives the song's
    play-time counter in SRAM).
- **Video / LCD** — `display_off` (`$349B`, GBDK 2.95 library routine,
  byte-identical to its `crt0.s`): waits for VBlank via `rLY` then clears
  `rLCDC` bit 7. `OamDmaRoutine` (`$34B2`): triggers OAM DMA from the shadow
  buffer `wOamBuffer` (`$DD00`); copied into HRAM and called there (DMA requires
  HRAM execution).
- **Input** — `ReadJoypad` (`$18BF`): standard two-pass matrix read (d-pad then
  buttons via `rJOYP`), returns a combined button bitmask.
- **Sync (link cable)** — `SerialHandler` (`$34BD`), `InitializeSerial` (`$01AA`),
  `SendSyncByte`/`SendSyncHandshake` (`$3467`/`$347A`), `ProcessSyncByte`
  (`$234D`). Each sync tick `DispatchSyncClock` (`$1E93`) resets `wSyncTick` and
  routes to the active mode handler (LSDJ/MIDI). Master/slave clock sync over the
  serial port — **fully written up in [`sync.md`](sync.md)**.
- **Song data access** — `ReadSongByte` (`$14D1`): an SDCC function
  `(bank, addr)` that `di`s, selects the SRAM bank (`$4000`), enables RAM
  (`$0A`→`$0000`), reads `[addr]`, then disables RAM. This is how the engine
  reads the working song out of SRAM banks 0-3. See
  [`data_model.md`](data_model.md) for the song structures it indexes and
  [`save_format.md`](save_format.md) for the `.sav` filesystem.
- **Banking / dispatch** — MBC5 ROM bank register at `$2000`, shadowed in
  `hCurrentBank` (`$FF8E`). Cross-bank calls use either an inline trampoline
  (`ld a, N` / `ld [$2000], a` / `jp|call $4000-$7FFF`, e.g. the per-screen
  `DispatchScreenDraw_<Screen>` stubs, or `CrashTrampoline` (`$0061`), which selects
  bank 1 and jumps to `CrashHandler` `$794B`, the "DONT PANIC!" crash screen),
  or — the dominant form — the far-call helpers `FarCall` (`$179A`),
  `FarCallArgument` (`$17BB`) and `FarCallByte` (`$0BFE`): the caller pushes an
  immediate address + bank and the helper pages the bank and `jp hl`s. In-bank
  dispatch uses `jp hl` jump tables (SDCC `switch`); all of them are resolved
  and followed by `tools/coverage.py` (see [`reversal.md`](reversal.md)).

## RAM / HRAM map

WRAM/HRAM variables are named in
[`../constants/ram.inc`](../constants/ram.inc) (~800 addresses, arrays tagged
`[N]`; the few raw addresses left are listed in [`reversal.md`](reversal.md)).
The addresses first identified on this page:

| Addr | Name | Meaning |
|------|------|---------|
| `$C578` | `wTimerReload` | timer reload derived from the tempo; copied to `rTIMA` each VBlank |
| `$C8F4` | `wSoundTick` | sound/playback tick counter (`TimerHandler`) |
| `$CBD9` / `$CBDA` | `wBootHardwareId` / `wBootAgbId` | boot hardware ids (CGB/DMG, AGB) saved at init |
| `$CBDD` / `$CBDE` | `wSyncReceiveByte` / `wSyncState` | last received sync byte / serial state machine |
| `$DD00` | `wOamBuffer` | OAM shadow buffer (OAM DMA source) |
| `$FF8E` | `hCurrentBank` | shadow of the selected ROM bank |
| `$FF90` | `hGameboyType` | 0 = DMG, 1 = CGB |

## Embedded tables

The jump/pointer tables that the first linear disassembly showed as bogus
instructions are now carved as data (`db`) by `tools/carve_clean.py` and
resolved by the coverage trace. One correction to the original guess here: the
bytes at `$09B9`-`$0A90` (between `WriteInstrumentByte` and `ReadInstrumentByte`) are
**not** a jump table but a 108-entry 16-bit ramp (`$0000`, `$0013`, `$0026`, …
`$07FC`): `UnusedRampTable`, since no code reads it.

## Where to go next

Function- and variable-level coverage is complete (see
[`reversal.md`](reversal.md) for the metric and method). What remains on the
asm side is naming refinement. The matching-C spike has its first matches
(`DrawMessageBar` and two wrappers, `c/bank01/`); next is validating it on
harder functions — see [`toolchain.md`](toolchain.md).
