#!/usr/bin/env python3
"""Matching-decompilation harness for LSDj (Track 2).

Compile a C function with the *period* GBDK 2.95 / SDCC 2.2.1 toolchain (the
compiler LSDj was built with) and compare the emitted machine code against the
original ROM:

  * --offset N  : diff the compiled bytes against ROM bytes at file offset N
  * --search    : scan the whole ROM for the compiled byte pattern
  * (neither)   : just print the compiled bytes (harness self-check)

The point of the spike: prove we can reproduce a real LSDj function byte-for-byte
under the original toolchain. If we can, matching C decompilation (Track 2) is
viable; if we can't, we stay asm-only (Track 1).

Pipeline (faithful to how LSDj was actually built -- asxxxx, not RGBDS):
    file.c --[sdcc -mgbz80 -S]--> file.asm --[as-gbz80 -l]--> file.lst
    -> parse code bytes between the ___<func>_start / ___<func>_end labels.

Requires: wine, and the toolchain fetched via tools/toolchain/fetch_gbdk295.sh.

Usage:
    tools/match/match_function.py --c snippet.c [--func NAME]
    tools/match/match_function.py --c snippet.c --func foo --search
    tools/match/match_function.py --c snippet.c --func foo --offset 0x1234
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
GBDK = Path(os.environ.get("GBDK_295", REPO_ROOT / "toolchain" / "gbdk-2.95"))
WINEPREFIX = Path(os.environ.get("WINEPREFIX", REPO_ROOT / "toolchain" / ".wine"))
BANK_SIZE = 0x4000

_NOISE = re.compile(r"libEGL|pci id for fd|wine|gecko|mono|fixme:|err:winediag", re.I)


def _wine_env() -> dict[str, str]:
    env = dict(os.environ)
    env["WINEPREFIX"] = str(WINEPREFIX)
    env["WINEDEBUG"] = "-all"
    try:
        winpath = subprocess.run(
            ["winepath", "-w", str(GBDK / "bin")],
            capture_output=True, text=True, env=env, check=True,
        ).stdout.strip()
        if winpath:
            env["WINEPATH"] = winpath
    except (subprocess.CalledProcessError, FileNotFoundError):
        pass
    return env


def _run_wine(exe: str, args: list[str], cwd: Path, env: dict[str, str]) -> str:
    proc = subprocess.run(
        ["wine", str(GBDK / "bin" / exe), *args],
        cwd=str(cwd), env=env, capture_output=True, text=True,
    )
    err = "\n".join(l for l in proc.stderr.splitlines() if not _NOISE.search(l))
    if proc.returncode != 0 and err.strip():
        sys.stderr.write(err + "\n")
    return err


def _preflight() -> None:
    if not (GBDK / "bin" / "sdcc.exe").is_file():
        sys.exit(f"error: toolchain missing at {GBDK}.\n"
                 f"Run tools/toolchain/fetch_gbdk295.sh first.")
    if shutil.which("wine") is None:
        sys.exit("error: wine is required to run the period sdcc.exe.")


def compile_to_listing(c_path: Path) -> tuple[str, str]:
    """Compile C -> asxxxx asm -> listing. Returns (asm_text, lst_text)."""
    _preflight()
    env = _wine_env()
    # Work on Wine's writable C: drive so sdcc's "\<tmp>" temp file succeeds.
    drive_c = WINEPREFIX / "drive_c"
    drive_c.mkdir(parents=True, exist_ok=True)
    if not (drive_c / "windows").exists():
        subprocess.run(["wineboot", "-i"], env=env,
                       capture_output=True, text=True)
    with tempfile.TemporaryDirectory(dir=str(drive_c), prefix="match-") as tmp:
        work = Path(tmp)
        stem = "unit"
        shutil.copyfile(c_path, work / f"{stem}.c")

        _run_wine("sdcc.exe", ["-mgbz80", "-S", f"{stem}.c"], work, env)
        asm = work / f"{stem}.asm"
        if not asm.is_file():
            sys.exit("error: sdcc produced no assembly (see errors above).")

        _run_wine("as-gbz80.exe", ["-l", "-o", f"{stem}.o", f"{stem}.asm"], work, env)
        lst = work / f"{stem}.lst"
        if not lst.is_file():
            sys.exit("error: as-gbz80 produced no listing (see errors above).")
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


def function_bytes(lst_text: str, func: str | None) -> bytes:
    func = (func or "").lstrip("_")
    available = sorted(set(re.findall(r"___(.+?)_start +[0-9A-Fa-f]{4}\b", lst_text)))
    if not func:
        if not available:
            sys.exit("error: no function boundary labels found; pass --func.")
        func = available[0]

    start = _label_addr(lst_text, f"___{func}_start")
    end = _label_addr(lst_text, f"___{func}_end")
    if start is None or end is None:
        sys.exit(f"error: function '{func}' not found. "
                 f"Available: {', '.join(available) or '(none)'}")

    addr_byte = parse_code_bytes(lst_text)
    out = bytearray()
    for a in range(start, end):
        if a not in addr_byte:
            sys.exit(f"error: non-contiguous code at ${a:04X} (relocation/data?); "
                     f"inspect the listing manually.")
        out.append(addr_byte[a])
    return bytes(out)


def gb_location(offset: int) -> str:
    bank = offset // BANK_SIZE
    addr = offset % BANK_SIZE + (0 if bank == 0 else 0x4000)
    return f"bank ${bank:02X}:${addr:04X}"


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--c", required=True, type=Path, help="C source file to compile")
    ap.add_argument("--func", help="function name to extract (default: first found)")
    ap.add_argument("--rom", type=Path, default=REPO_ROOT / "lsdj9_4_0.gb",
                    help="base ROM (default: lsdj9_4_0.gb)")
    ap.add_argument("--offset", type=lambda s: int(s, 0),
                    help="diff against ROM bytes at this file offset (e.g. 0x1234)")
    ap.add_argument("--search", action="store_true",
                    help="scan the ROM for the compiled byte pattern")
    ap.add_argument("--show-asm", action="store_true", help="print the SDCC asm")
    args = ap.parse_args()

    if not args.c.is_file():
        sys.exit(f"error: C file not found: {args.c}")

    asm, lst = compile_to_listing(args.c)
    if args.show_asm:
        print("=== SDCC 2.2.1 assembly ===")
        print(asm)
    code = function_bytes(lst, args.func)

    print(f"compiled {len(code)} bytes: {hexs(code)}")

    rom = b""
    if args.offset is not None or args.search:
        if not args.rom.is_file():
            sys.exit(f"error: ROM not found: {args.rom} (supply your own copy).")
        rom = args.rom.read_bytes()

    if args.offset is not None:
        target = rom[args.offset:args.offset + len(code)]
        print(f"ROM @ 0x{args.offset:06X} ({gb_location(args.offset)}): {hexs(target)}")
        if target == code:
            print("==> MATCH: bytes are identical.")
        else:
            diff = "".join("^^" if a != b else "  "
                           for a, b in zip(code, target.ljust(len(code), b"\0")))
            print("==> MISMATCH:")
            print(f"    diff: {' '.join(diff[i:i+2] for i in range(0, len(diff), 2))}")
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
                  "LSDj source would need matching flags/bank to appear).")


if __name__ == "__main__":
    main()
