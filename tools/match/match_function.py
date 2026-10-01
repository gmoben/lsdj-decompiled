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

The compiler's output is then rewritten with tools/match/lsdj.peep, the
peephole rules recovered from LSDj's own code (see docs/toolchain.md), by
tools/match/peep.py; pass --stock for the stock GBDK 2.95 rules only.

Pipeline (faithful to how LSDj was actually built -- asxxxx, not RGBDS):
    file.c --[sdcc -mgbz80 -S]--> file.asm --[peep.py lsdj.peep]--> file.asm
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
sys.path.insert(0, str(Path(__file__).resolve().parent))   # tools/match/peep.py


def _default_gbdk() -> Path:
    """GBDK 2.95-3 (small model by default, like LSDj); plain 2.95 as fallback."""
    for name in ("gbdk-2.95-3", "gbdk-2.95"):
        if (REPO_ROOT / "toolchain" / name / "bin" / "sdcc.exe").is_file():
            return REPO_ROOT / "toolchain" / name
    return REPO_ROOT / "toolchain" / "gbdk-2.95-3"


GBDK = Path(os.environ["GBDK_295"]) if os.environ.get("GBDK_295") else _default_gbdk()
# SDCC 2.2.1 built natively from the GBDK 2.95-2 sources
# (tools/toolchain/build_sdcc221.sh): same output as GBDK 2.95-3's Win32
# binaries, without their GC crashes, and no Wine. Preferred when present;
# LSDJ_USE_WINE=1 forces the Win32 tools.
NATIVE = Path(os.environ.get("LSDJ_NATIVE_BIN", REPO_ROOT / "toolchain" / "sdcc-2.2.1-native" / "bin"))


# Extra SDCC options (experiments; part of the cache key via toolchain_id).
SDCC_FLAGS = os.environ.get("LSDJ_SDCC_FLAGS", "").split()


def use_native() -> bool:
    return (NATIVE / "sdcc").is_file() and not os.environ.get("LSDJ_USE_WINE")


def toolchain_id() -> str:
    """Identifies the compiler for caches (both builds emit the same code)."""
    return (f"native:{NATIVE}" if use_native() else f"wine:{GBDK}") + " ".join([""] + SDCC_FLAGS)
WINEPREFIX = Path(os.environ.get("WINEPREFIX", REPO_ROOT / "toolchain" / ".wine"))
DEFAULT_PEEP = REPO_ROOT / "tools" / "match" / "lsdj.peep"
INCLUDE_DIR = REPO_ROOT / "c" / "include"   # shared prototypes/externs (lsdj.h)
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
    """Run a toolchain program (`sdcc.exe`, `as-gbz80.exe`, ...): natively
    when the native build is present, else the Win32 binary under Wine."""
    if use_native():
        rc, text = _run([str(NATIVE / exe.removesuffix(".exe")), *args], cwd, env)
        err = "\n".join(l for l in text.splitlines()
                        if not l.startswith(("Cannot exec", "?ASlink-Warning")) and l.strip())
        if rc != 0 and err.strip():
            sys.stderr.write(err + "\n")
        return rc
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
    """A scratch directory -- with Wine, on its writable C: drive (sdcc 2.2.1
    writes its temp file at the current drive's root, so it must run there)."""
    if use_native():
        env = dict(os.environ)
        env["PATH"] = f"{NATIVE}{os.pathsep}{env.get('PATH', '')}"   # sdcc runs sdcpp
        with tempfile.TemporaryDirectory(prefix="lsdj-match-") as tmp:
            yield Path(tmp), env
        return
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


def repair_globls(asm: str) -> str:
    """Undo SDCC 2.2.1's corruption of the public-symbol list in large units.

    With many symbols in a unit, sdcc.exe overwrites some of its `.globl`
    names with garbage bytes (`.globl '\xef\xbf\xbd`), dropping those
    functions from the exported names. The code is unaffected, but the
    garbage line is an assembler error and a function missing its `.globl`
    is a local label (which breaks `isolate`). Drop malformed names and
    re-declare every function the unit defines."""
    out = [l for l in asm.splitlines()
           if not (re.match(r"\s*\.globl\b", l)
                   and not re.fullmatch(r"\s*\.globl\s+[A-Za-z_$.][\w$.]*\s*", l))]
    have = set(re.findall(r"^\s*\.globl\s+(\S+)", "\n".join(out), re.M))
    missing = [f"\t.globl _{n}" for n in re.findall(r"^___(\w+?)_start:", asm, re.M)
               if f"_{n}" not in have]
    # SDCC calls its runtime helpers (`__muluchar`, ...) without declaring
    # them; GBDK's driver assembled with -g. Declare them so they link.
    missing += [f"\t.globl {n}" for n in sorted(set(re.findall(r"^\s*call\s+(__\w+)", asm, re.M)))
                if n not in have]
    if missing:
        at = next((i for i, l in enumerate(out) if re.match(r"\s*\.globl\b", l)), 0)
        out[at:at] = missing
    return "\n".join(out) + "\n"


STOCK_MARK = re.compile(r"@stock-rules\b")


def compile_unit(work: Path, env: dict[str, str], c_path: Path,
                 peep: Path | None) -> tuple[str, str]:
    """C -> asxxxx asm -> object + listing in `work`. Returns (asm, lst) text.

    A unit whose source says `@stock-rules` (in a comment) is GBDK library
    code: GBDK's own build compiled it, with SDCC's stock peephole rules and
    none of LSDj's, so lsdj.peep and the tracking passes are skipped for it."""
    if peep is not None and STOCK_MARK.search(c_path.read_text(errors="replace")):
        peep = None
    for stale in ("unit.asm", "unit.o", "unit.lst", "unit.ihx"):   # reused work dir
        (work / stale).unlink(missing_ok=True)
    shutil.copyfile(c_path, work / "unit.c")
    for h in INCLUDE_DIR.glob("*.h"):      # `#include "lsdj.h"` resolves beside the unit
        shutil.copyfile(h, work / h.name)
    # SDCC runs with its stock rules only: lsdj.peep is applied afterwards by
    # peep.py. SDCC 2.2.1's own matcher cannot see past a line's trailing
    # comment or bind a variable across a comma, and it crashes (exit 5) on
    # larger units when rules with a variable mnemonic (`%1 a`) are loaded.
    rc = _run_wine("sdcc.exe", ["-mgbz80", "-S", *SDCC_FLAGS, "unit.c"], work, env)
    asm = work / "unit.asm"
    if not asm.is_file():
        hint = ""
        if rc not in (0, 1):
            hint = (" sdcc.exe crashed (exit {}); SDCC 2.2.1 dies on some "
                    "constructs, e.g. testing *(UBYTE *)0xFFxx in an if -- "
                    "see docs/toolchain.md.".format(rc))
        sys.exit("error: sdcc produced no assembly." + hint)
    asm.write_text(repair_globls(asm.read_text(errors="replace")))
    if peep is not None:
        peep_mod = importlib.import_module("peep")
        rules = peep_mod.parse_rules(peep.read_text())
        lines = asm.read_text(errors="replace").splitlines()
        lines, _ = peep_mod.gb_cmp16(lines)             # LSDj's 16-bit compare codegen
        lines, _ = peep_mod.apply(rules, lines)
        targets = peep_mod.referenced_labels(lines)     # jump targets once the rules have run
        if not os.environ.get("LSDJ_NO_TRACK"):
            lines, _ = peep_mod.track_hram(lines, targets)
            lines, _ = peep_mod.track_a(lines, targets)
            lines, _ = peep_mod.track_e(lines, targets)
            lines, _ = peep_mod.track_const(lines, targets)
            lines, _ = peep_mod.track_de(lines, targets)
            lines, _ = peep_mod.dead_stores(lines, targets)
            lines, _ = peep_mod.dead_mem_stores(lines, targets)
        asm.write_text("\n".join(lines) + "\n")
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
    # as-gbz80 keeps 32 characters of a symbol (NCPS): `___<name>_start` of a
    # name over 23 characters is listed truncated
    start = _label_addr(lst_text, f"___{name}_start"[:32])
    end = _label_addr(lst_text, f"___{name}_end"[:32])
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
    # SDCC's runtime helpers (GBDK 2.95 libc/asm/gbz80/{mul,div}.s, called for
    # `*`, `/`, `%`) under the names our symbol map gives those routines.
    for sdcc_name, ours in RUNTIME_ALIASES.items():
        if ours in syms:
            syms.setdefault(sdcc_name, syms[ours])
    return syms


RUNTIME_ALIASES = {
    "__mulschar": "MulU8", "__muluchar": "MulU8",
    "__mulsint": "MulU16", "__muluint": "MulU16",
    "__divschar": "DivS8", "__modschar": "ModS8",
    "__divuchar": "DivU8", "__moduchar": "ModU8",
    "__divsint": "DivS16", "__modsint": "ModS16",
    "__divuint": "DivU16", "__moduint": "ModU16",
    "strcmp": "StrCompare",     # bank 0's SDCC strcmp (named StrCompare: `strcmp` is an rgbasm keyword)
    "DrawFormat2": "DrawFormat",  # the varargs DrawFormat called with two values
}


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


def isolate(asm: str, func: str) -> str:
    """The unit's asm with every *other* function definition renamed away, so
    that `func`'s calls to its neighbours become externs -- resolved to their
    ROM addresses like any other callee -- instead of binding to the copies
    in this unit (which the link places wherever `func`'s address puts them).
    Returns `asm` itself when `func` references no neighbour."""
    name = func.lstrip("_")
    body = re.search(rf"^___{re.escape(name)}_start:(.*?)^___{re.escape(name)}_end:", asm, re.M | re.S)
    others = [d for d in re.findall(r"^___(\w+?)_start:", asm, re.M) if d != name]
    if not body or not others:
        return asm
    used = [d for d in others if re.search(rf"\b_{re.escape(d)}\b", body.group(1))]
    for d in used:
        asm = re.sub(rf"^_{re.escape(d)}(::?)", rf"_{d}__iso\1", asm, flags=re.M)
    return asm


def assemble(work: Path, env: dict[str, str], asm: str) -> str:
    """Assemble `asm` as unit.o in `work`; return the new listing."""
    for stale in ("unit.o", "unit.lst"):
        (work / stale).unlink(missing_ok=True)
    (work / "unit.asm").write_text(asm)
    _run_wine("as-gbz80.exe", ["-l", "-o", "unit.o", "unit.asm"], work, env)
    if not (work / "unit.lst").is_file() or not (work / "unit.o").is_file():
        sys.exit("error: as-gbz80 produced no object/listing (see errors above).")
    return (work / "unit.lst").read_text(errors="replace")


def link_plan(asm: str, lst: str, func: str | None, bank: int, addr: int,
              syms: dict[str, tuple[int | None, int]],
              defines: dict[str, int]) -> tuple[str, int, int, list[str], list[str]]:
    """Everything the link of `func` at `addr` depends on, without running it:
    (name, start, end, linker -g arguments, notes). Exits on unresolved externs.
    verify_all.py keys its link cache on this."""
    name, start, end = function_span(lst, func)
    m = re.search(rf"^___{re.escape(name)}_start:(.*?)^___{re.escape(name)}_end:", asm, re.M | re.S)
    fbody = m.group(1) if m else asm
    defined = set(re.findall(r"^(_\w+)::?", asm, re.M))
    externs = sorted(set(re.findall(r"^\s*\.globl\s+(_\w+)", asm, re.M)) - defined)
    gargs, missing, other_bank = [], [], []
    for sym in externs:
        plain = sym[1:]
        if plain not in defines and plain not in syms and sym in syms:
            plain = sym                 # a runtime helper: `__muluchar`, ...
        if plain in defines:
            val = defines[plain]
        elif plain in syms:
            sbank, val = syms[plain]
            if sbank not in (None, 0, bank):
                other_bank.append(f"{plain} (${sbank:02X})")
        else:
            # A header's extern that this unit never uses still gets a .globl,
            # and a neighbour's may be unknown; only a name *this function*
            # references is an error (the rest stays unresolved, outside the
            # bytes we extract).
            if re.search(rf"\b{re.escape(sym)}\b", fbody):
                missing.append(plain)
            continue
        gargs.append(f"-g{sym}=0x{val:04X}")
    if missing:
        sys.exit("error: unresolved externs: " + ", ".join(missing) +
                 "\n(name them in src/*.sym / constants/ram.inc / "
                 "include/hardware.inc, or pass --define NAME=ADDR)")
    notes = []
    if other_bank:
        notes.append("note: callees in other ROM banks (the function must page them in): "
                     + ", ".join(other_bank))
    return name, start, end, gargs, notes


def link_function(work: Path, env: dict[str, str], asm: str, lst: str,
                  func: str | None, bank: int, addr: int,
                  syms: dict[str, tuple[int | None, int]],
                  defines: dict[str, int], quiet: bool = False) -> tuple[str, bytes, int]:
    """Link the unit so `func` lands at `addr`; return (name, bytes, start)."""
    name, start, end, gargs, notes = link_plan(asm, lst, func, bank, addr, syms, defines)
    if not quiet:
        for n in notes:
            print(n, file=sys.stderr)
    base = addr - start
    (work / "unit.ihx").unlink(missing_ok=True)   # the work dir may be reused
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
    return name, bytes(mem[a] for a in range(addr, addr + end - start)), start, \
        trailing_data(lst, end, mem, addr - start)


def trailing_data(lst: str, end: int, mem: dict[int, int], base: int) -> bytes:
    """The bytes the unit places right after a function: the constant segment
    SDCC flushes there (its string literals and const data), up to the next
    function in the unit or the first gap. The ROM must have them too --
    the code bytes alone only check their addresses, not their contents."""
    starts = [int(m.group(1), 16) for m in re.finditer(r"___\w+?_start +([0-9A-Fa-f]{4})\b", lst)]
    stop = min((a for a in starts if a >= end), default=None)
    out = bytearray()
    a = end
    while (stop is None or a < stop) and (base + a) in mem:
        out.append(mem[base + a])
        a += 1
    return bytes(out)


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


def asm_diff(code: bytes, target: bytes, addr: int) -> None:
    """Disassemble ours and the ROM's bytes and print them aligned side by
    side (difflib on the instruction text), `<<` marking differing rows."""
    import difflib
    sys.path.insert(0, str(REPO_ROOT / "tools"))
    disasm = importlib.import_module("disasm")

    def rows(b: bytes) -> list[str]:
        return [f"{r[2]}" for r in disasm.disassemble(b, 0, len(b), addr, [])]

    ours, rom = rows(code), rows(target)
    sm = difflib.SequenceMatcher(None, rom, ours, autojunk=False)
    print(f"    {'ROM':34s} {'ours'}")
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        n = max(i2 - i1, j2 - j1)
        for k in range(n):
            a = rom[i1 + k] if i1 + k < i2 else ""
            b = ours[j1 + k] if j1 + k < j2 else ""
            mark = "  " if tag == "equal" else "<<"
            if tag == "equal" and n > 6 and 2 < k < n - 3:
                if k == 3:
                    print(f"    {'...':34s} ...")
                continue
            print(f"  {mark}{a:34s} {b}")


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
    ap.add_argument("--diff", action="store_true",
                    help="with --target: on a mismatch, print both disassemblies aligned")
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
            if func and (iso := isolate(asm, func)) != asm:
                asm, lst = iso, assemble(work, env, iso)
            name, code, start, tail = link_function(work, env, asm, lst, func, bank,
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
            if args.diff and code != target:
                # compare the ROM over the same span plus a little slack
                asm_diff(code, rom[off:off + max(len(code), (extent or 0))], addr)
                sys.exit(2)
            if report_diff(code, target, addr):
                for line in listing_near(lst, start + next(
                        i for i in range(len(code)) if i >= len(target) or code[i] != target[i])):
                    print(f"    | {line}")
                sys.exit(2)
            rom_tail = rom[off + len(code):off + len(code) + len(tail)]
            if tail != rom_tail:
                k = next(i for i in range(len(tail)) if i >= len(rom_tail) or tail[i] != rom_tail[i])
                print(f"==> MISMATCH: the code matches, but the {len(tail)} byte(s) of literals/const "
                      f"data after it differ from ${addr + len(code) + k:04X} "
                      f"(ours {tail[k:k + 12].hex(' ')}, ROM {rom_tail[k:k + 12].hex(' ')})")
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
