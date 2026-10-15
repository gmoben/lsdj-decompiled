# Forking: a build that changes LSDj

`make compare` is this repository's gate: the build is LSDj 9.4.0, byte for
byte. A fork changes the program, so it builds the same tree in fork mode
and gates on behaviour instead: everything LSDj did, it still does
(`make behaviour`), plus whatever it adds, which its own tests check.

A fork lives in its own repository with this one as `upstream`, so fixes
here pull cleanly into it. It needs your ROM, as this one does: the build
regenerates the disassembly from it, and nothing ROM-derived is committed.

## Turning it on

A file `fork.mk` in the repository root (the Makefile includes it) with

    FORK = 1

or `make FORK=1 ...` for one build. In fork mode:

- `make match` (`tools/match/verify_all.py --fork`) compiles `c/` as usual
  and sorts each function: MATCH (the ROM's bytes), CHANGED (a function the
  ROM has, now other code), NEW (one it lacks). Its manifest is
  `build/c/fork.json`; `c/matched.json` stays the original's, the record of
  each matched function's ROM range (`addr` to `end`: code and the literals
  after it).
- the carve (`tools/carve_clean.py`, with `LSDJ_FORK` set) renders every
  function from its translation: a CHANGED one replaces its original range,
  a NEW one goes before its bank's free space, and the fork's asm overrides
  are applied (below).
- `make check`, which the pre-commit hook runs, is `make behaviour`.
- rgbfix rewrites the header checksum without the overwrite warning.

## Changing C

Edit a function in `c/bankNN/`. It is compiled with the period compiler
(SDCC 2.2.1 and `tools/match/lsdj.peep`, as for matching) and translated to
RGBDS source with only its own label: the original's local labels inside it
are gone, its jumps get names of their own (`.loop`, `.cont`). Everything
after it in the bank moves; every reference to it is a label, which is what
the shift test proves (`tools/shift_test.py`).

## New C

A function defined in `c/bankNN/*.c` that no `src/bankNN.sym` names is new:
it goes at the end of bank `$NN`'s code, before its free space. Call it from
other C by name, declared as usual; from another bank, with a banked call
(`FarCall(bank, FAR_FN(Name))`) as LSDj does.

Write it for a modern SDCC: a file that says `@sdcc4` (in a comment) is
compiled with SDCC 4.6 (`tools/toolchain/fetch_sdcc4.sh` puts it in
`toolchain/`) and translated by `tools/match/sdcc4.py`, every function in
it new. It includes `c/include/fork.h`, not `lsdj.h`, and can't have
variables of its own (RAM goes in `constants/ram.inc`) or need SDCC 4's
runtime library. The period compiler is what LSDj's own C needs, but on new
code its gbz80 port drops an operand now and then (`NR51_REG & x`, `(a & b)
| (c & d)`: the result of one side overwritten before the other is used);
SDCC 4 compiles the same C right, and smaller.

SDCC 4 uses LSDj's calling convention (`--sdcccall 0`: arguments on the
stack, a byte returned in e, a word in de) except that it doesn't keep bc
for its caller, and LSDj's compiled C keeps values in bc across calls. So
LSDj code reaches new code through a function that saves bc: the period
compiler's `FarCall(bank, FAR_FN(Name))` does (it pushes bc to pass the
address and pops it after), which is also how code in bank `$02` reaches a
bank with room. The banks' free space (bytes):

| `$00` | `$01` | `$02` | `$03` | `$04` | `$05` | `$06` | `$07` | `$1B` | `$1C` | `$1D` | `$1E` | `$1F` |
|------|------|------|------|------|------|------|------|------|------|------|------|------|
| 630 | 258 | 157 | 1,355 | 674 | 2,898 | 1,782 | 2,997 | 2,704 | 2,605 | 552 | 1,109 | 2,026 |

Bank `$00` is always mapped (the timer interrupt's code can call into it);
bank `$02`, the sound engine's, has little room.

## Asm and data

`fork/bankNN/Name.asm` replaces what the ROM has at `Name` (a global label of
`src/bankNN.sym`) up to the next global label with the file's lines: a
hand-written routine (`SoundDispatchCommand`'s command count), a table grown
by an entry (`CommandHandlers`, `CommandMnemonics`). The carve prints `Name:`
itself; the file has the rest, its own local labels included. A file whose
name the `.sym` lacks is new asm, put before the bank's free space under
that name.

New RAM goes in `constants/ram.inc` like any other, at addresses LSDj leaves
unused; C declares it `extern` with the same name.

## Checking a fork

- `make check` (`make behaviour`): the build next to LSDj 9.4.0 over the
  shift test's tour and seeded input, CGB and DMG, the screen, the sound and
  SRAM compared. A change to what LSDj already does fails it, as it should:
  keep the tour off what the fork changes (it never steps a command past Z,
  for one), and test the change itself.
- a feature's own test: play an example song (a `.sav` the fork builds,
  never commits) in PyBoy and check what the hardware does.

## What it can't do yet

- a hand-written asm routine can't become C in place: override it in asm,
  or call new C from it.
- a CHANGED function must be one `c/matched.json` records (all 678 C
  functions are).
- a label inside a changed function that other code jumps to is gone: keep
  that code, or override the caller.
