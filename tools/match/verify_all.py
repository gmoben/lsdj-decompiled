#!/usr/bin/env python3
"""Verify every matching-C source under c/ against the ROM (`make match`).

For each c/bankNN/*.c, compile it once with the period toolchain and link
each function it defines at that function's ROM address (from src/*.sym),
then compare the bytes. Prints one line per function and exits non-zero if
any function fails to match, so the C tree is a regression suite for the
compiler model (tools/match/lsdj.peep).

The harness keeps one wineserver alive across the batch, so the whole run
takes a few seconds.

With --write DIR (what `make match` does), every matched function's bytes
are written to DIR/bankNN/Name.bin and the manifest c/matched.json is
refreshed; tools/carve_clean.py then renders those ROM ranges as INCBINs of
the compiled bytes (or of the ROM when the bytes are absent), so the C is the
source of those bytes in the ROM build and `make compare` verifies it.

Usage: tools/match/verify_all.py [--write DIR] [FILE.c ...]
"""

from __future__ import annotations

import importlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
mf = importlib.import_module("match_function")

DEFN = re.compile(r"^(?:[A-Za-z_][\w \t*]*?\b)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*$")


def functions_in(src: str) -> list[str]:
    """Names of the functions *defined* (not just declared) in a C source."""
    out: list[str] = []
    lines = src.splitlines()
    for i, line in enumerate(lines):
        m = DEFN.match(line.strip())
        if not m or line.strip().startswith(("#", "/", "*")):
            continue
        # A definition is followed by "{" (same or next line).
        rest = lines[i + 1].strip() if i + 1 < len(lines) else ""
        if rest.startswith("{") or line.rstrip().endswith("{"):
            out.append(m.group(1))
    return out


MANIFEST = mf.REPO_ROOT / "c" / "matched.json"


def main() -> None:
    argv = sys.argv[1:]
    write_dir: Path | None = None
    if argv and argv[0] == "--write":
        write_dir = Path(argv[1])
        argv = argv[2:]
    files = [Path(a) for a in argv] or sorted((mf.REPO_ROOT / "c").glob("bank*/*.c"))
    if not files:
        sys.exit("no C sources under c/")
    rom_path = mf.REPO_ROOT / "lsdj9_4_0.gb"
    if not rom_path.is_file():
        sys.exit(f"error: ROM not found: {rom_path}")
    rom = rom_path.read_bytes()
    syms = mf.load_symbols()
    peep = mf.DEFAULT_PEEP

    failed = 0
    matched: dict[str, dict[str, str | int]] = {}
    # (match_function.workdir keeps one wineserver alive across the batch.)
    for c_path in files:
        funcs = functions_in(c_path.read_text())
        if not funcs:
            print(f"{c_path}: no function definitions found")
            failed += 1
            continue
        with mf.workdir() as (work, env):
            asm, lst = mf.compile_unit(work, env, c_path, peep)
            for func in funcs:
                bank, addr = syms.get(func, (None, 0))
                if bank is None:
                    print(f"  {func:24s} ?? not a ROM symbol (src/*.sym)")
                    failed += 1
                    continue
                name, code, _ = mf.link_function(work, env, asm, lst, func, bank, addr, syms, {})
                off = mf.rom_offset(bank, addr)
                ok = rom[off:off + len(code)] == code
                rel = c_path.relative_to(mf.REPO_ROOT).as_posix()
                print(f"  {name:24s} ${bank:02X}:${addr:04X}  {len(code):4d} B  "
                      f"{'MATCH' if ok else 'MISMATCH'}   ({rel})")
                failed += not ok
                if ok:
                    matched[name] = {"bank": f"{bank:02X}", "addr": f"{addr:04X}",
                                     "len": len(code), "src": rel}
                    if write_dir is not None:
                        out = write_dir / f"bank{bank:02x}" / f"{name}.bin"
                        out.parent.mkdir(parents=True, exist_ok=True)
                        out.write_bytes(code)
    if write_dir is not None and not failed and not argv:
        # Full run: the manifest is the complete list of C-owned ROM ranges.
        MANIFEST.write_text(json.dumps(dict(sorted(matched.items())), indent=1) + "\n")
        print(f"wrote {len(matched)} function(s) to {write_dir}/ and {MANIFEST.relative_to(mf.REPO_ROOT)}")
    if failed:
        sys.exit(f"==> {failed} function(s) do not match; run match_function.py --target on one for the diff.")
    print("==> all matching-C sources are byte-identical to the ROM.")


if __name__ == "__main__":
    main()
