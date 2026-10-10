#!/usr/bin/env python3
"""Search the register allocator's state for the perturbation that makes an
attempt match the ROM, with the debug compiler's knobs: SPILLSYM (a symbol
spilt at its definition), BUSYC / BUSYB (a register held over a range of
iCode sequence numbers). Prints every perturbation that scores at least as
well as the plain compile, best first. The result is a *state*, not C: it
says what the ROM's compile had in a register (or not) where ours differs.

    tools/match/perturb.py FILE.c FUNC [--target NAME] [--define N=V ...]
                           [--max-span N] [--single]
"""
import argparse, itertools, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEBUG_BIN = ROOT / "toolchain" / "sdcc-2.2.1-debug" / "bin"


def score(c: Path, func: str, target: str, defines: list[str], env: dict) -> int:
    cmd = [sys.executable, str(ROOT / "tools/match/match_function.py"), "--c", str(c), "--func", func,
           "--target", target, "--score"]
    for d in defines:
        cmd += ["--define", d]
    r = subprocess.run(cmd, capture_output=True, text=True, env={**os.environ, **env, "LSDJ_NATIVE_BIN": str(DEBUG_BIN)})
    o = r.stdout + r.stderr
    if re.search(r"byte-identical|MATCH: the code matches", o):
        return 0
    m = re.search(r"score: (\d+) differing row", o)
    return int(m.group(1)) if m else 9999


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("c", type=Path); ap.add_argument("func"); ap.add_argument("--target")
    ap.add_argument("--define", action="append", default=[])
    ap.add_argument("--max-span", type=int, default=12, help="longest BUSY range tried (plus whole-function)")
    ap.add_argument("--single", action="store_true", help="single knobs only (no pairs)")
    a = ap.parse_args()
    target = a.target or a.func
    c = a.c.resolve()
    base = score(c, a.func, target, a.define, {})
    print(f"plain: {base} rows", flush=True)
    # the symbols and sequence numbers from the allocator trace
    r = subprocess.run([sys.executable, str(ROOT / "tools/match/trace_alloc.py"), str(c), a.func, "--alloc"],
                       capture_output=True, text=True)
    syms = sorted(set(re.findall(r"in loop on result (\S+) \(seq(\d+) used\d+ nregs([1-9])\)", r.stdout)))
    seqs = sorted({int(s) for _, s, _ in syms} | {int(x) for x in re.findall(r"\(seq(\d+)", r.stdout)})
    if not seqs:
        sys.exit("no allocator trace (is the debug compiler built?)")
    lo, hi = seqs[0], seqs[-1]
    names = sorted({n for n, _, _ in syms}, key=lambda n: int(re.sub(r"\D", "", n) or 0))
    knobs: list[dict] = [{"SPILLSYM": n} for n in names]
    spans = [0, 1, 2, 3, 5, 8, a.max_span]
    for reg in ("BUSYC", "BUSYB"):
        for s in seqs:
            for w in spans:
                if s + w <= hi + 1:
                    knobs.append({reg: f"{s}-{s + w}"})
            knobs.append({reg: f"{s}-{hi + 1}"})
        knobs.append({reg: f"{lo}-{hi + 1}"})
    seen = set(); uniq = []
    for k in knobs:
        key = tuple(sorted(k.items()))
        if key not in seen:
            seen.add(key); uniq.append(k)
    print(f"{len(uniq)} single perturbations over seq {lo}..{hi}, symbols {names}", flush=True)
    results = []
    for k in uniq:
        s = score(c, a.func, target, a.define, k)
        results.append((s, k))
        if s < base:
            print(f"  {s:4d}  {k}", flush=True)
    results.sort(key=lambda t: t[0])
    best = [k for s, k in results if s < base][:12]
    if not a.single and best:
        print("pairs of the best singles:", flush=True)
        for k1, k2 in itertools.combinations(best, 2):
            if set(k1) & set(k2):
                continue
            k = {**k1, **k2}
            s = score(c, a.func, target, a.define, k)
            if s < min(sc for sc, kk in results if kk in (k1, k2)):
                print(f"  {s:4d}  {k}", flush=True)
            results.append((s, k))
    results.sort(key=lambda t: t[0])
    print("best:")
    for s, k in results[:8]:
        print(f"  {s:4d}  {k}")


if __name__ == "__main__":
    main()
