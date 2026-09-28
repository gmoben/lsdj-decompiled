# Toolchain recovery (Track 2 spike)

Status of the prerequisite for a **matching C decompilation**: pinning the exact
compiler LSDj was built with and proving we can reproduce its output byte-for-byte.

See [`AGENTS.md`](../AGENTS.md) → *Strategy* for why this gates Track 2.

## TL;DR

| Question | Answer |
|----------|--------|
| What compiled LSDj? | **GBDK 2.95** → **SDCC 2.2.1** (`gbz80` target), default `asxxxx` assembler/linker |
| Can we run it today? | **Yes** — the period `sdcc.exe` runs under **Wine** (32-bit PE) |
| Can we reproduce its codegen? | **Yes** — C → SDCC 2.2.1 asm → bytes, via `tools/match/match_function.py` (re-verified under wine-11.17: `add8` still compiles to the bytes below) |
| Is it the right toolchain? | **Strong evidence**: GBDK 2.95 library code in the ROM is byte-identical to the library sources (`display_off` ← `crt0.s`, `strcpy` ← `asm_string.s`, `set_bkg_palette` ← `cgb.s`, the SDCC `mul` runtime), and LSDj's code uses exactly the stack-frame idioms this compiler emits (below) |
| Matched a real LSDj function yet? | **Not yet** — the first target is chosen: **`DrawMessageBar`** (`$01:$74F7`, see *Goal*) |
| Go/no-go on Track 2? | **Promising, not yet proven.** Stay asm-first; pursue a first real match before betting on C |

## What we confirmed

- LSDj's own manual credits "The Game Boy Developer's Kit (GBDK)" and thanks
  Michael Hope (GBDK/SDCC author). GBDK 2.95 (2000) bundles SDCC 2.2.1.
- Running the period compiler prints:
  `SDCC : gbz80/z80 2.2.1 (UNIX)`.
- GBDK 2.95 ships both `asxxxx` (default `as-gbz80`/`link-gbz80`) and `rgbds`
  output libs. LSDj was almost certainly built with the default `asxxxx`
  toolchain, so the match harness uses that path (not RGBDS) for fidelity.
- **Storage allocation** (compiler probes, `--show-asm` on small test units
  modelled on LSDj routines): function locals live **on the stack** — a
  `lda sp,-N(sp)` frame (`add sp,-N`) with `lda hl,N(sp)` (`ld hl,sp+N`)
  accesses — and the `_OVERLAY` area stays empty. LSDj's code uses exactly these
  patterns, so every fixed-address `$Cxxx` access in the ROM is a real global
  or `static`, not compiler scratch (details in [`reversal.md`](reversal.md)).

Example — SDCC 2.2.1 codegen for `unsigned char add8(unsigned char a, unsigned char b)`:

```
_add8:
    push bc
    lda  hl,8(sp)
    ld   a,(hl+)
    add  a,(hl)
    ld   c,a
    ld   e,c
    pop  bc
    ret            ; bytes: C5 F8 08 2A 86 4F 59 C1 C9
```

## How to run it

```sh
# 1. Fetch the period toolchain into ./toolchain (git-ignored). Needs git + wine.
tools/toolchain/fetch_gbdk295.sh

# 2. Confirm the compiler runs.
tools/toolchain/sdcc295 --version            # -> SDCC : gbz80/z80 2.2.1

# 3. Compile a C function and compare against the ROM.
tools/match/match_function.py --c tools/match/examples/add8.c --func add8 --search
tools/match/match_function.py --c FILE --func NAME --offset 0xABCD   # diff at offset
```

### Wine quirks (handled by the wrappers)

- `sdcpp`/`as-gbz80` must be discoverable → the bin dir is added via `WINEPATH`.
- SDCC 2.2.1 writes its temp file at the **current drive's root**, so the tools
  must run from Wine's writable `C:` drive, or you get
  `sdcpp: \s??.: undocumented I/O error`. The wrappers `cd` into `drive_c`.
- File arguments are resolved to absolute paths before the directory switch.

## How the match harness works

`tools/match/match_function.py`:

```
file.c --[sdcc -mgbz80 -S]--> file.asm --[as-gbz80 -l]--> file.lst
       -> parse code bytes between ___<func>_start / ___<func>_end
       -> diff vs ROM@offset   (--offset)
       -> or scan whole ROM    (--search)
```

It is validated end-to-end: the MATCH path (vs a known byte blob) returns success,
the MISMATCH path reports a byte diff, and `--search` scans all 64 banks
(reporting `bank $NN:$ADDR` hits). Current limitation: it extracts **leaf**
functions (no unresolved relocations); functions with absolute references need
a link step (a follow-up).

## What's still unknown (blockers to a real match)

1. **Exact compiler flags.** Memory model (`--model-small` vs `--model-medium`),
   `--stack-auto`, optimization/allocation flags, and any LSDj-specific defines
   all change codegen. We must discover the flags LSDj actually used.
2. **"Modified" GBDK.** Johan may have patched GBDK/SDCC locally. The rbong
   mirror is our best lead; whether it is bit-identical to LSDj's build is
   unverified.
3. **Bank / link layout.** Matching a whole translation unit requires
   reproducing `as-gbz80`/`link-gbz80` area placement and function ordering.
4. **First target selection.** Need a small, self-contained (leaf) function to
   attempt first — found by tracing control flow from the entry point
   (`0x150`), or a GBDK library routine known to be linked into the ROM.

## Goal: first real match — `DrawMessageBar` (`$01:$74F7`)

Track 1 is complete enough that every function is a concrete byte target. The
first one to match in C is `DrawMessageBar`, bank `$01` `$74F7`-`$75BC`
(198 bytes): it pads `wStrBuffer` to 20 characters with spaces +
NUL, points `wMsgStrPtr` at it, on CGB writes the attribute row
`MsgBarAttrRow` to the top tilemap row (VRAM bank 1), sets the draw palettes,
draws the 20 characters to `$9800` with `DrawStringIndexed`, and arms
`wMsgTimer` (50 frames). Why this one:

- mid-sized and self-contained: two loops, one `if`, two callees
  (`CopyToBgTilemap`, `DrawStringIndexed`), no `switch`/jump table;
- idiomatic SDCC 2.2.1: `push bc` / `add sp,-2` frame, loop counter in `c`,
  1-byte args pushed with `push af; inc sp`, `adc a,$00` array indexing;
- it exercises what a real match needs beyond a leaf: references to globals,
  HRAM stores via `ldh`, and calls — i.e. it forces the harness's link step.

## Next steps

- [x] Carve one leaf function in asm (Track 1) to get a concrete byte target —
      superseded: every function is now carved and named.
- [ ] Add a link step to the harness so functions that reference globals and
      call other functions can be compared (relocations resolved against our
      symbol maps, code placed at the target address).
- [ ] Write `DrawMessageBar` in C and iterate to a byte match (flags, types,
      statement forms).
- [ ] Sweep plausible SDCC flag sets if the default codegen doesn't match; use
      `--search` to see if any compiled candidate appears in the ROM.
- [ ] Record the first byte-exact match here, then make the Track 2 go/no-go call.

## References

- GBDK 2.95 mirror: <https://github.com/rbong/gbdk> (`gbdk-2.95/`)
- SDCC: <https://sdcc.sourceforge.net/>
- Matching-decomp methodology (C via original compiler): pret `pokeemerald`.
