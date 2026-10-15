#!/usr/bin/env python3
"""Faithful SM83 (Game Boy) linear disassembler -> RGBDS assembly.

Design goal: **byte-exact round-tripping.** Every byte in the requested range is
decoded, and illegal/undefined opcodes (and explicit data ranges) are emitted as
`db`. Reassembling the output with RGBDS reproduces the input bytes exactly, so a
disassembled region can replace an `INCBIN` in src/main.asm while `make compare`
stays green. Semantic refinement (labels, code/data split) comes afterwards and
never changes the bytes.

`--selftest` round-trips every opcode through the real `rgbasm`/`rgblink`, which
proves the opcode table is correct before it touches the ROM.

Operands become names where that is certain: RAM/hardware addresses from
constants/ram.inc and hardware.inc, branch targets, and ROM addresses
(`rom_pointers()`: banked-call targets, bank 0's trampolines, a ROMX bank's own
pointers) -- so code can move.

Usage:
    tools/disasm.py --selftest
    tools/disasm.py --rom lsdj9_4_0.gb --bank 0 --data 0x104:0x150 -o src/bank00.asm
    tools/disasm.py --rom lsdj9_4_0.gb --start 0x150 --end 0x200 --org 0x150
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

import gfx

R8 = ["b", "c", "d", "e", "h", "l", "[hl]", "a"]
ALU = ["add a,", "adc a,", "sub a,", "sbc a,", "and a,", "xor a,", "or a,", "cp a,"]
CB_OPS = ["rlc", "rrc", "rl", "rr", "sla", "sra", "swap", "srl"]

# op -> (length, kind, template). kind drives operand formatting:
#   none  : no operand
#   n8    : 1 immediate byte   -> {n8}
#   n16   : 2 immediate bytes  -> {n16}
#   e8    : relative branch    -> {e8} (absolute target)
#   spadd : add sp, signed     -> {sp}
#   spld  : ld hl, sp + signed -> {sp}
#   stop  : 2-byte stop
#   db    : illegal opcode emitted as data
MAIN: dict[int, tuple[int, str, str]] = {
    0x00: (1, "none", "nop"),
    0x01: (3, "n16", "ld bc, {n16}"),
    0x02: (1, "none", "ld [bc], a"),
    0x03: (1, "none", "inc bc"),
    0x04: (1, "none", "inc b"),
    0x05: (1, "none", "dec b"),
    0x06: (2, "n8", "ld b, {n8}"),
    0x07: (1, "none", "rlca"),
    0x08: (3, "n16", "ld [{n16}], sp"),
    0x09: (1, "none", "add hl, bc"),
    0x0A: (1, "none", "ld a, [bc]"),
    0x0B: (1, "none", "dec bc"),
    0x0C: (1, "none", "inc c"),
    0x0D: (1, "none", "dec c"),
    0x0E: (2, "n8", "ld c, {n8}"),
    0x0F: (1, "none", "rrca"),
    0x10: (2, "stop", "stop"),
    0x11: (3, "n16", "ld de, {n16}"),
    0x12: (1, "none", "ld [de], a"),
    0x13: (1, "none", "inc de"),
    0x14: (1, "none", "inc d"),
    0x15: (1, "none", "dec d"),
    0x16: (2, "n8", "ld d, {n8}"),
    0x17: (1, "none", "rla"),
    0x18: (2, "e8", "jr {e8}"),
    0x19: (1, "none", "add hl, de"),
    0x1A: (1, "none", "ld a, [de]"),
    0x1B: (1, "none", "dec de"),
    0x1C: (1, "none", "inc e"),
    0x1D: (1, "none", "dec e"),
    0x1E: (2, "n8", "ld e, {n8}"),
    0x1F: (1, "none", "rra"),
    0x20: (2, "e8", "jr nz, {e8}"),
    0x21: (3, "n16", "ld hl, {n16}"),
    0x22: (1, "none", "ld [hl+], a"),
    0x23: (1, "none", "inc hl"),
    0x24: (1, "none", "inc h"),
    0x25: (1, "none", "dec h"),
    0x26: (2, "n8", "ld h, {n8}"),
    0x27: (1, "none", "daa"),
    0x28: (2, "e8", "jr z, {e8}"),
    0x29: (1, "none", "add hl, hl"),
    0x2A: (1, "none", "ld a, [hl+]"),
    0x2B: (1, "none", "dec hl"),
    0x2C: (1, "none", "inc l"),
    0x2D: (1, "none", "dec l"),
    0x2E: (2, "n8", "ld l, {n8}"),
    0x2F: (1, "none", "cpl"),
    0x30: (2, "e8", "jr nc, {e8}"),
    0x31: (3, "n16", "ld sp, {n16}"),
    0x32: (1, "none", "ld [hl-], a"),
    0x33: (1, "none", "inc sp"),
    0x34: (1, "none", "inc [hl]"),
    0x35: (1, "none", "dec [hl]"),
    0x36: (2, "n8", "ld [hl], {n8}"),
    0x37: (1, "none", "scf"),
    0x38: (2, "e8", "jr c, {e8}"),
    0x39: (1, "none", "add hl, sp"),
    0x3A: (1, "none", "ld a, [hl-]"),
    0x3B: (1, "none", "dec sp"),
    0x3C: (1, "none", "inc a"),
    0x3D: (1, "none", "dec a"),
    0x3E: (2, "n8", "ld a, {n8}"),
    0x3F: (1, "none", "ccf"),
    0xC0: (1, "none", "ret nz"),
    0xC1: (1, "none", "pop bc"),
    0xC2: (3, "n16", "jp nz, {n16}"),
    0xC3: (3, "n16", "jp {n16}"),
    0xC4: (3, "n16", "call nz, {n16}"),
    0xC5: (1, "none", "push bc"),
    0xC6: (2, "n8", "add a, {n8}"),
    0xC7: (1, "none", "rst $00"),
    0xC8: (1, "none", "ret z"),
    0xC9: (1, "none", "ret"),
    0xCA: (3, "n16", "jp z, {n16}"),
    0xCC: (3, "n16", "call z, {n16}"),
    0xCD: (3, "n16", "call {n16}"),
    0xCE: (2, "n8", "adc a, {n8}"),
    0xCF: (1, "none", "rst $08"),
    0xD0: (1, "none", "ret nc"),
    0xD1: (1, "none", "pop de"),
    0xD2: (3, "n16", "jp nc, {n16}"),
    0xD3: (1, "db", ""),
    0xD4: (3, "n16", "call nc, {n16}"),
    0xD5: (1, "none", "push de"),
    0xD6: (2, "n8", "sub a, {n8}"),
    0xD7: (1, "none", "rst $10"),
    0xD8: (1, "none", "ret c"),
    0xD9: (1, "none", "reti"),
    0xDA: (3, "n16", "jp c, {n16}"),
    0xDB: (1, "db", ""),
    0xDC: (3, "n16", "call c, {n16}"),
    0xDD: (1, "db", ""),
    0xDE: (2, "n8", "sbc a, {n8}"),
    0xDF: (1, "none", "rst $18"),
    0xE0: (2, "n8", "ldh [$ff{n8x}], a"),
    0xE1: (1, "none", "pop hl"),
    0xE2: (1, "none", "ldh [c], a"),
    0xE3: (1, "db", ""),
    0xE4: (1, "db", ""),
    0xE5: (1, "none", "push hl"),
    0xE6: (2, "n8", "and a, {n8}"),
    0xE7: (1, "none", "rst $20"),
    0xE8: (2, "spadd", "add sp, {sp}"),
    0xE9: (1, "none", "jp hl"),
    0xEA: (3, "n16", "ld [{n16}], a"),
    0xEB: (1, "db", ""),
    0xEC: (1, "db", ""),
    0xED: (1, "db", ""),
    0xEE: (2, "n8", "xor a, {n8}"),
    0xEF: (1, "none", "rst $28"),
    0xF0: (2, "n8", "ldh a, [$ff{n8x}]"),
    0xF1: (1, "none", "pop af"),
    0xF2: (1, "none", "ldh a, [c]"),
    0xF3: (1, "none", "di"),
    0xF4: (1, "db", ""),
    0xF5: (1, "none", "push af"),
    0xF6: (2, "n8", "or a, {n8}"),
    0xF7: (1, "none", "rst $30"),
    0xF8: (2, "spld", "ld hl, sp{sp}"),
    0xF9: (1, "none", "ld sp, hl"),
    0xFA: (3, "n16", "ld a, [{n16}]"),
    0xFB: (1, "none", "ei"),
    0xFC: (1, "db", ""),
    0xFD: (1, "db", ""),
    0xFE: (2, "n8", "cp a, {n8}"),
    0xFF: (1, "none", "rst $38"),
}

# Generate the regular middle of the map.
for _op in range(0x40, 0x80):
    if _op == 0x76:
        MAIN[_op] = (1, "none", "halt")
    else:
        MAIN[_op] = (1, "none", f"ld {R8[(_op >> 3) & 7]}, {R8[_op & 7]}")
for _op in range(0x80, 0xC0):
    MAIN[_op] = (1, "none", f"{ALU[(_op >> 3) & 7]} {R8[_op & 7]}")


def cb_mnemonic(op2: int) -> str:
    reg = R8[op2 & 7]
    hi = op2 >> 6
    if hi == 0:
        return f"{CB_OPS[(op2 >> 3) & 7]} {reg}"
    return f"{['bit', 'res', 'set'][hi - 1]} {(op2 >> 3) & 7}, {reg}"


def _fmt(length: int, kind: str, template: str, raw: bytes, addr: int) -> str:
    if kind in ("none", "stop"):
        return template
    if kind == "n8":
        return template.replace("{n8x}", f"{raw[1]:02x}").replace("{n8}", f"${raw[1]:02x}")
    if kind == "n16":
        return template.replace("{n16}", f"${raw[1] | (raw[2] << 8):04x}")
    if kind == "e8":
        off = raw[1] - 256 if raw[1] > 127 else raw[1]
        target = (addr + 2 + off) & 0xFFFF
        return template.replace("{e8}", f"${target:04x}")
    if kind == "spadd":
        v = raw[1] - 256 if raw[1] > 127 else raw[1]
        return template.replace("{sp}", str(v))
    if kind == "spld":
        v = raw[1] - 256 if raw[1] > 127 else raw[1]
        return template.replace("{sp}", f"{v:+d}")
    raise AssertionError(kind)


def _db(raw: bytes) -> str:
    return "db " + ", ".join(f"${b:02x}" for b in raw)


def _db_str(run: bytes) -> str:
    """`db "..."` for a printable run; escapes RGBDS-special chars (byte-exact)."""
    out = []
    for b in run:
        c = chr(b)
        out.append("\\" + c if c in '"\\{}' else c)
    return 'db "' + "".join(out) + '"'


FILL_MIN = 16    # a run of at least this many $FF bytes is padding


def _emit_tiles(data: bytes, lo: int, hi: int, base_addr: int, name: str) -> list[Row]:
    """A tile block (tools/gfx.py): its header with the count in decimal, the
    prefix the loader skips (a character map) as bytes, and the tiles as an
    INCBIN of the 2bpp rgbgfx makes of gfx/<name>.png."""
    mode, count = data[lo], data[lo + 1]
    start = lo + 2 + gfx.prefix_size(mode)
    what = ", after a character map (the tile for each ASCII code)" if mode & 3 == 1 else ""
    rows: list[Row] = [(base_addr, b"", f"; {count} tiles from {gfx.GFX_DIR}/{name}.png{what}", NOTE),
                       (base_addr, data[lo:lo + 2], f"db ${mode:02x}, {count}", None)]
    for k in range(lo + 2, start, 16):
        seg = data[k:min(k + 16, start)]
        rows.append((base_addr + (k - lo), seg, _db(seg), None))
    rows.append((base_addr + (start - lo), data[start:hi],
                 f'INCBIN "{gfx.BUILD_DIR}/{name}.2bpp", 0, {count} * 16', None))
    return rows


def _table_row(seg: bytes, kind: str) -> str:
    """One row of a typed table: UBYTE as hex bytes, BYTE as signed numbers,
    UWORD as hex words, RGB as CGB colours (the RGB macro; a word with bit
    15 set stays a word)."""
    if kind == "BYTE":
        return "db " + ", ".join(str(b - 256 if b > 127 else b) for b in seg)
    if kind in ("UWORD", "RGB"):
        words = [seg[i] | seg[i + 1] << 8 for i in range(0, len(seg), 2)]
        if kind == "RGB" and all(w < 0x8000 for w in words):
            return "RGB " + ", ".join(f"{w & 31},{w >> 5 & 31},{w >> 10 & 31}" for w in words)
        return "dw " + ", ".join(f"${w:04x}" for w in words)
    return _db(seg)


TEXT_LINE = 32   # a string with no newline or NUL breaks its line here


def _emit_text(data: bytes, lo: int, hi: int, base_addr: int) -> list[Row]:
    """Text: a `db` line per string -- a newline is `\\n` inside it and ends
    the line, a NUL ends it as `, 0` -- other bytes as numbers, a run of
    FILL_MIN or more $FF as padding (`ds`). Byte-exact."""
    rows: list[Row] = []
    items: list[str] = []
    chars: list[str] = []
    start = lo

    def close_string() -> None:
        if chars:
            items.append('"' + "".join(chars) + '"')
            chars.clear()

    def end_line(i: int) -> None:
        nonlocal start
        close_string()
        if items:
            rows.append((base_addr + (start - lo), data[start:i], "db " + ", ".join(items), None))
            items.clear()
        start = i

    i = lo
    while i < hi:
        x = data[i]
        if x == 0xFF:
            j = i
            while j < hi and data[j] == 0xFF:
                j += 1
            if j - i >= FILL_MIN:
                end_line(i)
                rows.append((base_addr + (i - lo), data[i:j], f"ds {j - i}, $ff", None))
                start = i = j
                continue
        i += 1
        if 0x20 <= x <= 0x7E:
            chars.append("\\" + chr(x) if chr(x) in '"\\{}' else chr(x))
            if len(chars) == TEXT_LINE:
                end_line(i)
        elif x == 0x0A:
            chars.append("\\n")
            end_line(i)
        elif x == 0x00:
            close_string()
            items.append("0")
            end_line(i)
        else:
            close_string()
            items.append(f"${x:02x}")
    end_line(hi)
    return rows


def _emit_data(data: bytes, lo: int, hi: int, base_addr: int) -> list[Row]:
    """Emit a data range as rows: runs of FILL_MIN or more $FF (the ROM's
    padding) as `ds n, $ff`, printable runs (>= 4) as `db "TEXT"`, the rest as
    hex `db` lines (<= 16 bytes). Byte-exact either way."""
    rows: list[Row] = []
    pend: list[tuple[int, int]] = []

    def flush() -> None:
        if not pend:
            return
        off0 = pend[0][0]
        bs = bytes(b for _, b in pend)
        for k in range(0, len(bs), 16):
            seg = bs[k:k + 16]
            rows.append((base_addr + (off0 - lo) + k, seg, _db(seg), None))
        pend.clear()

    i = lo
    while i < hi:
        if data[i] == 0xFF:
            j = i
            while j < hi and data[j] == 0xFF:
                j += 1
            if j - i >= FILL_MIN:
                flush()
                rows.append((base_addr + (i - lo), data[i:j], f"ds {j - i}, $ff", None))
                i = j
                continue
        if 0x20 <= data[i] <= 0x7E:
            j = i
            while j < hi and 0x20 <= data[j] <= 0x7E:
                j += 1
            if j - i >= 4:
                flush()
                run = data[i:j]
                rows.append((base_addr + (i - lo), run, _db_str(run), None))
                i = j
                continue
        pend.append((i, data[i]))
        i += 1
    flush()
    return rows


JR_OPS = {0x18, 0x20, 0x28, 0x30, 0x38}
JP_OPS = {0xC3, 0xC2, 0xCA, 0xD2, 0xDA}
CALL_OPS = {0xCD, 0xC4, 0xCC, 0xD4, 0xDC}
RST_OPS = {0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF}
# Instructions after which control does not fall through to the next address.
STOP_FLOW = {0x18, 0xC3, 0xC9, 0xD9, 0xE9}  # jr, jp, ret, reti, jp hl
# Undefined SM83 opcodes (appear only in data).
ILLEGAL = {0xD3, 0xDB, 0xDD, 0xE3, 0xE4, 0xEB, 0xEC, 0xED, 0xF4, 0xFC, 0xFD}

# Hardware-defined entry points worth naming when they fall in range.
VECTOR_NAMES = {
    0x0000: "Rst00", 0x0008: "Rst08", 0x0010: "Rst10", 0x0018: "Rst18",
    0x0020: "Rst20", 0x0028: "Rst28", 0x0030: "Rst30", 0x0038: "Rst38",
    0x0040: "VBlankInterrupt", 0x0048: "StatInterrupt", 0x0050: "TimerInterrupt",
    0x0058: "SerialInterrupt", 0x0060: "JoypadInterrupt", 0x0100: "Entry",
}

# Hardware I/O registers (names from include/hardware.inc, so they always
# resolve when bank source is assembled via main.asm). Substituting these for
# raw $FFxx addresses is byte-exact and hugely improves readability.
HW_REGS = {
    0xFF00: "rJOYP", 0xFF01: "rSB", 0xFF02: "rSC", 0xFF04: "rDIV", 0xFF05: "rTIMA",
    0xFF06: "rTMA", 0xFF07: "rTAC", 0xFF0F: "rIF",
    0xFF10: "rAUD1SWEEP", 0xFF11: "rAUD1LEN", 0xFF12: "rAUD1ENV", 0xFF13: "rAUD1LOW",
    0xFF14: "rAUD1HIGH", 0xFF16: "rAUD2LEN", 0xFF17: "rAUD2ENV", 0xFF18: "rAUD2LOW",
    0xFF19: "rAUD2HIGH", 0xFF1A: "rAUD3ENA", 0xFF1B: "rAUD3LEN", 0xFF1C: "rAUD3LEVEL",
    0xFF1D: "rAUD3LOW", 0xFF1E: "rAUD3HIGH", 0xFF20: "rAUD4LEN", 0xFF21: "rAUD4ENV",
    0xFF22: "rAUD4POLY", 0xFF23: "rAUD4GO", 0xFF24: "rAUDVOL", 0xFF25: "rAUDTERM",
    0xFF26: "rAUDENA", 0xFF40: "rLCDC", 0xFF41: "rSTAT", 0xFF42: "rSCY", 0xFF43: "rSCX",
    0xFF44: "rLY", 0xFF45: "rLYC", 0xFF46: "rDMA", 0xFF47: "rBGP", 0xFF48: "rOBP0",
    0xFF49: "rOBP1", 0xFF4A: "rWY", 0xFF4B: "rWX", 0xFF4C: "rSYS", 0xFF4D: "rSPD",
    0xFF4F: "rVBK", 0xFF50: "rBANK", 0xFF51: "rVDMA_SRC_HIGH", 0xFF52: "rVDMA_SRC_LOW",
    0xFF53: "rVDMA_DEST_HIGH", 0xFF54: "rVDMA_DEST_LOW", 0xFF55: "rVDMA_LEN",
    0xFF56: "rRP", 0xFF68: "rBGPI", 0xFF69: "rBGPD", 0xFF6A: "rOBPI", 0xFF6B: "rOBPD",
    0xFF6C: "rOPRI", 0xFF70: "rWBK", 0xFF71: "rPSW", 0xFF72: "rPSWX", 0xFF73: "rPSWY",
    0xFF74: "rPSM", 0xFF76: "rPCM12", 0xFF77: "rPCM34", 0xFFFF: "rIE",
}
for _i in range(16):
    HW_REGS[0xFF30 + _i] = f"rAUD3WAVE_{_i:X}"


def _sub_hw_regs(text: str) -> str:
    """Replace `[$ffXX]` memory operands with their hardware-register names."""
    def repl(m: re.Match[str]) -> str:
        name = HW_REGS.get(int(m.group(1), 16))
        return f"[{name}]" if name else m.group(0)
    return re.sub(r"\[\$(ff[0-9a-f]{2})\]", repl, text)


def _load_ram_syms() -> tuple[dict[int, str], dict[int, str]]:
    """Parse constants/ram.inc `DEF name EQU $addr  ; comment` lines.

    Returns (exact, offsets):
      exact   {addr: name}                 -- documented base addresses
      offsets {addr: "name + N"}           -- *interior* addresses of a
              documented multi-byte var/array, so a mid-array access renders
              as `wChannelFrequency + 4` instead of a raw `$c0f8` (byte-identical: the
              expression resolves to the same address). Extents come from the
              `; [N]` array-count and `16-bit` (stride 2) annotations, and are
              capped at the next named symbol so they can never swallow it."""
    exact: dict[int, str] = {}
    spans: list[tuple[int, int, str]] = []   # (base, byte_len, name)
    path = Path(__file__).resolve().parent.parent / "constants" / "ram.inc"
    if not path.is_file():
        return exact, {}
    for line in path.read_text().splitlines():
        m = re.match(r"\s*DEF\s+(\w+)\s+EQU\s+\$([0-9A-Fa-f]{2,4})\b(.*)", line)
        if not m:
            continue
        name, addr, rest = m.group(1), int(m.group(2), 16), m.group(3)
        exact[addr] = name
        comment = rest.split(";", 1)[1] if ";" in rest else ""
        cnt = re.match(r"\s*\[(\d+)\]", comment)        # array count at comment start
        count = int(cnt.group(1)) if cnt else 1
        stride = 2 if re.search(r"16-bit", comment) else 1
        blen = count * stride
        if blen > 1:
            spans.append((addr, blen, name))
    nexts = sorted(exact)                                # all named addresses
    offsets: dict[int, str] = {}
    for base, blen, name in sorted(spans):               # ascending: nearest base wins
        nxt = next((a for a in nexts if a > base), base + blen)
        end = min(base + blen, nxt)                      # cap at next named symbol
        for a in range(base + 1, end):
            offsets[a] = f"{name} + {a - base}"
    return exact, offsets


RAM_SYMS, RAM_OFFSETS = _load_ram_syms()


def _sub_ram_syms(text: str) -> str:
    """Replace `$XXXX` operands (bracketed or immediate) with documented RAM
    var names, including `base + N` for interior array addresses. Only known
    addresses are substituted, so unknown ones stay hex. Runs on instruction
    text only (the trailing address comment is appended afterwards), so it
    never corrupts the byte annotation."""
    def repl(m: re.Match[str]) -> str:
        a = int(m.group(1), 16)
        return RAM_SYMS.get(a) or RAM_OFFSETS.get(a, m.group(0))
    return re.sub(r"\$([0-9a-f]{4})\b", repl, text)


# A decoded row: (gb_addr, raw_bytes, asm_text, control_flow_target_or_None)
Row = tuple[int, bytes, str, "int | None"]


IncBin = tuple[int, int, str, int]   # (lo, hi) file offsets, source path, offset in it
# Matching C rendered from its RGBDS translation (tools/match/to_rgbds.py):
# (lo, hi) file offsets and the rows, each (cpu address, kind, text) -- kind
# I an instruction or data row, L a label of the translation's own, V a
# verbatim directive. Rows from it carry these `ref` markers:
C_ROW, C_TEXT = -1, -2
CLines = tuple[int, int, list[tuple[int, str, str]], bool]   # (lo, hi, rows, a fork's)
APPEND = -1                     # a fork's new code: lo and hi of rows put before the bank's free space
# A comment line of the carve's own (the free space at the end of a bank, a
# data block's format), printed verbatim before the row at its address.
NOTE = -3


# Banked calls: the caller pushes the callee's arguments, its address and its
# bank, then calls one of these bank-0 routines (src/bank00.sym).
FARCALL_NAMES = frozenset({"FarCall", "FarCallArgument", "FarCallByte", "FarCallArgumentByte",
                           "FarCallArgument2V", "FarCallArgument2", "FarCallArgument3"})
# `ld hl, nn` / `ld [hl], x` with nn one of these is an MBC5 register write
MBC_REGS = {0x2000: "rROMB0", 0x4000: "rRAMB"}
# A ROM-address operand: (bank, address) to print as a label, the same with
# "LOW"/"HIGH" for one byte of it, or a fixed name.
Pointer = "tuple[int, int] | tuple[int, int, str] | str"


def _decoded(row: Row) -> bool:
    return row[3] not in (C_ROW, C_TEXT, NOTE) and not row[2].startswith(("db", "dw", "ds", "RGB", "INCBIN"))


_PAIR_OF = {"a": "af", "b": "bc", "c": "bc", "d": "de", "e": "de", "h": "hl", "l": "hl"}


def _writes(t: str, reg: str) -> bool:
    """Whether decoded instruction text `t` may write register `reg`."""
    pair = _PAIR_OF[reg]
    if re.match(rf"(?:ld|ldh|inc|dec|swap|res|set|rl|rr|rlc|rrc|sla|sra|srl) {reg}\b", t):
        return True
    if re.match(rf"(?:pop|inc|dec) {pair}$", t) or t.startswith((f"ld {pair},", f"add {pair},")):
        return True
    if pair == "hl" and re.search(r"\[hl[+-]\]", t):
        return True
    if reg == "a" and (re.match(r"(?:add|adc|sub|sbc|and|or|xor) a\b", t)
                       or t in ("rla", "rra", "rlca", "rrca", "cpl", "daa")):
        return True
    return t.startswith(("call", "rst"))     # a call may change anything


def _reg_const(rows: list[Row], i: int, reg: str, depth: int = 0) -> tuple[int, int] | None:
    """The immediate `reg` holds before row i, following register copies back
    (`ld b, $fe / ... / ld l, b`): (row of the `ld r, $NN`, NN) or None."""
    for k in range(i - 1, max(-1, i - 25), -1):   # (it stops at the register's last write)
        if not _decoded(rows[k]):
            return None
        t = rows[k][2]
        m = re.fullmatch(rf"ld {reg}, (\$[0-9a-f]{{2}}|[a-e]|[hl])", t)
        if m:
            src = m.group(1)
            if src.startswith("$"):
                return k, int(src[1:], 16)
            return _reg_const(rows, k, src, depth + 1) if depth < 3 else None
        if _writes(t, reg):
            return None                   # written some other way
    return None


def _pair_halves(rows: list[Row], i: int, rr: str) -> tuple[int, int, int, int] | None:
    """For `push rr` at row i: the rows that load its halves with immediates
    (`ld c, $fe / ld b, $6e / push bc`, SDCC's way with a constant word, or via
    copies, `ld b, $fe / ld c, $6e / ld l, b / ld h, c / push hl`): (row of
    the high byte, the byte, row of the low byte, the byte)."""
    hi, lo = _reg_const(rows, i, rr[0]), _reg_const(rows, i, rr[1])
    if hi is None or lo is None:
        return None
    return hi[0], hi[1], lo[0], lo[1]


def _farcall_bank(nxt: list[Row], rr: str, far: set[int]) -> int | None:
    """The bank of a banked call whose target `rr` holds: `push rr`, then
    `ld a, BANK / push af / inc sp`, then (after any further pushes) a call
    to a FarCall* routine."""
    texts = [r[2] for r in nxt]
    if f"push {rr}" not in texts:
        return None
    j = texts.index(f"push {rr}")
    m = re.fullmatch(r"ld a, \$([0-9a-f]{2})", texts[j + 1]) if j + 1 < len(texts) else None
    if not m or texts[j + 2:j + 4] != ["push af", "inc sp"]:
        return None
    call = next((r for r in nxt[j + 4:j + 8] if r[2].startswith("call")), None)
    return int(m.group(1), 16) if call is not None and call[3] in far else None


def _byte_halves(rows: list[Row], i: int) -> tuple[int, int] | None:
    """SDCC's `Table[i]` with the address in two immediates, starting at row i:
    `ld a, LO / add a, r / ld x, a / ld a, HI / adc a, $00 / ld y, a` (byte
    index; `adc a, r2` for a word index in r2:r) or `ld a, r / add a, LO /
    ld x, a / ld a, r / adc a, HI / ld y, a`. (address, index of the HI row)
    or None."""
    if i + 5 >= len(rows) or not all(_decoded(r) for r in rows[i:i + 6]):
        return None
    t = [r[2] for r in rows[i:i + 6]]
    if not (re.fullmatch(r"ld [b-e], a|ld [hl], a", t[2]) and re.fullmatch(r"ld [b-e], a|ld [hl], a", t[5])):
        return None
    lo = re.fullmatch(r"ld a, \$([0-9a-f]{2})", t[0])
    hi = re.fullmatch(r"ld a, \$([0-9a-f]{2})", t[3])
    if lo and hi and re.fullmatch(r"add a, (?:[a-e]|[hl])", t[1]) \
            and re.fullmatch(r"adc a, (?:\$00|[a-e]|[hl])", t[4]):    # byte / word index
        return int(hi.group(1), 16) << 8 | int(lo.group(1), 16), i + 3
    lo = re.fullmatch(r"add a, \$([0-9a-f]{2})", t[1])
    hi = re.fullmatch(r"adc a, \$([0-9a-f]{2})", t[4])
    if lo and hi and re.fullmatch(r"ld a, [a-e]", t[0]) and re.fullmatch(r"ld a, [a-e]", t[3]):
        return int(hi.group(1), 16) << 8 | int(lo.group(1), 16), i + 4
    return None


def _stored_halves(rows: list[Row], i: int) -> tuple[int, int, int] | None:
    """An address stored a byte at a time (`ld [hl], $65 / inc hl /
    ld [hl], $6f`, or through registers, `ld b, $65 / ld c, $6f / ... /
    ld [hl], b / inc hl / ld [hl], c`: SDCC spilling a function pointer to the
    stack, or setting a RAM handler pointer): (the value, the rows holding
    its low and high immediates), or None. Only an address if the caller finds
    it named (the next banked call's bank, or this bank's own)."""
    if i + 2 >= len(rows) or not all(_decoded(r) for r in rows[i:i + 3]) \
            or rows[i + 1][2] != "inc hl":
        return None
    parts = []
    for k in (i, i + 2):
        m = re.fullmatch(r"ld \[hl\], (\$[0-9a-f]{2}|[a-e])", rows[k][2])
        if not m:
            return None
        if m.group(1).startswith("$"):
            parts.append((k, int(m.group(1)[1:], 16)))
        else:
            c = _reg_const(rows, k, m.group(1))
            if c is None:
                return None
            parts.append(c)
    (k_lo, lo), (k_hi, hi) = parts
    return hi << 8 | lo, k_lo, k_hi


def _pointer_stored(rows: list[Row], i: int) -> tuple[int, int, int] | None:
    """A pointer stored a byte at a time through a: `ld a, LO / ld [hl+], a /
    (ld c, a) / ld a, HI / ld [hl], a` (SoundNoteToPeriod aiming a channel at
    a period table): (the value, the rows of its low and high immediates), or
    None. Only an address if the caller finds it named."""
    if i + 4 >= len(rows) or not all(_decoded(r) for r in rows[i:i + 5]):
        return None
    t = [r[2] for r in rows[i:i + 5]]
    lo = re.fullmatch(r"ld a, \$([0-9a-f]{2})", t[0])
    if not lo or t[1] != "ld [hl+], a":
        return None
    k = i + 2 if re.fullmatch(r"ld [b-e], a", t[2]) else i + 1
    hi = re.fullmatch(r"ld a, \$([0-9a-f]{2})", rows[k + 1][2])
    if not hi or rows[k + 2][2] != "ld [hl], a":
        return None
    return int(hi.group(1), 16) << 8 | int(lo.group(1), 16), i, k + 1


def _next_farcall_bank(rows: list[Row], i: int, far: set[int]) -> int | None:
    """The bank of the next banked call within 40 rows (`ld a, BANK / push af /
    inc sp / call FarCall*`)."""
    for k in range(i, min(len(rows) - 3, i + 40)):
        if not _decoded(rows[k]):
            return None
        m = re.fullmatch(r"ld a, \$([0-9a-f]{2})", rows[k][2])
        if m and rows[k + 1][2] == "push af" and rows[k + 2][2] == "inc sp" \
                and rows[k + 3][2].startswith("call") and rows[k + 3][3] in far:
            return int(m.group(1), 16)
    return None


def _offset_halves(rows: list[Row], i: int) -> tuple[int, int] | None:
    """An address plus a constant, a byte at a time (SDCC doesn't fold
    `(UBYTE *)AsciiCharacterMap - 0x20`): `ld a, LO / add a, KL / (store a) /
    ld a, HI / adc a, KH`, the stores in between not touching a. (address,
    index of the HI row) or None; the offset stays a number."""
    if i + 3 >= len(rows) or not _decoded(rows[i]) or not _decoded(rows[i + 1]):
        return None
    lo = re.fullmatch(r"ld a, \$([0-9a-f]{2})", rows[i][2])
    if not lo or not re.fullmatch(r"add a, \$[0-9a-f]{2}", rows[i + 1][2]):
        return None
    for k in range(i + 2, min(len(rows) - 1, i + 7)):
        if not _decoded(rows[k]):
            return None
        hi = re.fullmatch(r"ld a, \$([0-9a-f]{2})", rows[k][2])
        if hi:
            if re.fullmatch(r"adc a, \$[0-9a-f]{2}", rows[k + 1][2]):
                return int(hi.group(1), 16) << 8 | int(lo.group(1), 16), k
            return None
        if _writes(rows[k][2], "a"):
            return None
    return None


def rom_pointers(rows: list[Row], bank: int | None, symbols: dict[int, str],
                 home: dict[int, str],
                 bank_syms=lambda b: {},
                 table_bank: dict[int, int] | None = None) -> tuple[dict[int, "tuple[int, int] | str"], list[str]]:
    """The 16-bit operands of decoded rows that are ROM addresses -- code that
    moves needs every one of them as a label: {row address: (bank, address)},
    or a register name for an MBC write; plus notes on operands that look like
    ROM addresses but stay numbers.

    - a banked call's target: `ld rr, nn` pushed right before
      `ld a, BANK / push af / inc sp / call FarCall*`;
    - bank 0's inline trampolines: `call`/`jp`/`ld rr` into $4000-$7FFF after
      the function wrote the bank to $2000;
    - a ROMX bank's own `ld rr, $4000-$7FFF`, except gb_cmp16's add form
      (`ld hl, K / add hl, rr / jp c`), a constant;
    - in bank 0, a global name of its own, or a local one pushed as a return
      address (`ld hl, .resume / push hl`: the far calls' pointer calls);
    - every non-null word of a pointer table (`dw $4098`, rows from
      --ptr-tables; `table_bank` gives the bank each row points into);
    - an address loaded as two bytes, SDCC's `Table[i]`: `ld a, LO / add a, r /
      ld e, a / ld a, HI / adc a, $00 / ld d, a` (or `add a, LO` ... `adc a, HI`
      for a word index), when the address is a ROM row -- returned for both
      rows as (bank, address, "LOW") and (bank, address, "HIGH");
      from another bank a small number matching a home name is as likely a
      constant (`ld de, $0200` for `$200 - x`), so it stays one;
    - a pointer stored a byte at a time through a (`ld a, LO / ld [hl+], a /
      ld a, HI / ld [hl], a`) when it is a global's address, bank 0's or this
      bank's own (SoundNoteToPeriod aiming a channel at DrumPeriodTable).
    Values that are neither (`ld bc, $0100`, kit-bank offsets) stay numbers."""
    far = {a for a, n in home.items() if n in FARCALL_NAMES}
    home_globals = {a for a, n in home.items() if not n.startswith(".")}
    out: dict[int, "tuple[int, int] | str"] = {}
    notes: list[str] = []
    index = {r[0]: k for k, r in enumerate(rows) if _decoded(r)}
    tracked: int | None = None            # bank 0: the ROMX bank this function mapped
    for i, row in enumerate(rows):
        addr, _raw, text, ref = row
        m = re.fullmatch(r"dw \$([0-9a-f]{4})", text)
        if m and ref is None and table_bank and addr in table_bank:
            v, tb = int(m.group(1), 16), table_bank[addr]
            if (tb == 0 and 0x0100 <= v < 0x4000) or (tb != 0 and 0x4000 <= v < 0x8000):
                out[addr] = (tb, v)
            elif v not in (0x0000, 0xFFFF):
                notes.append(f"${addr:04x}: {text} (outside bank ${tb:02x})")
            continue
        if not _decoded(row):
            continue
        if addr in symbols and not symbols[addr].startswith("."):
            tracked = None
        m = re.fullmatch(r"ld \[hl\], \$([0-9a-f]{2})", text)
        if m and i and rows[i - 1][2] == "ld hl, $2000":
            tracked = int(m.group(1), 16)
        if text == "ld [$2000], a":
            for k in range(i - 1, max(-1, i - 4), -1):
                mm = re.fullmatch(r"ld a, \$([0-9a-f]{2})", rows[k][2])
                if mm:
                    tracked = int(mm.group(1), 16)
                    break
        if bank == 0 and ref is not None and 0x4000 <= ref < 0x8000 \
                and text.startswith(("call", "jp")):
            if tracked is not None:
                out[addr] = (tracked, ref)
            else:
                notes.append(f"${addr:04x}: {text} (bank unknown)")
            continue
        m = re.fullmatch(r"push (bc|de|hl)", text)
        rr = m.group(1) if m else None
        fb = None
        if rr:
            fb = _farcall_bank([r for r in rows[i:i + 10] if _decoded(r)], rr, far)
        elif ref is not None and re.fullmatch(r"jp \$[0-9a-f]{4}|jr \$[0-9a-f]{4}", text) and ref in index:
            # a jump to the shared `push rr / ld a, BANK / ... / call FarCall`
            # tail of a switch: the pair is loaded before the jump
            k = index[ref]
            mk = re.fullmatch(r"push (bc|de|hl)", rows[k][2])
            if mk:
                rr = mk.group(1)
                fb = _farcall_bank([r for r in rows[k:k + 10] if _decoded(r)], rr, far)
        if rr and fb is not None:
            pair = _pair_halves(rows, i, rr)
            if pair is not None:
                k_hi, hi, k_lo, lo = pair
                tb = fb
                # which way round: the one that names something in the callee's bank
                for v, k_low, k_high in ((hi << 8 | lo, k_lo, k_hi), (lo << 8 | hi, k_hi, k_lo)):
                    t_bank = 0 if v < 0x4000 else tb
                    if (t_bank == 0 and 0x0100 <= v < 0x4000) or 0x4000 <= v < 0x8000:
                        if v in (home if t_bank == 0 else bank_syms(t_bank)):
                            out[rows[k_low][0]] = (t_bank, v, "LOW")
                            out[rows[k_high][0]] = (t_bank, v, "HIGH")
                            break
                else:
                    notes.append(f"${rows[k_lo][0]:04x}: banked call to ${hi:02x}{lo:02x} "
                                 f"in bank ${fb:02x} loaded as bytes: no name either way round")
        if m:
            continue
        stored = _stored_halves(rows, i)
        if stored is not None:
            v, k_lo, k_hi = stored
            fb = _next_farcall_bank(rows, i + 3, far)
            if fb is not None:
                tb = 0 if v < 0x4000 else fb
                name = (home if tb == 0 else bank_syms(tb)).get(v, ".")
            else:                         # no banked call: a name of this bank's own
                tb = bank
                name = symbols.get(v, ".") if bank and 0x4000 <= v < 0x8000 else "."
            if not name.startswith("."):
                out[rows[k_lo][0]] = (tb, v, "LOW")
                out[rows[k_hi][0]] = (tb, v, "HIGH")
            continue
        stored = _pointer_stored(rows, i)
        if stored is not None:
            v, k_lo, k_hi = stored
            tb = 0 if v < 0x4000 else bank
            name = (home if tb == 0 else symbols).get(v, ".") if 0x0100 <= v < 0x8000 and tb is not None else "."
            if not name.startswith("."):
                out[rows[k_lo][0]] = (tb, v, "LOW")
                out[rows[k_hi][0]] = (tb, v, "HIGH")
                continue
        half = _byte_halves(rows, i) or _offset_halves(rows, i)
        if half is not None:
            v, k_hi = half
            tb = 0 if v < 0x4000 else bank
            if tb is not None and (tb == 0 and 0x0100 <= v < 0x4000 or tb and 0x4000 <= v < 0x8000):
                out[addr] = (tb, v, "LOW")
                out[rows[k_hi][0]] = (tb, v, "HIGH")
            continue
        m = re.fullmatch(r"ld (bc|de|hl), \$([0-9a-f]{4})", text)
        if not m:
            continue
        rr, v = m.group(1), int(m.group(2), 16)
        nxt: list[Row] = []
        for r in rows[i + 1:i + 14]:
            if not _decoded(r):
                break
            nxt.append(r)
        texts = [r[2] for r in nxt]
        if rr == "hl" and v in MBC_REGS and texts and texts[0].startswith("ld [hl], "):
            out[addr] = MBC_REGS[v]
            continue
        fb = _farcall_bank(nxt, rr, far)
        if fb is not None and 0x0100 <= v < 0x8000:
            out[addr] = (0 if v < 0x4000 else fb, v)
            continue
        if 0x4000 <= v < 0x8000:
            if texts and re.fullmatch(r"add hl, (bc|de)", texts[0]) and \
                    any(re.fullmatch(r"j[pr] n?c, .*", t) for t in texts[1:3]):
                continue                  # gb_cmp16's add form: a constant
            if bank == 0:
                if tracked is not None:
                    out[addr] = (tracked, v)
                else:
                    notes.append(f"${addr:04x}: {text} (bank unknown)")
            else:
                out[addr] = (bank, v)
        elif bank == 0 and v >= 0x0100 and (
                v in home_globals or (v in home and rr == "hl" and texts[:1] == ["push hl"])):
            out[addr] = (0, v)
    return out, notes


def name_in_bank(syms: dict[int, str], addr: int) -> str | None:
    """The .sym name at `addr` as a reference from another bank: a local one
    with the global before it."""
    name = syms.get(addr)
    if name is None or not name.startswith("."):
        return name
    parent = max((a for a, n in syms.items() if a < addr and not n.startswith(".")), default=None)
    return f"{syms[parent]}{name}" if parent is not None else None


def disassemble(data: bytes, start: int, end: int, org: int,
                data_ranges: list[tuple[int, int]],
                anchors: set[int] | None = None,
                incbins: list[IncBin] | None = None,
                clines: list[CLines] | None = None,
                ptr_tables: list[tuple[int, int, int]] | None = None,
                tiles: list[tuple[int, int, str]] | None = None,
                tables: list[tuple[int, int, int, str]] | None = None,
                texts: list[tuple[int, int]] | None = None,
                fills: set[int] | None = None) -> list[Row]:
    """Return decoded rows covering file offsets [start, end).

    `anchors` is a set of file offsets that are known code-instruction starts
    (e.g. from a symbol map). The decoder never lets an instruction cross an
    anchor, so it re-syncs there: every anchor becomes a clean boundary, which
    guarantees labels attach and repairs alignment after embedded data.

    `incbins` are ranges whose bytes come from another file -- a function that
    now has matching C (tools/match): the range renders as `INCBIN` slices,
    split at every anchor inside it so each symbol still gets its label.

    `clines` are ranges matching C owns, rendered from their RGBDS
    translation: its rows replace the decode (their labels print as usual).
    A fork's (the fourth item set) are code that no longer matches the ROM
    -- changed C, an asm override -- so the range's own label prints and no
    other .sym label inside it, and no ROM bytes: the code is not the ROM's.
    One with lo = hi = APPEND is new code, put before the bank's free space.

    `ptr_tables` are (lo, hi, bank) pointer tables: one `dw $xxxx` row per
    word, which rom_pointers() turns into labels.

    `tiles` are (lo, hi, name) tile blocks whose tiles come from
    gfx/<name>.png (_emit_tiles).

    `tables` are (lo, hi, per_row, kind) typed tables, printed a row of
    per_row elements a line (_table_row; a screen's 20x18 map: a line per
    screen line); a label inside a word splits it into bytes.

    `texts` are (lo, hi) ranges of text, printed a line per string
    (_emit_text).

    `fills` are file offsets of padding labels (`Padding*`): an all-$FF data
    run there is `ds n, $ff` however short."""
    anchors = anchors or set()
    incbins = incbins or []
    clines = clines or []
    appends = [c for c in clines if c[0] == APPEND]
    clines = [c for c in clines if c[0] != APPEND]
    ptr_tables = ptr_tables or []
    tiles = tiles or []
    tables = tables or []
    texts = texts or []

    def next_data_edge(o: int) -> int:
        edges = [hi if lo <= o < hi else lo for lo, hi in data_ranges if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _p, _f in incbins if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _r, _f in clines if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _b in ptr_tables if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _n in tiles if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _w, _k in tables if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi in texts if o < hi]
        edges += [a for a in anchors if a > o]
        return min((e for e in edges if e > o), default=end)

    def in_data(o: int) -> bool:
        return any(lo <= o < hi for lo, hi in data_ranges)

    def in_incbin(o: int) -> IncBin | None:
        return next((ib for ib in incbins if ib[0] <= o < ib[1]), None)

    out: list[Row] = []
    used: set[int] = set()
    o = start
    while o < end:
        addr = org + (o - start)
        cl = next((c for c in clines if c[0] == o), None)
        if cl is not None and cl[3]:              # a fork's: its own label, its rows
            lo, hi, crows, _f = cl
            used.add(lo)
            out.extend((addr, b"", f"\t{text}" if kind == "I" else text, C_TEXT)
                       for _a, kind, text in crows)
            o = hi
            continue
        if cl is not None:
            lo, hi, crows, _f = cl
            used.add(lo)
            istarts = sorted({a for a, kind, _t in crows if kind == "I"})
            ends = {a: b for a, b in zip(istarts, istarts[1:] + [org + (hi - start)])}
            for a, kind, text in crows:
                if kind == "I":
                    fo = start + (a - org)
                    out.append((a, data[fo:start + (ends[a] - org)], text, C_ROW))
                else:
                    out.append((a, b"", text, C_TEXT))
            o = hi
            continue
        tb = next((t for t in tiles if t[0] == o), None)
        if tb is not None and tb[1] <= end:
            out.extend(_emit_tiles(data, o, tb[1], addr, tb[2]))
            o = tb[1]
            continue
        tx = next((x for x in texts if x[0] <= o < x[1]), None)
        if tx is not None:
            edge = min(next_data_edge(o), end)
            out.extend(_emit_text(data, o, edge, addr))
            o = edge
            continue
        tt = next((r for r in tables if r[0] <= o < r[1]), None)
        if tt is not None:
            # a row ends at the row's end, or where a label inside the table
            # starts (next_data_edge) -- the next row starts there
            tlo, _thi, per_row, kind = tt
            size = 2 if kind in ("UWORD", "RGB") else 1
            rel = o - tlo
            edge = min(o + per_row * size - rel % (per_row * size), next_data_edge(o), end)
            if rel % size:                      # after a label inside a word: its high byte
                edge, kind = o + 1, "UBYTE"
            elif (edge - o) % size:             # up to a label inside a word
                edge -= (edge - o) % size
                if edge == o:                   # the word's low byte
                    edge, kind = o + 1, "UBYTE"
            out.append((addr, data[o:edge], _table_row(data[o:edge], kind), None))
            o = edge
            continue
        pt = next((t for t in ptr_tables if t[0] <= o < t[1]), None)
        if pt is not None and o + 2 <= min(pt[1], end):
            out.append((addr, data[o:o + 2], f"dw ${data[o] | data[o + 1] << 8:04x}", None))
            o += 2
            continue
        ib = in_incbin(o)
        if ib is not None:
            edge = min(next_data_edge(o), ib[1], end)
            text = f'INCBIN "{ib[2]}", ${ib[3] + (o - ib[0]):06x}, ${edge - o:04x}'
            out.append((addr, data[o:edge], text, None))
            o = edge
            continue
        if in_data(o):
            edge = min(next_data_edge(o), end)
            if fills and o in fills and data[o:edge] == b"\xff" * (edge - o):
                out.append((addr, data[o:edge], f"ds {edge - o}, $ff", None))
                o = edge
                continue
            out.extend(_emit_data(data, o, edge, addr))
            o = edge
            continue

        op = data[o]
        limit = min(next_data_edge(o), end)
        if op == 0xCB:
            if o + 2 > limit:
                out.append((addr, data[o:o + 1], _db(data[o:o + 1]), None))
                o += 1
                continue
            raw = data[o:o + 2]
            out.append((addr, raw, cb_mnemonic(raw[1]), None))
            o += 2
            continue

        # `stop` is encoded `10 00` by RGBDS. Only decode it as `stop` when the
        # following byte is 00; otherwise emit `db $10` so the real byte survives.
        if op == 0x10:
            if o + 2 <= limit and data[o + 1] == 0x00:
                out.append((addr, data[o:o + 2], "stop", None))
                o += 2
            else:
                out.append((addr, data[o:o + 1], _db(data[o:o + 1]), None))
                o += 1
            continue

        length, kind, template = MAIN[op]
        # Do not let an instruction cross a data boundary or the end of range.
        if o + length > limit:
            seg = data[o:limit]
            out.append((addr, seg, _db(seg), None))
            o = limit
            continue

        raw = data[o:o + length]
        if kind == "db":
            out.append((addr, raw, _db(raw), None))
        else:
            ref: int | None = None
            if op in JR_OPS:
                off = raw[1] - 256 if raw[1] > 127 else raw[1]
                ref = (addr + 2 + off) & 0xFFFF
            elif op in JP_OPS or op in CALL_OPS:
                ref = raw[1] | (raw[2] << 8)
            out.append((addr, raw, _fmt(length, kind, template, raw, addr), ref))
        o += length
    # The free space at the end of a bank pads from wherever the content ends,
    # so the bank can grow into it (and an overflow stops the assembler).
    bank_end = org + (end - start)
    if out and bank_end & 0x3FFF == 0:
        a, raw, text, ref = out[-1]
        if text.startswith("ds ") and ref is None and a + len(raw) == bank_end:
            out[-1:] = [(a, b"", f"; Free space: {len(raw):,} bytes to the end of the bank.", NOTE),
                        (a, raw, f"ds ${bank_end:04x} - @, $ff", None)]
    if appends:                  # a fork's new code, before the free space
        k = next((i for i in range(len(out) - 1, -1, -1)
                  if out[i][3] is NOTE and out[i][2].startswith("; Free space")), len(out))
        new = [(APPEND, b"", f"\t{text}" if kind == "I" else text, C_TEXT)
               for _lo, _hi, crows, _f in appends for _a, kind, text in crows]
        out[k:k] = [(APPEND, b"", "", C_TEXT)] + new + [(APPEND, b"", "", C_TEXT)]
    missed = [f"${org + (lo - start):04x}" for lo, _hi, _r, _f in clines
              if lo not in used and start <= lo < end]
    if missed:   # the decode ran across their start: would silently drop the C
        raise ValueError(f"matching C ranges not rendered: {', '.join(missed)}")
    return out


def trace_code(data: bytes, start: int, end: int, org: int,
               seeds: list[int]) -> set[int]:
    """Recursive-descent reachability: return the set of file offsets that are
    code, starting from `seeds` (file offsets). Follows direct branches, calls,
    and rst within [start, end); stops at ret/reti/unconditional jr|jp/jp hl.
    Targets outside the range (other banks, RAM) are not followed. Bytes never
    reached stay data -- byte-safe either way, this only affects the rendering."""
    code: set[int] = set()
    seen: set[int] = set()
    work = [o for o in seeds if start <= o < end]
    while work:
        o = work.pop()
        last_hl: int | None = None
        while start <= o < end and o not in seen:
            seen.add(o)
            op = data[o]
            if op == 0xCB:
                length = 2
            elif op == 0x10:
                length = 2 if (o + 1 < end and data[o + 1] == 0) else 1
            else:
                length = MAIN[op][0]
            length = min(length, end - o)
            for i in range(length):
                code.add(o + i)

            if op == 0x21 and length == 3:  # ld hl, nn -- candidate jump-table base
                last_hl = data[o + 1] | (data[o + 2] << 8)

            target: int | None = None
            if op in JR_OPS and length == 2:
                off = data[o + 1] - 256 if data[o + 1] > 127 else data[o + 1]
                target = (org + (o - start) + 2 + off) & 0xFFFF
            elif (op in JP_OPS or op in CALL_OPS) and length == 3:
                target = data[o + 1] | (data[o + 2] << 8)
            elif op in RST_OPS:
                target = op & 0x38
            if target is not None:
                toff = start + (target - org)
                if start <= toff < end:
                    work.append(toff)

            # `jp hl` after `ld hl, TABLE`: follow the jump table.
            if op == 0xE9 and last_hl is not None:
                p = start + (last_hl - org)
                addr_form = o >= 3 and data[o - 3:o] == bytes([0x2A, 0x66, 0x6F])
                for _ in range(128):
                    if addr_form:  # table of 2-byte little-endian addresses
                        if p + 2 > end:
                            break
                        t = data[p] | (data[p + 1] << 8)
                        step = 2
                    else:          # table of `jp xxxx` instructions
                        if p + 3 > end or data[p] != 0xC3:
                            break
                        t = data[p + 1] | (data[p + 2] << 8)
                        step = 3
                    toff = start + (t - org)
                    # Conservative: target must be in range and a plausible code
                    # start (not an illegal opcode), else stop the table.
                    if not (org <= t < org + (end - start)) or data[toff] in ILLEGAL:
                        break
                    work.append(toff)
                    p += step

            if op in STOP_FLOW:
                break
            o += length
    return code


def complement_ranges(code: set[int], start: int, end: int) -> list[tuple[int, int]]:
    """File-offset ranges in [start, end) that are NOT in `code` (i.e. data)."""
    ranges = []
    o = start
    while o < end:
        if o in code:
            o += 1
            continue
        run = o
        while o < end and o not in code:
            o += 1
        ranges.append((run, o))
    return ranges


def parse_symbols(path: Path) -> dict[int, str]:
    """Parse a symbol map file. Lines: `ADDR Name` (ADDR hex, optional `$`/`0x`
    or `BANK:ADDR`); `;` starts a comment. Blank/short lines are ignored."""
    syms: dict[int, str] = {}
    for line in path.read_text().splitlines():
        if "@deploy-only" in line:
            continue        # deployed-symbol-only entries never anchor the carve
        line = line.split(";", 1)[0].strip()
        parts = line.split()
        if len(parts) < 2:
            continue
        addr = parts[0].split(":")[-1].lstrip("$")
        if addr.lower().startswith("0x"):
            addr = addr[2:]
        try:
            syms[int(addr, 16)] = parts[1]
        except ValueError:
            continue
    return syms


def parse_symbol_comments(path: Path) -> dict[int, str]:
    """The `.sym` comment of each address (`ADDR Name ; comment`)."""
    out: dict[int, str] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"^([0-9A-Fa-f]{4}) \S+\s*;\s*(.*\S)", line)
        if m and "@deploy-only" not in line:
            out[int(m.group(1), 16)] = m.group(2)
    return out


DOC_WIDTH = 100


def doc_lines(text: str) -> list[str]:
    """A comment above a label, wrapped: `; ...` lines."""
    return ["; " + line for line in textwrap.wrap(text, DOC_WIDTH - 2, break_on_hyphens=False)]


def label_docs(rows: list[Row], labels: dict[int, str], comments: dict[int, str],
               contracts: dict[str, str], prototypes: dict[str, str]) -> dict[int, list[str]]:
    """The comment lines above each label: its `.sym` comment; then for a
    routine of matching C its prototype, for a hand-written one its register
    contract (tools/regflow.py) -- a routine being a global label on code,
    not a jump table (`<Name>Jumps`)."""
    first = {}
    for row in rows:
        first.setdefault(row[0], row)
    out: dict[int, list[str]] = {}
    for addr, name in labels.items():
        lines = doc_lines(comments[addr]) if addr in comments else []
        row = first.get(addr)
        if not name.startswith(".") and row is not None:
            if name in prototypes:
                lines += doc_lines(prototypes[name])
            elif name in contracts and _decoded(row) and not name.endswith("Jumps"):
                lines += doc_lines(contracts[name])
        if lines:
            out[addr] = lines
    return out


def build_labels(rows: list[Row], lo: int, hi: int,
                 symbols: dict[int, str] | None = None,
                 prefix: str = "L",
                 extra_targets: set[int] | None = None) -> dict[int, str]:
    """Name interrupt vectors/entry, branch/call targets, and user symbols
    (which take precedence). `call` targets and symbols are *function entries* and
    get global labels (`prefix`+addr, e.g. `L06_5947`, unique across ROMX banks);
    jr/jp-only targets are internal control flow and get RGBDS *local* labels
    (`.L_xxxx`) scoped to the enclosing global function -- so a fully reverse-
    engineered bank has named functions + local control flow and zero global
    auto-labels. `render()` full-paths (`Func.L_xxxx`) any cross-function ref.
    Labels never change bytes."""
    symbols = symbols or {}
    # Dot-named user symbols (`4015 .loop`) are LOCAL name overrides: they
    # replace the auto `.L_xxxx` at that address (or name a sub-data row /
    # non-branch address) and never become globals.
    user_locals = {a: n for a, n in symbols.items() if n.startswith(".")}
    symbols = {a: n for a, n in symbols.items() if not n.startswith(".")}
    instr_addrs = {addr for addr, _raw, text, _ref in rows
                   if not text.startswith(("db", "dw", "INCBIN"))}
    # Every row-start address (code or data): a user symbol renders on any of
    # these, so a table marked as `db` still gets its label (data labels).
    row_addrs = {addr for addr, _raw, _text, _ref in rows}
    # Classify branch/call targets: `call` targets are function entries (global);
    # jr/jp-only targets are internal control flow and become RGBDS local labels
    # (".L_xxxx") scoped to the enclosing global function.
    call_targets: set[int] = set()
    branch_targets: set[int] = set()
    for _addr, _raw, text, ref in rows:
        if ref is not None and lo <= ref < hi and ref in instr_addrs:
            (call_targets if text.startswith("call") else branch_targets).add(ref)
    # addresses an operand points at (a jump table, a string): local labels too
    branch_targets |= {a for a in (extra_targets or set()) if lo <= a < hi and a in row_addrs}
    labels: dict[int, str] = {}
    for a, name in VECTOR_NAMES.items():
        if lo <= a < hi and a in instr_addrs:
            labels[a] = name
    for ref in call_targets:
        labels.setdefault(ref, f"{prefix}{ref:04X}")
    for a, name in symbols.items():  # user names override autogenerated ones
        if lo <= a < hi and a in row_addrs:  # code OR data row start
            labels[a] = name
    # Now `labels` holds the global (function) labels. Internal branch targets
    # become locals, but only if an enclosing global label precedes them (RGBDS
    # requires it); otherwise they fall back to a unique global auto-label.
    enclosing: dict[int, int | None] = {}
    cur: int | None = None
    for addr, _raw, text, _ref in rows:
        if addr in labels and not labels[addr].startswith("."):
            cur = addr
        enclosing[addr] = cur
    for ref in branch_targets:
        if ref in labels:
            continue
        if enclosing.get(ref) is not None:
            labels[ref] = user_locals.get(ref, f".L_{ref:04X}")
        else:
            labels[ref] = f"{prefix}{ref:04X}"
    # Remaining user locals (sub-data rows, fallthrough points): render wherever
    # a row starts under an enclosing global.
    for a, name in user_locals.items():
        if lo <= a < hi and a in row_addrs and a not in labels \
                and enclosing.get(a) is not None:
            labels[a] = name
    return labels


BYTE_LIST = re.compile(r"db \$[0-9A-Fa-f]{2}(?:, \$[0-9A-Fa-f]{2})*")


def merge_c_bytes(rows: list[Row], labels: dict[int, str]) -> list[Row]:
    """Matching C's const data comes from its translation a byte a row: the
    bytes of a run with no label inside, 16 a row."""
    out: list[Row] = []
    for row in rows:
        addr, raw, text, ref = row
        if ref == C_ROW and out and addr not in labels and BYTE_LIST.fullmatch(text) and len(raw) == 1:
            pa, praw, ptext, pref = out[-1]
            if pref == C_ROW and BYTE_LIST.fullmatch(ptext) and pa + len(praw) == addr and len(praw) < 16:
                out[-1] = (pa, praw + raw, f"{ptext}, {text[3:]}", C_ROW)
                continue
        out.append(row)
    return out


def render(rows: list[Row], comments: bool,
           labels: dict[int, str] | None = None,
           pointers: dict[int, tuple[str, str | int, str]] | None = None,
           sections: dict[int, str] | None = None,
           docs: dict[int, list[str]] | None = None) -> str:
    """`pointers` maps a row to its ROM-address operand: (the operand as
    printed, a fixed name or an address of this bank to print as its label,
    "" / "LOW" / "HIGH" for a byte of it). `sections` are SECTION lines to
    open before the row (and the label) at an address; `docs` the comment
    lines above a label (label_docs)."""
    docs = docs or {}
    labels = labels or {}
    rows = merge_c_bytes(rows, labels)
    pointers = pointers or {}
    sections = dict(sections or {})
    # Enclosing global label (name) per address, for scope-qualifying local refs.
    enclosing: dict[int, str] = {}
    cur_global = ""
    for addr, _raw, _text, _ref in rows:
        if addr in labels and not labels[addr].startswith("."):
            cur_global = labels[addr]
        enclosing[addr] = cur_global
    lines = []
    printed: set[int] = set()
    for addr, raw, text, ref in rows:
        if addr in sections:
            lines.append(sections.pop(addr))
        if addr in labels and addr not in printed:
            if docs and not labels[addr].startswith(".") and lines and lines[-1]:
                lines.append("")                 # a routine (or a table) starts
            lines.extend(docs.get(addr, ()))
            lines.append(f"{labels[addr]}:")
            printed.add(addr)
        if ref in (C_TEXT, NOTE):            # matching C's label / directive, a note
            lines.append(text)
            continue
        if ref == C_ROW:                     # matching C: already symbolic
            if comments and text.startswith('db "'):
                lines.append(f"\t{text:<28} ; ${addr:04x}")
            elif comments:
                hexb = " ".join(f"{b:02x}" for b in raw)
                lines.append(f"\t{text:<28} ; ${addr:04x}: {hexb}")
            else:
                lines.append(f"\t{text}")
            continue
        if ref is not None and ref in labels:
            tgt = labels[ref]
            if tgt.startswith(".") and enclosing.get(ref, "") != enclosing.get(addr, ""):
                # cross-function reference to a local label -> full path
                tgt = enclosing.get(ref, "") + tgt
            text = text.replace(f"${ref:04x}", tgt)
        elif ref is not None and text.startswith("jr"):
            # no label there (e.g. its own operand byte, `jr nc, @ + 1`:
            # rst $38 unless carry): relative, as the encoding is
            k = ref - addr
            text = text.replace(f"${ref:04x}", f"@ {'+' if k >= 0 else '-'} {abs(k)}")
        if addr in pointers:
            token, tgt, part = pointers[addr]
            if isinstance(tgt, int):
                name = labels[tgt]
                if name.startswith(".") and enclosing.get(tgt, "") != enclosing.get(addr, ""):
                    name = enclosing.get(tgt, "") + name
                tgt = name
            text = text.replace(token, f"{part}({tgt})" if part else tgt)
        if not text.startswith("INCBIN"):
            text = _sub_hw_regs(text)
            text = _sub_ram_syms(text)
        if comments and not text.startswith(('db "', "ds ", "INCBIN")):
            hexb = " ".join(f"{b:02x}" for b in raw)
            lines.append(f"\t{text:<28} ; ${addr:04x}: {hexb}")
        elif comments:
            lines.append(f"\t{text:<28} ; ${addr:04x}")
        else:
            lines.append(f"\t{text}")
    return "\n".join(lines)


def selftest() -> int:
    """Round-trip every opcode through rgbasm/rgblink. Returns 0 on success."""
    buf = bytearray()
    for op in range(0x100):
        if op == 0xCB:
            continue
        length = MAIN[op][0]
        buf.append(op)
        buf.extend(b"\x00" * (length - 1))
    for cb in range(0x100):
        buf += bytes([0xCB, cb])
    # Cover the stop quirk: 0x10 followed by a non-zero byte must stay `db $10`.
    buf += bytes([0x10, 0x0E, 0x00])

    data = bytes(buf)
    rows = disassemble(data, 0, len(data), 0, [])
    inc = Path(__file__).resolve().parents[1] / "include"
    asm = ('INCLUDE "hardware.inc"\n'
           'SECTION "selftest", ROM0[$0000]\n'
           + render(rows, comments=False) + "\n")

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        (td / "t.asm").write_text(asm)
        for cmd in (["rgbasm", "-i", str(inc), "-o", "t.o", "t.asm"],
                    ["rgblink", "-o", "t.gb", "t.o"]):
            r = subprocess.run(cmd, cwd=td, capture_output=True, text=True)
            if r.returncode != 0:
                sys.stderr.write(r.stdout + r.stderr)
                print("selftest: FAILED to assemble")
                return 1
        out = (td / "t.gb").read_bytes()[:len(data)]

    if out == data:
        print(f"selftest: OK ({len(data)} bytes, all opcodes round-trip)")
        return 0
    print("selftest: MISMATCH")
    for i, (a, b) in enumerate(zip(data, out)):
        if a != b:
            print(f"  offset {i}: expected {a:02X}, got {b:02X}")
            if i > 0:
                print(f"  context rows near here: {[r[2] for r in rows if r[0] <= i][-3:]}")
            break
    return 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, help="ROM to disassemble")
    ap.add_argument("--bank", type=lambda s: int(s, 0),
                    help="bank number (sets start/end/org automatically)")
    ap.add_argument("--start", type=lambda s: int(s, 0), help="start file offset")
    ap.add_argument("--end", type=lambda s: int(s, 0), help="end file offset (exclusive)")
    ap.add_argument("--org", type=lambda s: int(s, 0), help="GB address of --start")
    ap.add_argument("--data", default="",
                    help="comma-separated lo:hi file-offset ranges to emit as db")
    ap.add_argument("--ptr-tables", default="",
                    help="comma-separated lo:hi:bank file-offset ranges of pointer tables "
                         "(words pointing into bank): printed as `dw Label`")
    ap.add_argument("--pointer-notes", action="store_true",
                    help="list the operands that look like ROM addresses but stay numbers")
    ap.add_argument("--clines", default="",
                    help="comma-separated build/c/bankNN/Name.lines files: matching C's "
                         "RGBDS translation, rendered in place of the decode")
    ap.add_argument("--incbin", default="",
                    help="comma-separated lo:hi:PATH:OFF ranges to emit as INCBIN "
                         "from PATH at OFF (a function that has matching C)")
    ap.add_argument("--tiles", default="",
                    help="comma-separated lo:hi:NAME tile blocks whose tiles are "
                         "INCBINs of the 2bpp made from gfx/NAME.png (tools/gfx.py)")
    ap.add_argument("--tables", default="",
                    help="comma-separated lo:hi:PER_ROW:KIND typed tables (KIND UBYTE, "
                         "BYTE, UWORD or RGB), printed PER_ROW elements a line")
    ap.add_argument("--text", default="",
                    help="comma-separated lo:hi ranges of text, printed a line per string")
    ap.add_argument("--trace", action="store_true",
                    help="recursive-descent: only decode reachable code as code, "
                         "rest as data (auto-seeds vectors/entry for --bank 0)")
    ap.add_argument("--seed", default="",
                    help="extra comma-separated GB addresses to seed --trace")
    ap.add_argument("--symbols", type=Path,
                    help="symbol map file (ADDR Name per line) for label names")
    ap.add_argument("--globals", type=Path,
                    help="home (bank-0) symbol map; names cross-bank calls to it")
    ap.add_argument("--section", help="SECTION header to emit")
    ap.add_argument("--sections", default="",
                    help="comma-separated ADDR:NAME sections pinned at ADDR inside the bank "
                         "(a page-addressed table must not move)")
    ap.add_argument("--comments", action="store_true", help="annotate addr/bytes")
    ap.add_argument("--docs", type=Path,
                    help="JSON {contracts: {name: text}, prototypes: {name: text}}: printed "
                         "with each label's .sym comment above it (tools/regflow.py)")
    ap.add_argument("--no-labels", action="store_true",
                    help="do not generate labels for branch/call targets")
    ap.add_argument("-o", "--output", type=Path, help="output .asm (default: stdout)")
    ap.add_argument("--selftest", action="store_true",
                    help="round-trip all opcodes through rgbasm and exit")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(selftest())

    if not args.rom:
        ap.error("--rom is required (or use --selftest)")
    data = args.rom.read_bytes()

    if args.bank is not None:
        start = args.bank * 0x4000
        end = start + 0x4000
        org = 0x0000 if args.bank == 0 else 0x4000
    else:
        if args.start is None or args.end is None:
            ap.error("provide --bank, or both --start and --end")
        start, end = args.start, args.end
        org = args.org if args.org is not None else start

    explicit_data = []
    for part in filter(None, (p.strip() for p in args.data.split(","))):
        lo, hi = (int(x, 0) for x in part.split(":"))
        explicit_data.append((lo, hi))
    incbins: list[IncBin] = []
    tiles: list[tuple[int, int, str]] = []
    tables: list[tuple[int, int, int, str]] = []
    for part in filter(None, (p.strip() for p in args.tables.split(","))):
        lo_s, hi_s, per_s, kind = part.split(":")
        tables.append((int(lo_s, 0), int(hi_s, 0), int(per_s), kind))
    texts = [tuple(int(x, 0) for x in part.split(":"))
             for part in filter(None, (p.strip() for p in args.text.split(",")))]
    for part in filter(None, (p.strip() for p in args.tiles.split(","))):
        lo_s, hi_s, name = part.split(":")
        tiles.append((int(lo_s, 0), int(hi_s, 0), name))
    for part in filter(None, (p.strip() for p in args.incbin.split(","))):
        lo_s, hi_s, path, off_s = part.split(":")
        incbins.append((int(lo_s, 0), int(hi_s, 0), path, int(off_s, 0)))

    if args.trace:
        seeds: list[int] = []
        if args.bank == 0:
            seeds += [start + (a - org) for a in (0x100, 0x40, 0x48, 0x50, 0x58, 0x60)]
        for s in filter(None, (p.strip() for p in args.seed.split(","))):
            seeds.append(start + (int(s, 0) - org))
        if not seeds:
            ap.error("--trace needs seeds: use --bank 0 or pass --seed ADDR[,ADDR]")
        code = trace_code(data, start, end, org, seeds)
        for lo, hi in explicit_data:
            code -= set(range(lo, hi))
        data_ranges = complement_ranges(code, start, end)
        pct = 100 * len(code) // (end - start)
        print(f"trace: {len(code)} code bytes ({pct}%), "
              f"{len(data_ranges)} data run(s)", file=sys.stderr)
    else:
        data_ranges = explicit_data

    symbols = parse_symbols(args.symbols) if args.symbols else {}
    # Symbol addresses are known code starts: anchor the decoder to them so the
    # routines are clean boundaries and the decode re-syncs after embedded data.
    size = end - start
    anchors = {start + (a - org) for a in symbols if org <= a < org + size}
    # ROMX banks all share $4000-$7FFF, so prefix their auto-labels with the
    # bank to keep them globally unique once every bank is INCLUDEd together.
    prefix = "L" if args.bank in (0, None) else f"L{args.bank:02X}_"
    fills = {start + (a - org) for a, n in symbols.items() if n.startswith("Padding") and org <= a < org + size}
    clines: list[CLines] = []
    for part in filter(None, (p.strip() for p in args.clines.split(","))):
        doc = json.loads(Path(part).read_text())
        rows = [(int(a), k, t) for a, k, t in doc["rows"]]
        if doc.get("append"):
            clines.append((APPEND, APPEND, rows, True))
            continue
        lo_f = start + (int(doc["lo"]) - org)
        hi_f = start + (int(doc["hi"]) - org)
        clines.append((lo_f, hi_f, rows, bool(doc.get("fork"))))
    ptr_tables: list[tuple[int, int, int]] = []
    for part in filter(None, (p.strip() for p in args.ptr_tables.split(","))):
        lo_s, hi_s, b_s = part.split(":")
        ptr_tables.append((int(lo_s, 0), int(hi_s, 0), int(b_s, 0)))
    table_bank = {org + (o - start): tb for lo, hi, tb in ptr_tables for o in range(lo, hi, 2)}
    rows = disassemble(data, start, end, org, data_ranges, anchors, incbins, clines, ptr_tables, tiles, tables, texts, fills)
    # ROM-address operands print as labels (rom_pointers): names from the
    # .sym files beside --symbols; a target inside a data run first gets a row
    # of its own (decode again with it as an anchor)
    home = symbols if args.bank == 0 else (parse_symbols(args.globals) if args.globals else {})
    ptrs: dict[int, "tuple[int, int] | str"] = {}
    notes: list[str] = []
    same: set[int] = set()
    if args.bank is not None and not args.no_labels:
        def own(p: "tuple[int, int] | tuple[int, int, str] | str") -> int | None:
            return p[1] if isinstance(p, tuple) and p[0] == args.bank \
                and org <= p[1] < org + size else None
        sym_dir = Path(args.symbols).parent if args.symbols else None

        def bank_syms(b: int) -> dict[int, str]:
            f = sym_dir / f"bank{b:02x}.sym" if sym_dir else None
            return parse_symbols(f) if f and f.is_file() else {}
        ptrs, notes = rom_pointers(rows, args.bank, symbols, home, bank_syms, table_bank)
        # (every one: the first decode's 16-byte rows may start there by chance)
        split = {start + (t - org) for t in map(own, ptrs.values())
                 if t is not None
                 and any(lo <= start + (t - org) < hi for lo, hi in data_ranges)}
        if split - anchors:
            anchors |= split
            rows = disassemble(data, start, end, org, data_ranges, anchors, incbins, clines, ptr_tables, tiles, tables, texts, fills)
            ptrs, notes = rom_pointers(rows, args.bank, symbols, home, bank_syms, table_bank)
        same = {t for t in map(own, ptrs.values()) if t is not None}
    labels = ({} if args.no_labels
              else build_labels(rows, org, org + size, symbols, prefix, same))
    # Name cross-bank calls into home (bank 0): substitute references to home
    # addresses with their global names (defined in bank00.asm when assembled).
    if args.globals and args.bank not in (0, None) and not args.no_labels:
        for a, name in parse_symbols(args.globals).items():
            if a < 0x4000:
                labels.setdefault(a, name)
    pointers: dict[int, tuple[str, str | int, str]] = {}
    if ptrs:
        sym_dir = Path(args.symbols).parent if args.symbols else None
        c_labels = {a: t[:-1] for a, _raw, t, ref in rows
                    if ref == C_TEXT and re.fullmatch(r"[A-Za-z_][\w.]*:", t)}
        texts = {a: t for a, _raw, t, _ref in rows}
        for a, ptr in sorted(ptrs.items()):
            if isinstance(ptr, str):                  # an MBC register
                pointers[a] = (re.search(r"\$[0-9a-f]{4}", texts[a]).group(0), ptr, "")
                continue
            tb, v = ptr[0], ptr[1]
            part = ptr[2] if len(ptr) > 2 else ""
            # the operand to replace: the word, or the byte this row holds
            token = f"${v:04x}" if not part else f"${(v & 0xFF) if part == 'LOW' else v >> 8:02x}"
            if tb == args.bank and org <= v < org + size:
                if v in labels:
                    pointers[a] = (token, v, part)
                elif v in c_labels:
                    pointers[a] = (token, c_labels[v], part)
                else:
                    notes.append(f"${a:04x}: {texts[a]} (no row starts at ${v:04x})")
                continue
            name = name_in_bank(home if tb == 0 else
                                (parse_symbols(sym_dir / f"bank{tb:02x}.sym")
                                 if sym_dir and (sym_dir / f"bank{tb:02x}.sym").is_file() else {}), v)
            if name:
                pointers[a] = (token, name, part)
            else:
                notes.append(f"${a:04x}: {texts[a]} (${tb:02x}:{v:04x} has no name)")
    if notes:
        print(f"bank ${args.bank:02X}: {len(notes)} ROM-address-like operand(s) left as numbers"
              + ("" if args.pointer_notes else " (--pointer-notes lists them)"), file=sys.stderr)
        if args.pointer_notes:
            print("\n".join("  " + n for n in notes), file=sys.stderr)
    pinned = {}
    for part in filter(None, (p.strip() for p in args.sections.split(","))):
        a_s, name = part.split(":", 1)
        a = int(a_s, 0)
        pinned[a] = (f'SECTION "{name}", ROM0[${a:04X}]' if args.bank == 0
                     else f'SECTION "{name}", ROMX[${a:04X}], BANK[${args.bank:03X}]')
    docs = None
    if args.docs and args.symbols:
        extra = json.loads(args.docs.read_text())
        own = {a: n for a, n in labels.items() if a in symbols and symbols[a] == n}
        docs = label_docs(rows, own, parse_symbol_comments(args.symbols),
                          extra.get("contracts", {}), extra.get("prototypes", {}))
    body = render(rows, comments=args.comments, labels=labels, pointers=pointers, sections=pinned,
                  docs=docs)
    section = args.section
    if section is None:
        if args.bank == 0:
            section = 'SECTION "ROM Bank $000", ROM0[$0000]'
        elif args.bank is not None:
            section = f'SECTION "ROM Bank ${args.bank:03X}", ROMX[$4000], BANK[${args.bank:03X}]'
        else:
            section = f'SECTION "code_{start:06x}", ROM0[${org:04x}]'
    text = section + "\n" + body + "\n"

    if args.output:
        args.output.write_text(text)
        print(f"wrote {args.output} ({len(rows)} rows, {end - start} bytes)")
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
