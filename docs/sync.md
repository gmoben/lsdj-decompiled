# Link cable sync

How LSDj synchronises transport/timing over the Game Boy serial port (link
cable). Recovered from bank 0; routine addresses match `src/bank00.sym`.

## Game Boy serial recap

- `rSB` (`$FF01`) — 8-bit shift register (the byte sent/received).
- `rSC` (`$FF02`) — control: bit 7 = start/active, bit 0 = clock source
  (`1` = internal → **master/provides clock**, `0` = external → **slave/waits**).
- A **serial interrupt** fires when a byte finishes transferring.

LSDj uses a byte-per-event protocol: whoever is master writes a byte to `rSB`
and starts an internal-clock transfer (`rSC=$81`); the slave has armed an
external-clock transfer (`rSC=$80`) and receives it, taking the serial IRQ.

## Sync modes (the `SYNC` project setting)

The Project screen's `SYNC` field selects how LSDj clocks to the outside world.
**All modes use the same serial link port**; what differs is the clock
*direction* and the byte *source/format*. The value is the song `sync_setting`
property (`$3FBD`; WRAM mirror `$CB66`, loaded by `LoadSyncSetting` `$1CFF`).

The option strings are a table in bank `$1B` at `$59B7`, 7 bytes/entry (6 chars
+ NUL): `OFF`, `LSDJ`, `MIDI`, `KEYBD`, `ANA.IN`, `AN.OUT`, then the sub-setting
labels `PS/2 DELAY` and `TICKS/STEP`, then a blank — confirmed in this ROM.

The value encoding is **non-contiguous** — confirmed by the SYNC field draw
routine `DrawSyncField` (bank `$1B` `$58B1`), which reads `$CB66` and selects the
string with an explicit `cp` chain (values `2` and `4` are legacy/unused):

| `$CB66` | Label | Meaning |
|---------|-------|---------|
| `0` | `OFF` | No sync. |
| `1` | `LSDJ` | **Game Boy ↔ Game Boy** link sync. The cart that starts becomes the leader (clock master, `rSC=$81`) and streams position bytes + `$55` handshakes; the other follows (slave, `rSC=$80`). This is the `SerialHandler`/`SendSyncByte`/`ProcessSyncByte` path. |
| `3` | `MIDI` | **MIDI clock** via a MIDI→Game Boy adapter on the link port. LSDj is slave; the adapter clocks bytes in. Press START to wait for MIDI clock. Use 6-tick grooves (24 MIDI clocks/quarter → 24 bytes). |
| `5` | `KEYBD` | **PS/2 keyboard** via adapter — *not* tempo sync. The keyboard sends scan codes over the serial line and LSDj plays it like a piano. Calibrate with the `PS/2 DELAY` sub-setting. (`SerialHandlerTail` `$14B9` special-cases `$CB66==5`.) |
| `6` | `ANA.IN` | **Analog clock in**. External gear (Korg Volca/Monotribe, …) sends pulses; a converter turns each pulse into a serial byte (`$FF`). `TICKS/STEP` sets how many LSDj ticks per incoming pulse. |
| `7` | `AN.OUT` | **Analog clock out**. LSDj is the clock source and emits pulses (bytes) out the link port to drive external gear. `TICKS/STEP` sets the rate. |

`DrawSyncField` writes the chosen string to the screen at VRAM `$9885` (via the
`$C573` draw pointer), then `call $2F21` (string→VRAM).

Sub-settings shown beneath `SYNC` depending on mode: `KEYBD` → **PS/2 DELAY**
(calibration); `ANA.IN`/`AN.OUT` → **TICKS/STEP** (e.g. Monotribe 6, Volca C).

**Unifying mechanism:** LSDj sync is byte-based, ~sync24/DIN-sync style — 24
bytes per quarter note, each "pulse" carried as a serial byte (commonly `$FF`).
So MIDI/analog adapters merely convert their clock to/from serial bytes; the
same `SerialHandler` / `ProcessSyncByte` machinery (and the `$FD`-`$FF`
transport commands) drives every mode. `KEYBD` reuses the serial port for
scan-code input rather than tempo.

## Boot arming — `SerialInit` (`$01AA`)

```
xor a ; ldh [rSC], a        ; idle
ld a, $66 ; ldh [rSB], a    ; preload handshake byte 'f'
ld a, $80 ; ldh [rSC], a    ; arm as SLAVE (start, external clock)
ei
```

Both units boot armed as slaves; one becomes master when it drives a transfer.

## Receive — `SerialHandler` (`$34BD`, the serial IRQ)

State machine on `$CBDE`:

| `$CBDE` | meaning | action on received `rSB` |
|---------|---------|--------------------------|
| `2` | expect a data byte | store byte → `$CBDD`; state → `0` |
| `1` | expect handshake | if byte == `$55` → state `0` (synced) else state `4` (error) |
| other | idle | — |

Then it re-arms `rSC=$80` (slave) and continues into `SerialHandlerTail`
(`$14B9`): clears `$C68B`/`$CB69`, and if sync is active (`$CB65==0`) calls
`ProcessSyncByte`, finally returning via `TimerHandlerReturn`.

## Send (master side)

- `SendSyncByte` (`$3467`): `$CBDE=1`; `rSB ← $CBDC`; `rSC=$81` (internal clock,
  start) — clock one data byte out to the slave.
- `SendSyncHandshake` (`$347A`): `$CBDE=2`; `rSB ← $55`; `rSC=$80`.
- Multi-byte / MIDI-style streams use a loop (`$2486` → `SendByteStep` `$227C`),
  pushing successive bytes.

## Consuming received data — `ProcessSyncByte` (`$234D`)

Runs from the IRQ tail when GB-sync is active (`$CB66==1`, `$CB6B==0`,
`$C571==$FF`). The received byte `$CBDD`:

- `< $FD` → a **position/row** value → stored to `$CB71` (transport follows it).
- `>= $FD` → a **transport command** (`$FD`/`$FE`/`$FF`: start/stop/continue
  class) handled separately (`$2378`).

## Sync state (WRAM)

| Addr | Meaning |
|------|---------|
| `$CBDB` | sync position/index (`GetSyncPosition` `$34F0` reads it) |
| `$CBDC` | byte to transmit (master) |
| `$CBDD` | last byte received (slave); `<$FD` data, `>=$FD` transport |
| `$CBDE` | serial state machine (0 idle / 1 handshake / 2 data / 4 error) |
| `$CB65` | sync active flag (tail runs `ProcessSyncByte` when 0) |
| `$CB66` | sync mode (`1` = Game Boy sync slave) |
| `$CB6B`, `$CB71`, `$CB72`, `$CB78`, `$CB79` | counters / scratch |
| `$C571` | playback state gating sync |
| `$C68B` | cleared every serial IRQ |

The song's configured sync mode (off/slave/master/midi/nano/keyboard — see
[`data_model.md`](data_model.md) song properties) selects which of these paths
runs at startup.

## Magic bytes / control values

| Value | Use |
|-------|-----|
| `$80` | `rSC`: start transfer, external clock (slave) |
| `$81` | `rSC`: start transfer, internal clock (master) |
| `$55` | handshake byte ('U') |
| `$66` | boot preload byte ('f') |
| `$FD`-`$FF` | transport commands in the received stream |

## Transport, per-tick dispatch, and mode configuration

Recovered from bank 0 (verified by control flow + the `rIE`/`wSyncSendGate`
writes); these tie the serial primitives above to playback transport.

- `SyncApplyMode` (`$1F06`) — apply the current `wSyncMode` ($CB66) to the
  hardware: `di`, `wSyncRun=0`, then a `cp` chain selects per mode →
  `SyncSetSendMode`/`SyncSetRecvMode` (plus gate/handshake tweaks for
  KEYBD/ANA.IN). Called by `LoadSyncSetting` and when the SYNC setting changes.
- `SyncSetSendMode` (`$1EC8`) — leader/output config: `wSyncSendGate=1`,
  `$CB6D=0`, enable VBlank IRQ and **disable** the serial-receive IRQ.
- `SyncSetRecvMode` (`$1EE3`) — follower/input config: `wSyncSendGate=0`,
  `$CB6D=8`, **enable** the serial-receive IRQ, `wSyncTick=$FF`, then
  `SendSyncHandshake`.
- `SyncStartTransport` (`$1D29`) — on play-start (LSDJ/MIDI): if LSDJ and idle
  (`wSyncTick=$FF`), become leader (`wSyncRun=1`, `SyncSetSendMode`); otherwise
  announce with `wSyncHandshake=1` + `SendSyncHandshake`.
- `SyncStopTransport` (`$1D6C`) — on play-stop: LSDJ/MIDI reset
  (`wSyncRun=0`, `wSyncTick=$FF`, `wSyncHandshake=0`, `SyncSetRecvMode`);
  AN.OUT writes `$CB68=3`.
- `DispatchSyncClock` (`$1E93`) — runs each sync tick: `wSyncTick=$FF`, and if
  playing dispatch by mode → `SyncTickLSDJ` (1) or `SyncTickMidi` (3),
  preserving the ROM bank.
- `SyncTickLSDJ` (`$1D9D`) / `SyncTickMidi` (`$1E75`) — the per-tick handlers;
  in live mode they reset per-channel play state and redraw the SONG-screen row
  indicators (via bank 2 `$5F5E`), otherwise advance playback (bank 2 `$7C4C`).

## Related system helpers (found alongside)

`EnableInterrupts` (`$34F5`, `ei;ret`), `DisableInterrupts` (`$34F7`,
`di;ret`), `SoftReset` (`$34F9`: reload boot id `$CBD9`, `jp Start`),
`SetInterruptEnable` (`$34FF`: update `rIE`, ack `rIF` for newly-enabled IRQs).

## Confidence

Routine boundaries, the state machine, send/receive arming, and the variables
are **confirmed in this ROM's bank 0**. The exact semantics of individual
transport command bytes (`$FD`-`$FF`) and the MIDI/nano/keyboard variants need
runtime tracing (emulator) to pin down fully.
