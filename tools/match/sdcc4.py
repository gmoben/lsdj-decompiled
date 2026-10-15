#!/usr/bin/env python3
"""A fork's new C, compiled with a modern SDCC (4.6) and translated to RGBDS.

SDCC 2.2.1, the period compiler, is what LSDj's own C has to be built with
(it matches the ROM), but on new code it drops an operand now and then: its
gbz80 port is young. A fork's new functions are better compiled with a
current SDCC, in the calling convention LSDj's C uses (`--sdcccall 0`:
arguments on the stack, a byte returned in e, a word in de). The one
difference: SDCC 4 doesn't keep bc for its caller, and LSDj's compiled C
keeps values in bc across calls, so LSDj code calls new code through
something that saves bc (a fork's trampolines, docs/forking.md).

A file that says `@sdcc4` (in a comment) is compiled this way: every
function in it is new, put before its bank's free space (c/bankNN/ names the
bank). It includes c/include/fork.h, not lsdj.h (SDCC 2.2.1's dialect). It
can't have variables of its own (RAM goes in constants/ram.inc; declare it
extern), nor need SDCC 4's runtime library (32-bit or general multiply and
divide): the translation refuses either.

Get the compiler with tools/toolchain/fetch_sdcc4.sh (toolchain/sdcc-4.6.0).

    tools/match/sdcc4.py FILE.c        # print the RGBDS rows
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDCC = ROOT / "toolchain" / "sdcc-4.6.0" / "bin" / "sdcc"
MARK = re.compile(r"@sdcc4\b")
FLAGS = ["-msm83", "--sdcccall", "0", "--opt-code-size", "--fomit-frame-pointer",
         "-I", str(ROOT / "c" / "include"), "-DLSDJ_SDCC4"]

Row = tuple[int, str, str]


def is_sdcc4(source: str) -> bool:
    return bool(MARK.search(source))


def compile_file(path: Path) -> str:
    """The assembly SDCC 4 writes for a C file."""
    if not SDCC.is_file():
        raise SystemExit(f"no {SDCC.relative_to(ROOT)}: tools/toolchain/fetch_sdcc4.sh")
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / f"{path.stem}.asm"
        r = subprocess.run([str(SDCC), *FLAGS, "-S", str(path), "-o", str(out)],
                           capture_output=True, text=True)
        errors = [l for l in r.stderr.splitlines() if "warning 296" not in l and "default stdlib" not in l]
        if r.returncode or not out.is_file():
            raise SystemExit(f"{path}: sdcc failed:\n" + "\n".join(errors))
        return out.read_text()


def _number(text: str) -> str:
    v = int(text, 16) if text.lower().startswith("0x") else int(text)
    return f"${v:02X}" if 0 <= v <= 0xFF else (f"${v:04X}" if 0 <= v <= 0xFFFF else str(v))


class Translation:
    def __init__(self, asm: str, module: str, known: set[str]):
        self.asm, self.module, self.known = asm, module, known
        self.globls = set(re.findall(r"^\s*\.globl\s+_(\w+)", asm, re.M))
        self.statics: dict[str, str] = {}
        self.scope = ""

    def name(self, sym: str) -> str:
        """An SDCC symbol (`_x`) as the RGBDS build knows it."""
        if re.fullmatch(r"\d{5}\$", sym):
            return f".L{sym[:-1]}"
        if not sym.startswith("_"):
            raise ValueError(f"unexpected symbol {sym}")
        plain = sym[1:]
        if plain in self.statics:
            return self.statics[plain]
        if plain.startswith("_"):
            raise ValueError(f"{self.module}: needs SDCC 4's runtime ({sym}): not available in a fork")
        if plain not in self.known and plain not in self.globls:
            raise ValueError(f"{self.module}: no {plain} in the build (constants/ram.inc, src/*.sym, "
                             f"include/hardware.inc, or a new function)")
        return plain

    def expr(self, e: str) -> str:
        e = e.strip()
        if e.startswith("#"):
            e = e[1:].strip()
        m = re.fullmatch(r"([<>])\((.*)\)", e)
        if m:
            return f"{'LOW' if m.group(1) == '<' else 'HIGH'}({self.expr(m.group(2))})"
        e = re.sub(r"\b(\d{5}\$)", lambda m: self.name(m.group(1)), e)
        e = re.sub(r"(?<![\w$.])(_\w+)", lambda m: self.name(m.group(1)), e)
        e = re.sub(r"(?<![\w$.])(0[xX][0-9A-Fa-f]+|-?\d+)\b", lambda m: _number(m.group(1)), e)
        return re.sub(r"\s*\+\s*\$00\b", "", e)            # SDCC's `#_x + 0`: just x

    def operand(self, op: str) -> str:
        op = op.strip()
        m = re.fullmatch(r"\((hl[+-]?|de|bc|c)\)", op)
        if m:
            return f"[{m.group(1)}]"
        m = re.fullmatch(r"\((.+)\)", op)
        if m:
            return f"[{self.expr(m.group(1))}]"
        if op.lower() in {"a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp",
                          "z", "nz", "nc"}:
            return op.lower()
        return self.expr(op)

    def instruction(self, mn: str, ops: list[str]) -> str:
        if mn == "ldhl":                                  # ldhl sp, #n
            n = int(ops[1].strip().lstrip("#"), 0)
            return f"ld hl, sp{'+' if n >= 0 else '-'}{abs(n)}"
        if mn == "add" and ops and ops[0].strip() == "sp":
            n = int(ops[1].strip().lstrip("#"), 0)
            return f"add sp, {n}"
        if mn == "jp" and ops and ops[0].strip() == "(hl)":
            return "jp hl"
        conv = [self.operand(o) for o in ops]
        return f"{mn} " + ", ".join(conv) if conv else mn

    def rows(self) -> list[Row]:
        # static (module) labels get a unique name; collect them first
        for m in re.finditer(r"^(_\w+):(?!:)", self.asm, re.M):
            plain = m.group(1)[1:]
            if plain not in self.globls:
                self.statics[plain] = f"{self.module}_{plain}"
        out: list[Row] = []
        area = None
        for raw in self.asm.splitlines():
            line = raw.split(";", 1)[0].rstrip()
            if not line.strip():
                continue
            s = line.strip()
            m = re.match(r"\.area\s+(\w+)", s)
            if m:
                area = m.group(1)
                continue
            if s.startswith((".module", ".optsdcc", ".globl")):
                continue
            if area not in ("_CODE", "_HOME"):
                if s.startswith(".ds") or s.startswith((".db", ".dw")) or not s.startswith("."):
                    raise ValueError(f"{self.module}: {area}: `{s}`: no variables or initializers in "
                                     "new code (RAM goes in constants/ram.inc)")
                continue
            m = re.fullmatch(r"(\w+\$?)::?", s)
            if m:
                label = m.group(1)
                if re.fullmatch(r"\d{5}\$", label):
                    out.append((0, "L", f"{self.name(label)}:"))
                else:
                    name = self.name(label)
                    self.scope = name
                    out.append((0, "L", f"{name}:"))
                continue
            m = re.match(r"\.(db|dw|ascii)\s+(.*)", s)
            if m:
                kind, args = m.group(1), m.group(2)
                if kind == "ascii":
                    out.append((0, "I", f"db {args}"))
                else:
                    items = [self.expr(a) for a in re.split(r",\s*", args)]
                    out.append((0, "I", f"{kind} " + ", ".join(items)))
                continue
            if s.startswith("."):
                raise ValueError(f"{self.module}: unexpected directive `{s}`")
            parts = s.split(None, 1)
            mn = parts[0].lower()
            ops = [o for o in re.split(r",\s*", parts[1])] if len(parts) > 1 else []
            out.append((0, "I", self.instruction(mn, ops)))
        return out


def known_names() -> set[str]:
    """Names the RGBDS build defines: hardware.inc, ram.inc, every .sym label."""
    names: set[str] = set()
    for inc in (ROOT / "include" / "hardware.inc", ROOT / "constants" / "ram.inc"):
        names |= set(re.findall(r"^\s*DEF\s+(\w+)\s+EQU\b", inc.read_text(errors="replace"), re.M | re.I))
    for p in (ROOT / "src").glob("bank*.sym"):
        names |= set(re.findall(r"^[0-9A-Fa-f]{4}\s+([A-Za-z_]\w*)", p.read_text(), re.M))
    return names


def functions(asm: str) -> list[str]:
    return re.findall(r"^_(\w+)::", asm, re.M)


def translate(path: Path, extra: set[str] = frozenset()) -> tuple[list[str], dict[str, object]]:
    """(the functions a file defines, its .lines document: rows to append)."""
    asm = compile_file(path)
    t = Translation(asm, path.stem, known_names() | set(extra))
    rows = t.rows()
    return functions(asm), {"append": True, "rows": rows}


def main() -> int:
    for arg in sys.argv[1:]:
        funcs, doc = translate(Path(arg))
        print(f"; {arg}: {', '.join(funcs)}")
        for _a, kind, text in doc["rows"]:
            print(text if kind == "L" else f"\t{text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
