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


def _label_refs(lines: list[str], label: str) -> int:
    """References to `label` outside its own definition line."""
    pat = re.compile(rf"(?<![\w$]){re.escape(label)}(?![\w$])")
    return sum(len(pat.findall(_strip(l))) for l in lines
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
                    ok = bool(label) and _label_refs(lines, label) == cond[1]
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
