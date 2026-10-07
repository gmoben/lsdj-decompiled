#!/usr/bin/env python3
"""Why did SDCC 2.2.1 put a value in that register or slot?

Compiles a C file with the instrumented compiler (`tools/toolchain/
build_sdcc221.sh --debug` -> toolchain/sdcc-2.2.1-debug; same code as the
native build) and summarises one function:

  * the live range, type, spill location and final home of every iTemp
    (`[c]`, `[c b]`, `[remat]`, or the stack slot it was spilled to), from
    `--dumpall`'s .dumprassgn;
  * the dataflow passes (computeDataFlow's change counts: a third pass is
    what extends inner-loop temporaries, see docs/toolchain.md) and the loop
    invariants hoisted into preheaders;
  * hasIncomingDefs per last use inside a loop (`-> 1`: the live range was
    extended to the loop end);
  * with --alloc, the register allocator's own trace (spill choices,
    allocations, frees).

The work directory (sources, dumps, raw trace) is kept under
build/trace_alloc/FUNC for a closer look.

Usage: tools/match/trace_alloc.py FILE.c FUNC [--hid] [--loops] [--alloc] [--dfv]
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBUG_BIN = ROOT / "toolchain" / "sdcc-2.2.1-debug" / "bin"
INCLUDE = ROOT / "c" / "include"

ITEMP = re.compile(r"(iTemp\d+) \[k\d+ lr(\d+):(\d+) so:\d+\]\{ ia\d re(\d) rm(\d)\}\{([^}]*)\}"
                   r"(?:\{ sir@ ([^}]*)\})?(?:\[([^\]]*)\])?")
ALLOC_KEYS = ("serialRegAssign", "willCS", "spil", "alloced", "freed", "selectSpil",
              "createStackSpil", "No free", "positionRegs")


def run(c_file: Path, func: str, dfv: bool) -> tuple[Path, str]:
    if not (DEBUG_BIN / "sdcc").is_file():
        sys.exit("error: no instrumented compiler; build it with "
                 "tools/toolchain/build_sdcc221.sh --debug")
    work = ROOT / "build" / "trace_alloc" / func
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    shutil.copy(c_file, work / c_file.name)
    for h in INCLUDE.glob("*.h"):
        shutil.copy(h, work / h.name)
    env = dict(os.environ, LOOPDBG="1", PATH=f"{DEBUG_BIN}:{os.environ.get('PATH', '')}")
    if dfv:
        env["DFV"] = "1"
    flags = os.environ.get("LSDJ_SDCC_FLAGS", "").split()   # as tools/match/match_function.py
    p = subprocess.run(["sdcc", "-mgbz80", "-S", "--dumpall", *flags, c_file.name],
                       cwd=work, env=env, capture_output=True, text=True, errors="replace")
    (work / "trace.txt").write_text(p.stdout + p.stderr)
    if not (work / c_file.with_suffix(".asm").name).is_file():
        sys.exit(f"error: sdcc failed:\n{p.stderr or p.stdout}"[-2000:])
    return work, p.stdout


def segment(trace: str, func: str) -> tuple[list[str], list[str]]:
    """(optimiser lines, allocator lines) of FUNC from the raw trace."""
    opt: list[str] = []
    alloc: list[str] = []
    cur = None
    in_alloc = False
    for line in trace.splitlines():
        m = re.match(r"FUNC (\w+)", line)
        if m:
            cur, in_alloc = m.group(1), False
            continue
        m = re.match(r"-> z80_assignRegisters: entered (\w+)\.", line)
        if m:
            cur, in_alloc = m.group(1), True
            continue
        if cur != func:
            continue
        (alloc if in_alloc else opt).append(line)
    return opt, alloc


def itemps(dump: Path, func: str) -> dict[str, tuple[int, int, str, str, str, int]]:
    """iTemp -> (lr from, lr to, type, spill location, home, rematerialisable)."""
    out: dict[str, tuple[int, int, str, str, str, int]] = {}
    cur = None
    for line in dump.read_text(errors="replace").splitlines():
        m = re.search(r"\tproc _(\w+) ", line)
        if m:
            cur = m.group(1)
            continue
        if cur != func:
            continue
        for t in ITEMP.finditer(line):
            name, lo, hi, _re, rm, typ, sir, home = t.groups()
            out[name] = (int(lo), int(hi), typ.strip(), sir or "", (home or "").strip(), int(rm))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("c_file", type=Path)
    ap.add_argument("func", help="function name (without the leading underscore)")
    ap.add_argument("--hid", action="store_true", help="print every hasIncomingDefs result")
    ap.add_argument("--loops", action="store_true", help="print the hoisted invariants")
    ap.add_argument("--alloc", action="store_true", help="print the allocator trace")
    ap.add_argument("--dfv", action="store_true", help="print the blocks each dataflow pass changed")
    args = ap.parse_args()
    if not args.c_file.is_file():
        sys.exit(f"error: {args.c_file} not found")

    work, trace = run(args.c_file, args.func, args.dfv)
    opt, alloc = segment(trace, args.func)
    if not opt and not alloc:
        sys.exit(f"error: no function {args.func} in {args.c_file}")

    passes = [l.split("=")[1] for l in opt if l.startswith("DF iter")]
    hoisted = [l for l in opt if l.startswith("HOISTED")]
    hid = [l for l in opt if l.startswith("HID")]
    extended = sorted({l.split()[1] for l in hid if l.endswith("-> 1")},
                      key=lambda s: int(re.sub(r"\D", "", s) or 0))
    print(f"{args.func} ({args.c_file.name}): work dir {work.relative_to(ROOT)}")
    print(f"dataflow change counts per pass: {' '.join(passes) or '-'}")
    print(f"hoisted loop invariants: {len(hoisted)}")
    if args.loops:
        for l in opt:
            if l.startswith(("LI ", "HOISTED")):
                print(f"  {l}")
    print(f"live ranges extended to a loop end: {' '.join(extended) or '-'}")
    if args.hid:
        for l in hid:
            print(f"  {l}")
    if args.dfv:
        for l in opt:
            if l.startswith(("DFCH", "DF iter")):
                print(f"  {l}")

    dump = work / args.c_file.with_suffix(".dumprassgn").name
    temps = itemps(dump, args.func) if dump.is_file() else {}
    print(f"\n{'iTemp':8s} {'live':>9s}  {'type':22s} {'home':28s} spill location")
    for name in sorted(temps, key=lambda s: int(s[5:])):
        lo, hi, typ, sir, home, rm = temps[name]
        where = "remat" if rm else (home or "-")
        print(f"{name:8s} {lo:4d}:{hi:<4d}  {typ:22s} {where:28s} {sir}")
    print("(home: a register, a stack slot, `remat` (recomputed: an address), or `-`: none --"
          " packed into a or hl, or never used)")
    if args.alloc:
        print("\nallocator trace:")
        for l in alloc:
            if any(k in l for k in ALLOC_KEYS):
                print(f"  {l.strip()}")


if __name__ == "__main__":
    main()
