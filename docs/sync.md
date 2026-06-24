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

## Related system helpers (found alongside)

`EnableInterrupts` (`$34F5`, `ei;ret`), `DisableInterrupts` (`$34F7`,
`di;ret`), `SoftReset` (`$34F9`: reload boot id `$CBD9`, `jp Start`),
`SetInterruptEnable` (`$34FF`: update `rIE`, ack `rIF` for newly-enabled IRQs).

## Confidence

Routine boundaries, the state machine, send/receive arming, and the variables
are **confirmed in this ROM's bank 0**. The exact semantics of individual
transport command bytes (`$FD`-`$FF`) and the MIDI/nano/keyboard variants need
runtime tracing (emulator) to pin down fully.
