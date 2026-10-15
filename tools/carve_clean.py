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

import json
import re
import subprocess
import sys
from importlib import import_module
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
cov = import_module("coverage")
disasm = import_module("disasm")
gfx = import_module("gfx")
regflow = import_module("regflow")

BANK = 0x4000
ROOT = Path(__file__).resolve().parents[1]
_rom_env = os.environ.get("LSDJ_BASEROM", "lsdj9_4_0.gb")
ROM = Path(_rom_env) if Path(_rom_env).is_absolute() else ROOT / _rom_env

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
    0x00: [(0x10, 0x20), (0x104, 0x150), (0x207, 0x2C0), (0x2D12, 0x2D22), (0x300F, 0x301F),
           (0x824, 0x908), (0x9B9, 0xA91)],
    # bank 1 (UI): two LUTs decoded as code (byte+16-bit table at $40EA; a rising
    # byte curve at $42A0), one small zero-source region, the FX-command help
    # strings + message-bar attr row + screen-name strings ($6FB8-$74F7), the
    # ShowMessage strings between their Show* wrappers, and CommandHelpPointers +
    # the three S-command help strings ($776E-$781F).
    0x01: [(0xEA, 0x316), (0x1099, 0x10A0), (0x11EE, 0x1208), (0x124B, 0x1253),
           (0x17BF, 0x17D0),
           (0x2F8E, 0x34F7), (0x35C7, 0x35F8), (0x3602, 0x3616),
           (0x3620, 0x3635), (0x363F, 0x364C), (0x376E, 0x381F),
           (0x3B7A, 0x3BDE)],
    # PitchBendTable ($43D8-$45D8, read by SoundApplyPitchBend), the vibrato tables
    # ($7D81-$7DD1: VibratoDepthTable[16] + VibratoRateTableA/B[16 x 16-bit], read by
    # Command_V), and NoiseFrequencyTable ($5EDA, hides SoundSetNoisePolynomial at $5F52).
    0x02: [(0x3D8, 0x5D8), (0x1EDA, 0x1F52), (0x3D81, 0x3DD1), (0x3EAC, 0x3F24)],
    # bank 1B: SAVING... + %d + ON/OFF/N/A project strings.
    0x1B: [(0x58, 0x98), (0x10E, 0x9F8), (0xC75, 0xC96), (0x1120, 0x1126), (0x12D8, 0x12E2),
           (0x12E2, 0x12FB), (0x19C5, 0x19E7), (0x1C0C, 0x1C17), (0x1D75, 0x1D7C),
           (0x2A66, 0x2A8D), (0x1716, 0x1725), (0x1B06, 0x1B09), (0x1B56, 0x1B62),
           (0x2FFE, 0x30B8)],
    # bank 1C: file-screen prompt strings, an [8][8] rising value table ($6E3C)
    # decoded as code, and DefaultWaveFrameB1C ($4047, 16 bytes -- hides the
    # $4057 magic check). (These were two dict entries once; the second silently
    # replaced the first, so the prompt strings were never marked.)
    0x1C: [(0x189F, 0x18C1), (0x1AD6, 0x1B0C), (0x20BE, 0x20CC),
           (0x47, 0x57), (0x2E3C, 0x2E7C)],
    # bank 05: table-screen CUT/COPY strings.
    0x05: [(0x2D5, 0x2E7), (0x249A, 0x24A3), (0x2F38, 0x3208), (0x32A3, 0x343F)],
    # bank 06: sync-status strings + small unreached pairs + " --".
    0x06: [(0x22, 0x26), (0x36E4, 0x384F), (0x150F, 0x1511), (0x1A5E, 0x1A62), (0x3455, 0x3469),
           (0x349E, 0x34AD), (0x3510, 0x3524)],
    # bank 03: OUT OF SYNTHS string + kit DIST mode names.
    0x03: [(0x15, 0xF8), (0x33B, 0x36E), (0xAB4, 0xACC)],
    # bank 04: phrase-row CGB attribute rows + the $01 step table.
    0x04: [(0xC10, 0xC14), (0xCE1, 0xCE5), (0x10D8, 0x10FF), (0x14E3, 0x1500), (0x15D1, 0x15DE)],
    # bank 1D: softsynth shape banks ($4EF0/$50F0) decoded as code.
    0x1D: [(0x35A, 0x35D), (0xEF0, 0x1190), (0x20E6, 0x2116), (0x2DE7, 0x2E00)],
    # bank 1E: everything between DrawScreenColumnLabels and FillVramRegions is
    # data (font/UI tile blocks + synth waveform tables + a blank string).
    0x1E: [(0x2C8, 0x39FA)],
    # bank 1F: OOM-instr + OUT OF TABLES + DE-DUPE panel strings, the
    # phrase-column layout table ($64E1, hides $64FF), and "TYPE   SPEECH".
    0x1F: [(0x91A, 0x92A), (0xACE, 0xB00), (0xC11, 0xC20), (0x237D, 0x23B1), (0x244D, 0x248F),
           (0x24E1, 0x24FF), (0x2B32, 0x2B3B), (0x323F, 0x324D)],
    # bank 7: DMG+CGB tempo timer tables ($5E49/$6049, 256 x dw each), the PS/2
    # keyboard note/function maps ($662A/$66AA), and the keyboard-calibration
    # screen strings ($7171-$71BF).
    0x07: [(0x3F, 0x4B), (0x1E49, 0x2249), (0x262A, 0x272A), (0x2889, 0x28A2), (0x3171, 0x31BF)],
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
    # The trailing $FF to the end of the bank is padding, even where a stray
    # jump-table read made the trace reach a byte of it ($1E:$7FFF).
    t = hi
    while t > lo and rom[t - 1] == 0xFF:
        t -= 1
    while t < hi and t in code:          # an instruction's own $FF operand
        t += 1
    if hi - t >= disasm.FILL_MIN:
        runs.append((t, hi))
    # Union with manual ranges (file offsets) and the named data symbols.
    for ca, cb in MANUAL.get(bank, []):
        runs.append((bank * BANK + ca, bank * BANK + cb))
    runs += data_symbol_ranges(rom, bank, code)
    return merge_ranges(runs)


def data_symbol_ranges(rom: bytes, bank: int, code: set[int]) -> list[tuple[int, int]]:
    """File-offset extents of the bank's named data symbols (see
    coverage.is_data_symbol): from the symbol to the next global symbol. The
    trace no longer seeds these, so without this they would render as whatever
    their bytes happen to decode to. Safety net: stop early at an unnamed SDCC
    prologue the trace reached (real code following a table without a label)."""
    sym = ROOT / "src" / f"bank{bank:02x}.sym"
    if not sym.is_file():
        return []
    rows = cov.read_sym(sym)
    lo_cpu = 0x4000 if bank else 0
    globs = sorted({a for a, n, _ in rows if not n.startswith(".")})
    out: list[tuple[int, int]] = []
    for addr, name, comment in rows:
        if not cov.is_data_symbol(name, comment):
            continue
        nxt = next((g for g in globs if g > addr), lo_cpu + BANK)
        s, e = bank * BANK + (addr - lo_cpu), bank * BANK + (nxt - lo_cpu)
        for o in range(s + 1, e - 1):
            if o in code and rom[o] in (0xC5, 0xD5, 0xF5) and rom[o + 1] in (0xE8, 0xF8):
                e = o
                break
        if e > s:
            out.append((s, e))
    return out


def merge_ranges(runs: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Sort and coalesce overlapping/adjacent [s, e) ranges."""
    out: list[tuple[int, int]] = []
    for s, e in sorted(runs):
        if out and s <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], e))
        else:
            out.append((s, e))
    return out


def pointer_tables(bank: int) -> list[tuple[int, int, int]]:
    """Pointer tables the bank's .sym tags `[N] dw` (N words; `[N] dw @$BB`
    when they point into bank BB): (lo, hi) file offsets and the target bank,
    for disasm.py --ptr-tables, which prints them as `dw Label`."""
    out = []
    base = bank * BANK - (0 if bank == 0 else 0x4000)
    for addr, _name, comment in cov.read_sym(ROOT / "src" / f"bank{bank:02x}.sym"):
        m = re.match(r"\[(\d+)\] dw\b(?: @\$([0-9A-Fa-f]{2}))?", comment)
        if m:
            lo = base + addr
            out.append((lo, lo + 2 * int(m.group(1)), int(m.group(2), 16) if m.group(2) else bank))
    return out


# Sections pinned inside a bank: code reaches these by their page, not by a
# label -- SoundCalculateVibratoOffset reads VibratoShapes with `ld d, $02`
# and jumps into VibratoOffsetJumps with `ld h, $03 / jp hl` -- so a movable
# build must not move them (an overflow into one fails to link). The section
# after them is where bank 0's code can grow (tools/shift_test.py pads there).
PINNED_SECTIONS = {0: [(0x0200, "VibratoShapes"), (0x0300, "VibratoOffsetJumps"),
                       (0x0391, "ROM Bank $000 Code")]}


TEXT_NAME = re.compile(r"^Txt|Names$|String$")


def text_ranges(rom: bytes, bank: int) -> list[tuple[int, int]]:
    """Text the bank's .sym names as text (`Txt*`, `*Names`, `*String`, or a
    comment that opens with a string), from the symbol to the next global
    symbol less any trailing $FF padding, when at least 90% of it is text
    (printable ASCII, newlines, NULs): (lo, hi) file offsets for disasm.py
    --text, which prints a line per string."""
    sym = ROOT / "src" / f"bank{bank:02x}.sym"
    if not sym.is_file():
        return []
    rows = cov.read_sym(sym)
    lo_cpu = 0x4000 if bank else 0
    globs = sorted({a for a, n, _ in rows if not n.startswith(".")})
    out = []
    for addr, name, comment in rows:
        if name.startswith(".") or not (TEXT_NAME.search(name) or comment.startswith('"')):
            continue
        nxt = next((g for g in globs if g > addr), lo_cpu + BANK)
        lo, hi = bank * BANK + (addr - lo_cpu), bank * BANK + (nxt - lo_cpu)
        while hi > lo and rom[hi - 1] == 0xFF:
            hi -= 1
        text = sum(0x20 <= x <= 0x7E or x in (0x00, 0x0A) for x in rom[lo:hi])
        if hi > lo and text >= 0.9 * (hi - lo):
            out.append((lo, hi))
    return out


TABLE_TAG = re.compile(r"\[(\d+)\](?:\[(\d+)\])?(?:\s+(UBYTE|BYTE|UWORD|RGB)\b)?")
ELEMENT_SIZE = {"UBYTE": 1, "BYTE": 1, "UWORD": 2, "RGB": 2}
PER_ROW = {"UBYTE": 16, "BYTE": 16, "UWORD": 8, "RGB": 4}


def typed_tables(bank: int) -> list[tuple[int, int, int, str]]:
    """Tables the bank's .sym tags `[N] TYPE` or `[R][C] TYPE` -- TYPE UBYTE
    (the default), BYTE (signed), UWORD or RGB (a CGB colour); `[N] dw` is a
    pointer table instead: (lo, hi) file offsets, the elements a row (C, or
    PER_ROW) and the type, for disasm.py --tables. A table runs for its size,
    or to the next global symbol that is not data (a label inside it is fine)."""
    sym = ROOT / "src" / f"bank{bank:02x}.sym"
    if not sym.is_file():
        return []
    rows = cov.read_sym(sym)
    lo_cpu = 0x4000 if bank else 0
    base = bank * BANK - lo_cpu
    stops = sorted(a for a, n, c in rows if not n.startswith(".") and not cov.is_data_symbol(n, c))
    out = []
    for addr, name, comment in rows:
        m = TABLE_TAG.match(comment)
        if name.startswith(".") or not m or re.match(r"\[\d+\] dw\b", comment):
            continue
        kind = m.group(3) or "UBYTE"
        count = int(m.group(1)) * (int(m.group(2)) if m.group(2) else 1)
        per_row = int(m.group(2)) if m.group(2) else PER_ROW[kind]
        stop = next((a for a in stops if a > addr), lo_cpu + BANK)
        out.append((base + addr, base + min(addr + count * ELEMENT_SIZE[kind], stop), per_row, kind))
    return sorted(out)


def tile_blocks(rom: bytes, bank: int) -> list[tuple[int, int, str]]:
    """The bank's tile blocks: `Tiles*` symbols whose header (tools/gfx.py)
    sizes them, with no other symbol inside. (lo, hi) file offsets and the
    asset name their tiles go to (gfx/<name>.png)."""
    sym = ROOT / "src" / f"bank{bank:02x}.sym"
    if not sym.is_file():
        return []
    rows = cov.read_sym(sym)
    lo_cpu = 0x4000 if bank else 0
    addrs = sorted({a for a, _n, _c in rows})
    out = []
    for addr, name, _c in rows:
        if not name.startswith("Tiles"):
            continue
        lo = bank * BANK + (addr - lo_cpu)
        size = gfx.block_size(rom[lo:lo + 2])
        nxt = next((a for a in addrs if a > addr), lo_cpu + BANK)
        if size > 2 and nxt - addr >= size:
            out.append((lo, lo + size, gfx.asset_name(name)))
        else:
            print(f"bank ${bank:02X}: {name} is not a tile block of its own; left as bytes")
    return out


def c_ranges(bank: int) -> tuple[list[str], list[tuple[int, int, str, int]]]:
    """ROM ranges owned by matching C (c/matched.json, written by `make match`).
    With the compiler present, each function's RGBDS translation
    (build/c/bankNN/Name.lines, from tools/match/to_rgbds.py: the code plus the
    literals and const data after it) is rendered in place -- source whose
    references are labels, the step toward a relocatable build; those come
    back as paths for disasm.py --clines. Without it, the function's code
    range INCBINs the ROM itself: (lo, hi, path, offset) for --incbin. The
    build is identical either way, and a C file that stops matching breaks
    `make compare`."""
    manifest = ROOT / "c" / "matched.json"
    if FORK:
        return fork_ranges(bank)
    if not manifest.is_file():
        return [], []
    rom_name = os.environ.get("LSDJ_BASEROM", "lsdj9_4_0.gb")
    lines, incbins = [], []
    for name, m in json.loads(manifest.read_text()).items():
        if int(m["bank"], 16) != bank:
            continue
        addr, n = int(m["addr"], 16), int(m["len"])
        lo = bank * BANK + (addr - (0x4000 if bank else 0))
        src = ROOT / "build" / "c" / f"bank{bank:02x}" / f"{name}.lines"
        if src.is_file():
            lines.append(src.relative_to(ROOT).as_posix())
        else:
            incbins.append((lo, lo + n, rom_name, lo))
    return sorted(lines), sorted(incbins)


FORK = bool(os.environ.get("LSDJ_FORK"))      # a fork's build: `make FORK=1`


def fork_ranges(bank: int) -> tuple[list[str], list[tuple[int, int, str, int]]]:
    """A fork's build: every C function's translation (build/c/fork.json,
    verify_all.py --fork: as in the ROM, changed, or new), and the fork's
    asm (fork/bankNN/Name.asm: the code or data at the .sym's Name, up to the
    next global label, replaced by the file's lines; under a name the .sym
    lacks, new code put before the bank's free space)."""
    manifest = ROOT / "build" / "c" / "fork.json"
    if not manifest.is_file():
        sys.exit("a fork's build needs build/c/fork.json: make FORK=1 match")
    lines = []
    for name, m in json.loads(manifest.read_text()).items():
        if int(m["bank"], 16) != bank:
            continue
        src = ROOT / "build" / "c" / (m.get("lines") or f"bank{bank:02x}/{name}.lines")
        if not src.is_file():
            sys.exit(f"{name}: no {src.relative_to(ROOT)} (a fork's build needs the compiler)")
        if src.relative_to(ROOT).as_posix() not in lines:      # (an SDCC 4 file's, once for all)
            lines.append(src.relative_to(ROOT).as_posix())
    overrides = sorted((ROOT / "fork" / f"bank{bank:02x}").glob("*.asm"))
    if overrides:
        syms = disasm.parse_symbols(ROOT / "src" / f"bank{bank:02x}.sym")
        globals_ = sorted(a for a, n in syms.items() if not n.startswith("."))
        out_dir = ROOT / "build" / "fork" / f"bank{bank:02x}"
        out_dir.mkdir(parents=True, exist_ok=True)
        for path in overrides:
            name = path.stem
            rows = [[0, "V", line] for line in path.read_text().splitlines()]
            addr = next((a for a, n in syms.items() if n == name), None)
            if addr is None:
                doc = {"append": True, "rows": [[0, "L", f"{name}:"]] + rows}
            else:
                end = next((a for a in globals_ if a > addr), 0x4000 if bank == 0 else 0x8000)
                doc = {"lo": addr, "hi": end, "rows": [[addr, k, t] for _a, k, t in rows], "fork": True}
            dst = out_dir / f"{name}.lines"
            text = json.dumps(doc, indent=0)
            if not dst.is_file() or dst.read_text() != text:
                dst.write_text(text)
            lines.append(dst.relative_to(ROOT).as_posix())
    return sorted(lines), []


DEFINITION = re.compile(r"^((?:static\s+)?[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)"
                        r"(?:\s+(?:nonbanked|banked|critical))?)\s*$")


def c_prototypes() -> dict[str, str]:
    """{function: "c/bankNN/Module.c: its definition's first line"} for every
    function matching C owns, printed above its label."""
    manifest = ROOT / "c" / "matched.json"
    if not manifest.is_file():
        return {}
    matched = json.loads(manifest.read_text())
    out: dict[str, str] = {}
    for src in sorted({m["src"] for m in matched.values()}):
        lines = (ROOT / src).read_text().splitlines()
        for i, line in enumerate(lines[:-1]):
            m = DEFINITION.match(line)
            if m and lines[i + 1].strip() == "{" and m.group(2) in matched:
                out[m.group(2)] = f"{src}: {' '.join(m.group(1).split())}"
    return out


def write_docs(rom: bytes) -> Path:
    """build/docs.json for disasm.py --docs: every routine's register
    contract (tools/regflow.py, cached in build/regflow.json) and the
    prototype of each one matching C owns."""
    path = ROOT / "build" / "docs.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    contracts = regflow.load(rom, ROOT / "build" / "regflow.json")
    path.write_text(json.dumps({"contracts": contracts, "prototypes": c_prototypes()},
                               indent=0, sort_keys=True))
    return path


def main() -> None:
    rom = ROM.read_bytes()
    docs = write_docs(rom)
    ent = cov.collect_entries(rom)
    seeds = [(b, a) for b, addrs in ent.items() for a in addrs]
    code, _ = cov.global_trace(rom, seeds)

    notes = "--pointer-notes" in sys.argv[1:]    # list operands left as numbers
    want = {int(a, 16) for a in sys.argv[1:] if not a.startswith("-")}
    banks = sorted(int(p.name[4:6], 16) for p in (ROOT / "src").glob("bank*.sym"))
    for b in banks:
        if want and b not in want:
            continue
        runs = data_runs(code, rom, b)
        data_arg = ",".join(f"0x{s:X}:0x{e:X}" for s, e in runs)
        cmd = ["python3", "tools/disasm.py", "--rom", str(ROM),
               "--bank", f"0x{b:02x}", "--symbols", f"src/bank{b:02x}.sym",
               "--comments", "--docs", str(docs.relative_to(ROOT)), "-o", f"src/bank{b:02x}.asm"]
        if b != 0:
            cmd += ["--globals", "src/bank00.sym"]
        if notes:
            cmd += ["--pointer-notes"]
        if data_arg:
            cmd += ["--data", data_arg]
        tables = pointer_tables(b)
        if tables:
            cmd += ["--ptr-tables", ",".join(f"0x{lo:X}:0x{hi:X}:0x{tb:X}" for lo, hi, tb in tables)]
        texts = text_ranges(rom, b)
        if texts:
            cmd += ["--text", ",".join(f"0x{lo:X}:0x{hi:X}" for lo, hi in texts)]
        if b in PINNED_SECTIONS:
            cmd += ["--sections", ",".join(f"0x{a:X}:{n}" for a, n in PINNED_SECTIONS[b])]
        typed = typed_tables(b)
        if typed:
            cmd += ["--tables", ",".join(f"0x{lo:X}:0x{hi:X}:{n}:{k}" for lo, hi, n, k in typed)]
        blocks = tile_blocks(rom, b)
        for lo, hi, name in blocks:
            png = f"{gfx.GFX_DIR}/{name}.png"
            if gfx.extract_png(ROOT / png, gfx.block_tiles(rom[lo:hi])) == "differs":
                print(f"{png} is not the extraction of your ROM's tiles: kept as it is "
                      "(delete it to extract it again)")
        if blocks:
            cmd += ["--tiles", ",".join(f"0x{lo:X}:0x{hi:X}:{name}" for lo, hi, name in blocks)]
        clines, incbins = c_ranges(b)
        if incbins:
            cmd += ["--incbin", ",".join(f"0x{lo:X}:0x{hi:X}:{p}:0x{off:X}" for lo, hi, p, off in incbins)]
        if clines:
            cmd += ["--clines", ",".join(clines)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        db = sum(e - s for s, e in runs)
        n_c = len(clines) + len(incbins)
        extra = (f", {n_c} function(s) from matching C"
                 + (f" ({len(incbins)} as ROM INCBINs)" if incbins else "")) if n_c else ""
        if blocks:
            extra += f", {len(blocks)} tile block(s) to {gfx.GFX_DIR}/"
        print(f"bank ${b:02X}: {len(runs)} data run(s), {db} bytes marked db{extra}")

    subprocess.run(["python3", "tools/gen_main.py"], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
