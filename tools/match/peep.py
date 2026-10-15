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

import functools
import re
import sys
from pathlib import Path

# (pattern token-lines, replacement lines, restart, condition or None)
Rule = tuple[list[list[str]], list[str], bool, "tuple[str, int] | None"]


def parse_rules(text: str) -> list[Rule]:
    rules: list[Rule] = []
    for m in re.finditer(r"replace(\s+restart)?\s*\{(.*?)\}\s*by\s*\{(.*?)\}"
                         r"(?:[ \t]*if[ \t]+(?:labelRefCount[ \t]+(%\d+)[ \t]+(\d+)"
                         r"|operandIsReg[ \t]+(%\d+)))?", text, re.S):
        pat = [l.strip() for l in m.group(2).splitlines() if l.strip() and not l.strip().startswith(";")]
        rep = [l.rstrip() for l in m.group(3).splitlines() if l.strip()]
        # `if labelRefCount %N k`: label %N has k references; `if operandIsReg %N`:
        # %N bound to a register, not a memory operand
        cond = (m.group(4), int(m.group(5))) if m.group(4) else ((m.group(6), -1) if m.group(6) else None)
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


@functools.lru_cache(maxsize=None)
def _token_pattern(p: str) -> "re.Pattern[str]":
    return re.compile(_token_regex(p))


def _match_toks(pat: list[str], toks: list[str], binds: dict[str, str]) -> bool:
    """Match one pattern line against a source line's tokens, extending
    `binds` (left untouched on failure)."""
    if len(toks) != len(pat):
        return False
    trial = dict(binds)
    for p, t in zip(pat, toks):
        if "%" not in p:
            if p != t:
                return False
            continue
        m = _token_pattern(p).fullmatch(t)
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


def _match_line(pat: list[str], src: str, binds: dict[str, str]) -> bool:
    return _match_toks(pat, src.split(), binds)


def _keep_labels_reachable(pat: list[list[str]], rep: list[str], binds: dict[str, str],
                           lines: list[str], idx: list[int],
                           edited: list[int] | None = None) -> bool:
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
        if edited is not None:
            edited.append(j)
    return True


def apply_full(rules: list[Rule], lines: list[str]) -> tuple[list[str], int]:
    """The reference matcher: for each rule in order, every position of the
    file; a `restart` rule's replacement starts over from the first rule.
    Quadratic in the unit (minutes on a 17,000-line unit); `apply` below is
    the same algorithm skipping the positions whose text a rule has already
    seen. Kept for the equivalence check in test_peep.py."""
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
                    if cond[1] < 0:
                        ok = binds.get(cond[0], "") in ("a", "b", "c", "d", "e", "h", "l")
                    else:
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


_LABEL_LINE = re.compile(r"^[\w$.]+:")
_JUMPISH = ("jp", "jr", "call", "djnz", "rst")


def _tokens(line: str) -> list[str] | None:
    """The line's tokens without its comment; None for a blank/comment line."""
    t = _strip(line).split()
    return t or None


def _touches_labels(ls: list[str]) -> bool:
    """Whether any of these lines defines or refers to a label (changing the
    reference counts the global rules depend on)."""
    for l in ls:
        t = _strip(l).strip()
        if not t:
            continue
        if _LABEL_LINE.match(t) or t.split(None, 1)[0] in _JUMPISH:
            return True
    return False


class _Dirty:
    """Sorted, disjoint half-open intervals of positions a rule still has to
    attempt (every position, initially)."""

    __slots__ = ("iv",)

    def __init__(self, n: int):
        self.iv: list[list[int]] = [[0, n]] if n else []

    def add(self, lo: int, hi: int) -> None:
        if lo >= hi:
            return
        out: list[list[int]] = []
        placed = False
        for a, b in self.iv:
            if b < lo:
                out.append([a, b])
            elif a > hi:
                if not placed:
                    out.append([lo, hi])
                    placed = True
                out.append([a, b])
            else:                      # overlapping or adjacent: merge
                lo, hi = min(lo, a), max(hi, b)
        if not placed:
            out.append([lo, hi])
        self.iv = out

    def remove(self, lo: int, hi: int) -> None:
        """Positions [lo, hi) were attempted: no longer dirty."""
        out: list[list[int]] = []
        for a, b in self.iv:
            if b <= lo or a >= hi:
                out.append([a, b])
                continue
            if a < lo:
                out.append([a, lo])
            if b > hi:
                out.append([hi, b])
        self.iv = out

    def first_at_or_after(self, cursor: int) -> int | None:
        for a, b in self.iv:
            if b > cursor:
                return max(a, cursor)
        return None

    def shift(self, at: int, removed: int, added: int) -> None:
        """Lines [at, at+removed) were replaced by `added` lines: move the
        positions after them, clip the ones inside, and mark the new lines."""
        delta = added - removed
        end = at + removed
        out: list[list[int]] = []
        for a, b in self.iv:
            if a >= end:
                a, b = a + delta, b + delta
            else:
                if a > at:
                    a = at
                if b > end:
                    b += delta
                elif b > at:
                    b = at
            if a < b:
                out.append([a, b])
        self.iv = out
        self.add(at, at + added)


def _back_nonblank(toks: list, at: int, count: int) -> int:
    """The index of the `count`-th non-blank line before `at` (0 if fewer)."""
    i = at
    seen = 0
    while i > 0 and seen < count:
        i -= 1
        if toks[i] is not None:
            seen += 1
    return i


def apply(rules: list[Rule], lines: list[str]) -> tuple[list[str], int]:
    """Return (rewritten lines, number of replacements). The same result as
    apply_full, computed incrementally: a rule only (re)attempts the
    positions whose match window changed since it last looked -- every
    position at first, then the lines each replacement rewrote plus the
    window that can reach them. The `labelRefCount` rules and the ones that
    drop a label also depend on the jumps of the whole function, so a
    replacement that adds or removes a label or a jump re-dirties the
    enclosing function for them."""
    lines = list(lines)
    toks: list[list[str] | None] = [_tokens(l) for l in lines]
    n_rules = len(rules)
    maxpat = max((len(r[0]) for r in rules), default=1)
    is_global = []
    for pat, rep, _do_restart, cond in rules:
        kept = {r.strip() for r in rep}
        drops = any(len(p) == 1 and re.fullmatch(r"%\d+:", p[0]) and p[0] not in kept for p in pat)
        is_global.append(bool(cond is not None and cond[1] >= 0) or drops)
    dirty = [_Dirty(len(lines)) for _ in rules]
    n_replaced = 0

    def rewrite(at: int, removed: int, out: list[str], edited: list[int]) -> None:
        """Apply a replacement (lines [at, at+removed) -> out; `edited` are
        the jumps _keep_labels_reachable retargeted, in the old numbering)
        and re-dirty what it can affect."""
        old = lines[at:at + removed]
        delta = len(out) - removed
        edited = [e + delta if e >= at + removed else e for e in edited]
        lines[at:at + removed] = out
        toks[at:at + removed] = [_tokens(l) for l in out]
        for e in edited:
            toks[e] = _tokens(lines[e])
        labels = _touches_labels(old) or _touches_labels(out) or bool(edited)
        lo = _back_nonblank(toks, at, maxpat)
        hi = at + len(out)
        for e in edited:
            lo = min(lo, _back_nonblank(toks, e, maxpat))
            hi = max(hi, e + 1)
        slo = shi = 0
        if labels:
            slo, shi = _scope_bounds(lines, min(at, len(lines) - 1)) if lines else (0, 0)
            for e in edited:
                elo, ehi = _scope_bounds(lines, e)
                slo, shi = min(slo, elo), max(shi, ehi)
        for k in range(n_rules):
            d = dirty[k]
            d.shift(at, removed, len(out))
            d.add(lo, hi)
            if labels and is_global[k]:
                d.add(slo, shi)

    restart = True
    while restart:
        restart = False
        for r, (pat, rep, do_restart, cond) in enumerate(rules):
            d = dirty[r]
            cursor = 0
            while True:
                p = d.first_at_or_after(cursor)
                if p is None:
                    break
                # the attempt at p is the attempt at the first non-blank line from p
                j = p
                while j < len(lines) and toks[j] is None:
                    j += 1
                if j >= len(lines):
                    d.remove(p, len(lines))
                    break
                j0 = j
                idx: list[int] = []
                binds: dict[str, str] = {}
                ok = True
                for pl in pat:
                    while j < len(lines) and toks[j] is None:
                        j += 1
                    if j >= len(lines) or not _match_toks(pl, toks[j], binds):
                        ok = False
                        break
                    idx.append(j)
                    j += 1
                if ok and cond is not None:
                    if cond[1] < 0:
                        ok = binds.get(cond[0], "") in ("a", "b", "c", "d", "e", "h", "l")
                    else:
                        label = binds.get(cond[0], "")
                        ok = bool(label) and _label_refs(lines, label, idx[0]) == cond[1]
                edited: list[int] = []
                if ok:
                    ok = _keep_labels_reachable(pat, rep, binds, lines, idx, edited)
                d.remove(p, j0 + 1)
                if not ok:
                    cursor = j0 + 1
                    continue
                out: list[str] = []
                for rl in rep:
                    def sub(m: re.Match[str]) -> str:
                        return binds.get(m.group(0), m.group(0))
                    out.append(re.sub(r"%\d+", sub, rl))
                rewrite(idx[0], idx[-1] + 1 - idx[0], out, edited)
                n_replaced += 1
                if do_restart:
                    restart = True
                    break
                cursor = idx[0] + len(out)
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
    # hl already at the operand's low byte (a stack slot just stored through
    # hl: TapTempoStep's `sum < 0x100` after DivU16) -> fetched into de;
    # track_a then drops the `ld e,(hl)` of a byte just stored from e
    ([r"xor\s+a,a", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}",
      r"jp\s+(n?c),(\S+)"],
     lambda g: (["\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[0], g[1], g[2], g[3])),
    # hl one past the operand's low byte (a 16-bit temp just stored byte by
    # byte, DrawChainCell's `(UWORD)t < 0x78`): stepped back, then fetched
    ([r"xor\s+a,a", r"dec\s+hl", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl", r"ld\s+a,\(hl\)",
      rf"sbc\s+a,{_HEX}", r"jp\s+(n?c),(\S+)"],
     lambda g: (["\tdec\thl", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[0], g[1], g[2], g[3])),
    # hl one below the operand's low byte (the byte local just compared:
    # SpeechInstrInput's `i < 0x10` before `p >= 0xBDD0`): stepped up, then
    # fetched
    ([r"xor\s+a,a", r"inc\s+hl", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl", r"ld\s+a,\(hl\)",
      rf"sbc\s+a,{_HEX}", r"jp\s+(n?c),(\S+)"],
     lambda g: (["\tinc\thl", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[0], g[1], g[2], g[3])),
    # a global -> fetched into de (`CompressIsDefaultWave`)
    ([r"xor\s+a,a", r"ld\s+hl,(#[^;]+?)", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"inc\s+hl",
      r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}", r"jp\s+(n?c),(\S+)"],
     lambda g: ([f"\tld\thl,{g[0]}", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)"], "de", g[1], g[2], g[3], g[4])),
]

# The same compare can produce a stored boolean rather than a conditional
# jump. The only reached ROM instance is BuildOverwritePrompt ($1D:$6B8E):
# add hl,de; ld a,0; rla; xor a,1. No stock subtract/materialize form occurs
# in the reached program banks. Adding -N gives the inverse of subtract's
# borrow, so the numeric result must be inverted as well as branch polarity.
_CMP16_FORMS = [(pats, build, False) for pats, build in _CMP16] + [
    (pats[:-1] + [r"ld\s+a,#0x00", r"rla"], build, True)
    # Stack/register forms have enough context to restore a following
    # stack-relative store. Leave global materialization alone for now.
    for pats, build in _CMP16[:-1]
]


def _hl_stack_offset(lines: list[str], at: int) -> int | None:
    """The sp-relative offset hl holds just before line `at`, found by walking
    back to the last `lda hl,N(sp)` over hl steps (`inc hl`, `dec hl`, `(hl+)`,
    `(hl-)`) and `lda sp,M(sp)` adjustments and the fall-through of a
    conditional jump (a loop test: FreeUnusedInstrs's `i < 0x40` before its
    di); None past a label, unconditional jump, call or anything else that
    sets hl."""
    off = 0
    k = at - 1
    while k >= 0:
        t = _strip(lines[k]).strip()
        k -= 1
        if not t:
            continue
        m = re.fullmatch(r"lda\s+hl,(-?\d+)\(sp\)", t)
        if m:
            return int(m.group(1)) + off
        m = re.fullmatch(r"lda\s+sp,(-?\d+)\(sp\)", t)
        if m:
            off -= int(m.group(1))
            continue
        if t == "inc\thl" or "(hl+)" in t:
            off += 1          # happened after the lda: hl is one higher
            continue
        if t == "dec\thl" or "(hl-)" in t:
            off -= 1
            continue
        if re.fullmatch(r"j[pr]\s+n?[cz],\S+", t):
            continue          # reached on the fall-through: hl unchanged
        mn = t.split()[0] if t else ""
        if t.endswith(":") or mn in ("jp", "jr", "call", "ret", "reti", "rst", "push", "pop") \
                or re.match(r"(ld|add|inc|dec)\s+(hl|h|l)\b", t) or t.startswith("ld\thl"):
            return None
    return None


def _hl_global_offset(lines: list[str], at: int) -> tuple[str, int] | None:
    """The global and offset hl holds just before line `at`, found the way
    `_hl_stack_offset` finds a stack offset, back to the last `ld hl,#G` or
    `ld hl,#G + N` (a byte promoted into a pair: WordScreenInput's
    `(UWORD)wSpeechRow < 13` loads `ld c,(hl); ld b,#0x00` and leaves
    hl at the byte it goes on to increment)."""
    off = 0
    k = at - 1
    while k >= 0:
        t = _strip(lines[k]).strip()
        k -= 1
        if not t:
            continue
        m = re.fullmatch(r"ld\s+hl,#(\w+)(?: \+ (\d+))?", t)
        if m:
            return m.group(1), int(m.group(2) or 0) + off
        if t == "inc\thl" or "(hl+)" in t:
            off += 1
            continue
        if t == "dec\thl" or "(hl-)" in t:
            off -= 1
            continue
        if re.fullmatch(r"j[pr]\s+n?[cz],\S+", t):
            continue
        mn = t.split()[0] if t else ""
        if t.endswith(":") or mn in ("jp", "jr", "call", "ret", "reti", "rst", "push", "pop") \
                or re.match(r"(ld|add|inc|dec)\s+(hl|h|l)\b", t) or t.startswith("ld\thl") \
                or t.startswith("lda\t"):
            return None
    return None


def ifx_word_hl(lines: list[str]) -> tuple[list[str], int]:
    """Reload hl after `if (w)` on a stack word.

    SDCC 2.2.1 tests a 16-bit stack operand in genIfx with `lda hl,N(sp);
    ld a,(hl); inc hl; or a,(hl)` (its peephole folds the first two into
    `ld a,(hl+)`) and still knows hl, so the next use of that word steps
    back with `dec hl`. LSDj's compiler forgot hl after this test and
    reloaded it (DrawInstrFieldHelp's `if (s) ShowMessage(s)`: `ld hl,sp+0`
    again). A `w == 0` compare (genCmpEq, the same bytes) keeps hl in both
    (GetInstrEnvBytes' `dec hl; ld (hl),1`), so only genIfx's test counts."""
    lines = list(lines)
    n = 0
    i = 0
    while i < len(lines):
        if _strip(lines[i]).strip() or "; genIfx" not in lines[i]:
            i += 1
            continue
        code = []
        k = i + 1
        while k < len(lines) and len(code) < 4:
            t = _strip(lines[k]).strip()
            if t:
                if t.endswith(":"):
                    break
                code.append((k, t))
            k += 1
        ts = [t for _, t in code]
        if ts and re.fullmatch(r"lda\s+hl,\d+\(sp\)", ts[0]):
            ts = ts[1:]
            code = code[1:]
        if len(ts) >= 3 and ts[0] == "ld\ta,(hl+)" and ts[1] == "or\ta,(hl)" \
                and re.fullmatch(r"jp\s+n?z,\S+", ts[2]):
            jump = code[2][0]
            base = _hl_stack_offset(lines, jump)
            if base is not None:
                k = jump + 1
                while k < len(lines) and not _strip(lines[k]).strip():
                    k += 1
                adj = k
                off = 0
                while adj < len(lines) and _strip(lines[adj]).strip() in ("inc\thl", "dec\thl"):
                    off += 1 if _strip(lines[adj]).strip() == "inc\thl" else -1
                    adj += 1
                nxt = _strip(lines[adj]).strip() if adj < len(lines) else ""
                if adj > k and "(hl" in nxt and not nxt.endswith(":"):
                    lines[k:adj] = [f"\tlda\thl,{base + off}(sp)"]
                    n += 1
        i += 1
    return lines, n


def inline_asm_hl(lines: list[str]) -> tuple[list[str], int]:
    """Reload hl after an inline-asm block.

    SDCC 2.2.1's genInline leaves its pair cache alone, so a stack byte
    addressed right after `_asm di _endasm` is reached with `inc hl` from
    the previous `lda hl,N(sp)`; LSDj's compiler reloaded `ld hl,sp+N`
    there (RemapSeqChains' `i = 0` after the di). An inline line keeps the
    user's indentation after SDCC's tab (`\t        di`)."""
    lines = list(lines)
    n = 0
    i = 0
    while i < len(lines):
        raw = lines[i]
        t = _strip(raw).strip()
        if re.match(r"\t +\S", raw) and t and not t.endswith(":"):
            k = i + 1
            while k < len(lines) and not _strip(lines[k]).strip():
                k += 1
            adj = k
            off = 0
            while adj < len(lines) and _strip(lines[adj]).strip() in ("inc\thl", "dec\thl"):
                off += 1 if _strip(lines[adj]).strip() == "inc\thl" else -1
                adj += 1
            nxt = _strip(lines[adj]).strip() if adj < len(lines) else ""
            if adj > k and "(hl" in nxt and not nxt.endswith(":"):
                base = _hl_stack_offset(lines, i)
                if base is not None:
                    lines[k:adj] = [f"\tlda\thl,{base + off}(sp)"]
                    n += 1
        i += 1
    return lines, n


def gb_cmp16(lines: list[str], zero: bool = False) -> tuple[list[str], int]:
    """Rewrite word comparisons with a constant, branching or stored, to the
    add form; signed ones against 0 too when `zero` (GBDK's library build)."""
    lines = list(lines)
    n = 0
    i = 0
    while i < len(lines):
        for pats, build, materialized in _CMP16_FORMS:
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
                fetch, pair, lo, hi, cc, target = build(
                    groups + ["", ""] if materialized else groups)
                if not pair:
                    continue
                neg = (-(int(hi, 16) << 8 | int(lo, 16))) & 0xFFFF
                if not neg:
                    # x + 0 never carries: unlike nonzero thresholds, its
                    # carry is not the inverse of x - 0's borrow.
                    continue
                tail = (["\tld\ta,#0x00", "\trla", "\txor\ta,#0x01"]
                        if materialized else
                        [f"\tjp\t{'c' if cc == 'nc' else 'nc'},{target}"])
                out = fetch + [f"\tld\thl,#0x{neg:04X}", f"\tadd\thl,{pair}"] + tail
                lines[idx[0]:idx[-1] + 1] = out
                n += 1
                i = idx[0] + len(out) - 1
                known = None
                if fetch and fetch[0].startswith("\tlda\thl,"):
                    known = int(groups[0]) + 1
                elif fetch and fetch[0] == "\tld\te,(hl)":
                    known = _hl_stack_offset(lines, idx[0])
                    known = known + 1 if known is not None else None
                elif fetch and fetch[0] == "\tdec\thl":
                    # The fetch steps back from the high byte, then returns
                    # to it. The subsequent add destroys that stack address.
                    known = _hl_stack_offset(lines, idx[0])
                elif fetch and fetch[0] == "\tinc\thl":
                    # stepped up to the low byte, then to the high byte
                    known = _hl_stack_offset(lines, idx[0])
                    known = known + 2 if known is not None else None
                glob = None
                if not fetch:
                    # the operand was already in a pair; hl may still point at
                    # a stack byte the code goes on to use (DrawSpeechInstrScreen's
                    # `row < 4 + 14` then `GetTilemapAddr(0, row)`) or at the
                    # global byte it was promoted from (WordScreenInput's
                    # `(UWORD)wSpeechRow < 13` then `wSpeechRow++`)
                    known = _hl_stack_offset(lines, idx[0])
                    if known is None:
                        found = _hl_global_offset(lines, idx[0])
                        if found:
                            glob, known = found
                if fetch and fetch[0].startswith("\tld\thl,#"):
                    # a global: hl was left at its high byte the same way
                    # (CompressSong's `wCompressDst < 0x7FFF` then `wCompressDst + 1`)
                    gm = re.fullmatch(r"#(\w+)(?: \+ (\d+))?", groups[0].strip())
                    if gm:
                        glob = gm.group(1)
                        known = int(gm.group(2) or 0) + 1
                if known is not None:
                    _reseat_hl(lines, i, known, glob)
                break
        i += 1
    lines, signed = _signed_cmp16(lines, zero)
    return lines, n + signed


def _reseat_hl(lines: list[str], i: int, off: int, glob: str | None = None) -> None:
    """After a rewritten compare ending at line `i`: stock SDCC left hl at
    stack offset `off` (or `glob + off`) and may go on from there (`dec hl;
    ld a,(hl+)`); the add clobbered hl, so LSDj's compiler reloaded it
    (DrawBlocksUsed's `ld hl,sp+0`). A negation reloads it after its carry
    clear, before the zero it subtracts from (genUminus fetches the operand
    first: abs's `xor a,a; ld hl,sp+4; ld a,$00; sbc a,[hl]`), a shift
    after its carry clear (`or a,a; ld hl,sp+2; rl [hl]`)."""
    def code_after(j: int) -> int:
        j += 1
        while j < len(lines) and not _strip(lines[j]).strip():
            j += 1
        return j

    def text(j: int) -> str:
        return _strip(lines[j]).strip() if j < len(lines) else ""

    def negation(j: int) -> bool:
        return text(j) == "ld\ta,#0x00" and text(code_after(j)) == "sbc\ta,(hl)"

    start = code_after(i)
    clear = text(start) if text(start) in ("xor\ta,a", "or\ta,a") else ""
    if clear:
        start = code_after(start)
    adj = start
    while text(adj) in ("inc\thl", "dec\thl"):
        off += 1 if text(adj) == "inc\thl" else -1
        adj = code_after(adj)
    nxt = text(adj)
    if clear == "xor\ta,a":
        ok = negation(adj)
    elif clear:                           # a shift's carry clear (FxCmd_F_Wav's `step <<= 1`)
        ok = bool(re.fullmatch(r"(rl|rr|sla|sra|srl)\s+\(hl\)", nxt))
    else:
        ok = negation(adj) or ("(hl" in nxt and not nxt.endswith(":"))
    if not ok:
        return
    lines[start:adj] = ([f"\tld\thl,#{glob}" + (f" + {off}" if off else "")] if glob
                        else [f"\tlda\thl,{off}(sp)"])


# --- signed 16-bit compares with a constant ---------------------------------
#
# SDCC 2.2.1 compares a signed word with a constant by flipping both sign
# bits and subtracting -- `ld a,hi; xor a,#0x80; ld e,a; ld d,#K.hi^0x80;
# ld a,lo; sbc a,#K.lo; ld a,e; sbc a,d` for `x < K`, the constant first
# for `K < x` (`x > K`, `x <= K`): `ld e,#K.hi^0x80; ld a,hi; xor a,#0x80;
# ld d,a; ld a,#K.lo; sbc a,lo; ...` -- and against 0 tests bit 7 of the
# high byte (`ld a,hi; bit 7,a; jp z`, after genCmpLt/genCmpGt's comment).
# LSDj's compiler used the unsigned add form on the sign-flipped operand:
# `ld e,lo; ld a,hi; xor a,#0x80; ld d,a; ld hl,#-(K^0x8000); add hl,de`
# and a jump on the inverted carry (DrawCrashScreen's `i < 10`: `ld
# hl,#0x7FF6`); with the constant first, the add compares with K + 1 and
# keeps the jump (NameEditorInput's `> -1`: `#0x8000`). Against 0 it tested
# bit 7 as stock SDCC does (SynthRenderWaveA's `(WORD)samp < 0`), where
# GBDK's library build adds here too (abs's `num < 0`: `ld hl,#0x8000`).

_SCMP_STACK_RIGHT = ["<hl>", r"ld\s+a,\(hl\)", r"xor\s+a,#0x80", r"ld\s+e,a",
                     rf"ld\s+d,{_HEX}", r"dec\s+hl", r"ld\s+a,\(hl\)", rf"sbc\s+a,{_HEX}",
                     r"ld\s+a,e", r"sbc\s+a,d", r"jp\s+(n?c),(\S+)"]
_SCMP_STACK_LEFT = [rf"ld\s+e,{_HEX}", "<hl>", r"ld\s+a,\(hl\)", r"xor\s+a,#0x80",
                    r"ld\s+d,a", rf"ld\s+a,{_HEX}", r"dec\s+hl", r"sbc\s+a,\(hl\)",
                    r"ld\s+a,e", r"sbc\s+a,d", r"jp\s+(n?c),(\S+)"]
_SCMP_REG_RIGHT = [r"ld\s+a,([bc])", r"xor\s+a,#0x80", r"ld\s+e,a", rf"ld\s+d,{_HEX}",
                   r"ld\s+a,([bc])", rf"sbc\s+a,{_HEX}", r"ld\s+a,e", r"sbc\s+a,d",
                   r"jp\s+(n?c),(\S+)"]
_SCMP_REG_LEFT = [rf"ld\s+e,{_HEX}", r"ld\s+a,([bc])", r"xor\s+a,#0x80", r"ld\s+d,a",
                  rf"ld\s+a,{_HEX}", r"sbc\s+a,([bc])", r"ld\s+a,e", r"sbc\s+a,d",
                  r"jp\s+(n?c),(\S+)"]
_SCMP_STACK_ZERO = ["<hl>", r"ld\s+a,\(hl\)", r"bit\s+7,a", r"jp\s+(n?z),(\S+)"]
_SCMP_REG_ZERO = [r"ld\s+a,([bc])", r"bit\s+7,a", r"jp\s+(n?z),(\S+)"]
_OTHER_HALF = {"b": "c", "c": "b"}


def _match_seq(lines: list[str], i: int, pats: list[str]) -> tuple[list[int], list[str]] | None:
    """The lines (skipping blank and comment-only ones) from `i` on that match
    `pats` in order, with the regex groups; None when they do not. `<hl>`
    is SDCC seating hl on a stack byte: `lda hl,N(sp)` (group "N") or `inc
    hl` steps from where it was (group "+K", K possibly 0, no line)."""
    idx: list[int] = []
    groups: list[str] = []
    j = i
    for p in pats:
        while j < len(lines) and not _strip(lines[j]).strip():
            j += 1
        if p == "<hl>":
            t = _strip(lines[j]).strip() if j < len(lines) else ""
            m = re.fullmatch(r"lda\s+hl,(\d+)\(sp\)", t)
            if m:
                idx.append(j)
                groups.append(m.group(1))
                j += 1
                continue
            steps = 0
            while j < len(lines) and _strip(lines[j]).strip() == "inc\thl":
                idx.append(j)
                steps += 1
                j += 1
                while j < len(lines) and not _strip(lines[j]).strip():
                    j += 1
            groups.append(f"+{steps}")
            continue
        m = re.fullmatch(p, _strip(lines[j]).strip()) if j < len(lines) else None
        if not m:
            return None
        idx.append(j)
        groups += m.groups()
        j += 1
    return idx, groups


def _seat_hl(lines: list[str], at: int, how: str) -> tuple[int, list[str]] | None:
    """For `<hl>` matched as `how` at line `at` (the first line after it when
    it matched no line): the offset of the byte it seats hl on, and the
    instructions that seat hl one byte lower instead."""
    if not how.startswith("+"):
        return int(how), [f"\tlda\thl,{int(how) - 1}(sp)"]
    base = _hl_stack_offset(lines, at)
    if base is None:
        return None
    steps = int(how[1:]) - 1
    return base + steps + 1, (["\tinc\thl"] * steps if steps >= 0 else ["\tdec\thl"])


def _after_cmp_comment(lines: list[str], i: int) -> bool:
    """Line `i` is the first code after genCmpLt's or genCmpGt's comment."""
    k = i - 1
    while k >= 0 and not _strip(lines[k]).strip():
        if re.match(r"\s*;\s*genCmp(Lt|Gt)\b", lines[k]):
            return True
        k -= 1
    return False


def _signed_cmp16(lines: list[str], zero: bool) -> tuple[list[str], int]:
    """Rewrite signed word comparisons with a constant to the add form (with
    0 only when `zero`)."""
    lines = list(lines)
    n = 0
    i = 0
    flip = {"c": "nc", "nc": "c", "z": "c", "nz": "nc"}
    while i < len(lines):
        out = None
        for kind, pats in (("stack", _SCMP_STACK_RIGHT), ("stack-left", _SCMP_STACK_LEFT),
                           ("reg", _SCMP_REG_RIGHT), ("reg-left", _SCMP_REG_LEFT),
                           ("stack-zero", _SCMP_STACK_ZERO), ("reg-zero", _SCMP_REG_ZERO)):
            got = _match_seq(lines, i, pats)
            if got is None:
                continue
            idx, g = got
            if kind.endswith("zero") and not (zero and _after_cmp_comment(lines, idx[0])):
                continue
            if kind == "stack":
                how, flipped, cc, target = g[0], int(g[1], 16) << 8 | int(g[2], 16), g[3], g[4]
            elif kind == "stack-left":
                how, flipped, cc, target = g[1], int(g[0], 16) << 8 | int(g[2], 16), g[3], g[4]
            elif kind == "reg":
                if g[2] != _OTHER_HALF[g[0]]:
                    continue
                hi_reg, flipped, cc, target = g[0], int(g[1], 16) << 8 | int(g[3], 16), g[4], g[5]
            elif kind == "reg-left":
                if g[3] != _OTHER_HALF[g[1]]:
                    continue
                hi_reg, flipped, cc, target = g[1], int(g[0], 16) << 8 | int(g[2], 16), g[4], g[5]
            elif kind == "stack-zero":
                how, flipped, cc, target = g[0], 0x8000, g[1], g[2]
            else:
                hi_reg, flipped, cc, target = g[0], 0x8000, g[1], g[2]
            if kind.endswith("left"):
                k = (flipped + 1) & 0xFFFF        # K < x is x >= K + 1
                jump = cc
            else:
                k = flipped
                jump = flip[cc]
            if not k:
                continue                          # never carries
            if kind.startswith("stack"):
                # the first line `<hl>` matched, or the `ld a,(hl)` after it
                seat = _seat_hl(lines, idx[1] if kind == "stack-left" else idx[0], how)
                if seat is None:
                    continue
                hi_off, seat_lines = seat
                fetch = seat_lines + ["\tld\te,(hl)", "\tinc\thl", "\tld\ta,(hl)"]
                known = hi_off if kind == "stack-zero" else hi_off - 1
            else:
                fetch = [f"\tld\te,{_OTHER_HALF[hi_reg]}", f"\tld\ta,{hi_reg}"]
                known = _hl_stack_offset(lines, idx[0])
            out = fetch + ["\txor\ta,#0x80", "\tld\td,a", f"\tld\thl,#0x{(-k) & 0xFFFF:04X}",
                           "\tadd\thl,de", f"\tjp\t{jump},{target}"]
            break
        if out is None:
            i += 1
            continue
        lines[idx[0]:idx[-1] + 1] = out
        n += 1
        i = idx[0] + len(out) - 1
        if known is not None:
            _reseat_hl(lines, i, known)
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


def switch_chain(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop the reloads left in a long `switch` compare chain.

    Stock SDCC re-reads the switch variable before every `cp a,#n; jp z,L`
    of a sparse switch; the rules in lsdj.peep remove those reloads one
    chain position at a time (up to twelve, then by chaining), so a chain of
    twenty cases (WaveScreenInput's wButtons) keeps one. Here `ld hl,#G;
    ld a,(hl)` is dropped wherever the same pair already loaded `a` and only
    compares, conditional jumps and labels nothing jumps to lie between. The
    ROM's compiled code never reloads inside such a chain.

    A case whose test CSE shares with an earlier boolean (`xor a,a; or a,c;
    jp nz,L` in the middle of the chain: SynthScreenInput's `wButtons ==
    0x10`, also TableScreenInput and ProjScreenInput) clobbers `a` but
    not `hl`, so after it only the `ld a,(hl)` comes back (3 such sites; the
    7 `ld hl,nn` reloads after that test in the ROM load another global).

    A case value of type int (`case (int)9:`) widens the variable into a
    register pair for a 16-bit compare; inside the chain that copy is taken
    from `a` (`ld c,a`, InstrScreenInput's kit fields 9/11/13), not reread
    through `hl`."""
    out: list[str] = []
    n = 0
    scope = 0
    cur: str | None = None            # the global `ld hl,#G` loaded into a and hl
    hl_g: str | None = None           # G that hl still holds after `a` was clobbered
    i = 0
    while i < len(lines):
        raw = lines[i]
        t = _strip(raw).strip()
        if not t:
            out.append(raw)
            i += 1
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
                cur = hl_g = None
            elif f"{scope}:{t[:-1]}" in jump_targets:
                cur = hl_g = None
            out.append(raw)
            i += 1
            continue
        m = re.fullmatch(r"ld\s+hl,(#_\w+(?: \+ \d+)?)", t)
        if m:
            k = i + 1
            while k < len(lines) and not _strip(lines[k]).strip():
                k += 1
            nxt = _strip(lines[k]).strip() if k < len(lines) else ""
            widen = re.fullmatch(r"ld\s+([bcde]),\(hl\)", nxt)
            if widen and cur == m.group(1):
                # an int-typed case widens the variable `a` still holds
                n += 1
                out.extend(l for l in lines[i + 1:k])
                out.append(f"\tld\t{widen.group(1)},a")
                cur = None
                i = k + 1
                continue
            if nxt == "ld\ta,(hl)":
                if cur == m.group(1):
                    n += 1
                    out.extend(l for l in lines[i + 1:k])   # keep comments
                    i = k + 1
                    continue
                if hl_g == m.group(1):
                    n += 1                  # hl is still there: reload a only
                    cur, hl_g = m.group(1), None
                    i += 1
                    continue
                cur, hl_g = m.group(1), None
                out.append(raw)
                i += 1
                continue
            cur = hl_g = None
            out.append(raw)
            i += 1
            continue
        widen = re.fullmatch(r"ld\s+([bcde]),\(hl\)", t)
        if widen and cur is not None:
            n += 1                          # the same widening once a rule dropped `ld hl`
            out.append(f"\tld\t{widen.group(1)},a")
            cur = None
            i += 1
            continue
        if cur is not None and (t == "xor\ta,a" or re.fullmatch(r"or\s+a,[bcde]", t)):
            hl_g, cur = cur, None
        elif hl_g is not None and not (t == "xor\ta,a" or re.fullmatch(r"or\s+a,[bcde]", t)
                                       or re.fullmatch(r"jp\s+n?z,\S+", t)):
            hl_g = None
        if cur is not None and not (t in ("ld\ta,(hl)", "or\ta,a") or re.fullmatch(r"cp\s+a,#\S+", t)
                                    or re.fullmatch(r"jp\s+n?z,\S+", t)):
            cur = None
        out.append(raw)
        i += 1
    return out, n


_ALU = {"add", "adc", "sub", "sbc", "and", "or", "xor", "inc", "dec", "rla", "rra", "rlca",
        "rrca", "rl", "rr", "sla", "sra", "srl", "swap", "cpl"}


def _rmw_store(out: list[str], r: str) -> bool:
    """`ld a,(hl); <alu>; ld r,a` right before `ld (hl),r`: a variable
    incremented or decremented in place, as `while (--i)` compiles. SDCC's
    pre-decrement yields the variable itself, and LSDj's compiler rereads it
    for the test (`xor a,a; or a,(hl)`: SortBookmarks; no ROM site of this
    shape tests the register instead)."""
    tail = [t for t in (_strip(l).strip() for l in out) if t][-3:]
    if len(tail) < 3:
        return False
    alu, alu_ops = _ops(tail[1])
    return (tail[0] == "ld\ta,(hl)" or _ops(tail[0]) == ("ld", ["a", "(hl)"])) \
        and alu in _ALU and (alu_ops[:1] == ["a"] or (alu in ("inc", "dec") and alu_ops == ["a"])) \
        and _ops(tail[2]) == ("ld", [r, "a"])


def _forget_reg(eq: set[str], via: dict[str, str], r: str) -> None:
    """Register r changed: drop it from what `a` equals, and every register
    whose equality was known only through r (`ld b,c` after `ld c,a`)."""
    eq.discard(r)
    via.pop(r, None)
    for d, src in list(via.items()):
        if src == r:
            _forget_reg(eq, via, d)


def _after_word_zero_test(out: list[str]) -> bool:
    """The last three instructions emitted are `ld a,(hl+); or a,(hl); jp cc,L`
    from a compare (genCmpEq). genIfx's test of a word is the other way
    round (see ifx_word_hl) and does not count."""
    seq = []
    k = len(out)
    while k > 0:
        k -= 1
        t = _strip(out[k]).strip()
        if not t or t.endswith(":"):
            continue              # (a label the rules left unreferenced)
        seq.append(t)
        if len(seq) == 3:
            break
    if len(seq) < 3:
        return False
    if not (re.fullmatch(r"jp\s+(nz|z),\S+", seq[0]) is not None and seq[1] == "or\ta,(hl)"
            and seq[2] == "ld\ta,(hl+)"):
        return False
    while k > 0:                  # the code generator that emitted the test
        k -= 1
        m = re.match(r"; (gen\w+)", out[k])
        if m:
            return m.group(1) != "genIfx"
    return True


def track_a(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop loads of values the register already holds: `ld a,X` / `xor a,a`
    when `a` equals X, `ld r,a` when r already equals `a`, `ld hl,...` /
    `lda hl,N(sp)` when `hl` already points there, and `ld a,(hl)` right
    after an ALU result was stored through `hl` (see above)."""
    out: list[str] = []
    eq: set[str] = set()          # what `a` equals: "c", "#0x05", "(hl)"
    via: dict[str, str] = {}      # register -> the register its equality came through
    pair_half: set[str] = set()   # b/c holding half of a 16-bit call result (`ld c,d; ld b,e`)
    hl = None                     # ("stk", N - level) | ("imm", text) | None
    level = 0                     # bytes pushed since the block began
    a_alu = False                 # `a` holds a fresh ALU result
    hl_reg = None                 # b..e the byte at (hl) equals (`ld (hl),e` / `ld e,(hl)`)
    hl_copies: set[str] = set()   # other registers copied from that byte (`ld c,(hl)` -> `ld c,e`)
    hl_is_a = False               # the byte at (hl) is `a`, stored by `ld (hl),a` (not a pointer read)
    a_ptr = False                 # `a` was loaded through de/bc (`ld a,(de)`)
    a_pair = None                 # that pair ("de"/"bc") while the byte is still `a`
    dropped = 0
    code = [i for i, l in enumerate(lines) if _strip(l).strip()]
    nxt = {code[k]: code[k + 1] for k in range(len(code) - 1)}
    scope = 0

    def forget_hl() -> None:
        nonlocal hl, hl_reg, hl_is_a
        hl = None
        hl_reg = None
        hl_copies.clear()
        hl_is_a = False
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
                via.clear()
                forget_hl()
            elif f"{scope}:{t[:-1]}" in jump_targets:
                eq, a_alu = set(), False
                via.clear()
                forget_hl()
            out.append(raw)
            continue
        mn, ops = _ops(t)
        # a 16-bit call result copied into bc (`ld c,d; ld b,e`): the ROM's
        # one reread of a byte just stored from a register (`ld (hl),b; ld
        # a,(hl)`, OpenNameEditor's StrLength result) stores such a half;
        # the 46 sites that reuse the register stored a byte value
        for r in _written(mn, ops):
            pair_half.discard(r)
            hl_copies.discard(r)
        if mn == "ld" and ops in (["c", "d"], ["b", "e"]):
            prev = next((_strip(l).strip() for l in reversed(out) if _strip(l).strip()), "")
            if (ops == ["b", "e"] and prev == "ld\tc,d") or (ops == ["c", "d"] and prev == "ld\tb,e"):
                pair_half |= {"b", "c"}
        # --- (hl) == a after `ld (hl),a`: a test of that byte is `or a,a` (169
        # ROM sites; the value from an ALU op or a merge label), except that a
        # value read through a pointer (`x = *p; if (x)`) is reread: `xor a,a;
        # or a,(hl)` (27 sites, each after `ld a,(de)` / `ld a,(bc)`)
        hl_was_a = hl_is_a
        w_all = _written(mn, ops)
        if hl_is_a and ("a" in w_all or {"h", "l"} & w_all or mn in _WRITES_ALL
                        or (mn in ("jp", "jr") and len(ops) == 1)
                        or (mn == "ld" and ops and ops[0].startswith("(") and ops != ["(hl)", "a"])
                        or (mn in ("inc", "dec") and ops == ["(hl)"])):
            hl_is_a = False
        if mn == "ld" and ops == ["(hl)", "a"]:
            hl_is_a = not a_ptr
        if "a" in w_all:
            a_ptr = mn == "ld" and len(ops) == 2 and ops[1] in ("(de)", "(bc)")
            a_pair = ops[1][1:3] if a_ptr else None
        elif a_pair is not None and set(a_pair) & w_all:
            # the byte in `a` is remembered as "the byte at (de)", so its
            # copies are forgotten once the pair is rewritten: `ld a,(de);
            # ld c,a; ld e,(hl); inc hl; ld d,(hl); ld a,c; ld (de),a` keeps
            # the reload (SynthFilterPass, FileFormatBuf), while `ld a,(de);
            # ld b,a; cp a,#n; jp z; ld a,b` drops it (UnhighlightPhraseCell)
            a_pair = None
            eq = set()
        # --- (hl) == a register: after `ld (hl),e` (or a load) the byte at hl is e until
        # either changes (183 ROM sites reread it as `ld a,e`, 1 from (hl))
        if hl_reg is not None and not (mn == "ld" and len(ops) == 2 and ops[1] == "(hl)"):
            # (a load from (hl) keeps the equality whatever it writes; so does
            # a store through de/bc: 3 ROM sites copy the register after one,
            # none rereads -- StartScreenPlayback's `wChanActive[wSongCol] = 1;
            # wPlayStartChan = wSongCol` is `ld c,(hl); ...; ld (de),a; ld a,c`).
            # A conditional jump ends it for a global: FxCmd_E_Pu1/Pu2/Noi's
            # `wEnvSpeedPu1 = v; if (v) wEnvCounterPu1 = wEnvSpeedPu1;` reread
            # it (`ld (hl),c; or a,c; jp z; ld a,(hl)`, 3 sites), while a stack
            # slot keeps it (PhraseScreenInput's `ld (hl),e; ...; jp c; ld a,e`)
            w = _written(mn, ops)
            if hl_reg in w or {"h", "l"} & w or mn in _WRITES_ALL \
                    or (mn in ("jp", "jr") and (len(ops) == 1 or (hl is not None and hl[0] == "imm"))) \
                    or (mn == "ld" and ops and ops[0].startswith("(") and ops[0] not in ("(de)", "(bc)")) \
                    or (mn in ("inc", "dec") and ops == ["(hl)"]):
                hl_reg = None
                hl_copies.clear()
        if mn == "ld" and len(ops) == 2 and ops[0] == "(hl)" and ops[1] in ("b", "c", "d", "e"):
            eq.discard("(hl)")
            hl_reg = None if _rmw_store(out, ops[1]) or ops[1] in pair_half else ops[1]
            hl_copies.clear()
            out.append(raw)
            continue
        if mn == "ld" and len(ops) == 2 and ops[1] == "(hl)" and ops[0] in ("l", "h") \
                and hl_reg is not None and hl_reg in eq and "(hl)" not in eq:
            # a table index loaded from the byte just stored from a register
            # that `a` still equals: `ld (hl),e; ld a,e; cp; ...; ld l,a`
            # (CountFileBlocks)
            raw = raw.replace("(hl)", "a", 1)
            forget_hl()
            eq.add(ops[0])
            out.append(raw)
            continue
        if mn == "ld" and len(ops) == 2 and ops[1] == "(hl)" and ops[0] in ("b", "c", "d", "e"):
            if hl_reg is not None and "(hl)" not in eq:
                # the byte at hl is still in a register: LSDj's compiler
                # copied it (`ld (hl),e; add sp,3; ld c,e`: 18 + 2 + 3 ROM
                # sites) or, for the same register, skipped the reload
                # (FileCalcCompressedSize's result; the ROM never reloads
                # `ld e,(hl)` after `ld (hl),e`)
                if hl_reg == ops[0]:
                    dropped += 1
                    continue
                raw = raw.replace("(hl)", hl_reg, 1)
                hl_copies.add(ops[0])
                mn, ops = _ops(_strip(raw).strip())
            else:
                # a load sets up the same equality: after `ld c,(hl)` the ROM
                # never rereads the byte (171 `ld a,c`, 0 `ld a,(hl)`); stock
                # genCast's sign extension does (LoadSynthShape)
                hl_reg = ops[0]
                hl_copies.clear()
        # --- hl ---
        # hl is followed (for the `(hl)` reload below) but its own reloads
        # are left alone: whether LSDj's compiler kept hl across a compare
        # depends on the operand kind (a named local vs a spill slot), which
        # the text doesn't show -- the textual `!=` rule covers globals.
        if mn == "lda" and len(ops) == 2 and ops[0] == "hl":
            m = re.fullmatch(r"(-?\d+)\(sp\)", ops[1])
            if m:
                want = int(m.group(1)) - level
                if hl is not None and hl[0] == "stk" and hl[1] - want == 1 and _after_word_zero_test(out):
                    # a 16-bit local tested for zero (`ld a,(hl+); or a,(hl);
                    # jp nz`) and then assigned: the ROM steps hl back
                    # (GetInstrEnvBytes' `if (total == 0) total = 1`) where
                    # stock SDCC reloads it
                    raw = "\tdec\thl"
                    hl = ("stk", want)
                    hl_reg = None
                    hl_copies.clear()
                    hl_is_a = False
                    eq.discard("(hl)")
                    out.append(raw)
                    continue
                forget_hl()
                hl = ("stk", want)
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
            if hl_reg is not None and (ops[1] == hl_reg or ops[1] in hl_copies):
                # `ld (hl),e; ld a,e`: `a` is the byte at hl too; so after a
                # copy of it (`ld (hl),e; ld c,e; ld a,c; bit 6,a; jp nz`: the
                # byte is not reread, InstrScreenInput's kit flag toggles)
                eq.add("(hl)")
        elif mn == "ld" and ops == ["a", "(hl)"]:
            if "(hl)" in eq:
                dropped += 1
                continue
            if hl_reg is not None:
                raw = raw.replace("(hl)", hl_reg, 1)
                eq, a_alu = {"(hl)", hl_reg}, False
            else:
                eq, a_alu = {"(hl)"}, False
        elif mn == "xor" and ops == ["a", "a"]:
            n = nxt.get(i)
            follow = _ops(_strip(lines[n]).strip()) if n is not None else ("", [])
            if follow[0] == "or" and follow[1] == ["a", "(hl)"] and hl_was_a and n is not None:
                dropped += 1          # `a` is unchanged: what it equals still holds
                lines[n] = lines[n].replace("(hl)", "a", 1)
                continue
            if follow[0] == "or" and len(follow[1]) == 2 and follow[1][0] == "a" and follow[1][1] in eq:
                dropped += 1          # `or a,r` alone: `a` already equals r
                if follow[1][1] == "(hl)" and n is not None:
                    # the byte at hl is `a` itself: the test is `or a,a` (169
                    # after `ld (hl),a` in the ROM, never `or a,(hl)` alone)
                    lines[n] = lines[n].replace("(hl)", "a", 1)
                continue
            if follow[0] == "or" and follow[1] == ["a", "(hl)"] and hl_reg is not None and n is not None:
                # testing the byte just stored from a register: `ld a,e; or a,e`
                # (31 + 5 such tests in the ROM, each after `ld (hl),e` / `ld (hl),b`)
                lines[n] = lines[n].replace("(hl)", hl_reg, 1)
                out.append(f"\tld\ta,{hl_reg}")
                eq, a_alu = {hl_reg, "(hl)"}, False
                continue
            eq, a_alu = {"#0x00"}, False
        elif mn == "ld" and len(ops) == 2 and ops[1] == "(hl)" and ops[0] in ("b", "c", "d", "e", "l", "h") \
                and "(hl)" in eq:
            # `ld c,(hl)` while `a` still holds (hl): LSDj's compiler used `ld c,a`
            # (3 sites after `ld a,(hl); cp; jp`, none rereading; `ld l,a` for a
            # table index likewise, SynthWriteWaveCol)
            raw = raw.replace("(hl)", "a", 1)
            if ops[0] in ("l", "h"):
                forget_hl()
            eq.add(ops[0])
        elif mn == "ld" and len(ops) == 2 and ops[1] == "a" and ops[0] in _REGS:
            if ops[0] in eq:
                dropped += 1          # `ld c,a` when `a` already equals c: a no-op
                continue
            eq.add(ops[0])
            via.pop(ops[0], None)
            if ops[0] in "hl":
                forget_hl()
        elif mn == "ld" and ops == ["(hl)", "a"]:
            eq.discard("(hl)")
            if (a_alu or not a_ptr) and hl is not None:
                # a value stored through hl is not reread (`ld a,(hl)` goes),
                # unless it came through a pointer: all 43 such reloads in the
                # ROM follow `ld a,(de)` / `ld a,(bc)`
                eq.add("(hl)")
        elif mn == "ld" and len(ops) == 2 and ops[0] == "a":
            eq, a_alu = set(), False  # a loaded from memory (a pointer read keeps its reloads)
            if ops[1] in ("(hl+)", "(hl-)"):
                if hl is not None and hl[0] == "stk":
                    hl = ("stk", hl[1] + (1 if ops[1] == "(hl+)" else -1))
                    hl_reg = None
                    hl_copies.clear()
                    hl_is_a = False
                else:
                    forget_hl()
        elif mn in ("or", "and") and ops == ["a", "a"]:
            pass
        elif mn in _ALU and (not ops or ops[0] == "a"):
            eq, a_alu = set(), True
        elif mn in ("jp", "jr") and len(ops) == 2:
            # a conditional jump: `a` and `hl` survive the fall-through, but
            # after an equality test `a` no longer stands for the byte at hl:
            # SndPhraseHop's `*wPtrPhraseRow = wSeqNoteLatch; if
            # (wSeqNoteLatch) AdvanceCurChanCursors(wSeqNoteLatch);` reloads
            # it (`ld a,(hl); ld (de),a; or a,a; jp z; ld a,(hl)`, 2 sites,
            # none reusing it), like track_hram's `ldh` rereads; so does a
            # stack slot (PhraseScreenInput's `k = v & 0xF0; if (k)`: `ld
            # (hl),a; or a,a; jp z; ld a,(hl)`, 7 sites). A bit test of a
            # stack byte keeps it: InstrScreenInput's kit flag toggles test a
            # copy of the byte just stored (`ld (hl),e; ld c,e; ld a,c; bit
            # 6,a; jp nz`) and OR straight into it (4 sites)
            prev = next((_strip(l).strip() for l in reversed(out) if _strip(l).strip()), "")
            if ops[0] in ("z", "nz") and not (hl is not None and hl[0] == "stk"
                                              and prev.startswith("bit\t")):
                eq.discard("(hl)")
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
        elif mn == "ld" and ops[:1] == ["(hl)"] and len(ops) == 2 and ops[1].startswith("#"):
            # a literal stored through hl (`g = n`) leaves `a` standing for n
            # and nothing else: ResetWorkState's `wWordClipCell0[0] = 1`
            # reloads `ld a,#1` after six `g = 0` stores, TogglePlayback's
            # `wSongPlaying = 0; hSoundChan = 0` keeps the 0 in `a` (the ROM's
            # two sites; the tracker apparently took the stored literal for
            # `a`'s value, kept here only when it already was)
            if ops[1] not in eq:
                eq, a_alu = set(), False
                via.clear()
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
            for r in regs:
                _forget_reg(eq, via, r)
            if mn == "ld" and len(ops) == 2 and dest in _REGS and ops[1] in eq:
                # `ld b,c` while `a` equals c: b equals it too, but only
                # through c -- `ld c,a; ld b,c; ld a,b` loses the reload
                # (DrawSpeedGlyph) while `ld c,a; ld b,c; ld c,#0; ld a,b`
                # keeps it (FindFreeBlock): the ROM's knowledge is pairwise
                eq.add(dest)
                via[dest] = ops[1]
            if regs & {"h", "l"}:
                forget_hl()
            if not regs and not dest.startswith("("):
                eq, a_alu = set(), False
                forget_hl()
        elif mn in ("sla", "sra", "srl", "rl", "rr", "rlc", "rrc", "swap") and ops and ops[0] in _REGS \
                and ops[0] != "a":
            # a shift of another register leaves `a` and the byte at hl alone
            # (CopyWaveSelection's `i >> 1` between `v |= t` and the push of v)
            _forget_reg(eq, via, ops[0])
            if ops[0] in "hl":
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


def track_hram(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Skip an `ldh` reload of an HRAM byte a register still holds.

    SDCC 2.2.1 rereads an `sfr` at every use: its CSE never substitutes a
    register for a direct-space global (`isGlobalInNearSpace`, an 8051
    optimisation applied to every port). LSDj's compiler skipped loading the
    byte into a register that already held it: `ldh a,(X); ld r,a` goes when
    r holds X, within a block or across a jump (HandleInputCombos' two
    `hSoundChan` indexes in one block, SyncTickLsdj's loop body,
    SndUpdateChan's D across `if (d)`). It never copied the byte over from
    another register: a reload into b while c holds it stays (the `+=` target
    in SndUpdateChan and HandleInputCombos). `a` itself serves a reread
    in straight-line code (FxCmd_F_Wav's `step = hFxValS`: stock genCast
    reads the byte again for the sign's `rla`; no compiled code in the ROM
    rereads a byte `a` holds before a jump) and across an ordering test
    (SyncTickLsdj's `hSoundChan < 4`: `cp #4; jp nc`); after an equality test
    the byte is reread (`cp; jp nz; ldh a,(X)`: SndUpdateChan,
    SndRetrigNote). An HRAM store, a
    call or a jump target ends the reuse; a store through a pointer loaded
    from memory (`ld e,(hl); inc hl; ld d,(hl); ld (de),a`) ends it for a
    byte read before a conditional jump (the `+=` in SndUpdateChan's D),
    not for one read in this block (StartSongPlayback's `*active = 1`); a
    store to an array element (`#arr + c` in de) ends neither."""
    out: list[str] = []
    holds: dict[str, str] = {}    # register -> the `(X)` it holds
    carried: set[str] = set()     # registers whose byte was read before a conditional jump
    a_ok = False                  # `a`'s byte has crossed an ordering test since its read
    a_straight = False            # `a`'s byte was read with no jump since
    de_loaded = False             # de was last loaded from memory (a pointer of unknown target)
    dropped = 0
    scope = 0
    code = [i for i, l in enumerate(lines) if _strip(l).strip()]
    nxt = {code[k]: code[k + 1] for k in range(len(code) - 1)}
    skip: set[int] = set()

    def overwrites_a(i: int | None) -> bool:
        if i is None:
            return False
        m2, o2 = _ops(_strip(lines[i]).strip())
        return (m2 in ("ld", "ldh") and len(o2) == 2 and o2[0] == "a" and o2[1] != "a"
                and not o2[1].startswith("(hl")) or (m2 == "xor" and o2 == ["a", "a"])

    def forget() -> None:
        nonlocal a_ok, a_straight
        holds.clear()
        carried.clear()
        a_ok = a_straight = False

    def a_dead_from(i: int | None) -> bool:
        """`a` is overwritten (as overwrites_a has it) before anything reads
        it: instructions that leave `a` alone are skipped (`ld de,#x; ld
        l,c; ld h,#0; add hl,de; ld a,l`, the address of a 16-bit index)."""
        while i is not None:
            t2 = _strip(lines[i]).strip()
            if t2.endswith(":"):
                return False
            if overwrites_a(i):
                return True
            m2, o2 = _ops(t2)
            if m2 in ("jp", "jr", "call", "ret", "reti", "rst") or "a" in _written(m2, o2):
                return False
            if m2 in ("add", "adc", "sub", "sbc", "and", "or", "xor", "cp") and (not o2 or o2[0] == "a"):
                return False
            if (m2 in ("ld", "ldh") and len(o2) == 2 and o2[1] == "a") or (m2 == "push" and o2 == ["af"]):
                return False
            if m2 in ("bit", "rl", "rr", "sla", "sra", "srl", "swap", "res", "set", "rlc", "rrc", "daa") \
                    and o2 and o2[-1] == "a":
                return False
            i = nxt.get(i)
        return False

    for idx, raw in enumerate(lines):
        if idx in skip:
            continue
        t = _strip(raw).strip()
        if not t:
            out.append(raw)
            continue
        if t.endswith(":"):
            if not _LOCAL.fullmatch(t[:-1]):
                scope += 1
                forget()
            elif f"{scope}:{t[:-1]}" in jump_targets:
                forget()
            out.append(raw)
            continue
        mn, ops = _ops(t)
        if mn == "ldh" and len(ops) == 2 and ops[0] == "a" and ops[1].startswith("("):
            src = ops[1]
            if holds.get("a") == src and a_ok:
                dropped += 1
                continue
            n1 = nxt.get(idx)
            if n1 is not None:
                m1, o1 = _ops(_strip(lines[n1]).strip())
                if m1 == "ld" and len(o1) == 2 and o1[1] == "a" and o1[0] in _REGS \
                        and holds.get(o1[0]) == src and a_dead_from(nxt.get(n1)):
                    skip.add(n1)          # `ldh a,(X); ld r,a` with r == (X): both go
                    dropped += 2
                    continue
            if holds.get("a") == src and a_straight:
                dropped += 1
                continue
            out.append(raw)
            holds["a"] = src
            carried.discard("a")
            a_ok = False
            a_straight = True
            continue
        if mn == "ldh" and len(ops) == 2 and ops[1] == "a":
            forget()                      # an HRAM store: forget every reuse
            out.append(raw)
            continue
        if mn in ("jp", "jr") and len(ops) == 2:
            a_ok = ops[0] in ("c", "nc") and "a" in holds
            a_straight = False
            carried.update(holds)
            out.append(raw)
            continue
        if mn in _WRITES_ALL or (mn in ("jp", "jr") and len(ops) == 1):
            forget()
            out.append(raw)
            continue
        if mn == "ld" and len(ops) == 2 and ops[0] == "(de)" and de_loaded:
            # a store through a pointer of unknown target ends the reuse of
            # a byte read before a jump (SndUpdateChan's D), not of one
            # read in this block (StartSongPlayback's `*active = 1`)
            for r in carried:
                holds.pop(r, None)
            carried.clear()
            out.append(raw)
            continue
        w = _written(mn, ops)
        if mn == "ld" and len(ops) == 2 and ops[1] == "a" and ops[0] in _REGS:
            if "a" in holds:
                holds[ops[0]] = holds["a"]
                if "a" in carried:
                    carried.add(ops[0])
                else:
                    carried.discard(ops[0])
            else:
                holds.pop(ops[0], None)
                carried.discard(ops[0])
        else:
            for r in w:
                holds.pop(r, None)
                carried.discard(r)
            if "a" in w:
                a_ok = False
        if {"d", "e"} & w:
            de_loaded = mn == "ld" and len(ops) == 2 and ops[0] in ("d", "e") \
                and ops[1] in ("(hl)", "(hl+)", "(hl-)")
        out.append(raw)
    return out, dropped


def track_e(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld e,r` while `e` already equals r (b, c or d)."""
    return _track_half(lines, jump_targets, "e", ("b", "c", "d"))


def track_d(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld d,r` while `d` already equals r (b, c or e): the second
    `ld e,b; ld d,c` fetch of a pointer kept in bc (ProcessLinkQueue's
    `wKbdChanState[1]` read then cleared) loses both halves in the ROM."""
    return _track_half(lines, jump_targets, "d", ("b", "c", "e"))


def _track_half(lines: list[str], jump_targets: set[str], reg: str,
                sources: tuple[str, ...]) -> tuple[list[str], int]:
    out: list[str] = []
    eq: set[str] = set()          # registers equal to `reg`
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
        if mn == "ld" and len(ops) == 2 and ops[0] == reg and ops[1] in sources:
            if ops[1] in eq:
                dropped += 1
                continue
            eq = {ops[1]}
        elif mn == "ld" and len(ops) == 2 and ops[1] == reg and ops[0] in sources:
            eq.add(ops[0])
        elif mn in _WRITES_ALL or (mn in ("jp", "jr") and len(ops) == 1):
            eq = set()
        else:
            w = _written(mn, ops)
            if reg in w:
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


def track_de(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld de,#X` while `de` already holds X.

    SDCC 2.2.1's `fetchLitPair` remembers the literal last loaded into a
    pair but the `de` half of that is compiled out (`IS_GB && pairId ==
    PAIR_DE && 0`), so a global array's base is reloaded for `a[0] = x;
    a[1] = y` (the second load feeds `ld hl,#1; add hl,de`). LSDj's compiler
    kept the first load: the ROM has no `ld de,#X ... ld de,#X` pair within a
    basic block while `d`/`e` are untouched (LoadFile). A call,
    an unconditional jump or a jump target ends the knowledge."""
    out: list[str] = []
    held: str | None = None
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
                held = None
            elif f"{scope}:{t[:-1]}" in jump_targets:
                held = None
            out.append(raw)
            continue
        mn, ops = _ops(t)
        if mn == "ld" and len(ops) == 2 and ops[0] == "de" and ops[1].startswith("#"):
            if held == ops[1]:
                dropped += 1
                continue
            held = ops[1]
        elif mn in _WRITES_ALL or (mn in ("jp", "jr") and len(ops) == 1):
            held = None
        elif {"d", "e"} & _written(mn, ops):
            held = None
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
    """Any `ld r,...` overwrite makes an earlier unread `ld r,a` dead in
    LSDj's code: no such store survives in the ROM whatever overwrote it
    (`ld r,a`, `ld r,#n`, `ld r,(hl)`, `ld r,r2`...). A store killed by
    `pop bc` does survive."""
    return mn == "ld" and len(ops) == 2 and ops[0] == r


_DEAD_LOAD_SRC = re.compile(r"a|b|c|d|e|h|l|\(hl\)|\(bc\)|\(de\)|#.*")


def dead_stores(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld r,X` (r = b, c, d, e; X a register, `(hl)`, `(bc)`, `(de)` or
    a literal -- a load without side effects) overwritten unread within its
    block. The ROM has none of any kind: of its 2,411 `ld r,(hl)` and 1,367
    `ld r,#n` not one is rewritten before it is read (stock SDCC 2.2.1 emits
    them, e.g. the high byte of a 16-bit `&` whose result is then narrowed
    to a byte: FileUpgradeV0Kit)."""
    code = [(i, _ops(_strip(l).strip())) for i, l in enumerate(lines) if _strip(l).strip()]
    scope, scopes = 0, {}
    for i, (mn, ops) in code:
        t = _strip(lines[i]).strip()
        if t.endswith(":") and not _LOCAL.fullmatch(t[:-1]):
            scope += 1
        scopes[i] = scope
    drop: set[int] = set()
    for k, (i, (mn, ops)) in enumerate(code):
        if not (mn == "ld" and len(ops) == 2 and ops[0] in ("b", "c", "d", "e")
                and _DEAD_LOAD_SRC.fullmatch(ops[1])):
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


def dead_hl_steps(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `inc hl` / `dec hl` whose hl is rewritten (`ld hl,`, `lda hl,`,
    `pop hl`) before any read within the block: the step left behind a
    dead `ld r,(hl)`. The ROM has none of its 3,312 `inc/dec hl` followed by
    an hl rewrite unread."""
    code = [(i, _ops(_strip(l).strip())) for i, l in enumerate(lines) if _strip(l).strip()]
    scope, scopes = 0, {}
    for i, (mn, ops) in code:
        t = _strip(lines[i]).strip()
        if t.endswith(":") and not _LOCAL.fullmatch(t[:-1]):
            scope += 1
        scopes[i] = scope
    drop: set[int] = set()
    for k, (i, (mn, ops)) in enumerate(code):
        if not (mn in ("inc", "dec") and ops == ["hl"]):
            continue
        for j, (mn2, ops2) in code[k + 1:]:
            t = _strip(lines[j]).strip()
            if t.endswith(":"):
                if not _LOCAL.fullmatch(t[:-1]) or f"{scopes[j]}:{t[:-1]}" in jump_targets:
                    break
                continue
            if mn2 in ("jp", "jr", "call", "ret", "reti", "rst", "halt", "stop"):
                break
            text = " ".join(ops2)
            if (mn2 == "ld" and ops2 and ops2[0] == "hl") or (mn2 == "lda" and ops2 and ops2[0] == "hl") \
                    or (mn2 == "pop" and ops2 == ["hl"]):
                drop.add(i)
                break
            if re.search(r"\b(hl|h|l)\b", text) or "(hl" in text:
                break
    return [l for i, l in enumerate(lines) if i not in drop], len(drop)


# --- a bitwise result chained into the next literal op ----------------------
#
# LSDj's compiler kept the result of `x & n` (or `|`, `^`) in `a` when the
# next operation took it as its left operand with a literal on the right:
# the ROM has `and a,#0xF9; or a,#0x01` (FileUpgradeV3Kit, V3Pulse,
# SyncSetSendMode, the sound engine's `and a,#0x0F; xor a,#0x0F`) and never
# `and a,#n; ld r,a; or a,#m`. Stock SDCC 2.2.1 will not leave it in `a`
# (opPreservesA wants a symbol as the other operand), so the temporary gets
# a register, and once track_a has dropped the reload only its store is
# left. That store is dropped when the register is dead on every path that
# follows: rewritten or popped before any read, or the function returns. `b`
# and `c` survive calls (the callee saves them); `d` and `e` do not.

_BITWISE_LIT = re.compile(r"(?:and|or|xor)\s+a,#")


def _reg_dead_after(code: list[tuple[int, str]], k: int, r: str,
                    labels: dict[str, int], seen: set[str]) -> bool:
    """Register r is not read before it is rewritten on any path from
    code[k] on (code: one function's non-blank lines)."""
    while k < len(code):
        t = code[k][1]
        if t.endswith(":"):
            k += 1
            continue
        mn, ops = _ops(t)
        if mn in ("ret", "reti"):
            if not ops:
                return True
        elif mn == "call":
            if r in ("d", "e"):
                return True
        elif mn in ("jp", "jr"):
            target = ops[-1] if ops else ""
            if target not in labels:
                return False                  # jp (hl), or out of the function
            if target not in seen:
                seen.add(target)
                if not _reg_dead_after(code, labels[target], r, labels, seen):
                    return False
            if len(ops) == 1:
                return True
        elif mn in ("rst", "halt", "stop"):
            return False
        elif _reads_reg(mn, ops, r):
            return False
        elif mn == "ld" and len(ops) == 2 and ops[0] == r:
            return True
        elif mn == "pop" and ops and r in _PAIR_OF.get(ops[0], ""):
            return True
        k += 1
    return True


def chained_bitwise(lines: list[str]) -> tuple[list[str], int]:
    """Drop the dead `ld r,a` between two bitwise ops with literals."""
    drop: set[int] = set()
    bounds = [i for i, l in enumerate(lines)
              if _strip(l).strip().endswith(":") and not _LOCAL.fullmatch(_strip(l).strip()[:-1])]
    bounds.append(len(lines))
    for lo, hi in zip(bounds, bounds[1:]):
        code = [(i, _strip(lines[i]).strip()) for i in range(lo + 1, hi) if _strip(lines[i]).strip()]
        labels = {t[:-1]: k for k, (_, t) in enumerate(code) if t.endswith(":")}
        for k in range(1, len(code) - 1):
            mn, ops = _ops(code[k][1])
            if not (mn == "ld" and len(ops) == 2 and ops[0] in ("b", "c", "d", "e") and ops[1] == "a"):
                continue
            if not (_BITWISE_LIT.match(code[k - 1][1]) and _BITWISE_LIT.match(code[k + 1][1])):
                continue
            if _reg_dead_after(code, k + 1, ops[0], labels, set()):
                drop.add(code[k][0])
    return [l for i, l in enumerate(lines) if i not in drop], len(drop)


def dead_mem_stores(lines: list[str], jump_targets: set[str]) -> tuple[list[str], int]:
    """Drop `ld (hl),X` when the next thing that touches `hl` or `(hl)` in its
    block is another `ld (hl),Y`: the slot is rewritten unread. SDCC emits
    the pair when it spills a call result whose reads then become `ld a,e`
    (track_a) and the variable is reassigned at once (`v = f(); v |= v << 4`
    in FileUpgradeV4); the ROM keeps no such store."""
    code = [(i, _ops(_strip(l).strip())) for i, l in enumerate(lines) if _strip(l).strip()]
    scope, scopes = 0, {}
    for i, (mn, ops) in code:
        t = _strip(lines[i]).strip()
        if t.endswith(":") and not _LOCAL.fullmatch(t[:-1]):
            scope += 1
        scopes[i] = scope
    drop: set[int] = set()
    for k, (i, (mn, ops)) in enumerate(code):
        if not (mn == "ld" and len(ops) == 2 and ops[0] == "(hl)"):
            continue
        for j, (mn2, ops2) in code[k + 1:]:
            t = _strip(lines[j]).strip()
            if t.endswith(":"):
                if not _LOCAL.fullmatch(t[:-1]) or f"{scopes[j]}:{t[:-1]}" in jump_targets:
                    break
                continue
            if mn2 in ("jp", "jr", "call", "ret", "reti", "rst", "halt", "stop", "push", "pop"):
                break
            if mn2 == "ld" and len(ops2) == 2 and ops2[0] == "(hl)" and "hl" not in ops2[1]:
                drop.add(i)
                break
            if any("hl" in o or o in ("h", "l") for o in ops2):
                break                         # hl read, written or dereferenced
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
