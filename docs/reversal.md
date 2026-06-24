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
- the tracer follows direct branches/calls **and jump tables** (`jp hl` over a
  `jp`-list or a 2-byte address array), conservatively (targets must be in range
  and start on a legal opcode).

It reports, per code/data bank, the fraction provably reachable as code. The
remainder is data (tables, strings, graphics), padding, or code reached through
mechanisms not yet modelled.

### Current coverage

Run `tools/coverage.py`. As of writing, **56%** of the code/data banks trace as
reachable code, e.g. `$04` (Instrument) 93%, `$03` (Song) 86%, `$1D` 89%, while
the text-heavy `$1B`/`$1C` (Project/File/Help) are lower (≈45%, lots of strings)
and `$1E`/`$1F` are low (≈4%) because they are entered cross-bank from sites the
per-bank trace doesn't yet resolve.

Jump-table following alone raised the figure from 42% → 56%.

## Path to 100%

1. **Cross-bank entry discovery.** Find LSDj's banked-call helper(s) and seed the
   target banks from their call sites; follow trampolines *through* into the
   callee bank. This is what `$1E`/`$1F` need.
2. **Finish computed-jump resolution.** Cover the remaining `jp hl` dispatch and
   any pointer-table call sites.
3. **Code/data split.** Once coverage plateaus, the unreached remainder in a bank
   is data — mark it (`--data`) and carve cleanly; label graphics/strings/tables.
4. **Naming.** Replace auto `L*` labels with real names, routine by routine
   (engines, editors, helpers), keeping `docs/*` in sync.
5. **Track the number.** Re-run `tools/coverage.py` after each push; it is the
   objective progress meter toward a complete disassembly.

Throughout, the invariant is non-negotiable: **`make compare` stays green.**
