#!/usr/bin/env python3
"""Instruction pairs the ROM's compiled functions never contain.

Builds the set of adjacent-instruction pairs (operands kept, numbers
abstracted to `N`, labels to `L`) over every function matched from C in
src/bank*.asm, then lists the pairs an attempt's linked disassembly has
that the ROM has nowhere: a missing peephole rule shows up as such a pair
(the dead `ld r,(hl)` after a narrowed 16-bit `&` did).

    rom_bigrams.py --build            # cache the ROM pair set
    rom_bigrams.py FUNC ATTEMPT.c     # pairs in the attempt absent from the ROM
"""
import json, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / "build" / "rom_bigrams.json"


def norm(line: str) -> str | None:
    t = line.split(";")[0].strip()
    if not t or t.endswith(":") or t.startswith(".") or t.startswith(("db ", "dw ", "ds ", "RGB ")):
        return None
    mn, _, ops = t.partition(" ")
    ops = re.sub(r"\.[A-Za-z_]\w*", "N", ops)            # local labels first (`.a` is one)
    ops = re.sub(r"\$[0-9A-Fa-f]+", "N", ops)
    if mn in ("cp", "and", "or", "xor", "add", "sub", "adc", "sbc") and "," not in ops:
        ops = "a, " + ops                                 # `cp N` == `cp a, N`
    ops = re.sub(r"\b\d+\b", "N", ops)
    ops = re.sub(r"\b(?!(?:a|b|c|d|e|h|l|af|bc|de|hl|sp|nz|z|nc|N)\b)[A-Za-z_.][\w.]*\b", "N", ops)
    ops = ops.replace("LOW(N)", "N").replace("HIGH(N)", "N")
    return (mn + " " + re.sub(r"\s+", " ", ops)).strip()


def rom_pairs() -> set[str]:
    matched = json.load(open(ROOT / "c" / "matched.json"))
    names = {k for k in matched}
    pairs: set[str] = set()
    for asm in sorted((ROOT / "src").glob("bank*.asm")):
        cur = None; prev = None
        for line in open(asm, errors="replace"):
            m = re.match(r"^([A-Za-z_]\w*):", line)
            if m:
                cur = m.group(1) if m.group(1) in names else None
                prev = None
                continue
            if cur is None:
                continue
            n = norm(line)
            if n is None:
                if line.strip().startswith("."):
                    prev = None          # a label: not adjacent
                continue
            if prev is not None:
                pairs.add(prev + " | " + n)
            prev = n
    return pairs


def main() -> None:
    if len(sys.argv) >= 2 and sys.argv[1] == "--build":
        pairs = rom_pairs()
        CACHE.parent.mkdir(exist_ok=True)
        CACHE.write_text(json.dumps(sorted(pairs)))
        print(f"{len(pairs)} pairs from the ROM's compiled functions -> {CACHE}")
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    func, attempt = sys.argv[1], Path(sys.argv[2])
    pairs = set(json.loads(CACHE.read_text())) if CACHE.is_file() else rom_pairs()
    r = subprocess.run([sys.executable, str(ROOT / "tools/match/match_function.py"), "--c", str(attempt),
                        "--func", func, "--target", func, "--diff"], capture_output=True, text=True)
    out = (r.stdout + r.stderr).splitlines()
    try:
        start = next(i for i, l in enumerate(out) if re.match(r"\s*ROM\s+ours", l))
    except StopIteration:
        sys.exit("error: no aligned disassembly (does the attempt compile and link?)\n" + "\n".join(out[-5:]))
    ours = []
    for line in out[start + 1:]:
        if not line.startswith(("    ", "  <<", "  >>")):
            break
        col = line[39:].strip() if len(line) > 39 else ""
        if col and not col.startswith("."):
            ours.append(col)
        elif col.startswith("."):
            ours.append(None)
    prev = None; missing = []
    for ins in ours:
        n = norm(ins) if ins else None
        if n is None:
            prev = None
            continue
        if prev is not None and prev + " | " + n not in pairs:
            missing.append(prev + " | " + n)
        prev = n
    seen = set()
    for m in missing:
        if m not in seen:
            seen.add(m); print(m)
    if not missing:
        print("every adjacent pair of the attempt occurs somewhere in the ROM's compiled code")


if __name__ == "__main__":
    main()
