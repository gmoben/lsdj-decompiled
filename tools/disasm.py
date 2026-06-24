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


JR_OPS = {0x18, 0x20, 0x28, 0x30, 0x38}
JP_OPS = {0xC3, 0xC2, 0xCA, 0xD2, 0xDA}
CALL_OPS = {0xCD, 0xC4, 0xCC, 0xD4, 0xDC}

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


# A decoded row: (gb_addr, raw_bytes, asm_text, control_flow_target_or_None)
Row = tuple[int, bytes, str, "int | None"]


def disassemble(data: bytes, start: int, end: int, org: int,
                data_ranges: list[tuple[int, int]]) -> list[Row]:
    """Return decoded rows covering file offsets [start, end)."""
    def next_data_edge(o: int) -> int:
        edges = [hi if lo <= o < hi else lo for lo, hi in data_ranges if o < hi]
        return min((e for e in edges if e > o), default=end)

    def in_data(o: int) -> bool:
        return any(lo <= o < hi for lo, hi in data_ranges)

    out: list[Row] = []
    o = start
    while o < end:
        addr = org + (o - start)
        if in_data(o):
            edge = min(next_data_edge(o), end)
            chunk = data[o:edge]
            for i in range(0, len(chunk), 16):
                seg = chunk[i:i + 16]
                out.append((addr + i, seg, _db(seg), None))
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


def build_labels(rows: list[Row], lo: int, hi: int) -> dict[int, str]:
    """Name interrupt vectors/entry and any in-range branch/call target that
    lands on an instruction boundary. Labels never change emitted bytes."""
    instr_addrs = {addr for addr, _raw, text, _ref in rows if not text.startswith("db")}
    labels: dict[int, str] = {}
    for a, name in VECTOR_NAMES.items():
        if lo <= a < hi and a in instr_addrs:
            labels[a] = name
    for _addr, _raw, _text, ref in rows:
        if ref is not None and lo <= ref < hi and ref in instr_addrs and ref not in labels:
            labels[ref] = f"L{ref:04X}"
    return labels


def render(rows: list[Row], comments: bool,
           labels: dict[int, str] | None = None) -> str:
    labels = labels or {}
    lines = []
    for addr, raw, text, ref in rows:
        if addr in labels:
            lines.append(f"{labels[addr]}:")
        if ref is not None and ref in labels:
            text = text.replace(f"${ref:04x}", labels[ref])
        text = _sub_hw_regs(text)
        if comments:
            hexb = " ".join(f"{b:02x}" for b in raw)
            lines.append(f"\t{text:<28} ; ${addr:04x}: {hexb}")
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

    data_ranges = []
    for part in filter(None, (p.strip() for p in args.data.split(","))):
        lo, hi = (int(x, 0) for x in part.split(":"))
        data_ranges.append((lo, hi))

    rows = disassemble(data, start, end, org, data_ranges)
    labels = {} if args.no_labels else build_labels(rows, org, org + (end - start))
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
