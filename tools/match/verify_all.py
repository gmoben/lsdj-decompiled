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
are written to DIR/bankNN/Name.bin, its RGBDS translation (code plus the
literals and const data after it, each row with its ROM address:
tools/match/to_rgbds.py) to DIR/bankNN/Name.lines, and the manifest
c/matched.json is refreshed; tools/carve_clean.py then renders those ROM
ranges from the translation -- RGBDS source with symbolic operands, so the
C is the source of those bytes and of their references -- (or as INCBINs of
the ROM when it is absent), and `make compare` verifies it.

Given explicit files (which may live outside c/, e.g. work in progress) it
checks just those, reports where each mismatch starts, and writes nothing.

--fork is a fork's build (`make FORK=1`): C that no longer matches is not a
failure but the fork's code. A function the ROM has (c/matched.json, the
range it owned: `addr` to `end`) that compiles to other bytes is CHANGED,
translated to replace that range; one the ROM lacks (no src/*.sym address)
is NEW, translated to go before its bank's free space (c/bankNN/ names the
bank). Both are translated with only their own label: nothing of the
original's lands inside them. The manifest is build/c/fork.json (each
function's bank, range and status); c/matched.json stays the original's.
A file that says @sdcc4 is new code for a modern SDCC (tools/match/sdcc4.py):
compiled with that, all of it NEW.

Usage: tools/match/verify_all.py [--write DIR] [--fork] [FILE.c ...]
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
to_rgbds = importlib.import_module("to_rgbds")
sdcc4 = importlib.import_module("sdcc4")

DEFN = re.compile(r"^(?:[A-Za-z_][\w \t*]*?\b)?([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:nonbanked|banked|critical)?\s*$")
MANIFEST = mf.REPO_ROOT / "c" / "matched.json"
FORK_MANIFEST = mf.REPO_ROOT / "build" / "c" / "fork.json"
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
        ft = f.with_suffix(".tail")
        if not f.is_file() or not ft.is_file():
            work, env = self.work()
            self.wine_runs += 1
            shutil.copyfile(d / "unit.o", work / "unit.o")
            _, code, _, tail = mf.link_function(work, env, asm, lst, func, bank, addr, syms, {}, quiet=True)
            f.write_bytes(code)
            ft.write_bytes(tail)
        return name, f.read_bytes(), ft.read_bytes()


def main() -> None:
    argv = sys.argv[1:]
    write_dir: Path | None = None
    if argv and argv[0] == "--write":
        write_dir = Path(argv[1])
        argv = argv[2:]
    fork = "--fork" in argv
    argv = [a for a in argv if a != "--fork"]
    # (editor lock and autosave files -- Emacs' `.#Name.c` -- are not sources)
    files = [Path(a).resolve() for a in argv] or sorted(
        p for p in (mf.REPO_ROOT / "c").glob("bank*/*.c") if not p.name.startswith((".", "#")))
    if not files:
        sys.exit("no C sources under c/")
    rom_path = mf.REPO_ROOT / "lsdj9_4_0.gb"
    if not rom_path.is_file():
        sys.exit(f"error: ROM not found: {rom_path}")
    rom = rom_path.read_bytes()
    syms = mf.load_symbols()
    baseline = json.loads(MANIFEST.read_text()) if MANIFEST.is_file() else {}
    new_funcs: dict[str, int] = {}                   # a fork's new functions: their bank
    modern = [c for c in files if fork and sdcc4.is_sdcc4(c.read_text())]
    if fork:
        for c_path in files:
            m = re.search(r"bank([0-9a-fA-F]{2})", c_path.parent.name)
            for func in functions_in(c_path.read_text()):
                if func not in syms and m:
                    new_funcs[func] = int(m.group(1), 16)
        for func, bank in new_funcs.items():          # linked anywhere: the translation is labels
            syms[func] = (bank, 0x4000 if bank else 0x0150)
        to_rgbds.EXTRA_NAMES.update(new_funcs)
    files = [c for c in files if c not in modern]
    forked: dict[str, dict[str, str | int | None]] = {}
    failed = 0
    for c_path in modern:                         # a fork's new C for SDCC 4
        rel = c_path.relative_to(mf.REPO_ROOT).as_posix()
        bank = int(re.search(r"bank([0-9a-fA-F]{2})", c_path.parent.name).group(1), 16)
        try:
            funcs, doc = sdcc4.translate(c_path, set(new_funcs))
        except (SystemExit, ValueError) as e:
            print(f"{rel}: {e}")
            failed += 1
            continue
        lines = write_dir / f"bank{bank:02x}" / f"{c_path.stem}.sdcc4.lines" if write_dir else None
        if lines is not None:
            lines.parent.mkdir(parents=True, exist_ok=True)
            text = json.dumps(doc, indent=0)
            if not lines.is_file() or lines.read_text() != text:
                lines.write_text(text)
        n = sum(1 for _a, kind, _t in doc["rows"] if kind == "I")
        for func in funcs:
            print(f"  {func:24s} ${bank:02X}         NEW  SDCC 4, before the bank's free space   ({rel})")
            forked[func] = {"bank": f"{bank:02X}", "status": "new", "src": rel, "lo": None, "hi": None,
                            "lines": f"bank{bank:02x}/{c_path.stem}.sdcc4.lines"}
        print(f"  ({rel}: {n} instruction and data rows)")

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
                    name, code, tail = run.link(d, asm, lst, func, bank, addr, syms)
                except SystemExit as e:
                    print(f"  {func:24s} link failed: {e}   ({rel})")
                    failed += 1
                    continue
                off = mf.rom_offset(bank, addr)
                target = rom[off:off + len(code)]
                ok = target == code and func not in new_funcs
                where = ""
                if not ok and func in new_funcs:
                    where = "  new: before the bank's free space"
                elif not ok:
                    first = next((i for i in range(len(code)) if i >= len(target) or code[i] != target[i]),
                                 len(code))
                    ndiff = sum(a != b for a, b in zip(code, target))
                    where = f"  first diff +${first:02X} (${addr + first:04X}), {ndiff} B differ"
                else:
                    # the literals/const data the unit places after the code
                    rom_tail = rom[off + len(code):off + len(code) + len(tail)]
                    if rom_tail != tail:
                        ok = False
                        k = next(i for i in range(len(tail)) if i >= len(rom_tail) or tail[i] != rom_tail[i])
                        where = f"  literals/const data after the code differ at ${addr + len(code) + k:04X}"
                status = "MATCH" if ok else ("NEW" if func in new_funcs else
                                             "CHANGED" if fork else "MISMATCH")
                print(f"  {name:24s} ${bank:02X}:${addr:04X}  {len(code):4d} B  {status}{where}   ({rel})")
                if fork and not ok:
                    old = baseline.get(name)
                    if status == "CHANGED" and (old is None or "end" not in old):
                        print(f"  {name:24s} not in c/matched.json with its range: can't replace it")
                        failed += 1
                        continue
                    lines = write_dir / f"bank{bank:02x}" / f"{name}.lines" if write_dir else None
                    try:
                        doc = to_rgbds.annotate(asm, lst, name, bank, addr, len(code) + len(tail), syms,
                                                c_path.read_text(), fork=status.lower(),
                                                replaces=int(old["end"], 16) if status == "CHANGED" else None)
                    except (ValueError, KeyError) as e:
                        print(f"  {name:24s} no RGBDS translation ({e})")
                        failed += 1
                        continue
                    if lines is not None:
                        lines.parent.mkdir(parents=True, exist_ok=True)
                        text = json.dumps(doc, indent=0)
                        if not lines.is_file() or lines.read_text() != text:
                            lines.write_text(text)
                    forked[name] = {"bank": f"{bank:02X}", "status": status.lower(), "src": rel,
                                    "lo": None if status == "NEW" else f"{addr:04X}",
                                    "hi": None if status == "NEW" else old["end"]}
                    continue
                failed += not ok
                if ok:
                    matched[name] = {"bank": f"{bank:02X}", "addr": f"{addr:04X}",
                                     "len": len(code), "end": f"{addr + len(code) + len(tail):04X}",
                                     "src": rel}
                    forked[name] = {"bank": f"{bank:02X}", "status": "match", "src": rel,
                                    "lo": f"{addr:04X}", "hi": f"{addr + len(code) + len(tail):04X}"}
                    if write_dir is not None and not argv:
                        out = write_dir / f"bank{bank:02x}" / f"{name}.bin"
                        out.parent.mkdir(parents=True, exist_ok=True)
                        if not out.is_file() or out.read_bytes() != code:
                            out.write_bytes(code)
                        # the same range as RGBDS source (code + the data after it)
                        lines = out.with_suffix(".lines")
                        try:
                            text = json.dumps(to_rgbds.annotate(
                                asm, lst, name, bank, addr, len(code) + len(tail), syms,
                                c_path.read_text()), indent=0)
                        except (ValueError, KeyError) as e:
                            print(f"  {name:24s} no RGBDS translation ({e}); the build INCBINs it")
                            lines.unlink(missing_ok=True)
                        else:
                            if not lines.is_file() or lines.read_text() != text:
                                lines.write_text(text)
    if write_dir is not None and not failed and not argv and fork:
        # A fork's build: the original's manifest stays; this one is the fork's
        FORK_MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        text = json.dumps(dict(sorted(forked.items())), indent=1) + "\n"
        if not FORK_MANIFEST.is_file() or FORK_MANIFEST.read_text() != text:
            FORK_MANIFEST.write_text(text)
        changed = sum(v["status"] != "match" for v in forked.values())
        print(f"wrote {len(forked)} function(s) to {write_dir}/ and "
              f"{FORK_MANIFEST.relative_to(mf.REPO_ROOT)}: {changed} changed or new")
    elif write_dir is not None and not failed and not argv:
        # Full run: the manifest is the complete list of C-owned ROM ranges.
        text = json.dumps(dict(sorted(matched.items())), indent=1) + "\n"
        if not MANIFEST.is_file() or MANIFEST.read_text() != text:
            MANIFEST.write_text(text)
        print(f"wrote {len(matched)} function(s) to {write_dir}/ and "
              f"{MANIFEST.relative_to(mf.REPO_ROOT)} ({run.wine_runs} compile/link run(s); rest cached)")
    if failed:
        sys.exit(f"==> {failed} function(s) do not match; run match_function.py --target on one for the diff.")
    if fork:
        n = {k: sum(v["status"] == k for v in forked.values()) for k in ("match", "changed", "new")}
        print(f"==> a fork's build: {n['match']} function(s) as in the ROM, {n['changed']} changed, {n['new']} new.")
        return
    print(f"==> all {len(matched)} function(s) byte-identical to the ROM.")


if __name__ == "__main__":
    main()
