#!/usr/bin/env python3
"""Classify every named function as SDCC-compiled C or hand-written asm.

LSDj is mostly C, but the timing-critical core is hand-written assembly and
is not a Track 2 (matching C) target. The two are easy to tell apart in the
generated disassembly:

  * SDCC 2.2.1 addresses locals/arguments with `ld hl, sp+N` / `add sp, N`
    and branches with `jp cc` (it only emits `jr` inside its own shift loops);
  * hand-written code uses `jr` freely, `ldh`, and `add a,l; ld l,a` style
    address arithmetic, and never touches the stack frame that way;
  * SDCC 2.2.1 never addresses memory absolutely (`ld a,[nn]`, `ld [nn],a`,
    `ld [nn],sp`), never uses `[hl-]` and never returns with `reti`: none
    of the 575 functions matched so far contains one, so any of them marks
    hand-written code.

A function with any of the asm marks is asm; one with sp-relative access is
C; one with more `jr` than `jp cc` is asm; the rest ("C?") are short leaf
routines with no distinguishing feature. Functions under 6 instructions are
"tiny". Matching C already owns some functions (c/matched.json: "matched"),
the GBDK/SDCC library routines are "lib", and a named function the carve left
as `db` but that starts with SDCC's `push bc` prologue counts as C.

Usage: tools/match/code_kind.py            # per-bank summary
       tools/match/code_kind.py --list     # ... plus every asm-tagged function
       tools/match/code_kind.py --json F   # write {"bank:Name": kind} to F
"""

from __future__ import annotations

import argparse
import importlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
coverage = importlib.import_module("coverage")   # tools/coverage.py: read_sym, is_data_symbol

# Routines the heuristics misfile, each checked by hand: hand-written asm
# with no `jr` or no stack-frame access to give it away (register-passing
# helpers, `ldh a,(c)` kernels, unrolled sample shifts).
KNOWN_ASM = {
    "01:SaveCrashStack", "01:CopyScreenMapCgb",
    "01:CopyScreenMapDmg", "01:LoadObjPalette",
    "06:DrawScopePoint", "06:ShiftScopeSamples",
    "07:DrawHexOrDashes", "07:ScanSongSram", "07:WriteSongSramBytes",
    "1b:MixerKernel",
    "1c:EmitDefaultInstrument", "1c:EmitDefaultWave", "1c:FileClassifyToken",
    "1c:FilePutDecompressByte", "1c:ReadWorkingSongByte", "1c:WriteCompressedByte",
    # stack-argument readers that branch with `jr` / poll rSTAT
    "00:DrawTileRun", "00:WriteVramByteSafe",
    # leaf routines without a stack frame, checked by hand: conditional
    # returns mid-routine (`ret c`, `ret nz`: SDCC returns through its
    # epilogue), register arguments (`push de` as a ModU8 call), `add a,l;
    # ld l,a` / `inc l` address steps, `swap`, `stop`, unrolled `ld [hl+],a`,
    # tail-jumping dispatchers, the `ld b,b` debugger marker
    "00:SoundInterpResume", "00:DebugTrap", "00:KitIndexToBank", "00:AnyChannelFlagSet",
    "00:SwitchToDoubleSpeed", "00:VibratoOffsetJumps",
    "02:SoundCommandTableJump", "02:SoundRandom15", "02:SoundTriggerNote", "02:SoundSetupChannel",
    "02:SoundClearChannelCommandSlots", "02:SoundAdvanceChannelCursors", "02:SoundCountdownStep",
    "02:SoundFillFF", "02:Command_M", "02:SoundApplyTableTranspose", "02:SoundReadTableEnvelope",
    "02:SoundDeactivateChannel", "02:SoundReadTableTranspose", "02:SoundDecodeNibbles",
    # an `hl`-argument call wrapped in the C convention: RequestTileSlot pops
    # its frame before setting the return value, which SDCC never does
    "00:LoadTilesDispatch", "00:RequestTileSlot",
    # compiled-looking but runs off its end into DispatchScreenDraw (no
    # `call; ret`): no C function ends without returning
    "00:RedrawScreen",
    # the boot code's tail (falls in from Start's hardware setup, `ei; jp
    # main`, then crt0's `halt; jr` idle): the modified crt0, not C
    "00:InitSerial",
}

# GBDK 2.95-3 / SDCC 2.2.1 library code linked into bank 0, by the source it
# was assembled from (toolchain/gbdk-2.95-3/libc): not LSDj's C. (strlen is
# library C and is matched in c/bank00/GbdkLib.c with the stock rules.)
LIBRARY = {
    # libc/asm/gbz80/asm_string.s
    "00:strcpy", "00:memcpy", "00:StringCompare",
    # libc/asm/gbz80/div.s, mul.s (the SDCC runtime)
    "00:DivS8", "00:ModS8", "00:DivS16", "00:ModS16", "00:DivU8", "00:ModU8",
    "00:DivU16", "00:ModU16", "00:DivModS8", "00:DivModS16", "00:DivModU8",
    "00:DivModU16", "00:DivStep", "00:MulU8", "00:MulU16", "00:MulCharToInt",
    "00:MulInt",
    # libc/gb/delay.s, pad.s, cgb.s, set_bk_t.s, set_xy_t.s, rand.s
    "00:delay", "00:DelayTicks", "00:DelayLoop", "00:joypad",
    "00:set_bkg_palette", "00:set_bkg_tiles", "00:set_xy_btt", "00:set_xy_wtt",
    "00:set_xy_tt", "00:randw", "00:initrand",
}

LABEL = re.compile(r"^([A-Za-z_]\w*)::?")
JPCC = re.compile(r"^jp (nz|z|nc|c),")
# absolute memory operands SDCC 2.2.1 never emits (registers and ldh's [c] excluded)
ABSOLUTE = re.compile(r"^ld (a, \[(?!(?:hl[+-]?|de|bc|c)\])[^\]]+\]|\[(?!(?:hl[+-]?|de|bc|c)\])[^\]]+\], (a|sp))$")
BYTES = re.compile(r";\s*\$[0-9a-f]{4}:\s*((?:[0-9a-f]{2}\s?)+)")

KINDS: tuple[str, ...] = ("matched", "C", "C?", "asm", "lib", "tiny")


def _rom() -> bytes:
    p = ROOT / "lsdj9_4_0.gb"
    return p.read_bytes() if p.is_file() else b""


def _matched() -> dict[int, list[tuple[int, int, str]]]:
    """bank -> [(start, end, name)] ranges owned by matching C."""
    p = ROOT / "c" / "matched.json"
    out: dict[int, list[tuple[int, int, str]]] = {}
    if p.is_file():
        for name, m in json.loads(p.read_text()).items():
            a = int(m["addr"], 16)
            out.setdefault(int(m["bank"], 16), []).append((a, a + int(m["len"]), name))
    return out


def classify_bank_full(asm_path: Path, rom: bytes | None = None,
                       matched: dict[int, list[tuple[int, int, str]]] | None = None
                       ) -> dict[str, tuple[str, int, int]]:
    """name -> (kind, instruction count, code bytes) for one generated bank.

    Data labels are left out, and so are global labels inside a matched
    function (case bodies, loop heads the carve named)."""
    bank = int(asm_path.stem[4:], 16)
    rom = _rom() if rom is None else rom
    matched = _matched() if matched is None else matched
    owned = matched.get(bank, [])
    sym = asm_path.with_suffix(".sym")
    addrs: dict[str, int] = {}
    data: set[str] = set()
    order: list[tuple[int, str]] = []
    if sym.is_file():
        for a, n, c in coverage.read_sym(sym):
            if n.startswith(".") or "@deploy-only" in c:
                continue
            addrs[n] = a
            order.append((a, n))
            if coverage.is_data_symbol(n, c):
                data.add(n)
    order.sort()
    span = {n: (order[i + 1][0] if i + 1 < len(order) else (0x4000 if bank == 0 else 0x8000)) - a
            for i, (a, n) in enumerate(order)}

    funcs: dict[str, list[tuple[str, int]]] = {}
    has_db: dict[str, bool] = {}
    cur = None
    for line in asm_path.read_text().splitlines():
        m = LABEL.match(line)
        if m:
            cur = m.group(1)
            funcs[cur] = []
            has_db[cur] = False
            continue
        s = line.split(";")[0].strip()
        if not cur or not s or s.endswith(":") or s.startswith("SECTION"):
            continue
        if s.startswith(("db", "dw", "INCBIN")):
            has_db[cur] = True
            continue
        b = BYTES.search(line)
        funcs[cur].append((s, len(b.group(1).split()) if b else 0))

    out: dict[str, tuple[str, int, int]] = {}
    for name, ins in funcs.items():
        a = addrs.get(name)
        if a is not None:
            own = next(((lo, hi) for lo, hi, _ in owned if lo <= a < hi), None)
            if own:
                if a == own[0]:
                    out[name] = ("matched", 0, own[1] - own[0])
                continue
        if name in data:
            continue
        key = f"{asm_path.stem[4:]}:{name}"
        nbytes = sum(n for _, n in ins)
        if not ins:
            # carved as data: C if it opens with SDCC's `push bc` prologue
            if has_db[name] and a is not None and rom:
                off = bank * 0x4000 + (a - 0x4000 if bank else a)
                if rom[off] == 0xC5:
                    out[name] = ("C", 0, span.get(name, 0))
            continue
        text = [s for s, _ in ins]
        if key in LIBRARY:
            kind = "lib"
        elif key in KNOWN_ASM or any(ABSOLUTE.match(s) or s == "reti" or "[hl-]" in s
                                     for s in text):
            kind = "asm"
        elif len(text) < 6:
            kind = "tiny"
        else:
            sp = sum(s.startswith(("ld hl, sp+", "add sp,")) for s in text)
            jr = sum(s.startswith("jr ") for s in text)
            jpcc = sum(bool(JPCC.match(s)) for s in text)
            kind = "C" if sp else ("asm" if jr > jpcc else "C?")
        out[name] = (kind, len(text), nbytes)
    return out


def classify_bank(asm_path: Path) -> dict[str, tuple[str, int]]:
    """name -> (kind, instruction count) for one generated bank."""
    return {n: (k, c) for n, (k, c, _b) in classify_bank_full(asm_path).items()}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="list the asm-tagged functions")
    ap.add_argument("--json", type=Path, help="write the per-function map here")
    args = ap.parse_args()

    rom = _rom()
    matched = _matched()
    everything: dict[str, str] = {}
    tot_n: dict[str, int] = dict.fromkeys(KINDS, 0)
    tot_b: dict[str, int] = dict.fromkeys(KINDS, 0)
    print(f"{'bank':5s} " + " ".join(f"{k:>7s}" for k in KINDS)
          + f"  {'C matched (bytes)':>17s}  {'asm+lib share':>13s}")

    def row(label: str, n: dict[str, int], b: dict[str, int]) -> str:
        c_all = b["matched"] + b["C"] + b["C?"]
        code = c_all + b["asm"] + b["lib"]
        return (f"{label:5s} " + " ".join(f"{n[k]:7d}" for k in KINDS)
                + f"  {b['matched'] / max(c_all, 1):17.0%}  {(b['asm'] + b['lib']) / max(code, 1):13.0%}")

    for p in sorted((ROOT / "src").glob("bank*.asm")):
        bank = p.stem[4:]
        kinds = classify_bank_full(p, rom, matched)
        n: dict[str, int] = dict.fromkeys(KINDS, 0)
        b: dict[str, int] = dict.fromkeys(KINDS, 0)
        for name, (kind, _cnt, nbytes) in kinds.items():
            n[kind] += 1
            b[kind] += nbytes
            everything[f"{bank}:{name}"] = kind
        for k in KINDS:
            tot_n[k] += n[k]
            tot_b[k] += b[k]
        print(row(f"${bank.upper()}", n, b))
        if args.list:
            names = [nm for nm, (k, _, _) in kinds.items() if k == "asm"]
            if names:
                print("      asm: " + ", ".join(names))
    print(row("all", tot_n, tot_b))
    c_all = tot_b["matched"] + tot_b["C"] + tot_b["C?"]
    print(f"C code: {tot_b['matched']} of ~{c_all} bytes matched "
          f"({tot_n['matched']} functions); {tot_b['C'] + tot_b['C?']} bytes in "
          f"{tot_n['C'] + tot_n['C?']} C/C? functions to go; "
          f"asm {tot_b['asm']} B, library {tot_b['lib']} B")
    if args.json:
        args.json.write_text(json.dumps(everything, indent=0, sort_keys=True))
        print(f"wrote {args.json}")


if __name__ == "__main__":
    main()
