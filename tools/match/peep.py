#!/usr/bin/env python3
"""Apply SDCC-style peephole rules to an asxxxx assembly file, our way.

The harness compiles with SDCC 2.2.1's stock rules and then applies
tools/match/lsdj.peep -- the rewrites LSDj's compiler made on top -- here.
SDCC's own matcher is unusable for them: a `%n` variable never spans a comma,
a source line is compared *with* its trailing comment (`ld a,(hl) ; 2`),
which no rule can name because `;` starts a comment in the rule file, and it
crashes (exit 5) on larger units once rules with a variable mnemonic
(`%1 a`) are loaded. LSDj's compiler evidently tracked what `a` (and `hl`)
held in its code generator, not in text rules; the rules reproduce that.

Rule syntax is SDCC's:

    replace [restart] {
        <line>...
    } by {
        <line>...
    }

    } [if labelRefCount %n K]

Lines are compared token by token (whitespace-separated); `%n` binds one
token, consistently within a rule; a `label:` line matches a label line.
Comment-only and blank lines in the source are skipped while matching.
Case-sensitive, like SDCC (so `LD a,c` stays out of reach of `ld a,%1`).
The optional condition (later SDCC's syntax) requires the label bound to
`%n` to be referenced exactly K times in the file outside its definition --
e.g. a label the trampoline collapse left behind, which no jump reaches.

A rule never leaves a jump dangling: if it drops a label that other lines
still reference, the match is refused -- unless the label was a trampoline
(`%n:` then `jp %m`) reached only by conditional jumps, which are then
retargeted to %m (a multi-byte `==` jumps through one trampoline per byte).

Usage: tools/match/peep.py RULES.peep FILE.asm      # rewrites FILE.asm in place
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# (pattern token-lines, replacement lines, restart, condition or None)
Rule = tuple[list[list[str]], list[str], bool, "tuple[str, int] | None"]


def parse_rules(text: str) -> list[Rule]:
    rules: list[Rule] = []
    for m in re.finditer(r"replace(\s+restart)?\s*\{(.*?)\}\s*by\s*\{(.*?)\}"
                         r"(?:[ \t]*if[ \t]+labelRefCount[ \t]+(%\d+)[ \t]+(\d+))?", text, re.S):
        pat = [l.strip() for l in m.group(2).splitlines() if l.strip() and not l.strip().startswith(";")]
        rep = [l.rstrip() for l in m.group(3).splitlines() if l.strip()]
        cond = (m.group(4), int(m.group(5))) if m.group(4) else None
        rules.append(([p.split() for p in pat], rep, bool(m.group(1)), cond))
    return rules


_LOCAL = re.compile(r"\d+\$")


def _scope_bounds(lines: list[str], i: int) -> tuple[int, int]:
    """The asxxxx local-label scope around line i: `nnnnn$` labels are local
    to the span between two ordinary labels (SDCC restarts the numbering in
    every function, so the same name recurs across a unit)."""
    def is_boundary(l: str) -> bool:
        t = _strip(l).strip()
        return t.endswith(":") and not _LOCAL.fullmatch(t.rstrip(":"))
    lo = i
    while lo > 0 and not is_boundary(lines[lo]):
        lo -= 1
    hi = i + 1
    while hi < len(lines) and not is_boundary(lines[hi]):
        hi += 1
    return lo, hi


def _label_refs(lines: list[str], label: str, at: int) -> int:
    """References to `label` outside its own definition line, within the
    scope it lives in (line `at` is inside that scope)."""
    lo, hi = _scope_bounds(lines, at) if _LOCAL.fullmatch(label) else (0, len(lines))
    pat = re.compile(rf"(?<![\w$]){re.escape(label)}(?![\w$])")
    return sum(len(pat.findall(_strip(l))) for l in lines[lo:hi]
               if not _strip(l).strip().startswith(label + ":"))


def _strip(line: str) -> str:
    return line.split(";", 1)[0].rstrip()


def _token_regex(p: str) -> str:
    """`hl,#%1` -> `hl,#(?P<v1>.+?)`: variables bind inside a token, up to the
    next literal character (a comma, colon, ...), as SDCC's do."""
    parts = re.split(r"(%\d+)", p)
    return "".join(f"(?P<v{x[1:]}>.+?)" if re.fullmatch(r"%\d+", x) else re.escape(x)
                   for x in parts)


def _match_line(pat: list[str], src: str, binds: dict[str, str]) -> bool:
    toks = src.split()
    if len(toks) != len(pat):
        return False
    trial = dict(binds)
    for p, t in zip(pat, toks):
        if "%" not in p:
            if p != t:
                return False
            continue
        m = re.fullmatch(_token_regex(p), t)
        if not m:
            return False
        for k, v in m.groupdict().items():
            key = "%" + k[1:]
            if trial.get(key, v) != v:
                return False
            trial[key] = v
    binds.clear()
    binds.update(trial)
    return True


def _keep_labels_reachable(pat: list[list[str]], rep: list[str], binds: dict[str, str],
                           lines: list[str], idx: list[int]) -> bool:
    """A rule may drop a label (`%n:` in the pattern, not in the replacement).
    References to it outside the matched lines must not dangle. When the
    label was a trampoline (`%n:` then `jp %m`) and every such reference is
    a conditional jump, they are retargeted to %m (as `jp cc`), as the ROM
    has it for a multi-byte `==` (one `jp nz,T` per byte, all through the
    same trampoline). Otherwise the match is refused. Mutates `lines` only
    when it returns True."""
    kept = {r.strip() for r in rep}
    edits: dict[int, str] = {}
    for k, p in enumerate(pat):
        if len(p) != 1 or not re.fullmatch(r"%\d+:", p[0]) or p[0] in kept:
            continue
        label = binds[p[0][:-1]]
        lo, hi = _scope_bounds(lines, idx[0]) if _LOCAL.fullmatch(label) else (0, len(lines))
        ref = re.compile(rf"(?<![\w$]){re.escape(label)}(?![\w$])")
        outside = [j for j in range(lo, hi) if not idx[0] <= j <= idx[-1]
                   and ref.search(_strip(lines[j]))
                   and not _strip(lines[j]).strip().startswith(label + ":")]
        if not outside:
            continue
        nxt = pat[k + 1] if k + 1 < len(pat) else []
        if len(nxt) != 2 or nxt[0] != "jp" or not re.fullmatch(r"%\d+", nxt[1]):
            return False
        target = binds[nxt[1]]
        for j in outside:
            mn, ops = _ops(_strip(lines[j]).strip())
            if mn not in ("jp", "jr") or len(ops) != 2 or ops[1] != label:
                return False
            edits[j] = f"\tjp\t{ops[0]},{target}"
    for j, s in edits.items():
        lines[j] = s
    return True


def apply(rules: list[Rule], lines: list[str]) -> tuple[list[str], int]:
    """Return (rewritten lines, number of replacements)."""
    lines = list(lines)
    n_replaced = 0
    restart = True
    while restart:
        restart = False
        for pat, rep, do_restart, cond in rules:
            i = 0
            while i < len(lines):
                # candidate instruction/label lines from i, skipping comments/blanks
                idx: list[int] = []
                binds: dict[str, str] = {}
                j = i
                ok = True
                for p in pat:
                    while j < len(lines) and not _strip(lines[j]).strip():
                        j += 1
                    if j >= len(lines) or not _match_line(p, _strip(lines[j]), binds):
                        ok = False
                        break
                    idx.append(j)
                    j += 1
                if ok and idx and cond is not None:
                    label = binds.get(cond[0], "")
                    ok = bool(label) and _label_refs(lines, label, idx[0]) == cond[1]
                if ok and idx:
                    ok = _keep_labels_reachable(pat, rep, binds, lines, idx)
                if not ok or not idx:
                    i += 1
                    continue
                out: list[str] = []
                for r in rep:
                    def sub(m: re.Match[str]) -> str:
                        return binds.get(m.group(0), m.group(0))
                    out.append(re.sub(r"%\d+", sub, r))
                lines[idx[0]:idx[-1] + 1] = out
                n_replaced += 1
                if do_restart:
                    restart = True
                    break
                i = idx[0] + len(out)
            if restart:
                break
    return lines, n_replaced


# --- 16-bit compares with a constant --------------------------------------------
#
# LSDj's compiler compared a 16-bit value with a constant the way later SDCC
# releases do on the Game Boy: `ld hl,#-lit; add hl,<pair>` and a jump on the
# inverted carry (`p < 0xAFF0` is `ld hl,#0x5010; add hl,de; jp c,skip`), the
# operand used in place when it sits in bc/de and fetched into de otherwise.
# SDCC 2.2.1 subtracts byte by byte (`xor a,a; ld a,lo; sbc a,#lo; ld a,hi;
# sbc a,#hi; jp nc,skip`). The ROM has 28 such adds and no 16-bit `sbc`
# compare with a constant (its `sbc` chains are all 32-bit).

_HEX = r"#(0x[0-9A-Fa-f]+)"
_CMP16 = [
    # stack operand -> fetched into de
    ([r"xor\s+a,a", r"lda\s+hl,(\d+)\(sp\)", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl",
      r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"jp\s+(n?c),(\S+)"],
     lambda g: ([f"\tlda\thl,{g[0]}(sp)", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[1], g[2], g[3], g[4])),
    # operand already in bc or de
    ([r"xor\s+a,a", r"ld\s+a,([ce])", rf"sbc\s+a,{_HEX}", r"ld\s+a,([bd])", rf"sbc\s+a,{_HEX}",
      r"jp\s+(n?c),(\S+)"],
     lambda g: ([], {"cb": "bc", "ed": "de"}.get(g[0] + g[2], ""), g[1], g[3], g[4], g[5])),
    # a global -> fetched into de (`FileReadCompressedBlock`)
    ([r"xor\s+a,a", r"ld\s+hl,(#[^;]+?)", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl",
      r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"jp\s+(n?c),(\S+)"],
     lambda g: ([f"\tld\thl,{g[0]}", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[1], g[2], g[3], g[4])),
]


def gb_cmp16(lines: list[str]) -> tuple[list[str], int]:
    """Rewrite SDCC 2.2.1's 16-bit `x < lit` / `x >= lit` into LSDj's add form."""
    lines = list(lines)
    n = 0
    i = 0
    while i < len(lines):
        for pats, build in _CMP16:
            idx: list[int] = []
            groups: list[str] = []
            j = i
            for p in pats:
                while j < len(lines) and not _strip(lines[j]).strip():
                    j += 1
                m = re.fullmatch(p, _strip(lines[j]).strip()) if j < len(lines) else None
                if not m:
                    break
                idx.append(j)
                groups += m.groups()
                j += 1
            else:
                fetch, pair, lo, hi, cc, target = build(groups)
                if not pair:
                    continue
                neg = (-(int(hi, 16) << 8 | int(lo, 16))) & 0xFFFF
                out = fetch + [f"\tld\thl,#0x{neg:04X}", f"\tadd\thl,{pair}",
                               f"\tjp\t{'c' if cc == 'nc' else 'nc'},{target}"]
                lines[idx[0]:idx[-1] + 1] = out
                n += 1
                i = idx[0] + len(out) - 1
                break
        i += 1
    return lines, n


# --- accumulator tracking -----------------------------------------------------
#
# LSDj's compiler knew what `a` held: after `ld a,c` / `ld c,a` / `ld a,#5`
# / `xor a,a`, a later load of the same value is dropped, as long as nothing
# in between changed `a` or the source and no *jump target* intervened (a
# label SDCC's own output references; the dead labels its stock peephole
# rules leave behind don't count). The textual rules in lsdj.peep cover
# shapes of this one by one; `track_a` applies it wherever it holds.

_REGS = set("bcdehl")
_PAIRS = {"bc": "bc", "de": "de", "hl": "hl", "af": "a"}
# instructions that change neither `a` nor any register
_NEUTRAL = {"cp", "bit", "push", "nop"}


def _ops(line: str) -> tuple[str, list[str]]:
    parts = line.split(None, 1)
    if not parts:
        return "", []
    ops = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
    return parts[0], ops


def referenced_labels(lines: list[str]) -> set[str]:
    """Jump targets in `lines`: local labels as "scope:label" (their names
    recur across functions -- see _scope_bounds), other labels as themselves."""
    refs: set[str] = set()
    scope = 0
    for l in lines:
        t = _strip(l).strip()
        if not t:
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
            continue
        refs.update(f"{scope}:{x}" for x in re.findall(r"\b(\d+\$)", t))
        refs.update(re.findall(r"\b(_\w+)\b", t))
    return refs


_ALU = {"add", "adc", "sub", "sbc", "and", "or", "xor", "inc", "dec", "rla", "rra", "rlca",
        "rrca", "rl", "rr", "sla", "sra", "srl", "swap", "cpl"}


def track_a(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop loads of values the register already holds: `ld a,X` / `xor a,a`
    when `a` equals X, `ld r,a` when r already equals `a`, `ld hl,...` /
    `lda hl,N(sp)` when `hl` already points there, and `ld a,(hl)` right
    after an ALU result was stored through `hl` (see above)."""
    out: list[str] = []
    eq: set[str] = set()          # what `a` equals: "c", "#0x05", "(hl)"
    hl = None                     # ("stk", N - level) | ("imm", text) | None
    level = 0                     # bytes pushed since the block began
    a_alu = False                 # `a` holds a fresh ALU result
    dropped = 0
    code = [i for i, l in enumerate(lines) if _strip(l).strip()]
    nxt = {code[k]: code[k + 1] for k in range(len(code) - 1)}
    scope = 0

    def forget_hl() -> None:
        nonlocal hl
        hl = None
        eq.discard("(hl)")

    for i, raw in enumerate(lines):
        t = _strip(raw).strip()
        if not t:
            out.append(raw)
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
                eq, level, a_alu = set(), 0, False
                forget_hl()
            elif f"{scope}:{t[:-1]}" in jump_targets:
                eq, a_alu = set(), False
                forget_hl()
            out.append(raw)
            continue
        mn, ops = _ops(t)
        # --- hl ---
        # hl is followed (for the `(hl)` reload below) but its own reloads
        # are left alone: whether LSDj's compiler kept hl across a compare
        # depends on the operand kind (a named local vs a spill slot), which
        # the text doesn't show -- the textual `!=` rule covers globals.
        if mn == "lda" and len(ops) == 2 and ops[0] == "hl":
            m = re.fullmatch(r"(-?\d+)\(sp\)", ops[1])
            if m:
                forget_hl()
                hl = ("stk", int(m.group(1)) - level)
                out.append(raw)
                continue
        if mn == "ld" and len(ops) == 2 and ops[0] == "hl" and ops[1].startswith("#"):
            forget_hl()
            hl = ("imm", ops[1])
            out.append(raw)
            continue
        # --- a ---
        if mn == "ld" and len(ops) == 2 and ops[0] == "a" and (ops[1] in _REGS or ops[1].startswith("#")):
            if ops[1] in eq:
                dropped += 1
                continue
            eq, a_alu = {ops[1]}, False
        elif mn == "ld" and ops == ["a", "(hl)"]:
            if "(hl)" in eq:
                dropped += 1
                continue
            eq, a_alu = {"(hl)"}, False
        elif mn == "xor" and ops == ["a", "a"]:
            n = nxt.get(i)
            follow = _ops(_strip(lines[n]).strip()) if n is not None else ("", [])
            if follow[0] == "or" and len(follow[1]) == 2 and follow[1][0] == "a" and follow[1][1] in eq:
                dropped += 1          # `or a,r` alone: `a` already equals r
                continue
            eq, a_alu = {"#0x00"}, False
        elif mn == "ld" and len(ops) == 2 and ops[1] == "(hl)" and ops[0] in ("b", "c", "d", "e") \
                and "(hl)" in eq:
            # `ld c,(hl)` while `a` still holds (hl): LSDj's compiler used `ld c,a`
            # (3 sites after `ld a,(hl); cp; jp`, none rereading)
            raw = raw.replace("(hl)", "a", 1)
            eq.add(ops[0])
        elif mn == "ld" and len(ops) == 2 and ops[1] == "a" and ops[0] in _REGS:
            if ops[0] in eq:
                dropped += 1          # `ld c,a` when `a` already equals c: a no-op
                continue
            eq.add(ops[0])
            if ops[0] in "hl":
                forget_hl()
        elif mn == "ld" and ops == ["(hl)", "a"]:
            eq.discard("(hl)")
            if a_alu and hl is not None:
                eq.add("(hl)")        # a fresh ALU result, stored through hl
        elif mn == "ld" and len(ops) == 2 and ops[0] == "a":
            eq, a_alu = set(), False  # a loaded from memory (a pointer read keeps its reloads)
            if ops[1] in ("(hl+)", "(hl-)"):
                forget_hl()
        elif mn in ("or", "and") and ops == ["a", "a"]:
            pass
        elif mn in _ALU and (not ops or ops[0] == "a"):
            eq, a_alu = set(), True
        elif mn in ("jp", "jr") and len(ops) == 2:
            pass                      # a conditional jump: `a` and `hl` survive the fall-through
        elif mn in _NEUTRAL:
            if mn == "push":
                level += 2
        elif mn == "inc" and ops == ["sp"]:
            level -= 1
        elif mn == "dec" and ops == ["sp"]:
            level += 1
        elif mn == "lda" and ops and ops[0] == "sp":
            m = re.fullmatch(r"(-?\d+)\(sp\)", ops[1]) if len(ops) == 2 else None
            if m:
                level -= int(m.group(1))
            else:
                forget_hl()
        elif mn == "ld" and len(ops) == 2 and ops[0].startswith("(") and ops[0] not in ("(hl+)", "(hl-)"):
            if ops[0] != "(hl)" and ops[1] != "a":
                eq.discard("(hl)")    # a store elsewhere may alias (storing `a` itself cannot break a == (hl))
        elif mn == "ldh" and len(ops) == 2 and ops[1] == "a":
            pass
        elif mn in ("inc", "dec") and ops == ["hl"]:
            if hl is not None and hl[0] == "stk":
                hl = ("stk", hl[1] + (1 if mn == "inc" else -1))
                eq.discard("(hl)")
            else:
                forget_hl()
        elif mn in ("inc", "dec") and ops == ["(hl)"]:
            eq.discard("(hl)")
        elif mn in ("ld", "lda", "inc", "dec", "pop", "add") and ops and ops[0] != "a":
            dest = ops[0]
            if mn == "ld" and dest in ("(hl+)", "(hl-)"):
                if hl is not None and hl[0] == "stk":
                    hl = ("stk", hl[1] + (1 if dest == "(hl+)" else -1))
                else:
                    forget_hl()
                eq.discard("(hl)")
                out.append(raw)
                continue
            regs = set(_PAIRS.get(dest, dest)) if dest in _PAIRS else ({dest} if dest in _REGS else set())
            if dest.startswith("("):
                regs = set()
            if mn == "ld" and len(ops) == 2 and ops[1] in ("(hl+)", "(hl-)"):
                regs |= {"h", "l"}
            if mn == "pop":
                level -= 2
            eq -= regs
            if mn == "ld" and len(ops) == 2 and dest in _REGS and ops[1] in eq:
                eq.add(dest)          # `ld b,c` while `a` equals c: b equals it too
            if regs & {"h", "l"}:
                forget_hl()
            if not regs and not dest.startswith("("):
                eq, a_alu = set(), False
                forget_hl()
        else:
            eq, a_alu = set(), False  # anything else (calls, jumps, returns): forget
            forget_hl()
            if mn in ("call", "rst", "ret", "reti", "jp", "jr"):
                pass
        out.append(raw)
    return out, dropped


# --- the return register -----------------------------------------------------
#
# The same knowledge for `e`, which holds call results and return values:
# after `ld c,e` (a call's result kept in c), a later `return c` -- SDCC's
# `ld e,c` -- is dropped while neither register changed and no jump target
# intervened (`FindCursorInstr` returns the value it just read that way).

_WRITES_ALL = {"call", "rst", "ret", "reti"}


def _written(mn: str, ops: list[str]) -> set[str]:
    """Registers an instruction may write (8-bit names; `a` included)."""
    if mn in ("push", "cp", "bit", "nop", "halt", "di", "ei"):
        return set()
    if mn in ("jp", "jr"):
        return set()
    if mn in ("ld", "lda", "ldh", "pop", "inc", "dec", "add", "adc", "sub", "sbc", "and",
              "or", "xor", "rl", "rr", "rlc", "rrc", "sla", "sra", "srl", "swap", "res",
              "set", "rla", "rra", "rlca", "rrca", "cpl"):
        out: set[str] = set()
        if mn in ("rla", "rra", "rlca", "rrca", "cpl") or not ops:
            return {"a"}
        dest = ops[1] if mn in ("res", "set") and len(ops) == 2 else ops[0]
        if dest in _PAIRS:
            out |= set(_PAIRS[dest]) if dest != "af" else {"a"}
        elif dest in _REGS or dest == "a":
            out.add(dest)
        for o in ops:
            if o in ("(hl+)", "(hl-)"):
                out |= {"h", "l"}
        if mn in ("sub", "sbc", "and", "or", "xor", "adc") and len(ops) == 2 and ops[0] == "a":
            out = {"a"}
        return out
    return set("abcdehl")


def track_e(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld e,r` while `e` already equals r (b, c or d)."""
    out: list[str] = []
    eq: set[str] = set()          # registers equal to e
    dropped = 0
    scope = 0
    for raw in lines:
        t = _strip(raw).strip()
        if not t:
            out.append(raw)
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
                eq = set()
            elif f"{scope}:{t[:-1]}" in jump_targets:
                eq = set()
            out.append(raw)
            continue
        mn, ops = _ops(t)
        if mn == "ld" and len(ops) == 2 and ops[0] == "e" and ops[1] in ("b", "c", "d"):
            if ops[1] in eq:
                dropped += 1
                continue
            eq = {ops[1]}
        elif mn == "ld" and len(ops) == 2 and ops[1] == "e" and ops[0] in ("b", "c", "d"):
            eq.add(ops[0])
        elif mn in _WRITES_ALL or (mn in ("jp", "jr") and len(ops) == 1):
            eq = set()
        else:
            w = _written(mn, ops)
            if "e" in w:
                eq = set()
            else:
                eq -= w
        out.append(raw)
    return out, dropped


def track_const(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld r,#n` (r = b..e) while r already holds n: the high byte of a
    widened index stays 0 for the next one (`ReadWordCell`)."""
    out: list[str] = []
    val: dict[str, str] = {}      # register -> the constant it holds
    dropped = 0
    scope = 0
    for raw in lines:
        t = _strip(raw).strip()
        if not t:
            out.append(raw)
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
                val = {}
            elif f"{scope}:{t[:-1]}" in jump_targets:
                val = {}
            out.append(raw)
            continue
        mn, ops = _ops(t)
        if mn == "ld" and len(ops) == 2 and ops[0] in ("b", "c", "d", "e") and ops[1].startswith("#"):
            if val.get(ops[0]) == ops[1]:
                dropped += 1
                continue
            val[ops[0]] = ops[1]
        elif mn in _WRITES_ALL or (mn in ("jp", "jr") and len(ops) == 1):
            val = {}
        else:
            for r in _written(mn, ops):
                val.pop(r, None)
        out.append(raw)
    return out, dropped


# --- dead register stores ----------------------------------------------------
#
# Within a basic block LSDj's compiler never left `ld r,a` (r = b..e) that a
# later `ld r,a` rewrites before anything reads r: of 1999 `ld r,a` in the
# ROM's compiled functions, none is (stock SDCC 2.2.1 emits them, e.g. the
# low byte of a 16-bit `&` stored and then rewritten by the `|` that
# follows). This generalises the textual dead-store rules. Stores killed any
# other way (`pop bc`, `ld r,#n`) survive in the ROM, as do those dead only
# across a call or a branch: blocks end at jumps, calls, returns and
# referenced labels.

_PAIR_OF = {"bc": "bc", "de": "de", "hl": "hl"}


def _reads_reg(mn: str, ops: list[str], r: str) -> bool:
    if mn == "ld" and len(ops) == 2:
        dst, src = ops
        if src == r or (src in ("(bc)", "(de)") and r in src):
            return True
        if dst in ("(bc)", "(de)") and r in dst:
            return True
        return False
    if mn in ("lda", "ldh"):
        return False
    if mn == "push":
        return r in _PAIR_OF.get(ops[0], "") if ops else True
    if mn in ("add", "adc", "sub", "sbc", "and", "or", "xor", "cp"):
        if ops and ops[0] in ("hl", "sp"):
            return r in ops[-1]
        return bool(ops) and ops[-1] == r
    if mn in ("inc", "dec"):
        return bool(ops) and (ops[0] == r or (len(ops[0]) == 2 and r in ops[0]))
    if mn in ("bit", "res", "set", "rl", "rr", "sla", "sra", "srl", "swap", "rlc", "rrc"):
        return bool(ops) and ops[-1] == r
    if mn in ("pop", "nop", "rla", "rra", "rlca", "rrca", "cpl", "scf", "ccf"):
        return False
    return True                              # unknown: assume it reads


def _restores_reg(mn: str, ops: list[str], r: str) -> bool:
    """`ld r,a` or `ld r,#n`: the overwrites that make an earlier unread
    `ld r,a` dead in LSDj's code (none of either kind survives in the ROM;
    a store killed by `pop bc` does)."""
    return mn == "ld" and len(ops) == 2 and ops[0] == r and (ops[1] == "a" or ops[1].startswith("#"))


def dead_stores(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld r,a` (r = b, c, d, e) overwritten unread within its block."""
    code = [(i, _ops(_strip(l).strip())) for i, l in enumerate(lines) if _strip(l).strip()]
    scope, scopes = 0, {}
    for i, (mn, ops) in code:
        t = _strip(lines[i]).strip()
        if t.endswith(":") and not _LOCAL.fullmatch(t[:-1]):
            scope += 1
        scopes[i] = scope
    drop: set[int] = set()
    for k, (i, (mn, ops)) in enumerate(code):
        if not (mn == "ld" and len(ops) == 2 and ops[1] == "a" and ops[0] in ("b", "c", "d", "e")):
            continue
        r = ops[0]
        for j, (mn2, ops2) in code[k + 1:]:
            t = _strip(lines[j]).strip()
            if t.endswith(":"):
                if not _LOCAL.fullmatch(t[:-1]) or f"{scopes[j]}:{t[:-1]}" in jump_targets:
                    break
                continue                      # a dead label: same block
            if mn2 in ("jp", "jr", "call", "ret", "reti", "rst", "halt", "stop"):
                break
            if _reads_reg(mn2, ops2, r):
                break
            if _restores_reg(mn2, ops2, r):
                drop.add(i)
                break
            if mn2 in ("ld", "pop") and ops2 and (ops2[0] == r or r in _PAIR_OF.get(ops2[0], "")):
                break                         # overwritten some other way: kept
    return [l for i, l in enumerate(lines) if i not in drop], len(drop)


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rules = parse_rules(Path(sys.argv[1]).read_text())
    asm = Path(sys.argv[2])
    lines, n = apply(rules, asm.read_text(errors="replace").splitlines())
    asm.write_text("\n".join(lines) + "\n")
    print(f"{asm}: {n} replacement(s)")


if __name__ == "__main__":
    main()
