#!/usr/bin/env python3
"""Translate matching C's compiled assembly into RGBDS source.

The ROM build used to place matched C as INCBINs of bytes linked at the
original addresses, which pins every function in place; a fork that changes
code has to let the code move. Assembled by RGBDS from source instead, the
C's calls, jumps and data references are labels the linker resolves wherever
things end up.

SDCC 2.2.1's output (after lsdj.peep and the tracking passes, i.e. the text
as-gbz80 assembles in tools/match) is asxxxx syntax; the translation is
mechanical: `(hl)` -> `[hl]`, `lda hl,N(sp)` -> `ld hl, sp+N`, `lda sp,N(sp)`
-> `add sp, N`, `#0x12` -> `$12`, `#<_x`/`#>_x` -> `LOW(x)`/`HIGH(x)`,
`_name` -> `name` (SDCC runtime names through match_function's aliases, GBDK
register names to include/hardware.inc's), `.db`/`.ascii` -> `db`. A
function's text runs from its start label through the literals and const
data SDCC places after it, up to the next function.

Labels: the carve keeps printing every src/bankNN.sym label (so every tool
reading the generated .asm still sees them), and the C's jumps and literals
refer to those by name: an SDCC label (`00105$`) becomes the .sym label at
its address (`.notSong`), as a local reference where the jump is in the same
scope and `Parent.local` elsewhere. Labels nothing refers to (SDCC's
fall-through blocks) are dropped. The translation defines only names the
.sym lacks: a switch's jump table (`.screenCases`, after what it switches
on, else `.jumpTable`) and a string literal (`.strSaveSong`, after its
text). Each row carries its ROM address (from as-gbz80's listing), which is
how the carve interleaves the two.

verify_all.py --write saves each matched function's rows as
build/c/bankNN/Name.lines (JSON); tools/carve_clean.py renders them in place.

--check builds every matched function (or those of the given C files) the
same way -- rows plus the .sym labels -- assembles each with rgbasm/rgblink at
its ROM address, every other symbol an EQU, and compares the code and the
bytes after it with the ROM.

Usage: tools/match/to_rgbds.py --check [FILE.c ...]
       tools/match/to_rgbds.py FILE.c FUNC        # print the rendered rows
"""

from __future__ import annotations

import argparse
import functools
import importlib
import json
import re
import shutil
import subprocess
import sys
from contextlib import ExitStack
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools"))
mf = importlib.import_module("match_function")

START = re.compile(r"^___(\w+?)_start:")
REGS = {"a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp",
        "z", "nz", "nc"}          # (`c` the register or the condition)
KEYWORDS = {"LOW", "HIGH"}

Row = tuple[int, str, str]        # (ROM address, kind, text); kind I/L/V


# --- the unit ----------------------------------------------------------------

def split_functions(asm: str) -> dict[str, tuple[int, list[tuple[int, str]]]]:
    """Function name -> (line number of its start label, [(line number, line)])
    from the start label up to the next function: its code, then the
    literals and const data placed after it. Line numbers are 1-based, as in
    as-gbz80's listing."""
    out: dict[str, tuple[int, list[tuple[int, str]]]] = {}
    cur: list[tuple[int, str]] | None = None
    for n, line in enumerate(asm.splitlines(), 1):
        m = START.match(line)
        if m:
            cur = []
            out[m.group(1)] = (n, cur)
            continue
        s = line.strip()
        if s.startswith(".area") and "_CODE" not in s:
            cur = None
            continue
        if cur is not None:
            cur.append((n, line))
    return out


def listing_addrs(lst: str) -> dict[int, int]:
    """unit.asm line number -> area-relative address, from as-gbz80's listing
    (fixed columns: address at 3-6, up to six bytes ending at column 24, the
    line number in 25-30)."""
    out: dict[int, int] = {}
    for line in lst.splitlines():
        if len(line) < 31:
            continue
        a, n = line[3:7], line[25:31].strip()
        if n.isdigit() and re.fullmatch(r"[0-9A-F]{4}", a):
            out.setdefault(int(n), int(a, 16))
    return out


def module_equates(asm: str) -> dict[str, str]:
    """Symbols the unit defines by value (`_X = 0xff90`: an `sfr at` / `at`
    declaration): RGBDS name -> RGBDS expression."""
    return {m.group(1)[1:]: re.sub(r"0x([0-9A-Fa-f]+)", r"$\1", m.group(2))
            for m in re.finditer(r"^\s*(_\w+)\s*=\s*(\S+)", asm, re.M)}


@functools.lru_cache(maxsize=None)
def rgbds_names() -> tuple[frozenset[str], dict[int, str]]:
    """(names the RGBDS build defines: hardware.inc, ram.inc, every .sym
    label; hardware.inc's first name for each register address)."""
    names: set[str] = set()
    hw: dict[int, str] = {}
    for inc in (ROOT / "include" / "hardware.inc", ROOT / "constants" / "ram.inc"):
        for m in re.finditer(r"^\s*DEF\s+(\w+)\s+EQU\s+(\S+)", inc.read_text(errors="replace"), re.M | re.I):
            names.add(m.group(1))
            v = m.group(2)
            if inc.name == "hardware.inc" and v.startswith("$"):
                hw.setdefault(int(v[1:], 16), m.group(1))
    for p in (ROOT / "src").glob("bank*.sym"):
        for line in p.read_text().splitlines():
            m = re.match(r"^\s*[0-9A-Fa-f]{4}\s+([A-Za-z_]\w*)", line)
            if m and "@deploy-only" not in line:
                names.add(m.group(1))
    return frozenset(names), hw


@functools.lru_cache(maxsize=None)
def bank_labels(bank: int) -> dict[int, str]:
    """The labels the carve prints in a bank: src/bankNN.sym as
    tools/disasm.py parses it (one name per address, locals start with `.`)."""
    disasm = importlib.import_module("disasm")
    return disasm.parse_symbols(ROOT / "src" / f"bank{bank:02x}.sym")


def qualified(labels: dict[int, str], addr: int) -> str:
    """The .sym label at `addr` as a reference: a local one with its parent."""
    name = labels[addr]
    if not name.startswith("."):
        return name
    parent = max((a for a, n in labels.items() if a <= addr and not n.startswith(".")), default=None)
    return f"{labels[parent]}{name}" if parent is not None else name


def source_addresses(source: str, labels: dict[int, str]) -> dict[int, str]:
    """ROM addresses the C names with a macro whose value is the number the
    original source evidently used (`#define TxtHelpPan ((char *)0x6E0C)`:
    stored a byte at a time, which an address of a symbol never is) -- those
    whose name is the .sym label at that address."""
    out: dict[int, str] = {}
    for m in re.finditer(r"^\s*#define\s+(\w+)\s+\(\(\s*(?:const\s+)?\w+\s*\*\s*\)\s*"
                         r"0x([0-9A-Fa-f]{4})\s*\)", source, re.M):
        name, addr = m.group(1), int(m.group(2), 16)
        if labels.get(addr) == name:
            out[addr] = name
    return out


def is_ram(v: int) -> bool:
    return 0xC000 <= v <= 0xDFFF or 0xFF80 <= v <= 0xFFFE


def is_compare16(after: list[str]) -> bool:
    """Whether `ld hl, $N` followed by `after` is SDCC's 16-bit compare
    (gb_cmp16's `ld hl,#-N; add hl,rr`, its carry tested next), not an
    address plus an index."""
    return (len(after) == 2 and bool(re.fullmatch(r"add hl, (?:bc|de)", after[0]))
            and bool(re.match(r"(?:jp|jr) n?c,|ld a, \$00$", after[1])))


def _farcall_bank(texts: list[str], rr: str) -> int | None:
    """The bank of a banked call whose target `rr` holds (`FarCall(5, 0x747D)`):
    `push rr`, then `ld a, BANK / push af / inc sp`, then a FarCall* call."""
    if f"push {rr}" not in texts:
        return None
    j = texts.index(f"push {rr}")
    m = re.fullmatch(r"ld a, \$([0-9A-Fa-f]{2})", texts[j + 1]) if j + 1 < len(texts) else None
    if not m or texts[j + 2:j + 4] != ["push af", "inc sp"]:
        return None
    call = next((t for t in texts[j + 4:j + 8] if t.startswith("call ")), "")
    disasm = importlib.import_module("disasm")
    return int(m.group(1), 16) if call[5:].strip() in disasm.FARCALL_NAMES else None


def _strip_comment(line: str) -> str:
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            break
        out.append(ch)
    return "".join(out).rstrip()


# --- translation ----------------------------------------------------------------

class Translator:
    """One function's rows: the code and the data after it, by ROM address."""

    def __init__(self, asm: str, lst: str, func: str, func_addr: int, size: int,
                 labels: dict[int, str], syms: dict[str, tuple[int | None, int]],
                 source: str = ""):
        self.func, self.labels, self.syms = func, labels, syms
        self.named = source_addresses(source, labels)
        self.equates = module_equates(asm)
        start_line, self.lines = split_functions(asm)[func]
        self.addrs = listing_addrs(lst)
        self.base = func_addr - self.addrs[start_line]
        self.lo, self.hi = func_addr, func_addr + size
        self.rename: dict[str, str] = {}      # SDCC data label -> its placeholder
        self.targets: dict[str, int] = {}     # placeholder key -> the label's ROM address
        self.texts: dict[str, str] = {}       # a literal's key -> its text (for its name)
        self.guards: dict[str, str] = {}      # name -> value, for `IF !DEF`
        self.globals = sorted((a, n) for a, n in labels.items() if not n.startswith("."))
        key = None
        for n, raw in self.lines:
            s = _strip_comment(raw).strip()
            am = re.fullmatch(r'\.ascii\s+"(.*)"', s)
            if am and key is not None:
                self.texts[key] = self.texts.get(key, "") + am.group(1)
            if not s.endswith(":"):
                continue
            name = s[:-1]
            key = None
            if name.startswith("___") or name == f"_{func}":
                continue
            m = re.fullmatch(r"(\d{5})\$", name)
            if n not in self.addrs:
                if m:
                    continue              # checked in rows() if something jumps there
                raise ValueError(f"{func}: no address for label {name}")
            key = m.group(1) if m else name
            self.targets[key] = self.base + self.addrs[n]
            if not m:
                self.rename[name] = f"\x00{key}\x00"

    # operands
    def symbol(self, name: str) -> str:
        if name in self.rename:
            return self.rename[name]
        plain = name[1:] if name.startswith("_") else name
        plain = mf.RUNTIME_ALIASES.get(plain) or mf.RUNTIME_ALIASES.get(name) or plain
        names, hw = rgbds_names()
        if plain in names:
            return plain
        if plain in self.equates:
            self.guards[plain] = self.equates[plain]
            return plain
        if plain in self.syms:
            bank, val = self.syms[plain]
            if bank is None and val in hw:            # a GBDK register name
                return hw[val]
            self.guards[plain] = f"${val:04X}"
            return plain
        raise ValueError(f"{self.func}: no value for symbol {plain}")

    def expr(self, e: str) -> str:
        e = e.strip()
        if e.startswith("#"):
            e = e[1:]
        m = re.fullmatch(r"([<>])\(?(.*?)\)?", e)
        if m and e[0] in "<>":
            return f"{'LOW' if m.group(1) == '<' else 'HIGH'}({self.expr(m.group(2))})"
        e = re.sub(r"\b(\d{5})\$", lambda m: f"\x00{m.group(1)}\x00", e)
        e = re.sub(r"0x([0-9A-Fa-f]+)", r"$\1", e)
        e = re.sub(r"(?<![\w.$])(_\w+)", lambda m: self.symbol(m.group(1)), e)
        e = re.sub(r"\s*\+\s*0(?=\s*\+)", "", e)    # SDCC's `#_x + 0 + 1`: `x + 1`
        return re.sub(r"\s*\+\s*0$", "", e)        # SDCC's `#_x + 0`: just `x`

    def operand(self, op: str) -> str:
        op = op.strip()
        m = re.fullmatch(r"\((hl[+-]?|de|bc|c)\)", op)
        if m:
            return f"[{m.group(1)}]"
        m = re.fullmatch(r"\((.+)\)", op)
        if m:                                   # ldh (_X+0)
            return f"[{self.expr(m.group(1))}]"
        if op in REGS:
            return op
        return self.expr(op)

    def data_rows(self, addr: int, kind: str, items: list[str]) -> list[Row]:
        """`db`/`dw` rows, split wherever the carve prints a label inside."""
        width = 2 if kind == "dw" else 1
        rows: list[Row] = []
        cur: list[str] = []
        cur_addr = addr
        for i, it in enumerate(items):
            a = addr + i * width
            if cur and a in self.labels:
                rows.append((cur_addr, "I", f"{kind} " + ", ".join(cur)))
                cur, cur_addr = [], a
            cur.append(it)
        if cur:
            rows.append((cur_addr, "I", f"{kind} " + ", ".join(cur)))
        return rows

    def text_rows(self, addr: int, text: str) -> list[Row]:
        """An `.ascii` string as `db "..."` rows (RGBDS escapes), split
        wherever the carve prints a label inside."""
        rows: list[Row] = []
        cur: list[str] = []
        cur_addr = addr
        for i, ch in enumerate(text):
            if cur and addr + i in self.labels:
                rows.append((cur_addr, "I", 'db "' + "".join(cur) + '"'))
                cur, cur_addr = [], addr + i
            cur.append("\\" + ch if ch in '"\\{}' else ch)
        if cur:
            rows.append((cur_addr, "I", 'db "' + "".join(cur) + '"'))
        return rows

    def rom_operands(self, rows: list[Row]) -> list[Row]:
        """ROM addresses the C spells as numbers, as labels (a build where
        code moves needs them): a banked call's target and the addresses
        source_addresses() found -- `ld rr` operands, and the two bytes the
        compiler stores one at a time (`ld [hl], $25 / inc hl / ld [hl], $6D`)."""
        disasm = importlib.import_module("disasm")
        idx = [k for k, r in enumerate(rows) if r[1] == "I"]
        texts = [rows[k][2] for k in idx]
        out = list(rows)
        for n, k in enumerate(idx):
            m = re.fullmatch(r"ld (bc|de|hl), \$([0-9A-Fa-f]{4})", texts[n])
            if m:
                rr, v = m.group(1), int(m.group(2), 16)
                if rr == "hl" and is_compare16(texts[n + 1:n + 3]):
                    # SDCC's 16-bit compare: hl = -N, so `add hl, rr` carries when rr >= N
                    out[k] = (rows[k][0], "I", f"ld hl, -${0x10000 - v:X}")
                    continue
                name = self.named.get(v)
                if name is None:
                    bank = _farcall_bank(texts[n + 1:n + 14], rr)
                    if bank is not None:
                        name = disasm.name_in_bank(bank_labels(0 if v < 0x4000 else bank), v)
                if name is None and is_ram(v):        # a C literal pointer into named RAM
                    name = disasm.RAM_SYMS.get(v) or disasm.RAM_OFFSETS.get(v)
                if name:
                    out[k] = (rows[k][0], "I", f"ld {rr}, {name}")
                continue
            m = re.fullmatch(r"(ld \[)\$([0-9A-Fa-f]{4})(\], a)|(ld a, \[)\$([0-9A-Fa-f]{4})(\])", texts[n])
            if m:
                pre, v, post = (m.group(1), m.group(2), m.group(3)) if m.group(1) else (m.group(4), m.group(5), m.group(6))
                v = int(v, 16)
                name = (disasm.RAM_SYMS.get(v) or disasm.RAM_OFFSETS.get(v)) if is_ram(v) else None
                if name:
                    out[k] = (rows[k][0], "I", f"{pre}{name}{post}")
                continue
            m = re.fullmatch(r"ld \[hl\], \$([0-9A-Fa-f]{2})", texts[n])
            if m and texts[n + 1:n + 2] == ["inc hl"] and n + 2 < len(texts):
                m2 = re.fullmatch(r"ld \[hl\], \$([0-9A-Fa-f]{2})", texts[n + 2])
                name = self.named.get(int(m.group(1), 16) | int(m2.group(1), 16) << 8) if m2 else None
                if name:
                    out[k] = (rows[k][0], "I", f"ld [hl], LOW({name})")
                    k2 = idx[n + 2]
                    out[k2] = (rows[k2][0], "I", f"ld [hl], HIGH({name})")
        return out

    # labels
    def scope_of(self, addr: int) -> str:
        """The global label a local one at `addr` belongs to (the last one the
        carve prints at or before it)."""
        names = [n for a, n in self.globals if a <= addr]
        return names[-1] if names else self.func

    def propose(self, key: str, addr: int, rows: list[Row]) -> str:
        """A name for a label the .sym lacks: a string literal after its text,
        a switch's jump table after what it switches on."""
        if key in self.texts:
            words = re.findall(r"[A-Za-z0-9]+", self.texts[key])
            return ".str" + ("".join(w.capitalize() for w in words)[:24] or "Blank")
        if not re.fullmatch(r"\d{5}", key):
            return "." + re.sub(r"^_+", "", key)
        ph = f"\x00{key}\x00"
        texts = [t for _a, kind, t in rows if kind == "I"]
        at = next((i for i, t in enumerate(texts) if t == f"ld hl, {ph}"), None)
        body = [t for a, kind, t in rows if kind == "I" and a >= addr][:2]
        if at is not None and body and all(t.startswith("jp ") for t in body):
            return self.switch_name(texts[max(0, at - 8):at])
        refs = [a for a, kind, t in rows if kind == "I" and ph in t]
        if any(a >= addr for a in refs):
            return ".loop"
        first = body[0] if body else ""
        if first.startswith(("add sp", "pop ", "ret")):
            return ".done"
        m = re.fullmatch(r"call (\w+)", first)
        return "." + m.group(1)[0].lower() + m.group(1)[1:] if m else ".cont"

    @staticmethod
    def switch_name(before: list[str]) -> str:
        """A jump table named after the variable genJumpTab indexes it by
        (`ld e,X; ld d,$00; ld hl,table`): `.currentScreenCases`, or
        `.jumpTable` for a local."""
        var = re.compile(r"(?:ldh a, \[|ld hl, )[wh]([A-Z]\w*)(?:\s*\+\s*0)?\]?")
        i = next((j for j in range(len(before) - 1, -1, -1) if before[j].startswith("ld e, ")), None)
        if i is None:
            return ".jumpTable"
        src = before[i][len("ld e, "):]
        want = "ld hl, " if src == "[hl]" else ("ld a, " if src == "a" else None)
        for t in reversed(before[:i]) if want else ():
            if src == "a" and t.startswith("ldh a, "):
                m = var.fullmatch(t)
                return "." + m.group(1)[0].lower() + m.group(1)[1:] + "Cases" if m else ".jumpTable"
            if t.startswith(want) or (src == "a" and re.match(r"\w+ a\b", t)):
                m = var.fullmatch(t)
                return "." + m.group(1)[0].lower() + m.group(1)[1:] + "Cases" if m else ".jumpTable"
        return ".jumpTable"

    def resolve_labels(self, rows: list[Row]) -> list[Row]:
        """Name each placeholder's label and write every reference to it from
        where it sits: `.local` in the label's own scope, `Parent.local`
        outside it, a global label by its name."""
        names: dict[str, str] = {}
        taken: dict[str, set[str]] = {}
        for a, n in self.labels.items():
            if n.startswith("."):
                taken.setdefault(self.scope_of(a), set()).add(n)
        used = {k for _a, _kind, t in rows for k in re.findall(r"\x00([^\x00]+)\x00(?!:)", t)}
        for k, a in sorted(self.targets.items(), key=lambda kv: kv[1]):
            if a in self.labels:
                names[k] = self.labels[a]
                continue
            if re.fullmatch(r"\d{5}", k) and k not in used:
                continue
            base = self.propose(k, a, rows)
            name, n = base, 1
            while name in taken.setdefault(self.scope_of(a), set()):
                n += 1
                name = f"{base}{n}"
            taken[self.scope_of(a)].add(name)
            names[k] = name
        missing = used - names.keys()
        if missing:
            raise ValueError(f"{self.func}: no address for label {sorted(missing)[0]}")

        def ref(k: str, at: int) -> str:
            name = names[k]
            if not name.startswith("."):
                return name
            scope = self.scope_of(self.targets[k])
            return name if self.scope_of(at) == scope else f"{scope}{name}"

        out: list[Row] = []
        for a, kind, t in rows:
            if kind == "L":
                k = t[1:-2]
                if k not in names or self.targets[k] in self.labels:
                    continue                  # nothing jumps here, or the carve prints the .sym's
                out.append((a, kind, f"{names[k]}:"))
            else:
                out.append((a, kind, re.sub(r"\x00([^\x00]+)\x00", lambda m: ref(m.group(1), a), t)))
        return out

    def rows(self) -> list[Row]:
        out: list[Row] = []
        text_end: tuple[int, int] | None = None   # (row, end address) of the last string
        for n, raw in self.lines:
            s = _strip_comment(raw).strip()
            if not s:
                continue
            a = self.base + self.addrs[n] if n in self.addrs else None
            if s.endswith(":"):
                name = s[:-1]
                m = re.fullmatch(r"(\d{5})\$", name)
                k = m.group(1) if m else name
                if k in self.targets and self.targets[k] not in self.labels:
                    out.append((self.targets[k], "L", f"\x00{k}\x00:"))
                continue              # the function's own and the .sym-named labels: the carve's
            if s.startswith((".globl", ".module", ".area")) or re.fullmatch(r"_\w+\s*=\s*\S+", s):
                continue
            if a is None:
                raise ValueError(f"{self.func}: no address for line {n}: {s}")
            if s.startswith(".ascii"):
                am = re.fullmatch(r'\.ascii\s+"(.*)"', s)
                if not am:
                    raise ValueError(f"unexpected .ascii: {s}")
                out += self.text_rows(a, am.group(1))
                text_end = (len(out) - 1, a + len(am.group(1)))
                continue
            if s.startswith((".db", ".dw")):
                kind, rest = s.split(None, 1)
                items = [self.expr(x) for x in rest.split(",")]
                # a string's NUL terminator joins its line: db "OK!", 0
                if (kind == ".db" and items == ["$00"] and text_end and text_end[1] == a
                        and a not in self.labels and text_end[0] == len(out) - 1):
                    k, row = text_end[0], out[text_end[0]]
                    out[k] = (row[0], row[1], row[2] + ", 0")
                    text_end = None
                    continue
                out += self.data_rows(a, kind[1:], items)
                continue
            parts = s.split(None, 1)
            mn = parts[0].lower()
            ops = parts[1].split(",") if len(parts) > 1 else []
            if mn == "lda":
                sm = re.fullmatch(r"(-?\d+)\(sp\)", ops[1].strip())
                if not sm:
                    raise ValueError(f"unexpected lda operand: {s}")
                k = int(sm.group(1))
                text = f"add sp, {k}" if ops[0].strip() == "sp" else f"ld hl, sp{'+' if k >= 0 else '-'}{abs(k)}"
            elif mn == "jp" and ops and ops[0].strip() == "(hl)":
                text = "jp hl"
            else:
                conv = [self.operand(o) for o in ops]
                text = f"{mn} " + ", ".join(conv) if conv else mn
            out.append((a, "I", text))
        out = self.rom_operands(out)
        out = self.resolve_labels(out)
        guards = [(self.lo, "V", f"IF !DEF({k})\nDEF {k} EQU {v}\nENDC")
                  for k, v in sorted(self.guards.items())]
        rows = guards + out
        # every label the carve prints inside must land on a row start
        starts = {a for a, kind, _t in rows if kind == "I"}
        for a in self.labels:
            if self.lo <= a < self.hi and a not in starts:
                raise ValueError(f"{self.func}: .sym label {self.labels[a]} at ${a:04X} "
                                 f"is not at an instruction or data row")
        return rows


def annotate(asm: str, lst: str, func: str, bank: int, func_addr: int, size: int,
             syms: dict[str, tuple[int | None, int]], source: str = "") -> dict[str, object]:
    """The rows verify_all.py saves as build/c/bankNN/Name.lines."""
    t = Translator(asm, lst, func, func_addr, size, bank_labels(bank), syms, source)
    return {"lo": func_addr, "hi": func_addr + size, "rows": t.rows()}


def render(rows: list[Row], labels: dict[int, str], lo: int, hi: int) -> list[str]:
    """Rows with the carve's labels, as tools/disasm.py prints them."""
    out: list[str] = []
    printed: set[int] = set()
    for a, kind, text in rows:
        if kind == "I" and a not in printed and a in labels and lo <= a < hi:
            out.append(f"{labels[a]}:")
            printed.add(a)
        out.append(text if kind in "LV" else f"\t{text}")
    return out


# --- check ------------------------------------------------------------------

def check(files: list[Path]) -> int:
    va = importlib.import_module("verify_all")
    rgbasm, rgblink = shutil.which("rgbasm"), shutil.which("rgblink")
    if not rgbasm or not rgblink:
        sys.exit("error: rgbasm/rgblink not on PATH")
    rom = (ROOT / "lsdj9_4_0.gb").read_bytes()
    manifest = json.loads((ROOT / "c" / "matched.json").read_text())
    syms = mf.load_symbols()
    want = {p.resolve() for p in files}
    work = ROOT / "build" / "to_rgbds"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    ok = bad = 0
    by_src: dict[str, list[str]] = {}
    for name, m in manifest.items():
        by_src.setdefault(m["src"], []).append(name)
    with ExitStack() as st:
        runner = va.Runner(st, mf.DEFAULT_PEEP)
        for src, names in sorted(by_src.items()):
            c_path = (ROOT / src).resolve()
            if want and c_path not in want:
                continue
            d, asm, lst = runner.compile(c_path)
            for name in sorted(names):
                m = manifest[name]
                bank, addr = int(m["bank"], 16), int(m["addr"], 16)
                _n, code, tail = runner.link(d, asm, lst, name, bank, addr, syms)
                try:
                    t = Translator(asm, lst, name, addr, len(code) + len(tail), bank_labels(bank),
                                   syms, c_path.read_text())
                    rows = t.rows()
                except (ValueError, KeyError) as e:
                    print(f"  {name}: {e}")
                    bad += 1
                    continue
                labels = bank_labels(bank)
                text = render(rows, labels, addr, addr + len(code) + len(tail))
                if f"{name}:" not in text:
                    text.insert(0, f"{name}:")
                body = "\n".join(text)
                # everything else the body names is an EQU of its address here
                defined = set(re.findall(r"^([A-Za-z_][\w.]*):", body, re.M))
                defined |= set(re.findall(r"^DEF (\w+)", body, re.M))
                defs = []
                for ref in sorted(set(re.findall(r"(?<![$.\w])([A-Za-z_]\w*)(?![\w.])", body))):
                    if ref in defined or ref in REGS or ref in KEYWORDS:
                        continue
                    if ref in syms:
                        defs.append(f"DEF {ref} EQU ${syms[ref][1]:04X}")
                sect = (f'SECTION "c", ROM0[${addr:04X}]' if bank == 0 else
                        f'SECTION "c", ROMX[${addr:04X}], BANK[${bank:02X}]')
                f = work / f"{name}.asm"
                f.write_text("\n".join(defs + [sect, body]) + "\n")
                r = subprocess.run([rgbasm, "-o", str(f.with_suffix(".o")), str(f)],
                                   capture_output=True, text=True)
                if r.returncode == 0:
                    r = subprocess.run([rgblink, "-o", str(f.with_suffix(".gb")),
                                        str(f.with_suffix(".o"))], capture_output=True, text=True)
                if r.returncode != 0:
                    err = r.stderr.strip().splitlines()
                    print(f"  {name}: {' | '.join(err[:3])}")
                    bad += 1
                    continue
                img = f.with_suffix(".gb").read_bytes()
                off = bank * 0x4000 + (addr - (0x4000 if bank else 0))
                want_bytes = code + tail
                got = img[off:off + len(want_bytes)]
                if got != want_bytes or rom[off:off + len(want_bytes)] != want_bytes:
                    k = next((i for i in range(len(want_bytes)) if i >= len(got) or got[i] != want_bytes[i]), 0)
                    print(f"  {name}: differs at +${k:X} (rgbds {got[k:k+6].hex(' ')}, "
                          f"sdcc {want_bytes[k:k+6].hex(' ')})")
                    bad += 1
                    continue
                ok += 1
    print(f"==> {ok} function(s) assemble byte-identically from RGBDS source; {bad} differ")
    return 1 if bad else 0


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    if a.check:
        sys.exit(check([Path(x) for x in a.args]))
    if len(a.args) != 2:
        ap.error("give FILE.c FUNC, or --check")
    va = importlib.import_module("verify_all")
    c_path, func = Path(a.args[0]).resolve(), a.args[1]
    manifest = json.loads((ROOT / "c" / "matched.json").read_text())
    if func not in manifest:
        sys.exit(f"error: {func} is not a matched function")
    m = manifest[func]
    bank, addr = int(m["bank"], 16), int(m["addr"], 16)
    syms = mf.load_symbols()
    with ExitStack() as st:
        runner = va.Runner(st, mf.DEFAULT_PEEP)
        d, asm, lst = runner.compile(c_path)
        _n, code, tail = runner.link(d, asm, lst, func, bank, addr, syms)
    ann = annotate(asm, lst, func, bank, addr, len(code) + len(tail), syms, c_path.read_text())
    print("\n".join(render(ann["rows"], bank_labels(bank), ann["lo"], ann["hi"])))


if __name__ == "__main__":
    main()
