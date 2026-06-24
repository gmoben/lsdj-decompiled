# Toward 100% reversal — methodology & coverage

The build already reproduces the ROM **100% byte-for-byte** (`make compare`).
"100% reversal" is the stronger goal: every byte classified as code or data,
all code disassembled with meaningful labels, all data structured.

## What counts

The 1 MiB ROM is 64 × 16 KiB banks:

- **13 code/data banks** — the actual program (`$00`-`$07`, `$1B`-`$1F`). **This
  is the reversal target, and all 13 are now carved into the source tree.**
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
   a 2-byte address array), and LSDj's two **farcall helpers** `FarCall` (`$179A`,
   caller pushes `addr` then `bank`) and `FarCallArg` (`$17BB`, same but also
   passes one byte argument). The helper switches bank and calls; every call
   site's `(bank, addr)` pair is extracted as an entry point.
- conservative throughout (jump-table targets must be in range and start on a
  legal opcode), so the figure is a sound lower bound.

### Current coverage

Run `tools/coverage.py`. As of writing, **64%** of the code/data banks trace as
reachable code:

| bank | code% | | bank | code% |
|------|-------|-|------|-------|
| `$00` | 83% | | `$1B` | 54% |
| `$03` | 86% | | `$1C` | 65% |
| `$04` | 93% | | `$1D` | 89% |
| `$05` | 69% | | `$1E` | 8% |
| `$06` | 85% | | `$1F` | 12% |
| `$07` | 73% | | `$01`/`$02` | 38% / 77% |

The lower banks are legitimately data-heavy: `$1B`/`$1C` (Project/File/Help) are
string-heavy, and `$01`/`$1E` carry large zero-padded data tables. Progression:
per-bank 42% → jump tables 56% → bank-aware trace 60% → farcall 68% →
computed-farcall tables 71% → second farcall helper (`FarCallArg`) 72% →
**fill-stop (honest) 64%**.

The last step *lowered* the number on purpose. `rst $38` ($FF) and `nop` ($00)
were not flow-terminators, so any stray computed target landing in padding made
the tracer walk the whole run as bogus "code" (`rst $38`/`nop`), inflating the
metric and keeping the carve from rendering the padding as `db`. Stopping on a
clear fill run (two `$FF`, or eight `$00` -- real alignment is never that long)
both corrects the metric and cleans ~12k lines of fill noise to `db`. Note the
disassembly never *hides* code: carve marks bytes `db` only when they are **both**
unreached and data-like, so a real function after a padding gap still decodes as
code even though the tracer stops short of it.

`$1F` (12%) is a **metric artifact, not missing disassembly**. The bank is the
instrument subsystem and carves as ~93% code (8977 instruction lines vs 146 `db`),
but it is dominated by deep *computed* / RAM-indirect dispatch chains (the
instrument-type handlers in §`$4000` switch on instrument field 0, then editor
handlers call each other through pointers held in RAM). Static recursive-descent
cannot follow a `jp hl`/`ret` whose address came from RAM, so whole subtrees read
as unreached even though they are real, correctly-decoded code. The unreached runs
were inspected (`$5413`, `$4A65`, `$7097` are `push bc; add sp,-N` prologues;
`$77F5` is the "COPIED WORD"/"CUT WORD" string pool) and confirmed to interleave
short label strings ("ATK", "ON", "0.5") with code, so forcing data ranges would
risk *hiding* code — the conservative carve is correct here.

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
