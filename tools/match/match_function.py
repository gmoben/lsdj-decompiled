#!/usr/bin/env python3
"""Matching-decompilation harness for LSDj (Track 2).

Compile a C function with the *period* GBDK 2.95-3 / SDCC 2.2.1 toolchain (the
compiler LSDj was built with; small model = direct calls, args at sp+4) and
compare the emitted machine code against the original ROM:

  * --target NAME|BB:AAAA : link the function at its ROM address (every extern
                            resolved from our symbol maps) and diff it against
                            the ROM there -- the normal way to check a match
  * --offset N            : diff *unlinked* bytes against ROM file offset N
                            (leaf functions only -- no relocations)
  * --search              : scan the whole ROM for the unlinked byte pattern
  * (none of these)       : just print the compiled bytes (harness self-check)

The compiler also loads tools/match/lsdj.peep, the peephole rules recovered
from LSDj's own code (see docs/toolchain.md); pass --stock to compile with the
stock GBDK 2.95 rules only.

Pipeline (faithful to how LSDj was actually built -- asxxxx, not RGBDS):
    file.c --[sdcc -mgbz80 -S --peep-file lsdj.peep]--> file.asm
           --[as-gbz80 -l -o]--> file.o + file.lst
           --[link-gbz80 -i -b_CODE=<addr> -g_<sym>=<addr>...]--> file.ihx
    -> the function's bytes (between ___<func>_start / ___<func>_end) vs ROM.

Requires: wine, and the toolchain fetched via tools/toolchain/fetch_gbdk295.sh.

Usage:
    tools/match/match_function.py --c c/bank01/DrawMessageBar.c --target DrawMessageBar
    tools/match/match_function.py --c snippet.c [--func NAME] [--show-asm]
    tools/match/match_function.py --c snippet.c --func foo --search
    tools/match/match_function.py --c snippet.c --func foo --offset 0x1234
"""

from __future__ import annotations

import argparse
import importlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def _default_gbdk() -> Path:
    """GBDK 2.95-3 (small model by default, like LSDj); plain 2.95 as fallback."""
    for name in ("gbdk-2.95-3", "gbdk-2.95"):
        if (REPO_ROOT / "toolchain" / name / "bin" / "sdcc.exe").is_file():
            return REPO_ROOT / "toolchain" / name
    return REPO_ROOT / "toolchain" / "gbdk-2.95-3"


GBDK = Path(os.environ["GBDK_295"]) if os.environ.get("GBDK_295") else _default_gbdk()
WINEPREFIX = Path(os.environ.get("WINEPREFIX", REPO_ROOT / "toolchain" / ".wine"))
DEFAULT_PEEP = REPO_ROOT / "tools" / "match" / "lsdj.peep"
BANK_SIZE = 0x4000
TOOL_TIMEOUT = 120  # seconds per compiler/assembler/linker run

_NOISE = re.compile(r"libEGL|pci id for fd|wine|gecko|mono|fixme:|err:winediag", re.I)


def _wine_env() -> dict[str, str]:
    env = dict(os.environ)
    env["WINEPREFIX"] = str(WINEPREFIX)
    env["WINEDEBUG"] = "-all"
    # A crashing sdcc.exe must fail fast instead of hanging in winedbg.
    env["WINEDLLOVERRIDES"] = "winedbg.exe=d"
    return env


def _run(cmd: list[str], cwd: Path | None, env: dict[str, str]) -> tuple[int, str]:
    """Run a Wine client and return (exit code, combined output).

    Never through pipes: the first client of a Wine session spawns the
    session's background services (services.exe, explorer.exe, ...), which
    inherit its stdout/stderr and outlive it -- a captured pipe would then
    block until they exit (seconds without a persistent wineserver, forever
    with one). Output goes to a file instead.
    """
    with tempfile.TemporaryFile("w+") as out:
        try:
            proc = subprocess.run(cmd, cwd=None if cwd is None else str(cwd), env=env,
                                  stdin=subprocess.DEVNULL, stdout=out,
                                  stderr=subprocess.STDOUT, timeout=TOOL_TIMEOUT)
        except subprocess.TimeoutExpired:
            sys.exit(f"error: {cmd[0]} {cmd[1] if len(cmd) > 1 else ''} timed out "
                     f"after {TOOL_TIMEOUT}s.")
        out.seek(0)
        return proc.returncode, out.read()


def _run_wine(exe: str, args: list[str], cwd: Path, env: dict[str, str]) -> int:
    rc, text = _run(["wine", str(GBDK / "bin" / exe), *args], cwd, env)
    err = "\n".join(l for l in text.splitlines() if not _NOISE.search(l))
    if rc != 0 and err.strip():
        sys.stderr.write(err + "\n")
    return rc


def _hold_wineserver(env: dict[str, str]) -> None:
    """Keep a wineserver alive for 60 s after the last client, so the Wine
    session (and its ~1 s service start-up) is shared by consecutive runs."""
    try:
        subprocess.run(["wineserver", "-p60"], env=env, stdin=subprocess.DEVNULL,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass


def _preflight() -> None:
    if not (GBDK / "bin" / "sdcc.exe").is_file():
        sys.exit(f"error: toolchain missing at {GBDK}.\n"
                 f"Run tools/toolchain/fetch_gbdk295.sh first.")
    if shutil.which("wine") is None:
        sys.exit("error: wine is required to run the period sdcc.exe.")


@contextmanager
def workdir():
    """A scratch directory on Wine's writable C: drive (sdcc 2.2.1 writes its
    temp file at the current drive's root, so it must run from there)."""
    _preflight()
    env = _wine_env()
    drive_c = WINEPREFIX / "drive_c"
    drive_c.mkdir(parents=True, exist_ok=True)
    _hold_wineserver(env)
    if not (drive_c / "windows").exists():
        _run(["wineboot", "-i"], None, env)
    rc, winpath = _run(["winepath", "-w", str(GBDK / "bin")], None, env)
    if rc == 0 and winpath.strip():
        env["WINEPATH"] = winpath.strip().splitlines()[-1]
    with tempfile.TemporaryDirectory(dir=str(drive_c), prefix="match-") as tmp:
        yield Path(tmp), env


def compile_unit(work: Path, env: dict[str, str], c_path: Path,
                 peep: Path | None) -> tuple[str, str]:
    """C -> asxxxx asm -> object + listing in `work`. Returns (asm, lst) text."""
    shutil.copyfile(c_path, work / "unit.c")
    args = ["-mgbz80", "-S"]
    if peep is not None:
        shutil.copyfile(peep, work / "rules.peep")
        args += ["--peep-file", "rules.peep"]
    rc = _run_wine("sdcc.exe", [*args, "unit.c"], work, env)
    asm = work / "unit.asm"
    if not asm.is_file():
        hint = ""
        if rc not in (0, 1):
            hint = (" sdcc.exe crashed (exit {}); SDCC 2.2.1 dies on some "
                    "constructs, e.g. testing *(UBYTE *)0xFFxx in an if -- "
                    "see docs/toolchain.md.".format(rc))
        sys.exit("error: sdcc produced no assembly." + hint)
    _run_wine("as-gbz80.exe", ["-l", "-o", "unit.o", "unit.asm"], work, env)
    lst = work / "unit.lst"
    if not lst.is_file() or not (work / "unit.o").is_file():
        sys.exit("error: as-gbz80 produced no object/listing (see errors above).")
    return asm.read_text(errors="replace"), lst.read_text(errors="replace")


def parse_code_bytes(lst_text: str) -> dict[int, int]:
    """Map area-relative address -> emitted byte from an asxxxx listing body.

    Listing body lines look like:  ``   0001 F8 08                71 \\tlda ...``
    i.e. a 4-hex address, then single-space-separated hex byte pairs, then (after
    a run of spaces) the decimal source line number, then the source. We read the
    leading hex-pair run only, which naturally yields nothing for label lines.
    """
    addr_byte: dict[int, int] = {}
    for line in lst_text.splitlines():
        m = re.match(r"\s*([0-9A-Fa-f]{4}) (.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        bm = re.match(r"((?:[0-9A-Fa-f]{2} )*[0-9A-Fa-f]{2})(?: |$)", m.group(2))
        if bm:
            for i, pair in enumerate(bm.group(1).split()):
                addr_byte[addr + i] = int(pair, 16)
    return addr_byte


def _label_addr(lst_text: str, name: str) -> int | None:
    """Address of a symbol from the listing's symbol table (``name  ADDR flags``).

    Matches the symbol-table entry (name followed by spaces then a 4-hex value),
    not the body label line (where the name is immediately followed by ``:``).
    """
    m = re.search(rf"{re.escape(name)} +([0-9A-Fa-f]{{4}})\b", lst_text)
    return int(m.group(1), 16) if m else None


def function_span(lst_text: str, func: str | None) -> tuple[str, int, int]:
    """(name, start, end) of a function, area-relative, from the listing."""
    name = (func or "").lstrip("_")
    available = sorted(set(re.findall(r"___(.+?)_start +[0-9A-Fa-f]{4}\b", lst_text)))
    if not name:
        if not available:
            sys.exit("error: no function boundary labels found; pass --func.")
        name = available[0]
    start = _label_addr(lst_text, f"___{name}_start")
    end = _label_addr(lst_text, f"___{name}_end")
    if start is None or end is None:
        sys.exit(f"error: function '{name}' not found. "
                 f"Available: {', '.join(available) or '(none)'}")
    return name, start, end


def function_bytes(lst_text: str, func: str | None) -> bytes:
    """Unlinked function bytes (only valid for code without relocations)."""
    func, start, end = function_span(lst_text, func)
    addr_byte = parse_code_bytes(lst_text)
    out = bytearray()
    for a in range(start, end):
        if a not in addr_byte:
            sys.exit(f"error: non-contiguous code at ${a:04X} (relocation?). "
                     f"Functions that reference globals or call others need "
                     f"--target, which links them at their ROM address.")
        out.append(addr_byte[a])
    return bytes(out)


# --- symbols + linking ------------------------------------------------------

def load_symbols() -> dict[str, tuple[int | None, int]]:
    """Every name we can resolve -> (ROM bank, or None for RAM/IO; address)."""
    syms: dict[str, tuple[int | None, int]] = {}
    # GBDK's register names (VBK_REG, LCDC_REG, ...) from the library's sfr.s.
    sfr = GBDK / "libc" / "gb" / "sfr.s"
    if sfr.is_file():
        org = None
        for line in sfr.read_text(errors="replace").splitlines():
            m = re.match(r"\s*\.org\s+(0x[0-9A-Fa-f]+)", line)
            if m:
                org = int(m.group(1), 16)
                continue
            m = re.match(r"_(\w+)::", line)
            if m and org is not None:
                syms[m.group(1)] = (None, org)
    # Our WRAM/HRAM map, and the hardware registers (MBC control writes such
    # as rROMB0 at $2000 appear in LSDj's C as plain variables).
    for inc in (REPO_ROOT / "include" / "hardware.inc", REPO_ROOT / "constants" / "ram.inc"):
        for m in re.finditer(r"^\s*DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]{4})\b",
                             inc.read_text(errors="replace"), re.M | re.I):
            syms[m.group(1)] = (None, int(m.group(2), 16))
    # The working-song SRAM structures (SramInstruments = bank 1 $B080, ...),
    # from tools/gen_sym.py's map; these are CPU addresses in the $A000 window.
    try:
        sys.path.insert(0, str(REPO_ROOT / "tools"))
        gen_sym = importlib.import_module("gen_sym")
        for off, name in gen_sym.SRAM_MAP:
            syms[name] = (None, 0xA000 + (off & 0x1FFF))
    except Exception:  # noqa: BLE001 - the harness must still work without it
        pass
    # Our ROM symbol maps (global names only; one link namespace, so unique).
    for p in sorted((REPO_ROOT / "src").glob("bank*.sym")):
        bank = int(p.stem[4:], 16)
        for line in p.read_text().splitlines():
            m = re.match(r"^\s*([0-9A-Fa-f]{4})\s+([A-Za-z_]\w*)", line)
            if m:
                syms.setdefault(m.group(2), (bank, int(m.group(1), 16)))
    return syms


def rom_offset(bank: int, addr: int) -> int:
    return bank * BANK_SIZE + (addr - (0x4000 if bank else 0))


def parse_target(spec: str, syms: dict[str, tuple[int | None, int]]) -> tuple[str, int, int]:
    m = re.fullmatch(r"\$?([0-9A-Fa-f]{1,2}):\$?([0-9A-Fa-f]{4})", spec)
    if m:
        bank, addr = int(m.group(1), 16), int(m.group(2), 16)
        return f"${bank:02X}:${addr:04X}", bank, addr
    sbank, saddr = syms.get(spec, (None, 0))
    if sbank is None:
        sys.exit(f"error: unknown target '{spec}' (use a src/bankNN.sym name or BB:AAAA).")
    return spec, sbank, saddr


def rom_extent(syms: dict[str, tuple[int | None, int]], bank: int, addr: int) -> int | None:
    """Distance to the next global symbol in the same bank (informational)."""
    nxt = [a for b, a in syms.values() if b == bank and a > addr]
    return min(nxt) - addr if nxt else None


def link_function(work: Path, env: dict[str, str], asm: str, lst: str,
                  func: str | None, bank: int, addr: int,
                  syms: dict[str, tuple[int | None, int]],
                  defines: dict[str, int]) -> tuple[str, bytes, int]:
    """Link the unit so `func` lands at `addr`; return (name, bytes, start)."""
    name, start, end = function_span(lst, func)
    defined = set(re.findall(r"^(_\w+)::?", asm, re.M))
    externs = sorted(set(re.findall(r"^\s*\.globl\s+(_\w+)", asm, re.M)) - defined)
    gargs, missing, other_bank = [], [], []
    for sym in externs:
        plain = sym[1:]
        if plain in defines:
            val = defines[plain]
        elif plain in syms:
            sbank, val = syms[plain]
            if sbank not in (None, 0, bank):
                other_bank.append(f"{plain} (${sbank:02X})")
        else:
            missing.append(plain)
            continue
        gargs.append(f"-g{sym}=0x{val:04X}")
    if missing:
        sys.exit("error: unresolved externs: " + ", ".join(missing) +
                 "\n(name them in src/*.sym / constants/ram.inc / "
                 "include/hardware.inc, or pass --define NAME=ADDR)")
    if other_bank:
        print("note: callees in other ROM banks (the function must page them in): "
              + ", ".join(other_bank), file=sys.stderr)
    base = addr - start
    # Functions land in _CODE, or in _HOME when declared `nonbanked`; a unit
    # uses one or the other, so both areas are based at the same address.
    _run_wine("link-gbz80.exe",
              ["-n", "--", "-i", "-m", "unit.ihx", f"-b_CODE=0x{base:04X}",
               f"-b_HOME=0x{base:04X}", *gargs, "unit.o"],
              work, env)
    ihx = work / "unit.ihx"
    if not ihx.is_file():
        sys.exit("error: link-gbz80 produced no output (see errors above).")
    mem: dict[int, int] = {}
    for line in ihx.read_text().splitlines():
        line = line.strip()
        if not line.startswith(":") or int(line[7:9], 16) != 0:
            continue
        n, a = int(line[1:3], 16), int(line[3:7], 16)
        for i, byte in enumerate(bytes.fromhex(line[9:9 + 2 * n])):
            mem[a + i] = byte
    missing_addr = [a for a in range(addr, addr + end - start) if a not in mem]
    if missing_addr:
        sys.exit(f"error: linked image has no byte at ${missing_addr[0]:04X}.")
    return name, bytes(mem[a] for a in range(addr, addr + end - start)), start


def listing_near(lst: str, rel_addr: int, span: int = 6) -> list[str]:
    """Listing lines whose area-relative address is within `span` of rel_addr."""
    out = []
    for line in lst.splitlines():
        m = re.match(r"\s*([0-9A-Fa-f]{4}) ", line)
        if m and abs(int(m.group(1), 16) - rel_addr) <= span:
            out.append(line.rstrip())
    return out


# --- output -----------------------------------------------------------------

def gb_location(offset: int) -> str:
    bank = offset // BANK_SIZE
    addr = offset % BANK_SIZE + (0 if bank == 0 else 0x4000)
    return f"bank ${bank:02X}:${addr:04X}"


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def report_diff(code: bytes, target: bytes, addr: int) -> list[int]:
    diffs = [i for i in range(max(len(code), len(target)))
             if i >= len(code) or i >= len(target) or code[i] != target[i]]
    if not diffs:
        return diffs
    first = diffs[0]
    print(f"==> MISMATCH: {len(diffs)} differing byte(s); first at "
          f"+${first:02X} (${addr + first:04X})")
    lo = max(0, first - 4)
    hi = min(max(len(code), len(target)), first + 12)
    for off in range(lo, hi):
        t = f"{target[off]:02X}" if off < len(target) else "--"
        c = f"{code[off]:02X}" if off < len(code) else "--"
        print(f"    ${addr + off:04X}  rom {t}  ours {c}{'  <<' if t != c else ''}")
    return diffs


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--c", required=True, type=Path, help="C source file to compile")
    ap.add_argument("--func", help="function name to extract (default: the target "
                                   "name, else the first function found)")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "lsdj9_4_0.gb",
                    help="base ROM (default: lsdj9_4_0.gb)")
    ap.add_argument("--target", help="link at this ROM location and diff: a "
                                     "src/bankNN.sym name or BB:AAAA")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=ADDR",
                    help="extra/overriding link symbol (repeatable)")
    ap.add_argument("--offset", type=lambda s: int(s, 0),
                    help="diff unlinked bytes against ROM bytes at this file offset")
    ap.add_argument("--search", action="store_true",
                    help="scan the ROM for the unlinked byte pattern")
    ap.add_argument("--stock", action="store_true",
                    help="compile without tools/match/lsdj.peep")
    ap.add_argument("--peep", type=Path, default=DEFAULT_PEEP,
                    help="peephole rule file (default: tools/match/lsdj.peep)")
    ap.add_argument("--show-asm", action="store_true", help="print the SDCC asm")
    ap.add_argument("--dump", type=Path, metavar="FILE",
                    help="with --target: also write the linked function bytes to FILE")
    args = ap.parse_args()

    if not args.c.is_file():
        sys.exit(f"error: C file not found: {args.c}")
    peep = None if args.stock else args.peep
    if peep is not None and not peep.is_file():
        sys.exit(f"error: peephole file not found: {peep}")
    defines = {}
    for d in args.define:
        k, _, v = d.partition("=")
        defines[k.strip()] = int(v, 0)

    rom = b""
    if args.target or args.offset is not None or args.search:
        if not args.rom.is_file():
            sys.exit(f"error: ROM not found: {args.rom} (supply your own copy).")
        rom = args.rom.read_bytes()

    with workdir() as (work, env):
        asm, lst = compile_unit(work, env, args.c, peep)
        if args.show_asm:
            print("=== SDCC 2.2.1 assembly ===")
            print(asm)

        if args.target:
            syms = load_symbols()
            label, bank, addr = parse_target(args.target, syms)
            func = args.func or (label if not label.startswith("$") else None)
            name, code, start = link_function(work, env, asm, lst, func, bank,
                                              addr, syms, defines)
            off = rom_offset(bank, addr)
            target = rom[off:off + len(code)]
            extent = rom_extent(syms, bank, addr)
            if args.dump:
                args.dump.write_bytes(code)
            print(f"linked {name} at ${bank:02X}:${addr:04X}: {len(code)} bytes"
                  + (f" (ROM routine spans {extent} bytes to the next symbol)"
                     if extent is not None else "")
                  + ("" if peep is None else f"; rules: {peep.name}"))
            if report_diff(code, target, addr):
                for line in listing_near(lst, start + next(
                        i for i in range(len(code)) if i >= len(target) or code[i] != target[i])):
                    print(f"    | {line}")
                sys.exit(2)
            print(f"==> MATCH: byte-identical to ROM ${bank:02X}:${addr:04X}-"
                  f"${addr + len(code) - 1:04X}"
                  + ("" if extent is None or extent <= len(code) else
                     f" (NB: {extent - len(code)} more ROM byte(s) before the next symbol)"))
            return

        code = function_bytes(lst, args.func)

    print(f"compiled {len(code)} bytes: {hexs(code)}")

    if args.offset is not None:
        target = rom[args.offset:args.offset + len(code)]
        print(f"ROM @ 0x{args.offset:06X} ({gb_location(args.offset)}): {hexs(target)}")
        if target == code:
            print("==> MATCH: bytes are identical.")
        else:
            report_diff(code, target, args.offset)
            sys.exit(2)

    if args.search:
        hits = []
        start = 0
        while (i := rom.find(code, start)) != -1:
            hits.append(i)
            start = i + 1
        if hits:
            print(f"==> found {len(hits)} occurrence(s):")
            for h in hits:
                print(f"    0x{h:06X}  ({gb_location(h)})")
        else:
            print("==> not found in ROM (expected for synthetic snippets; a real "
                  "LSDj function usually references globals -- use --target).")


if __name__ == "__main__":
    main()
