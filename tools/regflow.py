#!/usr/bin/env python3
"""Register contracts: what each routine takes in registers, hands back in
them and clobbers, derived from the code.

LSDj's C follows SDCC's convention (arguments on the stack, the result in e
or de, bc saved), and its prototype says the rest. The hand-written asm --
the interrupt handlers, the sound driver's core, the tile loader -- passes
values in registers by its own agreements, and those are what this works
out, for the carve to print above each such routine.

For every code symbol in src/bankNN.sym, a forward pass over its code
follows what each register holds: its value at entry, a constant, or
something else. A move copies that, a push and a pop carry it over the
stack (a register pushed and popped back is preserved, not used; one pushed
and dropped with `add sp` was an argument), a call applies the callee's own
contract, so the contracts are a fixed point over the call graph:

  in        the registers whose entry value the routine uses (an operand,
            an address, a store, a branch on a flag, a callee's input, an
            argument pushed for a callee), and `stack` when it reads its
            caller's stack frame
  changes   the registers that may hold something else at a return

Then a backward pass over every caller finds what it reads after each call
before writing it, again to a fixed point (a caller returning at once hands
the question on to its own callers):

  out       the changes some caller reads
  clobbers  the other changes

The flags count as two registers, zero and carry. A jump to another
routine's entry is a tail call. A banked call through FarCall & co. is the
routine it calls (the address the caller pushes) plus the helper's own
effects; a `jp hl` reaches every entry of a jump table, every fragment of a
page of fragments, or, with a return address pushed, is a call. What can't
be resolved -- a `jp hl` to an address computed some other way, a call into
the bank the caller maps, a stack pointer moved by hand -- marks the
contract partial, and says why.

    tools/regflow.py NAME...        # contracts of these routines
    tools/regflow.py --asm          # every routine that is not C
    tools/regflow.py --json FILE    # all of them
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from importlib import import_module  # noqa: E402

disasm = import_module("disasm")
coverage = import_module("coverage")

ROOT = Path(__file__).resolve().parents[1]
REGS = ("a", "b", "c", "d", "e", "h", "l", "zf", "cf")
IDX = {r: i for i, r in enumerate(REGS)}
FLAGS = ("zf", "cf")
R8 = disasm.R8
X = ("X",)
FLAG_OF = {0: "zf", 1: "zf", 2: "cf", 3: "cf"}            # nz z nc c
# FarCall & co.: the caller pushes the target's address (and the bank and the
# arguments); the helper maps the bank and calls it (tools/coverage.py)
FARCALLS = {"FarCall", "FarCallArgument", "FarCallByte", "FarCallArgumentByte",
            "FarCallArgument2V", "FarCallArgument2", "FarCallArgument3"}
# calls the routine in hl, so whatever that routine does (resolved at FarCall's
# call sites; anywhere else the contract is partial)
CALL_HL = "CallHL"
TAG_DW = re.compile(r"^\[(\d+)\] dw\b")
# code run from RAM: the routine in ROM it is a copy of (MixerKernel, unrolled
# by InitializeKitMixer, returns at its end)
RAM_CODE = {0xFF80: "OamDmaRoutine", 0xD480: "MixerKernel"}
ENDS_RETURN = {"MixerKernel"}
# handlers called through a pointer in RAM, by what the code stores there
RAM_POINTERS = {0xC984: ("FlushBlockToSram", "VerifyBlockAgainstSram")}   # wBlockSinkPointer


def phys(bank: int, cpu: int) -> int:
    return cpu if cpu < 0x4000 else bank * 0x4000 + cpu - 0x4000


MEMORY_WRITE = re.compile(r"(?:ld|ldh) \[|(?:inc|dec|rlc|rrc|rl|rr|sla|sra|swap|srl|res \d,|set \d,) \[hl\]")


def regs_of(names) -> tuple:
    """`flags` stands for both flags."""
    out = []
    for r in names:
        out += list(FLAGS) if r == "flags" else [r]
    return tuple(out)


class Op:
    """One instruction's effect on the registers."""
    __slots__ = ("addr", "length", "kind", "target", "cond", "uses", "writes", "moves",
                 "consts", "push", "pop", "sp", "sp_read", "bank_write", "text")

    def __init__(self, addr: int, length: int, text: str):
        self.addr, self.length, self.text = addr, length, text
        self.kind = "seq"          # seq jump jumpcc call callcc ret retcc jphl stop
        self.target = None
        self.cond = None           # the flag a conditional reads
        self.uses: tuple = ()      # registers read (as a value or an address)
        self.writes: tuple = ()    # registers given a new value
        self.moves: tuple = ()     # (dst, src) copies
        self.consts: tuple = ()    # (reg, value) constants loaded
        self.push: tuple = ()      # registers pushed, high byte first
        self.pop: tuple = ()       # registers popped, low byte first
        self.sp = 0                # bytes the stack grows by; None: sp set by hand
        self.sp_read = None        # ld hl, sp+n: n
        self.bank_write = None     # a store into the bank register: "a", or the value


def decode(rom: bytes, bank: int, cpu: int) -> Op:
    p = phys(bank, cpu)
    op = rom[p]
    if op == 0xCB:
        raw = rom[p:p + 2]
        o = Op(cpu, 2, disasm.cb_mnemonic(raw[1]))
        r = R8[raw[1] & 7]
        grp, sub = raw[1] >> 6, (raw[1] >> 3) & 7
        val = ("h", "l") if r == "[hl]" else (r,)
        if grp == 0:                                 # rotates, shifts, swap
            o.uses = val + (("cf",) if sub in (2, 3) else ())
            o.writes = (() if r == "[hl]" else (r,)) + FLAGS
        elif grp == 1:                               # bit
            o.uses, o.writes = val, ("zf",)
        else:                                        # res / set
            o.uses, o.writes = val, (() if r == "[hl]" else (r,))
        return o
    length, kind, template = disasm.MAIN[op]
    raw = rom[p:p + length]
    if kind == "db":
        o = Op(cpu, 1, f"db ${op:02x}")
        o.kind = "stop"
        return o
    o = Op(cpu, length, disasm._fmt(length, kind, template, raw, cpu))
    n16 = raw[1] | raw[2] << 8 if length == 3 else None
    if op in (0x00, 0x76, 0xF3, 0xFB, 0x10, 0x08):     # nop halt di ei stop ld [nn],sp
        return o
    if op in (0x01, 0x11, 0x21):                     # ld rr, n16
        hi, lo = {0x01: ("b", "c"), 0x11: ("d", "e"), 0x21: ("h", "l")}[op]
        o.consts = ((hi, n16 >> 8), (lo, n16 & 0xFF))
        return o
    if op == 0x31:                                   # ld sp, n16
        o.sp = None
        return o
    if op in (0x02, 0x12, 0x22, 0x32):                # ld [rr], a
        rr = {0x02: ("b", "c"), 0x12: ("d", "e")}.get(op, ("h", "l"))
        o.uses = ("a",) + rr
        o.writes = ("h", "l") if op in (0x22, 0x32) else ()
        return o
    if op in (0x0A, 0x1A, 0x2A, 0x3A):                # ld a, [rr]
        rr = {0x0A: ("b", "c"), 0x1A: ("d", "e")}.get(op, ("h", "l"))
        o.uses = rr
        o.writes = ("a",) + (("h", "l") if op in (0x2A, 0x3A) else ())
        return o
    if op in (0x03, 0x13, 0x23, 0x0B, 0x1B, 0x2B):    # inc/dec rr
        rr = [("b", "c"), ("d", "e"), ("h", "l")][(op >> 4) & 3]
        o.uses = o.writes = rr
        return o
    if op in (0x33, 0x3B):                           # inc/dec sp
        o.sp = -1 if op == 0x33 else 1
        return o
    if op < 0x40 and op & 7 in (4, 5):               # inc/dec r
        r = R8[(op >> 3) & 7]
        if r == "[hl]":
            o.uses, o.writes = ("h", "l"), ("zf",)
        else:
            o.uses, o.writes = (r,), (r, "zf")
        return o
    if op < 0x40 and op & 7 == 6:                    # ld r, n8
        r = R8[(op >> 3) & 7]
        if r == "[hl]":
            o.uses = ("h", "l")
        else:
            o.consts = ((r, raw[1]),)
        return o
    if op in (0x07, 0x0F, 0x17, 0x1F):                # rlca rrca rla rra
        o.uses = ("a",) + (("cf",) if op in (0x17, 0x1F) else ())
        o.writes = ("a",) + FLAGS
        return o
    if op in (0x09, 0x19, 0x29, 0x39):                # add hl, rr
        rr = {0x09: ("b", "c"), 0x19: ("d", "e"), 0x29: (), 0x39: ()}[op]
        o.uses = ("h", "l") + rr
        o.writes = ("h", "l", "cf")
        return o
    if op in disasm.JR_OPS:
        off = raw[1] - 256 if raw[1] > 127 else raw[1]
        o.target = (cpu + 2 + off) & 0xFFFF
        if op == 0x18:
            o.kind = "jump"
        else:
            o.kind, o.cond = "jumpcc", FLAG_OF[(op >> 3) & 3]
        return o
    if op == 0x27:                                   # daa
        o.uses, o.writes = ("a", "cf"), ("a",) + FLAGS
        return o
    if op == 0x2F:                                   # cpl
        o.uses = o.writes = ("a",)
        return o
    if op == 0x37:                                   # scf
        o.consts = (("cf", 1),)
        return o
    if op == 0x3F:                                   # ccf
        o.uses = o.writes = ("cf",)
        return o
    if 0x40 <= op < 0x80:                            # ld r, r'
        d, s = R8[(op >> 3) & 7], R8[op & 7]
        if d == "[hl]":
            o.uses = (s, "h", "l")
        elif s == "[hl]":
            o.uses, o.writes = ("h", "l"), (d,)
        elif d != s:
            o.moves = ((d, s),)
        return o
    if 0x80 <= op < 0xC0 or op in (0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE):
        alu = (op >> 3) & 7
        src = None if op >= 0xC0 else R8[op & 7]
        if src == "a" and alu in (2, 5):             # sub a / xor a: 0, whatever a was
            o.consts = (("a", 0), ("zf", 1), ("cf", 0))
            return o
        if src == "a" and alu == 7:                  # cp a: the flags only
            o.consts = (("zf", 1), ("cf", 0))
            return o
        src_regs = ("h", "l") if src == "[hl]" else ((src,) if src else ())
        o.uses = ("a",) + src_regs + (("cf",) if alu in (1, 3) else ())
        if alu in (4, 5, 6):                         # and, xor, or: carry cleared
            o.writes, o.consts = ("a", "zf"), (("cf", 0),)
        else:
            o.writes = (("a",) if alu != 7 else ()) + FLAGS
        return o
    if op in (0xC0, 0xC8, 0xD0, 0xD8):                # ret cc
        o.kind, o.cond = "retcc", FLAG_OF[(op >> 3) & 3]
        return o
    if op in (0xC9, 0xD9):                           # ret, reti
        o.kind = "ret"
        return o
    if op in (0xC1, 0xD1, 0xE1, 0xF1):                # pop rr
        o.pop = {0xC1: ("c", "b"), 0xD1: ("e", "d"), 0xE1: ("l", "h"), 0xF1: ("flags", "a")}[op]
        return o
    if op in (0xC5, 0xD5, 0xE5, 0xF5):                # push rr
        o.push = {0xC5: ("b", "c"), 0xD5: ("d", "e"), 0xE5: ("h", "l"), 0xF5: ("a", "flags")}[op]
        return o
    if op in disasm.JP_OPS:
        o.target = n16
        if op == 0xC3:
            o.kind = "jump"
        else:
            o.kind, o.cond = "jumpcc", FLAG_OF[(op >> 3) & 3]
        return o
    if op in disasm.CALL_OPS:
        o.target = n16
        if op == 0xCD:
            o.kind = "call"
        else:
            o.kind, o.cond = "callcc", FLAG_OF[(op >> 3) & 3]
        return o
    if op in disasm.RST_OPS:
        o.kind, o.target = "call", op & 0x38
        return o
    if op == 0xE0:                                   # ldh [n8], a
        o.uses = ("a",) if raw[1] != 0x04 else ()   # rDIV resets whatever is written
        return o
    if op == 0xF0:                                   # ldh a, [n8]
        o.writes = ("a",)
        return o
    if op == 0xE2:                                   # ldh [c], a
        o.uses = ("a", "c")
        return o
    if op == 0xF2:                                   # ldh a, [c]
        o.uses, o.writes = ("c",), ("a",)
        return o
    if op == 0xE8:                                   # add sp, e8
        off = raw[1] - 256 if raw[1] > 127 else raw[1]
        o.sp, o.writes = -off, FLAGS
        return o
    if op == 0xE9:
        o.kind, o.uses = "jphl", ("h", "l")
        return o
    if op == 0xEA:                                   # ld [n16], a
        o.uses = ("a",)
        if 0x2000 <= n16 < 0x4000:
            o.bank_write = "a"
        return o
    if op == 0xFA:                                   # ld a, [n16]
        o.writes = ("a",)
        return o
    if op == 0xF8:                                   # ld hl, sp+e8
        o.sp_read = raw[1] - 256 if raw[1] > 127 else raw[1]
        o.writes = ("h", "l") + FLAGS
        return o
    if op == 0xF9:                                   # ld sp, hl
        o.uses, o.sp = ("h", "l"), None
        return o
    raise ValueError(f"no semantics for ${op:02x}")


# --- the program ------------------------------------------------------------

class Program:
    def __init__(self, rom: bytes):
        self.rom = rom
        self.ops: dict[tuple[int, int], Op] = {}
        self.names: dict[tuple[int, int], str] = {}
        self.comments: dict[tuple[int, int], str] = {}
        self.entries: set[tuple[int, int]] = set()        # code symbols
        self.ends: dict[tuple[int, int], int] = {}        # a symbol's next symbol
        for sym in sorted((ROOT / "src").glob("bank*.sym")):
            bank = int(sym.name[4:6], 16)
            rows = [(a, n, c) for a, n, c in coverage.read_sym(sym) if not n.startswith(".")]
            addrs = sorted({a for a, _, _ in rows})
            for addr, name, comment in rows:
                key = (bank if addr >= 0x4000 else 0, addr)
                self.names[key] = name
                self.comments[key] = comment
                nxt = [a for a in addrs if a > addr]
                self.ends[key] = nxt[0] if nxt else (0x4000 if addr < 0x4000 else 0x8000)
                if not coverage.is_data_symbol(name, comment):
                    self.entries.add(key)
        self.by_name = {n: k for k, n in self.names.items()}

    def op(self, key: tuple[int, int]) -> Op:
        o = self.ops.get(key)
        if o is None:
            o = self.ops[key] = decode(self.rom, *key)
        return o

    def symbol_at(self, key: tuple[int, int]) -> tuple[int, int] | None:
        """The symbol whose range holds key."""
        best = None
        for k in self.names:
            if k[0] == key[0] and k[1] <= key[1] and (best is None or k[1] > best[1]):
                best = k
        return best


class Contract:
    __slots__ = ("inputs", "may", "must", "returns", "partial", "outputs", "stack_args", "why",
                 "where", "called")

    def __init__(self):
        self.inputs: set[str] = set()
        self.may: set[str] = set()
        self.must: set[str] = set()
        self.returns = False
        self.partial: set[str] = set()
        self.outputs: set[str] = set()
        self.stack_args = False
        self.why: dict[str, int] = {}             # an input -> where it is first used
        self.where: dict[str, int] = {}           # a partial reason -> where it arises
        self.called = False                       # some analysed code calls or jumps here

    def sig(self):
        return (frozenset(self.inputs), frozenset(self.may), self.returns,
                frozenset(self.partial), self.stack_args)


# A state: (registers' tokens, the stack's tokens -- None when unknown,
# FOREIGN while sp points into a buffer --, the mapped ROMX bank or None,
# the last constant loaded into hl or None, the stack saved while sp points
# into a buffer or None). A token is ("E", reg) -- the register's value at
# entry --, ("K", value), ("F", zero, carry) for the pushed flags, or X.
FOREIGN = "foreign"
POPS_PUSHED = "pops what the code before it pushed"

def join_tok(a, b):
    return a if a == b else X


def join_stack(a, b):
    if a == b:
        return a
    if a is None or b is None or a == FOREIGN or b == FOREIGN or len(a) != len(b):
        return None
    return tuple(join_tok(x, y) for x, y in zip(a, b))


def join(s, t):
    regs = tuple(join_tok(x, y) for x, y in zip(s[0], t[0]))
    return (regs, join_stack(s[1], t[1]), s[2] if s[2] == t[2] else None,
            s[3] if s[3] == t[3] else None, join_stack(s[4], t[4]))


class Analysis:
    def __init__(self, prog: Program):
        self.p = prog
        self.contracts: dict[tuple[int, int], Contract] = defaultdict(Contract)

    def key_of(self, state, code_bank: int, cpu: int):
        """Where an address in code running from code_bank lands: bank 0, the
        same ROMX bank, or from bank 0 the bank mapped (None: the caller's);
        code in RAM is the ROM routine it was copied from (None: unknown)."""
        if cpu < 0x4000:
            return (0, cpu)
        if cpu >= 0x8000:
            return self.p.by_name.get(RAM_CODE.get(cpu, ""))
        if code_bank:
            return (code_bank, cpu)
        if state[2] is not None:
            return (state[2], cpu)
        # not mapped here: the one bank with a routine there, if only one has
        banks = [b for b, a in self.p.entries if a == cpu and b]
        return (banks[0], cpu) if len(banks) == 1 else None

    def farcall_targets(self, code_bank: int, site: int):
        """The routines a FarCall-family call at `site` reaches: the address
        the caller pushes (or the pointer table it reads it from) and the
        bank it pushes."""
        rom = self.p.rom
        j = phys(code_bank, site)
        win = rom[max(0, j - 40):j]
        bank = last_hl = table = None
        addrs = []
        for k in range(len(win)):
            if k + 3 < len(win) and win[k] == 0x3E and win[k + 2] == 0xF5 and win[k + 3] == 0x33:
                bank = win[k + 1]
            if win[k] in (0x01, 0x11, 0x21) and k + 2 < len(win):
                v = win[k + 1] | win[k + 2] << 8
                if 0x4000 <= v < 0x8000:
                    addrs.append(v)
                    if win[k] == 0x21:
                        last_hl = v
            for lo_op, hi_op in ((0x0E, 0x06), (0x1E, 0x16), (0x2E, 0x26)):
                if k + 3 < len(win):
                    if win[k] == lo_op and win[k + 2] == hi_op:
                        addrs.append(win[k + 1] | win[k + 3] << 8)
                    elif win[k] == hi_op and win[k + 2] == lo_op:
                        addrs.append(win[k + 3] | win[k + 1] << 8)
            if win[k:k + 3] == bytes([0x2A, 0x66, 0x6F]):
                table = last_hl
        if bank is None:
            return None
        if table is not None:
            tkey = (code_bank, table)
            n = self.table_size(tkey) or 0
            words = [rom[phys(code_bank, table + 2 * i)] | rom[phys(code_bank, table + 2 * i) + 1] << 8
                     for i in range(n)]
            keys = [(bank, w) for w in words if (bank, w) in self.p.entries]
            return keys or None
        addrs = [v for v in addrs if 0x4000 <= v < 0x8000 and (bank, v) in self.p.entries]
        return [(bank, addrs[-1])] if addrs else None

    def table_size(self, key) -> int | None:
        m = TAG_DW.match(self.p.comments.get(key, ""))
        return int(m.group(1)) if m else None

    def jphl_targets(self, code_bank: int, state, site: int):
        """Where a `jp hl` at `site` goes: hl itself when it is a constant;
        the entries of the jump table whose address hl last held (read as
        words, `ld a,[hl+]; ld h,[hl]; ld l,a` before the jump, or a list of
        `jp`s); the fragments of a page when only h is known."""
        regs, _stack, _bank, base, _saved = state
        h, l_ = regs[IDX["h"]], regs[IDX["l"]]
        if h[0] == "K" and l_[0] == "K":
            return [h[1] << 8 | l_[1]]
        rom = self.p.rom
        if h[0] == "K" and base is None:             # a page of fragments
            base = h[1] << 8
        if base is None:
            return None
        if base >= 0x8000:                           # a handler pointer in RAM
            names = RAM_POINTERS.get(base)
            return [self.p.by_name[n][1] for n in names] if names else None
        p = phys(code_bank, site)
        words = bytes([0x2A, 0x66, 0x6F]) in rom[max(0, p - 8):p]
        tkey = self.key_of(state, code_bank, base)
        if tkey is None:
            return None
        if not words and rom[phys(*tkey)] != 0xC3 and tkey in self.p.entries:
            # fragments: the code there, and after each `ret` in it
            out, a = [base], base
            while a < self.p.ends[tkey]:
                o = self.p.op((tkey[0], a))
                a += o.length
                if o.kind == "ret" and a < self.p.ends[tkey]:
                    out.append(a)
            return out
        size = self.table_size(tkey)
        out = []
        q = base
        for _ in range(size or 64):
            pe = phys(tkey[0], q)
            if words:
                t, step = rom[pe] | rom[pe + 1] << 8, 2
            else:
                if rom[pe] != 0xC3:
                    break
                t, step = rom[pe + 1] | rom[pe + 2] << 8, 3
            if size is None and q != base and (tkey[0], q) in self.p.names:
                break
            tk = self.key_of(state, code_bank, t)
            if tk is None or rom[phys(*tk)] in disasm.ILLEGAL:
                break
            out.append(t)
            q += step
        return out or None

    # -- the forward pass --------------------------------------------------------
    def analyze(self, root: tuple[int, int], record: bool = False):
        c = Contract()
        start = (tuple(("E", r) for r in REGS), (), root[0] or None, None, None)
        states: dict[tuple[int, int], tuple] = {}
        work = [(root, start)]
        exits = []
        graph = {}          # key -> (op, inline successors, calls)
        while work:
            key, st = work.pop()
            old = states.get(key)
            new = st if old is None else join(old, st)
            if new == old:
                continue
            states[key] = new
            succ, ex, calls, via = self.step(root, key, self.p.op(key), new, c)
            graph[key] = (self.p.op(key), [k for k, _ in succ if k not in via], calls)
            work.extend(succ)
            exits.extend(ex)
        must = None
        for regs, stack in exits:
            changed = {r for r, t in zip(REGS, regs) if t != ("E", r)}
            for r, t in zip(REGS, regs):                 # handed back in another register
                if t[0] == "E" and t[1] != r:
                    c.inputs.add(t[1])
            c.may |= changed
            must = changed if must is None else must & changed
            c.returns = True
            if (stack is None or stack == FOREIGN) and POPS_PUSHED not in c.partial:
                c.partial.add("moves sp")
            elif stack:
                c.partial.add("returns through its stack")
        c.must = must or set()
        return (c, graph) if record else c

    def use(self, c: Contract, regs, names, at=None):
        for r in regs_of(names):
            t = regs[IDX[r]]
            if t[0] == "E":
                c.inputs.add(t[1])
                if at is not None:
                    c.why.setdefault(t[1], at)

    def flag(self, c: Contract, reason: str, at: int):
        c.partial.add(reason)
        c.where.setdefault(reason, at)

    def apply_call(self, c: Contract, regs, callees, at=None):
        out = list(regs)
        for k in callees:
            callee = self.contracts[k]
            self.use(c, regs, sorted(callee.inputs), at)
            name = self.p.names.get(k)
            if callee.partial and name not in FARCALLS and name != CALL_HL:
                c.partial.add(f"via {name or 'a call'}")
                if at is not None:
                    c.where.setdefault(f"via {name or 'a call'}", at)
            for r in callee.may:
                out[IDX[r]] = X
        return tuple(out)

    def step(self, root, key, o: Op, st, c: Contract):
        """Inline successors [(key, state)], exits [(regs, stack)], calls
        [("call", callees, return key) or ("tail", callees)]."""
        regs, stack, bank, hl, saved = st
        code_bank = key[0]
        regs = list(regs)
        self.use(c, regs, o.uses + ((o.cond,) if o.cond else ()), o.addr)
        if o.bank_write == "a":
            t = regs[IDX["a"]]
            bank = t[1] if t[0] == "K" else None
        elif o.text.startswith("ld [hl], $") and regs[IDX["h"]][0] == regs[IDX["l"]][0] == "K":
            if 0x2000 <= (regs[IDX["h"]][1] << 8 | regs[IDX["l"]][1]) < 0x4000:
                bank = int(o.text.split("$")[1], 16)
        # hl as an offset from the entry sp ("S", n): reading at n >= 2 reads
        # the caller's frame (above the return address)
        h_tok = regs[IDX["h"]]
        if h_tok[0] == "S" and "[hl" in o.text and h_tok[1] >= 2:
            c.stack_args = True
        s_next = None
        if h_tok[0] == "S" and regs[IDX["l"]] == h_tok:
            if o.text in ("inc hl", "dec hl"):
                s_next = h_tok[1] + (1 if o.text == "inc hl" else -1)
            elif "[hl+]" in o.text or "[hl-]" in o.text:
                s_next = h_tok[1] + (1 if "[hl+]" in o.text else -1)
        if o.text in ("add hl, bc", "add hl, de"):
            hi, lo = (IDX["b"], IDX["c"]) if o.text.endswith("bc") else (IDX["d"], IDX["e"])
            if regs[hi][0] == regs[lo][0] == "K":
                hl = regs[hi][1] << 8 | regs[lo][1]
        for d, s in o.moves:
            regs[IDX[d]] = regs[IDX[s]]
        for r in o.writes:
            regs[IDX[r]] = X
        for r, v in o.consts:
            regs[IDX[r]] = ("K", v)
        names = [r for r, _ in o.consts]
        if "h" in names and "l" in names:
            hl = regs[IDX["h"]][1] << 8 | regs[IDX["l"]][1]
        if o.push and stack not in (None, FOREIGN):   # a byte each; the flags are F
            stack = stack + tuple(("F", regs[IDX["zf"]], regs[IDX["cf"]]) if r == "flags"
                                  else regs[IDX[r]] for r in o.push)
        if o.pop:
            if stack in (None, FOREIGN) or len(stack) < len(o.pop):
                if stack not in (None, FOREIGN):
                    self.flag(c, POPS_PUSHED, o.addr)
                    stack = None
                for r in regs_of(o.pop):
                    regs[IDX[r]] = X
            else:
                for r in o.pop:                      # low byte first: the top
                    t, stack = stack[-1], stack[:-1]
                    if r == "flags":
                        regs[IDX["zf"]], regs[IDX["cf"]] = (t[1], t[2]) if t[0] == "F" else (X, X)
                    else:
                        regs[IDX[r]] = X if t[0] == "F" else t
        if o.sp is None:
            if o.uses:                               # ld sp, hl: back from a buffer
                if saved is not None:
                    stack, saved = saved, None
                else:
                    stack = None
                    self.flag(c, "moves sp", o.addr)
            else:                                    # ld sp, n16: into a buffer
                if stack not in (None, FOREIGN) and saved is None:
                    saved = stack
                stack = FOREIGN
        elif o.sp and stack not in (None, FOREIGN):
            if o.sp > 0:
                stack = stack + (X,) * o.sp
            elif len(stack) >= -o.sp:
                dropped, stack = stack[len(stack) + o.sp:], stack[:len(stack) + o.sp]
                for t in dropped:                    # an argument pushed for a callee
                    if t[0] == "E":
                        c.inputs.add(t[1])
            else:
                stack = None
                self.flag(c, POPS_PUSHED, o.addr)
        if o.sp_read is not None and o.text.startswith("ld hl, sp"):
            if stack not in (None, FOREIGN):
                regs[IDX["h"]] = regs[IDX["l"]] = ("S", o.sp_read - len(stack))
        elif s_next is not None:
            regs[IDX["h"]] = regs[IDX["l"]] = ("S", s_next)
        regs = tuple(regs)
        state = (regs, stack, bank, hl, saved)
        nxt = (o.addr + o.length) & 0xFFFF
        succ, exits, calls, via = [], [], [], set()

        def flow_to(cpu, state):
            """A jump or fall-through: inline, or a tail call when it reaches
            another routine's entry."""
            tkey = self.key_of(state, code_bank, cpu)
            if tkey is None:
                self.flag(c, "code in RAM" if cpu >= 0x8000 else "the caller's bank", o.addr)
                return
            if self.p.names.get(root) in ENDS_RETURN and cpu == self.p.ends[root]:
                exits.append((state[0], state[1]))
                return
            if tkey != root and tkey in self.p.entries and state[1] == ():
                callee = self.contracts[tkey]
                r2 = self.apply_call(c, state[0], [tkey], o.addr)
                if callee.returns:
                    exits.append((r2, state[1]))
                calls.append(("tail", (tkey,)))
                return
            succ.append((tkey, state))

        k = o.kind
        if k == "stop":
            self.flag(c, f"an illegal opcode at ${o.addr:04X}", o.addr)
        elif k in ("ret", "retcc"):
            if stack not in (None, FOREIGN) and len(stack) >= 2 and stack[-1][0] == stack[-2][0] == "K":
                # `push hl / ret` with an address pushed: a jump there
                flow_to(stack[-2][1] << 8 | stack[-1][1], (regs, stack[:-2], bank, None, saved))
            else:
                exits.append((regs, stack))
            if k == "retcc":
                flow_to(nxt, state)
        elif k == "jump":
            flow_to(o.target, state)
        elif k == "jumpcc":
            flow_to(o.target, state)
            flow_to(nxt, state)
        elif k == "jphl":
            targets = self.jphl_targets(code_bank, state, o.addr)
            if targets is not None:
                for t in targets:
                    flow_to(t, state)
            elif stack not in (None, FOREIGN) and len(stack) >= 2 and stack[-1][0] == stack[-2][0] == "K":
                # `ld hl, .resume / push hl / ... / jp hl`: a call through hl,
                # to a routine that may change any register
                self.flag(c, "calls hl", o.addr)
                ret = stack[-2][1] << 8 | stack[-1][1]
                flow_to(ret, ((X,) * len(REGS), stack[:-2], bank, None, saved))
            else:
                self.flag(c, "jp hl", o.addr)
        elif k in ("call", "callcc"):
            tkey = self.key_of(state, code_bank, o.target)
            if k == "callcc":
                flow_to(nxt, state)
            if tkey is None:
                self.flag(c, "code in RAM" if o.target >= 0x8000 else "the caller's bank", o.addr)
                return succ, exits, calls, via
            name = self.p.names.get(tkey)
            callees = [tkey]
            if name in FARCALLS:
                # the caller pushed (arguments,) the address, then the bank
                far = None
                if stack not in (None, FOREIGN) and len(stack) >= 3 \
                        and all(t[0] == "K" for t in stack[-3:]):
                    addr = stack[-3][1] << 8 | stack[-2][1]
                    fkey = (stack[-1][1] if addr >= 0x4000 else 0, addr)
                    if fkey in self.p.entries:
                        far = [fkey]
                if far is None:
                    far = self.farcall_targets(code_bank, o.addr)
                if far is None:
                    self.flag(c, f"{name}'s target", o.addr)
                else:
                    callees += far
            if name == CALL_HL:
                self.flag(c, "calls hl", o.addr)
            r2 = self.apply_call(c, regs, callees, o.addr)
            returns = self.contracts[tkey].returns or name in FARCALLS or name == CALL_HL
            rkey = self.key_of(state, code_bank, nxt)
            calls.append(("call", tuple(callees), rkey if returns else None))
            if returns:
                n = len(succ)
                flow_to(nxt, (r2, stack, bank, None, saved))
                if k == "call" and len(succ) > n:
                    via.add(succ[-1][0])             # reached through the call only
        else:
            flow_to(nxt, state)
        return succ, exits, calls, via

    def call_hl(self) -> Contract:
        """CallHL's contract: hl, and whatever the routine in hl does."""
        c = Contract()
        c.inputs, c.may, c.returns, c.partial = {"h", "l"}, set(REGS), True, {"calls hl"}
        return c

    def run(self, verbose=False):
        keys = sorted(self.p.entries)
        for rnd in range(60):
            changed = 0
            for k in keys:
                c = self.call_hl() if self.p.names.get(k) == CALL_HL else self.analyze(k)
                old = self.contracts[k]
                old.must = c.must
                c.inputs |= old.inputs
                c.may |= old.may
                c.returns |= old.returns
                c.partial |= old.partial
                c.stack_args |= old.stack_args
                if c.sig() != old.sig():
                    self.contracts[k] = c
                    changed += 1
            if verbose:
                print(f"round {rnd}: {changed} changed", file=sys.stderr)
            if not changed:
                break
        for k in keys:                       # resolved at each call site
            if self.p.names.get(k) in FARCALLS:
                self.contracts[k].partial -= {"calls hl", f"via {CALL_HL}"}
        self.outputs(keys, verbose)

    # -- the backward pass ---------------------------------------------------------
    def outputs(self, keys, verbose=False):
        graphs = {k: self.analyze(k, record=True)[1] for k in keys}
        for rnd in range(60):
            after: dict[tuple, set[str]] = defaultdict(set)
            for k, graph in graphs.items():
                for callees, live in self.live_after_calls(k, graph):
                    for ck in callees:
                        after[ck] |= live
            changed = 0
            for k in after:
                if k in self.contracts:
                    self.contracts[k].called = True
            for k in keys:
                c = self.contracts[k]
                out = c.may & after.get(k, set())
                if out != c.outputs:
                    c.outputs = out
                    changed += 1
            if verbose:
                print(f"outputs round {rnd}: {changed} changed", file=sys.stderr)
            if not changed:
                break

    def live_after_calls(self, root, graph, live_out: dict | None = None):
        """[(callees, registers live right after the call)] for every call and
        tail call in root's code (after a tail call: root's own outputs).

        Backward liveness over the registers and the stack's bytes (top
        first): a pushed register is live only if the byte it pushed is
        popped into a live register or dropped by `add sp` (an argument a
        callee read), so `push de / call ... / pop de` does not read de; a
        byte dropped by `inc sp` (`push af / inc sp`: the flags) is dead.
        A byte no instruction is seen to consume is dead too, so a push/pop
        pair around a loop does not keep its register live by itself."""
        outs = frozenset(self.contracts[root].outputs)
        empty = (frozenset(), ())
        live: dict[tuple[int, int], tuple] = {k: empty for k in graph}

        def join(a, b):
            sa, sb = a[1], b[1]
            n = max(len(sa), len(sb))
            sa, sb = sa + (False,) * (n - len(sa)), sb + (False,) * (n - len(sb))
            return (a[0] | b[0], tuple(x or y for x, y in zip(sa, sb)))

        def flows(key):
            o, succs, calls = graph[key]
            out = None
            for sk in succs:
                if sk in live:
                    out = live[sk] if out is None else join(out, live[sk])
            if o.kind in ("ret", "retcc"):
                out = (outs, ()) if out is None else join(out, (outs, ()))
            sites = []
            for kind, callees, rkey in ((x[0], x[1], x[2] if len(x) > 2 else None) for x in calls):
                after = (outs, ()) if kind == "tail" else live.get(rkey, empty) if rkey else empty
                sites.append((callees, set(after[0])))
                must = frozenset().union(*(self.contracts[ck].must for ck in callees))
                cin = frozenset().union(*(self.contracts[ck].inputs for ck in callees))
                before = ((after[0] - must) | cin, after[1])
                out = before if out is None else join(out, before)
            regs, stack = out if out is not None else empty
            # an instruction that only sets registers nobody reads is dead:
            # its operands are not read either (`or a, a` clearing carry)
            computed = set(o.writes) | {d for d, _ in o.moves}
            constants = {r for r, _ in o.consts}
            if (o.kind == "seq" and (computed or constants) and not o.push and not o.pop
                    and not o.sp and o.sp_read is None and not MEMORY_WRITE.match(o.text)
                    and not computed & regs):
                return (regs - constants, stack), sites
            # the instruction itself, backwards
            if o.pop:                                    # its bytes, top first
                names = [("cf", "zf") if r == "flags" else (r,) for r in o.pop]
                stack = tuple(bool(set(n) & regs) for n in names) + stack
                regs = regs - set(regs_of(o.pop))
            if o.push:                                   # pushed high byte first
                names = [("zf", "cf") if r == "flags" else (r,) for r in o.push]
                top = stack[:len(names)] + (False,) * (len(names) - len(stack))
                stack = stack[len(names):]
                for n, alive in zip(reversed(names), top):
                    if alive:
                        regs = regs | set(n)
            if o.sp is None or o.sp_read is not None:     # sp moved, or a stack address taken
                stack = tuple(True for _ in stack)
            elif o.sp:
                if o.sp > 0:                             # add sp, -n / dec sp: slots made
                    stack = stack[o.sp:]
                else:                                    # dropped: arguments, or inc sp's junk
                    stack = (o.text != "inc sp",) * -o.sp + stack
            uses = set(regs_of(o.uses)) | ({o.cond} if o.cond else set()) | {r for _, r in o.moves}
            kills = set(o.writes) | {d for d, _ in o.moves} | {r for r, _ in o.consts}
            return (frozenset(uses) | (regs - kills), stack), sites

        for _ in range(200):
            changed = False
            for key in graph:
                li, _ = flows(key)
                if li != live[key]:
                    live[key] = li
                    changed = True
            if not changed:
                break
        if live_out is not None:                         # for tools/regflow.py --why
            live_out.update(live)
        out = []
        for key in graph:
            out += flows(key)[1]
        return out


# --- presentation -------------------------------------------------------------

def regs_text(regs: set[str]) -> str:
    """`a, hl, carry`: pairs whole, the flags by name."""
    rs = set(regs)
    parts = ["a"] if "a" in rs else []
    for pair, (hi, lo) in (("bc", ("b", "c")), ("de", ("d", "e")), ("hl", ("h", "l"))):
        if hi in rs and lo in rs:
            parts.append(pair)
        else:
            parts += [r for r in (hi, lo) if r in rs]
    parts += [n for f, n in (("zf", "zero"), ("cf", "carry")) if f in rs]
    return ", ".join(parts) or "-"


def clobber_text(regs: set[str]) -> str:
    """In the usual pair notation: af for a and the flags, f for the flags."""
    rs = set(regs)
    parts = []
    if "a" in rs:
        parts.append("af" if rs & set(FLAGS) else "a")
    elif rs & set(FLAGS):
        parts.append("f")
    for pair, (hi, lo) in (("bc", ("b", "c")), ("de", ("d", "e")), ("hl", ("h", "l"))):
        if hi in rs and lo in rs:
            parts.append(pair)
        else:
            parts += [r for r in (hi, lo) if r in rs]
    return ", ".join(parts) or "-"


def describe(c: Contract) -> str:
    ins = regs_text(c.inputs)
    if c.stack_args:
        ins = "stack" if ins == "-" else ins + ", stack"
    if not c.returns:
        text = f"in: {ins}; does not return"
    elif not c.called:
        changes = clobber_text(c.may)
        text = f"in: {ins}; changes: {'nothing' if changes == '-' else changes} (no caller)"
    else:
        text = f"in: {ins}; out: {regs_text(c.outputs)}; clobbers: {clobber_text(c.may - c.outputs)}"
    partial = c.partial if c.returns else {r for r in c.partial if not r.startswith("via ")}
    if partial:
        text += f" (partial: {', '.join(sorted(partial))})"
    return text


def contracts(rom: bytes, verbose: bool = False) -> tuple[Program, Analysis]:
    prog = Program(rom)
    an = Analysis(prog)
    an.run(verbose=verbose)
    return prog, an


def cache_key(rom: bytes) -> str:
    h = hashlib.sha1(rom)
    for f in sorted((ROOT / "src").glob("bank*.sym")) + [Path(__file__), Path(coverage.__file__)]:
        h.update(f.read_bytes())
    return h.hexdigest()


def load(rom: bytes, cache: Path | None = None) -> dict[str, str]:
    """{routine name: its contract}, cached in `cache` by the ROM, the
    symbols and this file."""
    key = cache_key(rom)
    if cache and cache.is_file():
        data = json.loads(cache.read_text())
        if data.get("key") == key:
            return data["contracts"]
    prog, an = contracts(rom)
    out = {prog.names[k]: describe(an.contracts[k]) for k in sorted(prog.entries)}
    if cache:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"key": key, "contracts": out}, indent=0, sort_keys=True))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*")
    ap.add_argument("--rom", type=Path, default=ROOT / "lsdj9_4_0.gb")
    ap.add_argument("--asm", action="store_true", help="every routine that is not C")
    ap.add_argument("--json", type=Path)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--why", action="store_true", help="where each input is first used")
    args = ap.parse_args()
    prog, an = contracts(args.rom.read_bytes(), args.verbose)
    if args.json:
        args.json.write_text(json.dumps({prog.names[k]: describe(an.contracts[k])
                                         for k in sorted(prog.entries)}, indent=1))
    names = list(args.names)
    if args.asm:
        matched = set(json.loads((ROOT / "c" / "matched.json").read_text()))
        names += [prog.names[k] for k in sorted(prog.entries) if prog.names[k] not in matched]
    for n in names:
        k = prog.by_name.get(n)
        if k is None:
            print(f"{n}: no such routine")
            continue
        print(f"{n} (${k[0]:02X}:${k[1]:04X}): {describe(an.contracts[k])}")
        if args.why:
            for r, at in sorted(an.contracts[k].where.items()):
                print(f"    {r}: at ${at:04X}: {prog.op((k[0] if at >= 0x4000 else 0, at)).text}")
            for r, at in sorted(an.contracts[k].why.items()):
                print(f"    {r} at ${at:04X}: {prog.op((k[0] if at >= 0x4000 else 0, at)).text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
