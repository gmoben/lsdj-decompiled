# Toward 100% reversal — methodology & coverage

The build already reproduces the ROM **100% byte-for-byte** (`make compare`).
"100% reversal" is the stronger goal: every byte classified as code or data,
all code disassembled with meaningful labels, all data structured.

## What counts

The 1 MiB ROM is 64 × 16 KiB banks:

- **~13 code/data banks** — the actual program (`$00`-`$07`, `$1B`-`$1F`, plus
  string/helper banks `$01`/`$02`). **This is the reversal target.**
- **21 sample-kit banks** (`$08`-`$1A`, `$20`-`$21`) — PCM data; identified and
  named (see `bank_map.md`), reversed as data.
- **30 empty banks** (`$22`-`$3F`) — `$FF` fill / spare.

## The coverage metric

`tools/coverage.py` measures **reachable code** via recursive-descent from every
control-transfer entry point:

- reset/interrupt vectors and `Entry`;
- **bank-switch trampolines** (`ld a,N; ld [$2000],a; call/jp $4000-$7FFF`)
  scanned ROM-wide → per-bank entry addresses;
- the **dispatch tables** (screen draw/update) and every hand-named symbol
  (`src/*.sym`);
- a **bank-aware global trace** (`global_trace`): state is `(cpu, pending_bank)`;
  a window byte's bank is implied by its physical offset, and `call/jp
  $4000-$7FFF` resolve against the pending bank set by the most recent `$2000`
  write — so the trace follows control flow **through** bank switches.
- it follows direct branches/calls, **jump tables** (`jp hl` over a `jp`-list or
  a 2-byte address array), and LSDj's **farcall helper** `L179A` (the caller
  pushes `addr` then `bank`; the helper switches bank and calls). Every
  `call $179A` site's `(bank, addr)` pair is extracted as an entry point.
- conservative throughout (jump-table targets must be in range and start on a
  legal opcode), so the figure is a sound lower bound.

### Current coverage

Run `tools/coverage.py`. As of writing, **71%** of the code/data banks trace as
reachable code:

| bank | code% | | bank | code% |
|------|-------|-|------|-------|
| `$00` | 89% | | `$1B` | 67% |
| `$03` | 86% | | `$1C` | 76% |
| `$04` | 93% | | `$1D` | 89% |
| `$05` | 71% | | `$1E` | 28% |
| `$06` | 85% | | `$1F` | 12% |
| `$07` | 88% | | `$01`/`$02` | 66% / 79% |

The text-heavy banks (`$1B`/`$1C` Project/File/Help) are legitimately lower (lots
of string data). Progression: per-bank 42% → jump tables 56% → bank-aware trace
60% → farcall 68% → **computed-farcall tables 71%**. `$1F` (12%) remains the
outlier; some of its instrument-type dispatch is reached through tables not yet
modelled.

The coverage tracer resolves the **computed-address farcall**: when a
`call $179A` pushes an immediate `bank` but reads the `addr` from a pointer
table (`ld a,[hl+]; ld h,[hl]; ld l,a`), it reads the table from the *caller's*
bank and validates targets in the *callee's* bank.

## Path to 100%

1. **Code/data split (cleaner carves).** The unreached remainder in a bank is
   data or not-yet-reached code. `tools/carve_clean.py` re-carves a bank marking
   only the unreached runs that look like **data** (illegal-opcode-dense, ASCII,
   or `$00`/`$FF` fill) as `db`, leaving ambiguous/code-like runs as
   instructions — so graphics/strings/tables read as data without hiding code.
2. **More computed dispatch.** Keep modelling indirect dispatch (e.g. the rest of
   `$1F`).
3. **Naming.** Replace auto `L*` labels with real names, routine by routine.
4. **Track the number.** Re-run `tools/coverage.py` after each push.

Throughout, the invariant is non-negotiable: **`make compare` stays green.**
