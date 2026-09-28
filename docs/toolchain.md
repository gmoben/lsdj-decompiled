# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95** → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker — **plus extra peephole rules** (the "modified GBDK"; six recovered so far, below) |
| Can we run it today? | **Yes** — the period `sdcc.exe` runs under **Wine** (32-bit PE; re-verified on wine-11.17) |
| Is it the right toolchain? | **Yes, for the code generator**: LSDj's instruction selection, register allocation and frame idioms are SDCC 2.2.1's line for line, and GBDK 2.95 library code in the ROM is byte-identical to its sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, `set_bkg_tiles` ← `set_bk_t.s` + `set_xy_t.s`, the SDCC `mul` runtime). Stock 2.95 does **not** match on its own: LSDj's build also ran peephole rules that stock 2.95 lacks |
| Matched a real LSDj function yet? | **Yes — three.** `DrawMessageBar` (`$01:$74F7`, 198 bytes) and the wrappers `ShowOutOfMemory`/`ShowTapAddChain` are byte-identical when compiled with stock 2.95 + [`tools/match/lsdj.peep`](../tools/match/lsdj.peep) and linked at their ROM addresses |
| Go/no-go on Track 2? | **Provisional go.** Matching C is demonstrated; before committing, validate the recovered rule set on more (and harder) functions and settle how C objects enter the RGBDS build |

## First matches

| Function | Where | Size | C source |
|----------|-------|------|----------|
| `DrawMessageBar` | `$01:$74F7`-`$75BC` | 198 B | [`c/bank01/DrawMessageBar.c`](../c/bank01/DrawMessageBar.c) |
| `ShowOutOfMemory` | `$01:$75BD`-`$75C6` | 10 B | [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) |
| `ShowTapAddChain` | `$01:$75F8`-`$7601` | 10 B | [`c/bank01/ShowMessages.c`](../c/bank01/ShowMessages.c) |

Reproduce (each prints `==> MATCH`; `--stock` shows the mismatch without the
recovered rules — `DrawMessageBar` comes out 205 bytes instead of 198):

```sh
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar
tools/match/match_function.py --c c/bank01/ShowMessages.c --target ShowOutOfMemory
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar --stock
```

`DrawMessageBar` was chosen because it is mid-sized and self-contained (two
loops, one `if`, two callees, no jump table) yet exercises everything a real
match needs beyond a leaf function: globals, HRAM stores via `ldh`, and calls.

## The "modified GBDK": recovered peephole rules

Stock 2.95 compiles the C above to *almost* LSDj's bytes; every remaining
difference is a peephole-level rewrite. Each rule below is supported
**ROM-wide**, not just in the matched functions: across LSDj's code the
"before" pattern (what stock 2.95 emits) essentially never survives, while the
"after" form is common. `SDCC 2.2.1` accepts extra rules with
`--peep-file`, and the harness loads `tools/match/lsdj.peep` by default.

| # | Rewrite | "Before" left in ROM | "After" in ROM | Provenance |
|---|---------|---------------------:|---------------:|------------|
| R1 | `or a,X` / `or a,a` → `or a,X` | 0 | 575 (`xor a; ld hl,nn; or a,(hl); jp z/nz` tests) | in GBDK **2.96a**'s rule set |
| R2 | `ld r,a` / `xor a,a` / `or a,r` → `ld r,a` / `or a,r` | 0 | 41 (`ld b,a; or a,b`) | inferred |
| R3 | `ld r,a` / `ld a,r` → `ld r,a` | 2 | 42 (before a byte push) | in GBDK **2.96a**'s rule set; 2 unexplained exceptions (e.g. `DrawTileHexCell`, after `ld a,(bc)`) |
| R4 | `ld a,N` / `push af; inc sp` / `ld a,N` → drop the reload | 0 | 10 | inferred |
| R5 | `ld a,N` / `ldh (n),a` / `ld hl,nn` / `push hl` / `ld a,N` → drop the reload | 0 | 5 | inferred (two textual forms, for `ld hl,#sym + 0`) |

R4/R5 look like one underlying behaviour — the compiler knew `a` still held
the constant — which LSDj's code shows across other shapes too (0 redundant
`ld a,N` after `ldh (n),a`, 0 after `ld hl,nn; push hl`). It may have been
codegen-level constant tracking rather than individual rules; the specific
rules are enough for the functions matched so far. Expect `lsdj.peep` to grow
(and possibly generalise) as more functions are matched.

Where the evidence came from: SDCC's rule files are plain text in the source
tree, and [juju.nz/src/juju/gbdk-releases](https://juju.nz/src/juju/gbdk-releases)
has the full GBDK+SDCC source for every release tag (2.91-2.96a).
`sdcc/src/z80/peeph.def` at `gbdk-2.96a` contains R1 and R3; at `gbdk-2.95`
and `gbdk-2.95-2` it does not.

## Compiler versions checked

| Build | SDCC | Result for `DrawMessageBar` |
|-------|------|-----------------------------|
| GBDK 2.95 Win32 (rbong mirror, `tools/toolchain/`) | 2.2.1 | codegen matches line for line; needs the recovered rules → **match** |
| GBDK 2.95-3 Win32 (SourceForge `gbdk-win32/2.95-3`, 2000-08-19) | 2.2.1 | identical output to 2.95 — no extra rules |
| GBDK 2.96a Linux i586 (SourceForge `gbdk/2.96`, 2001-11-04; runs natively with 32-bit glibc) | 2.3.1 | **different codegen lineage**: no `push bc` frame, locals on the stack, byte args pushed as 16-bit pairs, `add hl,de` indexing → ruled out as LSDj's compiler, though its peephole file has R1/R3. SFRs must be written `extern volatile sfr NAME;` (`sfr` is the whole type) |

So LSDj's compiler is best described as **SDCC 2.2.1 (GBDK 2.95) code
generation + later/extra peephole rules** — consistent with LSDj's own "modified
GBDK 2.95".

## Writing C that matches (what we learned)

- **HRAM/IO bytes written with `ldh`** must be declared
  `extern volatile sfr UBYTE NAME;` (GBDK 2.95's `USE_SFR_FOR_REG` style). A
  literal `*(UBYTE *)0xFFxx = v` compiles to `ld de,#0xFFxx; ld (de),a`.
- **SDCC 2.2.1 crashes** on `if (*(UBYTE *)0xFFxx)` (and 2.3.1 segfaults on the
  same). Bytes that LSDj *tests* through `ld hl,$FFxx; or a,(hl)` (e.g.
  `hGameboyType`) are plain `extern` variables placed there at link time.
- Bank-0 callees need **`nonbanked`** prototypes, otherwise calls go through
  `call banked_call` / `.dw fn, 0`.
- `&array[0]` (or a cast) loads the address into `de` first, as LSDj does;
  plain `array` stores the two immediate bytes directly.
- `if (!x)` tests via `xor a; or a,r` and jumps straight to the target;
  `if (x == 0)` compiles through a `jp`/`jr`/`jp` trampoline.
- Byte arguments are pushed right-to-left with `push af; inc sp`.

## How to run it

```sh
# 1. Fetch the period toolchain into ./toolchain (git-ignored). Needs git + wine.
tools/toolchain/fetch_gbdk295.sh

# 2. Confirm the compiler runs.
tools/toolchain/sdcc295 --version            # -> SDCC : gbz80/z80 2.2.1

# 3. Compile C and compare against the ROM.
tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar
tools/match/match_function.py --c FILE --target NAME --show-asm   # see SDCC's asm
tools/match/match_function.py --c tools/match/examples/add8.c --func add8 --search
```

Speed: each Wine invocation starts a `wineserver`, which made a compile take
~25 s. Keep one alive for an iteration session and it drops to ~1-2 s:
`WINEPREFIX=$PWD/toolchain/.wine wineserver -p` (stop with `wineserver -k`).

### Wine quirks (handled by the wrappers)

- `sdcpp`/`as-gbz80` must be discoverable → the bin dir is added via `WINEPATH`.
- SDCC 2.2.1 writes its temp file at the **current drive's root**, so the tools
  must run from Wine's writable `C:` drive, or you get
  `sdcpp: \s??.: undocumented I/O error`. The wrappers `cd` into `drive_c`.
- File arguments are resolved to absolute paths before the directory switch.
- A crashing `sdcc.exe` otherwise hangs in `winedbg`; the harness sets
  `WINEDLLOVERRIDES=winedbg.exe=d` and a timeout so it fails fast. (Note the
  `sdcc295` wrapper leaves `.asm/.lst/.o/.sym` beside the input; the harness
  works in a temp dir.)

## How the match harness works

`tools/match/match_function.py`:

```
file.c --[sdcc -mgbz80 -S --peep-file lsdj.peep]--> file.asm
       --[as-gbz80 -l -o]--> file.o + file.lst
--target NAME|BB:AAAA:
       --[link-gbz80 -i -b_CODE=<addr> -g_<sym>=<addr> ...]--> file.ihx
       -> bytes between ___<func>_start / ___<func>_end, diffed against the ROM
without --target (leaf functions only, no relocations):
       -> diff vs ROM@offset   (--offset)  or scan the whole ROM (--search)
```

Every extern is resolved from GBDK's `libc/gb/sfr.s` register names,
`constants/ram.inc`, and `src/bank*.sym` (warning if a ROM symbol lives in a
bank the target can't reach); `--define NAME=ADDR` adds or overrides one. On a
mismatch it prints the first differing bytes and the listing lines around them.

## What's still unknown

1. **The complete rule set.** Six rules are recovered from three functions;
   more will surface (R3's two exceptions hint at a condition we haven't
   modelled, and R4/R5 may really be constant tracking in the code generator).
2. **Flags.** The default `lcc` compile is `sdcc -mgbz80 --c1mode` after a
   separate `sdcpp` pass (`-DGB=1 -DGAMEBOY=1 -DINT_16_BITS …`); nothing so far
   needed more, but banked code (`-Wf-bo<N>`) and other files may.
3. **Whole translation units.** Per-function matching links one function at
   its address. Reproducing LSDj's original object/link layout (function order
   within a bank) is only needed if we want to build banks from C wholesale.
4. **Getting C into the build.** `make compare` is RGBDS; C objects are asxxxx.
   Options: SDCC's `--asm=rgbds` backend, or converting the linked bytes/listing
   into an RGBDS include. Not started.

## Next steps

- [x] Carve one leaf function in asm (Track 1) to get a concrete byte target —
      superseded: every function is now carved and named.
- [x] Add a link step to the harness (externs resolved from our symbol maps,
      code placed at the target address).
- [x] Match `DrawMessageBar` in C (plus two small wrappers).
- [ ] Validate `lsdj.peep` on more and harder functions: `DrawTileHexCell` (R3's
      exception), a `switch`/jump-table function, a sound-engine routine,
      something with 16-bit arithmetic and locals.
- [ ] Decide how matched C enters the RGBDS build (see *unknown 4*), then make
      the Track 2 go/no-go call.

## References

- GBDK 2.95 mirror: <https://github.com/rbong/gbdk> (`gbdk-2.95/`)
- GBDK releases with SDCC source per tag: <https://juju.nz/src/juju/gbdk-releases>
- GBDK binaries: <https://sourceforge.net/projects/gbdk/files/> (`gbdk-win32/`, `gbdk/2.96/`)
- SDCC: <https://sdcc.sourceforge.net/>
- Matching-decomp methodology (C via original compiler): pret `pokeemerald`.
