#!/usr/bin/env python3
"""Translate matching C's compiled assembly into RGBDS source.

The ROM build places matched C as INCBINs of bytes linked at the original
addresses, which pins every function in place: a fork that changes code has
to let the code move. Assembled by RGBDS from source instead, the C's calls,
jumps and data references become labels the linker resolves wherever things
end up.

SDCC 2.2.1's output (after lsdj.peep and the tracking passes, i.e. the text
as-gbz80 assembles in tools/match) is asxxxx syntax; the translation is
mechanical: `(hl)` -> `[hl]`, `lda hl,N(sp)` -> `ld hl, sp+N`, `lda sp,N(sp)`
-> `add sp, N`, `#0x12` -> `$12`, `#<_x`/`#>_x` -> `LOW(x)`/`HIGH(x)`,
`_name` -> `name` (SDCC runtime names through match_function's aliases),
`00105$` -> `.L00105`, the unit's `__str_N` -> a global per module, `.db` /
`.ascii` -> `db`. A function's text runs from its start label through the
literals and const data SDCC places after it, up to the next function.

--check translates every matched function (or those of the given C files),
assembles each with rgbasm/rgblink at its ROM address -- every other symbol
an EQU from the symbol maps -- and compares the code and the bytes after it
with the ROM.

Usage: tools/match/to_rgbds.py --check [FILE.c ...]
       tools/match/to_rgbds.py FILE.c FUNC        # print the translation
"""

from __future__ import annotations

import argparse
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
va = importlib.import_module("verify_all")
mf = va.mf

START = re.compile(r"^___(\w+?)_start:")
REGS = {"a", "b", "c", "d", "e", "h", "l", "af", "bc", "de", "hl", "sp",
        "z", "nz", "nc"}          # (`c` the register or the condition)
KEYWORDS = {"LOW", "HIGH"}


def split_functions(asm: str) -> dict[str, list[str]]:
    """Function name -> the unit's lines from its start label up to the next
    function (its code, then the literals and const data placed after it)."""
    out: dict[str, list[str]] = {}
    cur: list[str] | None = None
    for line in asm.splitlines():
        m = START.match(line)
        if m:
            cur = out.setdefault(m.group(1), [])
            continue
        s = line.strip()
        if s.startswith(".area") and "_CODE" not in s:
            cur = None
            continue
        if cur is not None:
            cur.append(line)
    return out


def module_equates(asm: str) -> dict[str, str]:
    """Symbols the unit defines by value (`_X = 0xff90`: an `sfr at` / `at`
    declaration), as RGBDS names -> RGBDS expressions."""
    return {_symbol(m.group(1), ""): _expr(m.group(2), "")
            for m in re.finditer(r"^\s*(_\w+)\s*=\s*(\S+)", asm, re.M)}


def _symbol(name: str, module: str) -> str:
    """An asxxxx symbol as the RGBDS build names it."""
    m = re.fullmatch(r"__str_(\d+)", name)
    if m:
        return f"{module}_str_{m.group(1)}"
    if name.startswith("_"):
        plain = name[1:]
        return mf.RUNTIME_ALIASES.get(plain) or mf.RUNTIME_ALIASES.get(name) or plain
    return name


def _expr(e: str, module: str) -> str:
    """An operand expression (no addressing brackets)."""
    e = e.strip()
    if e.startswith("#"):
        e = e[1:]
    m = re.fullmatch(r"([<>])\(?(.*?)\)?", e)
    if m and e[0] in "<>":
        return f"{'LOW' if m.group(1) == '<' else 'HIGH'}({_expr(m.group(2), module)})"
    e = re.sub(r"\b(\d{5})\$", r".L\1", e)
    e = re.sub(r"0x([0-9A-Fa-f]+)", r"$\1", e)
    e = re.sub(r"(?<![\w.$])(_\w+)", lambda m: _symbol(m.group(1), module), e)
    return e


def _operand(op: str, module: str) -> str:
    op = op.strip()
    m = re.fullmatch(r"\((hl[+-]?|de|bc|c)\)", op)
    if m:
        return f"[{m.group(1)}]"
    m = re.fullmatch(r"\((.+)\)", op)
    if m:                                   # ldh (_X+0)
        return f"[{_expr(m.group(1), module)}]"
    if op in REGS:
        return op
    return _expr(op, module)


def _strip_comment(line: str) -> str:
    out, q = [], False
    for ch in line:
        if ch == '"':
            q = not q
        if ch == ";" and not q:
            break
        out.append(ch)
    return "".join(out).rstrip()


def translate(lines: list[str], module: str) -> list[str]:
    out: list[str] = []
    for raw in lines:
        line = _strip_comment(raw)
        s = line.strip()
        if not s:
            continue
        if s.endswith(":"):
            name = s[:-1]
            if re.fullmatch(r"___\w+_end", name):
                continue
            m = re.fullmatch(r"(\d{5})\$", name)
            out.append(f".L{m.group(1)}:" if m else f"{_symbol(name, module)}:")
            continue
        if s.startswith((".globl", ".module", ".area")):
            continue
        m = re.fullmatch(r"(_\w+)\s*=\s*(\S+)", s)
        if m:
            out.append(f"DEF {_symbol(m.group(1), module)} EQU {_expr(m.group(2), module)}")
            continue
        if s.startswith(".ascii"):
            am = re.fullmatch(r'\.ascii\s+"(.*)"', s)
            if not am:
                raise ValueError(f"unexpected .ascii: {s}")
            text = am.group(1)
            out.append("\tdb " + ", ".join(f"${ord(ch):02X}" for ch in text))
            continue
        if s.startswith((".db", ".dw")):
            kind, rest = s.split(None, 1)
            out.append(f"\t{kind[1:]} " + ", ".join(_expr(x, module) for x in rest.split(",")))
            continue
        parts = s.split(None, 1)
        mn = parts[0].lower()
        ops = [o for o in parts[1].split(",")] if len(parts) > 1 else []
        if mn == "lda":
            sm = re.fullmatch(r"(-?\d+)\(sp\)", ops[1].strip())
            if not sm:
                raise ValueError(f"unexpected lda operand: {s}")
            n = int(sm.group(1))
            if ops[0].strip() == "sp":
                out.append(f"\tadd sp, {n}")
            else:
                out.append(f"\tld hl, sp{'+' if n >= 0 else '-'}{abs(n)}")
            continue
        if mn == "jp" and ops and ops[0].strip() == "(hl)":
            out.append("\tjp hl")
            continue
        conv = [_operand(o, module) for o in ops]
        out.append(f"\t{mn} " + ", ".join(conv) if conv else f"\t{mn}")
    return out


def references(text: list[str], defined: set[str]) -> set[str]:
    refs: set[str] = set()
    for line in text:
        s = line.strip()
        if s.endswith(":") or s.startswith("DEF "):
            continue
        body = s.split(None, 1)[1] if " " in s else ""
        for tok in re.findall(r"(?<![$.\w])([A-Za-z_]\w*)", body):
            if tok not in REGS and tok not in KEYWORDS and tok not in defined:
                refs.add(tok)
    return refs


def check(files: list[Path]) -> int:
    rgbasm = shutil.which("rgbasm")
    rgblink = shutil.which("rgblink")
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
            module = "C_" + "_".join(Path(src).with_suffix("").parts[1:])
            funcs = split_functions(asm)
            equates = module_equates(asm)
            for name in sorted(names):
                m = manifest[name]
                bank, addr = int(m["bank"], 16), int(m["addr"], 16)
                _n, code, tail = runner.link(d, asm, lst, name, bank, addr, syms)
                if name not in funcs:
                    print(f"  {name}: not found in the unit")
                    bad += 1
                    continue
                text = translate(funcs[name], module)
                defined = {t.strip()[:-1] for t in text if t.strip().endswith(":")}
                defined |= {t.split()[1] for t in text if t.startswith("DEF ")}
                defs, missing = [], []
                for ref in sorted(references(text, defined)):
                    if ref in equates:
                        defs.append(f"DEF {ref} EQU {equates[ref]}")
                    elif ref in syms:
                        defs.append(f"DEF {ref} EQU ${syms[ref][1]:04X}")
                    else:
                        missing.append(ref)
                if missing:
                    print(f"  {name}: unresolved {', '.join(missing)}")
                    bad += 1
                    continue
                sect = (f'SECTION "c", ROM0[${addr:04X}]' if bank == 0 else
                        f'SECTION "c", ROMX[${addr:04X}], BANK[${bank:02X}]')
                src_text = "\n".join(defs + [sect] + text) + "\n"
                f = work / f"{name}.asm"
                f.write_text(src_text)
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
                if got != want_bytes or rom[off:off + len(code)] != code:
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
    c_path, func = Path(a.args[0]).resolve(), a.args[1]
    with ExitStack() as st:
        runner = va.Runner(st, mf.DEFAULT_PEEP)
        _d, asm, _lst = runner.compile(c_path)
    module = "C_" + "_".join(c_path.relative_to(ROOT).with_suffix("").parts[1:])
    funcs = split_functions(asm)
    if func not in funcs:
        sys.exit(f"error: {func} not in {c_path.name}")
    print("\n".join(translate(funcs[func], module)))


if __name__ == "__main__":
    main()
