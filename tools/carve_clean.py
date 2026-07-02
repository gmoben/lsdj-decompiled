#!/usr/bin/env python3
"""Re-carve the disassembled banks with a trace-driven code/data split.

Uses the bank-aware reachability trace (tools/coverage) to find each bank's
*unreached* byte runs, and marks as `db` only the runs that clearly look like
**data** -- illegal-opcode-dense (graphics/tables), printable-ASCII (strings), or
`$00`/`$FF` fill (padding). Code-like unreached runs are left as instructions, so
no real code is hidden. Reached code is always disassembled.

This produces cleaner carves (strings/graphics/tables read as data) without
losing code, and keeps `make compare` byte-for-byte identical. Re-run after the
coverage trace improves.

Usage: tools/carve_clean.py            # re-carve every src/bankNN.asm
       tools/carve_clean.py 04 1d      # just these banks
"""

from __future__ import annotations

import subprocess
import sys
from importlib import import_module
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
cov = import_module("coverage")
disasm = import_module("disasm")

BANK = 0x4000
ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "lsdj9_4_0.gb"

# Unconditional control-flow opcodes (jr/jp/call/ret variants) -- their density
# distinguishes real code from graphics/table data. `$FF`/rst are excluded (the
# fill byte would skew it); fill is handled separately.
CTRL_FLOW = frozenset({
    0x18, 0x20, 0x28, 0x30, 0x38,                      # jr
    0xC3, 0xC2, 0xCA, 0xD2, 0xDA, 0xE9,                # jp / jp hl
    0xCD, 0xC4, 0xCC, 0xD4, 0xDC,                      # call
    0xC9, 0xC8, 0xC0, 0xD0, 0xD8, 0xD9,                # ret / reti
})

# Known data tables the content heuristic alone would miss (bytes that decode as
# legal opcodes but are really data).
MANUAL: dict[int, list[tuple[int, int]]] = {
    # bitmask table, cartridge header, signed modulation LUTs (triangle/ramp/
    # clamp tables at $0200; the leading fill run already covers up to $0206),
    # instrument param-label/char tables ($0824), and a 16-bit offset LUT ($09B9).
    0x00: [(0x10, 0x20), (0x104, 0x150), (0x207, 0x2C0),
           (0x824, 0x908), (0x9B9, 0xA91)],
    # bank 1 (UI): two LUTs decoded as code (byte+16-bit table at $40EA; a rising
    # byte curve at $42A0), one small zero-source region, the FX-command help
    # strings + message-bar attr row + screen-name strings ($6FB8-$74F7), the
    # ShowMessage strings between their Show* wrappers, and CommandHelpTable +
    # the three S-command help strings ($776E-$781F).
    0x01: [(0xEA, 0x1D2), (0x2A0, 0x316), (0x11EE, 0x1208), (0x124B, 0x1253),
           (0x17BF, 0x17D0),
           (0x2FB8, 0x34F7), (0x35C7, 0x35F8), (0x3602, 0x3616),
           (0x3620, 0x3635), (0x363F, 0x364C), (0x376E, 0x381F),
           (0x3B7A, 0x3BDE)],
    # PitchBendTable ($43D8-$45D8, read by ApplyPitchBend), the vibrato tables
    # ($7D81-$7DD1: VibDepthTable[16] + VibRateTableA/B[16 x 16-bit], read by
    # FxCmd_V), and NoiseFreqTable ($5EDA, hides SetNoisePoly at $5F52).
    0x02: [(0x3D8, 0x5D8), (0x1EDA, 0x1F52), (0x3D81, 0x3DD1)],
    # bank 1C: an [8][8] rising value table ($6E3C) decoded as code, and
    # DefaultWaveFrameB1C ($4047, 16 bytes -- hides the $4057 magic check).
    0x1C: [(0x47, 0x57), (0x2E3C, 0x2E7C)],
    # bank 05: table-screen CUT/COPY strings.
    0x05: [(0x249A, 0x24A3)],
    # bank 04: phrase-row CGB attribute rows + the $01 step table.
    0x04: [(0x10D8, 0x10FF), (0x14E3, 0x1500)],
    # bank 1D: softsynth shape banks ($4EF0/$50F0) decoded as code.
    0x1D: [(0xEF0, 0x1190)],
    # bank 1E: everything between ScreenPreDraw and GfxFillVramRegions is
    # data (font/UI tile blocks + synth waveform tables + a blank string).
    0x1E: [(0x2C8, 0x39FA)],
    # bank 1F: OOM-instr + OUT OF TABLES + DE-DUPE panel strings, the
    # phrase-column layout table ($64E1, hides $64FF), and "TYPE   SPEECH".
    0x1F: [(0xACE, 0xB00), (0xC11, 0xC20), (0x237D, 0x23B1), (0x244D, 0x248B),
           (0x24E1, 0x24FF), (0x2B32, 0x2B3B), (0x323F, 0x324D)],
    # bank 7: DMG+CGB tempo timer tables ($5E49/$6049, 256 x dw each), the PS/2
    # keyboard note/function maps ($662A/$66AA), and the keyboard-calibration
    # screen strings ($7171-$71BF).
    0x07: [(0x1E49, 0x2249), (0x262A, 0x272A), (0x2889, 0x28A2), (0x3171, 0x31BF)],
}


def data_like(seg: bytes) -> bool:
    """True for an *unreached* run that is data. Two tiers:

    1. Strong, version-independent signals: dense illegal opcodes (graphics),
       `$00`/`$FF` fill (padding), or a long printable run (text).
    2. Coverage-assisted: with ~99.6% code coverage, an unreached run that does
       NOT start with a function prologue is almost certainly data (tile
       graphics, pointer/lookup tables) even though it decodes as legal opcodes.
       Runs that *start* with an SDCC prologue (`push bc/de/af; add sp,-N` or
       `; ld hl,sp+N`, or a bare `ld hl,sp+N`) are left as code -- this is what
       protects the handful of not-yet-reached functions from being hidden.
    """
    n = len(seg)
    if not n:
        return False
    if (seg[0] in (0xC5, 0xD5, 0xF5) and n >= 2 and seg[1] in (0xE8, 0xF8)) \
            or seg[0] == 0xF8:
        return False                                   # function start -> code
    illegal = sum(1 for b in seg if b in disasm.ILLEGAL) / n
    fill = (seg.count(0) + seg.count(0xFF)) / n
    # Fraction of bytes inside a *long* printable run (>= 4) -- real text. Code's
    # printable opcodes are short, scattered runs, so its fraction stays low.
    str_bytes = i = 0
    while i < n:
        if 0x20 <= seg[i] <= 0x7E:
            j = i
            while j < n and 0x20 <= seg[j] <= 0x7E:
                j += 1
            if j - i >= 4:
                str_bytes += j - i
            i = j
        else:
            i += 1
    if illegal >= 0.06 or fill >= 0.60 or str_bytes / n >= 0.55:
        return True
    # Control-flow-opcode density cleanly separates code from graphics/tables:
    # real SM83 code runs ~0.10-0.15 (jr/jp/call/ret everywhere); tile graphics
    # and pointer/lookup tables run ~0.03-0.06. A long unreached run that low on
    # control flow is data even though every byte decodes as a legal opcode.
    cf = sum(b in CTRL_FLOW for b in seg) / n
    return n >= 64 and cf <= 0.07


def func_brackets(rom: bytes, lo: int, hi: int, code: set[int]) -> list[tuple[int, int]]:
    """Locate SDCC functions in the *unreached* region [lo,hi): a prologue
    `push bc/de/af; add sp,-N|ld hl,sp+N` bracketed by the matching epilogue
    `pop bc/de/af; ret`. Used to keep a not-yet-reached function rendered as code
    even when it sits mid-run inside what is otherwise data (the data_like
    coverage heuristic only inspects a run's first byte)."""
    POP = {0xC5: 0xC1, 0xD5: 0xD1, 0xF5: 0xF1}
    out: list[tuple[int, int]] = []
    o = lo
    while o < hi - 1:
        if o not in code and rom[o] in POP and rom[o + 1] in (0xE8, 0xF8):
            pop, e, end = POP[rom[o]], o + 2, None
            while e < min(o + 800, hi - 1):
                if rom[e] == pop and rom[e + 1] == 0xC9:   # pop bc; ret
                    end = e + 2
                    break
                e += 1
            if end is not None:
                out.append((o, end))
                o = end
                continue
        o += 1
    return out


def data_runs(code: set[int], rom: bytes, bank: int, minlen: int = 4) -> list[tuple[int, int]]:
    lo, hi = bank * BANK, (bank + 1) * BANK
    protect = func_brackets(rom, lo, hi, code)

    def blocked(x: int) -> bool:        # reached, or inside a protected function
        return x in code or any(a <= x < b for a, b in protect)

    runs: list[tuple[int, int]] = []
    o = lo
    while o < hi:
        if blocked(o):
            o += 1
            continue
        s = o
        while o < hi and not blocked(o):
            o += 1
        if o - s >= minlen and data_like(rom[s:o]):
            runs.append((s, o))
    # Union with manual ranges (file offsets).
    for ca, cb in MANUAL.get(bank, []):
        runs.append((bank * BANK + ca, bank * BANK + cb))
    return sorted(runs)


def main() -> None:
    rom = ROM.read_bytes()
    ent = cov.collect_entries(rom)
    seeds = [(b, a) for b, addrs in ent.items() for a in addrs]
    code, _ = cov.global_trace(rom, seeds)

    want = {int(a, 16) for a in sys.argv[1:]}
    banks = sorted(int(p.name[4:6], 16) for p in (ROOT / "src").glob("bank*.sym"))
    for b in banks:
        if want and b not in want:
            continue
        runs = data_runs(code, rom, b)
        data_arg = ",".join(f"0x{s:X}:0x{e:X}" for s, e in runs)
        cmd = ["python3", "tools/disasm.py", "--rom", "lsdj9_4_0.gb",
               "--bank", f"0x{b:02x}", "--symbols", f"src/bank{b:02x}.sym",
               "--comments", "-o", f"src/bank{b:02x}.asm"]
        if b != 0:
            cmd += ["--globals", "src/bank00.sym"]
        if data_arg:
            cmd += ["--data", data_arg]
        subprocess.run(cmd, cwd=ROOT, check=True)
        db = sum(e - s for s, e in runs)
        print(f"bank ${b:02X}: {len(runs)} data run(s), {db} bytes marked db")

    subprocess.run(["python3", "tools/gen_main.py"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
