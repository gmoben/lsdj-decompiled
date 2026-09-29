#!/usr/bin/env python3
"""Verify every matching-C source under c/ against the ROM (`make match`).

For each c/bankNN/*.c, compile it once with the period toolchain and link
each function it defines at that function's ROM address (from src/*.sym),
then compare the bytes. Prints one line per function and exits non-zero if
any function fails to match, so the C tree is a regression suite for the
compiler model (tools/match/lsdj.peep).

Results are cached under build/match-cache/: a unit's compiler output is
keyed on the C source, the peephole rules and the harness code; a
function's linked bytes additionally on its address and the values of every
extern it uses. Only changed sources (or changed symbol values) reach Wine,
so `make match` and the pre-commit hook stay fast as the C tree grows.

With --write DIR (what `make match` does), every matched function's bytes
are written to DIR/bankNN/Name.bin and the manifest c/matched.json is
refreshed; tools/carve_clean.py then renders those ROM ranges as INCBINs of
the compiled bytes (or of the ROM when the bytes are absent), so the C is the
source of those bytes in the ROM build and `make compare` verifies it.

Given explicit files (which may live outside c/, e.g. work in progress) it
checks just those, reports where each mismatch starts, and writes nothing.

Usage: tools/match/verify_all.py [--write DIR] [FILE.c ...]
"""

from __future__ import annotations

import hashlib
import importlib
import json
import re
import shutil
import sys
from contextlib import ExitStack
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
mf = importlib.import_module("match_function")

DEFN = re.compile(r"^(?:[A-Za-z_][\w \t*]*?\b)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:nonbanked|banked|critical)?\s*$")
MANIFEST = mf.REPO_ROOT / "c" / "matched.json"
CACHE = mf.REPO_ROOT / "build" / "match-cache"
HARNESS = [mf.REPO_ROOT / "tools" / "match" / n for n in ("match_function.py", "peep.py")]


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


def _sha(*parts: bytes | str) -> str:
    h = hashlib.sha1()
    for p in parts:
        h.update(p.encode() if isinstance(p, str) else p)
        h.update(b"\0")
    return h.hexdigest()[:20]


class Runner:
    """Compile/link with caching; opens the Wine work dir only when needed."""

    def __init__(self, stack: ExitStack, peep: Path):
        self.stack, self.peep = stack, peep
        headers = sorted(mf.INCLUDE_DIR.glob("*.h"))
        self.salt = _sha(peep.read_bytes(), *(p.read_bytes() for p in HARNESS + headers), mf.toolchain_id())
        self._work = None
        self.wine_runs = 0

    def work(self):
        if self._work is None:
            self._work = self.stack.enter_context(mf.workdir())
        return self._work

    def compile(self, c_path: Path) -> tuple[Path, str, str]:
        d = CACHE / _sha(self.salt, c_path.read_bytes())
        if not all((d / f).is_file() for f in ("unit.asm", "unit.lst", "unit.o")):
            work, env = self.work()
            self.wine_runs += 1
            mf.compile_unit(work, env, c_path, self.peep)
            d.mkdir(parents=True, exist_ok=True)
            for f in ("unit.asm", "unit.lst", "unit.o"):
                shutil.copyfile(work / f, d / f)
        return d, (d / "unit.asm").read_text(errors="replace"), (d / "unit.lst").read_text(errors="replace")

    def link(self, d: Path, asm: str, lst: str, func: str, bank: int, addr: int,
             syms) -> tuple[str, bytes]:
        iso = mf.isolate(asm, func)
        if iso != asm:
            # Calls to neighbours in this unit: reassemble with them renamed
            # away (cached like a compile), so they link to their ROM addresses.
            d2 = d / f"iso-{func}"
            if not all((d2 / f).is_file() for f in ("unit.lst", "unit.o")):
                work, env = self.work()
                self.wine_runs += 1
                mf.assemble(work, env, iso)
                d2.mkdir(parents=True, exist_ok=True)
                for f in ("unit.lst", "unit.o"):
                    shutil.copyfile(work / f, d2 / f)
            d, asm, lst = d2, iso, (d2 / "unit.lst").read_text(errors="replace")
        name, start, _end, gargs, _notes = mf.link_plan(asm, lst, func, bank, addr, syms, {})
        f = d / f"{name}-{_sha(str(addr - start), *gargs)}.bin"
        if not f.is_file():
            work, env = self.work()
            self.wine_runs += 1
            shutil.copyfile(d / "unit.o", work / "unit.o")
            _, code, _ = mf.link_function(work, env, asm, lst, func, bank, addr, syms, {}, quiet=True)
            f.write_bytes(code)
        return name, f.read_bytes()


def main() -> None:
    argv = sys.argv[1:]
    write_dir: Path | None = None
    if argv and argv[0] == "--write":
        write_dir = Path(argv[1])
        argv = argv[2:]
    files = [Path(a).resolve() for a in argv] or sorted((mf.REPO_ROOT / "c").glob("bank*/*.c"))
    if not files:
        sys.exit("no C sources under c/")
    rom_path = mf.REPO_ROOT / "lsdj9_4_0.gb"
    if not rom_path.is_file():
        sys.exit(f"error: ROM not found: {rom_path}")
    rom = rom_path.read_bytes()
    syms = mf.load_symbols()

    failed = 0
    matched: dict[str, dict[str, str | int]] = {}
    with ExitStack() as stack:
        run = Runner(stack, mf.DEFAULT_PEEP)
        for c_path in files:
            try:
                rel = c_path.relative_to(mf.REPO_ROOT).as_posix()
            except ValueError:
                rel = str(c_path)
            funcs = functions_in(c_path.read_text())
            if not funcs:
                print(f"{rel}: no function definitions found")
                failed += 1
                continue
            try:
                d, asm, lst = run.compile(c_path)
            except SystemExit as e:
                print(f"{rel}: compile failed: {e}")
                failed += len(funcs)
                continue
            for func in funcs:
                bank, addr = syms.get(func, (None, 0))
                if bank is None:
                    print(f"  {func:24s} ?? not a ROM symbol (src/*.sym)   ({rel})")
                    failed += 1
                    continue
                try:
                    name, code = run.link(d, asm, lst, func, bank, addr, syms)
                except SystemExit as e:
                    print(f"  {func:24s} link failed: {e}   ({rel})")
                    failed += 1
                    continue
                off = mf.rom_offset(bank, addr)
                target = rom[off:off + len(code)]
                ok = target == code
                where = ""
                if not ok:
                    first = next(i for i in range(len(code)) if code[i] != target[i])
                    ndiff = sum(a != b for a, b in zip(code, target))
                    where = f"  first diff +${first:02X} (${addr + first:04X}), {ndiff} B differ"
                print(f"  {name:24s} ${bank:02X}:${addr:04X}  {len(code):4d} B  "
                      f"{'MATCH' if ok else 'MISMATCH'}{where}   ({rel})")
                failed += not ok
                if ok:
                    matched[name] = {"bank": f"{bank:02X}", "addr": f"{addr:04X}",
                                     "len": len(code), "src": rel}
                    if write_dir is not None and not argv:
                        out = write_dir / f"bank{bank:02x}" / f"{name}.bin"
                        out.parent.mkdir(parents=True, exist_ok=True)
                        if not out.is_file() or out.read_bytes() != code:
                            out.write_bytes(code)
    if write_dir is not None and not failed and not argv:
        # Full run: the manifest is the complete list of C-owned ROM ranges.
        text = json.dumps(dict(sorted(matched.items())), indent=1) + "\n"
        if not MANIFEST.is_file() or MANIFEST.read_text() != text:
            MANIFEST.write_text(text)
        print(f"wrote {len(matched)} function(s) to {write_dir}/ and "
              f"{MANIFEST.relative_to(mf.REPO_ROOT)} ({run.wine_runs} compile/link run(s); rest cached)")
    if failed:
        sys.exit(f"==> {failed} function(s) do not match; run match_function.py --target on one for the diff.")
    print(f"==> all {len(matched)} function(s) byte-identical to the ROM.")


if __name__ == "__main__":
    main()
