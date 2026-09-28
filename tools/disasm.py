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

Usage:
    tools/disasm.py --selftest
    tools/disasm.py --rom lsdj9_4_0.gb --bank 0 --data 0x104:0x150 -o src/bank00.asm
    tools/disasm.py --rom lsdj9_4_0.gb --start 0x150 --end 0x200 --org 0x150
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

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


def _emit_data(data: bytes, lo: int, hi: int, base_addr: int) -> list[Row]:
    """Emit a data range as rows: printable runs (>= 4) as `db "TEXT"`, the rest
    as hex `db` lines (<= 16 bytes). Byte-exact either way."""
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
    0x0040: "VBlankInterrupt", 0x0048: "STATInterrupt", 0x0050: "TimerInterrupt",
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
              as `wChanFreq + 4` instead of a raw `$c0f8` (byte-identical: the
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


def disassemble(data: bytes, start: int, end: int, org: int,
                data_ranges: list[tuple[int, int]],
                anchors: set[int] | None = None,
                incbins: list[IncBin] | None = None) -> list[Row]:
    """Return decoded rows covering file offsets [start, end).

    `anchors` is a set of file offsets that are known code-instruction starts
    (e.g. from a symbol map). The decoder never lets an instruction cross an
    anchor, so it re-syncs there: every anchor becomes a clean boundary, which
    guarantees labels attach and repairs alignment after embedded data.

    `incbins` are ranges whose bytes come from another file -- a function that
    now has matching C (tools/match): the range renders as `INCBIN` slices,
    split at every anchor inside it so each symbol still gets its label."""
    anchors = anchors or set()
    incbins = incbins or []

    def next_data_edge(o: int) -> int:
        edges = [hi if lo <= o < hi else lo for lo, hi in data_ranges if o < hi]
        edges += [hi if lo <= o < hi else lo for lo, hi, _p, _f in incbins if o < hi]
        edges += [a for a in anchors if a > o]
        return min((e for e in edges if e > o), default=end)

    def in_data(o: int) -> bool:
        return any(lo <= o < hi for lo, hi in data_ranges)

    def in_incbin(o: int) -> IncBin | None:
        return next((ib for ib in incbins if ib[0] <= o < ib[1]), None)

    out: list[Row] = []
    o = start
    while o < end:
        addr = org + (o - start)
        ib = in_incbin(o)
        if ib is not None:
            edge = min(next_data_edge(o), ib[1], end)
            text = f'INCBIN "{ib[2]}", ${ib[3] + (o - ib[0]):06x}, ${edge - o:04x}'
            out.append((addr, data[o:edge], text, None))
            o = edge
            continue
        if in_data(o):
            edge = min(next_data_edge(o), end)
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


def build_labels(rows: list[Row], lo: int, hi: int,
                 symbols: dict[int, str] | None = None,
                 prefix: str = "L") -> dict[int, str]:
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
                   if not text.startswith(("db", "INCBIN"))}
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


def render(rows: list[Row], comments: bool,
           labels: dict[int, str] | None = None) -> str:
    labels = labels or {}
    # Enclosing global label (name) per address, for scope-qualifying local refs.
    enclosing: dict[int, str] = {}
    cur_global = ""
    for addr, _raw, _text, _ref in rows:
        if addr in labels and not labels[addr].startswith("."):
            cur_global = labels[addr]
        enclosing[addr] = cur_global
    lines = []
    for addr, raw, text, ref in rows:
        if addr in labels:
            lines.append(f"{labels[addr]}:")
        if ref is not None and ref in labels:
            tgt = labels[ref]
            if tgt.startswith(".") and enclosing.get(ref, "") != enclosing.get(addr, ""):
                # cross-function reference to a local label -> full path
                tgt = enclosing.get(ref, "") + tgt
            text = text.replace(f"${ref:04x}", tgt)
        if not text.startswith("INCBIN"):
            text = _sub_hw_regs(text)
            text = _sub_ram_syms(text)
        if comments and not text.startswith(('db "', "INCBIN")):
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
    ap.add_argument("--incbin", default="",
                    help="comma-separated lo:hi:PATH:OFF ranges to emit as INCBIN "
                         "from PATH at OFF (a function that has matching C)")
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
    ap.add_argument("--comments", action="store_true", help="annotate addr/bytes")
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
    rows = disassemble(data, start, end, org, data_ranges, anchors, incbins)
    labels = ({} if args.no_labels
              else build_labels(rows, org, org + size, symbols, prefix))
    # Name cross-bank calls into home (bank 0): substitute references to home
    # addresses with their global names (defined in bank00.asm when assembled).
    if args.globals and args.bank not in (0, None) and not args.no_labels:
        for a, name in parse_symbols(args.globals).items():
            if a < 0x4000:
                labels.setdefault(a, name)
    body = render(rows, comments=args.comments, labels=labels)
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
