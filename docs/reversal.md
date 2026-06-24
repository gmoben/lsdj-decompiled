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

Run `tools/coverage.py`. As of writing, **68%** of the code/data banks trace as
reachable code:

| bank | code% | | bank | code% |
|------|-------|-|------|-------|
| `$00` | 87% | | `$1B` | 54% |
| `$03` | 86% | | `$1C` | 76% |
| `$04` | 93% | | `$1D` | 89% |
| `$05` | 64% | | `$1E` | 28% |
| `$06` | 85% | | `$1F` | 7% |
| `$07` | 71% | | `$01`/`$02` | 65% / 79% |

The text-heavy banks (`$1B`/`$1C` Project/File/Help) are legitimately lower (lots
of string data). Progression this far: per-bank 42% → jump tables 56% →
bank-aware trace 60% → farcall resolution **68%**.

`$1F` is the outlier: it is all code (SDCC stack frames), reached via
**computed-address** farcalls — the instrument screen dispatches to per-type draw
routines through a *table* of `$1F` addresses, which the immediate-arg scan can't
see.

## Path to 100%

1. **Computed-address dispatch tables.** Resolve the instrument-type (and similar)
   farcall/jump tables whose targets come from a table, not an immediate — the
   main thing keeping `$1F` low. (Needs tracking which bank a table *lives in* vs
   which bank its *targets* run in.)
2. **Code/data split.** Once coverage plateaus, the unreached remainder in a bank
   is data — mark it (`--data`) and carve cleanly; label graphics/strings/tables.
3. **Naming.** Replace auto `L*` labels with real names, routine by routine,
   keeping `docs/*` in sync.
4. **Track the number.** Re-run `tools/coverage.py` after each push; it is the
   objective progress meter toward a complete disassembly.

Throughout, the invariant is non-negotiable: **`make compare` stays green.**
