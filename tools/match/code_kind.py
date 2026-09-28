#!/usr/bin/env python3
"""Classify every named function as SDCC-compiled C or hand-written asm.

LSDj is mostly C, but the timing-critical core is hand-written assembly and
is not a Track 2 (matching C) target. The two are easy to tell apart in the
generated disassembly:

  * SDCC 2.2.1 addresses locals/arguments with `ld hl, sp+N` / `add sp, N`
    and branches with `jp cc` (it only emits `jr` inside its own shift loops);
  * hand-written code uses `jr` freely, `ldh`, and `add a,l; ld l,a` style
    address arithmetic, and never touches the stack frame that way.

A function with any sp-relative access is C; one with more `jr` than `jp cc`
and no sp-relative access is asm; the rest ("C?") are short leaf routines
with no distinguishing feature. Functions under 6 instructions are skipped.

Usage: tools/match/code_kind.py            # per-bank summary
       tools/match/code_kind.py --list     # ... plus every asm-tagged function
       tools/match/code_kind.py --json F   # write {"bank:Name": kind} to F
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LABEL = re.compile(r"^([A-Za-z_]\w*)::?")
JPCC = re.compile(r"^jp (nz|z|nc|c),")


def classify_bank(asm_path: Path) -> dict[str, tuple[str, int]]:
    """name -> (kind, instruction count) for one generated bank."""
    funcs: dict[str, list[str]] = {}
    cur = None
    for line in asm_path.read_text().splitlines():
        m = LABEL.match(line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
            continue
        s = line.split(";")[0].strip()
        if cur and s and not s.endswith(":") and not s.startswith(("db", "dw", "INCBIN", "SECTION")):
            funcs[cur].append(s)
    out: dict[str, tuple[str, int]] = {}
    for name, ins in funcs.items():
        if len(ins) < 6:
            out[name] = ("tiny", len(ins))
            continue
        sp = sum(s.startswith(("ld hl, sp+", "add sp,")) for s in ins)
        jr = sum(s.startswith("jr ") for s in ins)
        jpcc = sum(bool(JPCC.match(s)) for s in ins)
        kind = "C" if sp else ("asm" if jr > jpcc else "C?")
        out[name] = (kind, len(ins))
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list the asm-tagged functions")
    ap.add_argument("--json", type=Path, help="write the per-function map here")
    args = ap.parse_args()

    everything: dict[str, str] = {}
    tot = {"C": 0, "C?": 0, "asm": 0, "tiny": 0}
    tot_ins = {"asm": 0, "all": 0}
    print(f"{'bank':5s} {'C':>5s} {'C?':>5s} {'asm':>5s} {'tiny':>5s}  {'asm share of instructions':>26s}")
    for p in sorted((ROOT / "src").glob("bank*.asm")):
        bank = p.stem[4:]
        kinds = classify_bank(p)
        n = {"C": 0, "C?": 0, "asm": 0, "tiny": 0}
        ins_asm = ins_all = 0
        for name, (kind, cnt) in kinds.items():
            n[kind] += 1
            ins_all += cnt
            if kind == "asm":
                ins_asm += cnt
            everything[f"{bank}:{name}"] = kind
        for k in n:
            tot[k] += n[k]
        tot_ins["asm"] += ins_asm
        tot_ins["all"] += ins_all
        print(f"${bank.upper():4s} {n['C']:5d} {n['C?']:5d} {n['asm']:5d} {n['tiny']:5d}  {ins_asm / max(ins_all, 1):26.0%}")
        if args.list:
            names = [nm for nm, (k, _) in kinds.items() if k == "asm"]
            if names:
                print("      asm: " + ", ".join(names))
    print(f"{'all':5s} {tot['C']:5d} {tot['C?']:5d} {tot['asm']:5d} {tot['tiny']:5d}  "
          f"{tot_ins['asm'] / max(tot_ins['all'], 1):26.0%}")
    if args.json:
        args.json.write_text(json.dumps(everything, indent=0, sort_keys=True))
        print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
