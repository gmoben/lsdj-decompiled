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


def track_a(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld a,X` / `xor a,a` whose value `a` already holds (see above)."""
    out: list[str] = []
    eq: set[str] = set()          # sources `a` currently equals: "c", "#0x05"
    dropped = 0
    code = [i for i, l in enumerate(lines) if _strip(l).strip()]
    nxt = {code[k]: code[k + 1] for k in range(len(code) - 1)}
    scope = 0
    for i, raw in enumerate(lines):
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
        if mn == "ld" and len(ops) == 2 and ops[0] == "a" and (ops[1] in _REGS or ops[1].startswith("#")):
            if ops[1] in eq:
                dropped += 1
                continue
            eq = {ops[1]}
        elif mn == "xor" and ops == ["a", "a"]:
            n = nxt.get(i)
            follow = _ops(_strip(lines[n]).strip()) if n is not None else ("", [])
            if follow[0] == "or" and len(follow[1]) == 2 and follow[1][0] == "a" and follow[1][1] in eq:
                dropped += 1          # `or a,r` alone: `a` already equals r
                continue
            eq = {"#0x00"}
        elif mn == "ld" and len(ops) == 2 and ops[1] == "a" and ops[0] in _REGS:
            eq.add(ops[0])
        elif mn == "ld" and len(ops) == 2 and ops[0] == "a":
            eq = set()                # a loaded from memory
        elif mn in _NEUTRAL or (mn in ("jp", "jr") and len(ops) == 2):
            pass                      # flags/stack/fall-through: `a` unchanged
        elif mn in ("or", "and") and ops == ["a", "a"]:
            pass
        elif mn == "inc" and ops == ["sp"] or mn == "lda" and ops and ops[0] == "sp":
            pass
        elif mn == "ld" and len(ops) == 2 and ops[0].startswith("(") and ops[0] not in ("(hl+)", "(hl-)"):
            pass                      # a store to memory
        elif mn == "ldh" and len(ops) == 2 and ops[1] == "a":
            pass
        elif mn in ("ld", "lda", "inc", "dec", "pop", "add") and ops and ops[0] != "a":
            dest = ops[0]
            if mn == "ld" and dest in ("(hl+)", "(hl-)"):
                dest = "hl"
            regs = set(_PAIRS.get(dest, dest)) if dest in _PAIRS else ({dest} if dest in _REGS else set())
            if dest.startswith("("):
                regs = set()
            if mn == "ld" and len(ops) == 2 and ops[1] in ("(hl+)", "(hl-)"):
                regs |= {"h", "l"}
            eq -= regs
            if not regs and not dest.startswith("("):
                eq = set()
        else:
            eq = set()                # anything else: forget
        out.append(raw)
    return out, dropped


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
