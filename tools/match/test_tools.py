#!/usr/bin/env python3
"""Regressions for the Track 2 helper tools: rom_grep's pattern syntax,
code_kind's never-from-SDCC operand test, match_function's --score
address normalisation, and to_rgbds's reading of as-gbz80 listings and
asxxxx operands.

Run with: python3 tools/match/test_tools.py
"""

import importlib
import unittest

rom_grep = importlib.import_module("rom_grep")
code_kind = importlib.import_module("code_kind")
match_function = importlib.import_module("match_function")
to_rgbds = importlib.import_module("to_rgbds")


class PatternTests(unittest.TestCase):
    def test_tokens(self):
        pat = rom_grep.compile_pattern("21 (?? ??) 7e b7 c2|ca ?? ?? 21 \\1")
        self.assertTrue(pat.search(bytes.fromhex("21 66 cb 7e b7 ca 2e 62 21 66 cb")))
        self.assertIsNone(pat.search(bytes.fromhex("21 66 cb 7e b7 ca 2e 62 21 67 cb")))
        self.assertIsNone(pat.search(bytes.fromhex("21 66 cb 7e b7 c3 2e 62 21 66 cb")))

    def test_range_and_wildcard_match_any_byte(self):
        pat = rom_grep.compile_pattern("40-4f ?? 0a")
        self.assertTrue(pat.fullmatch(bytes.fromhex("4f 0a 0a")))   # `??` matches a newline byte
        self.assertIsNone(pat.fullmatch(bytes.fromhex("50 00 0a")))


class AbsoluteOperandTests(unittest.TestCase):
    def test_absolute_forms_are_asm_marks(self):
        for t in ("ld a, [wKitHalfSpeed]", "ld [$c000], a", "ld [wStack], sp"):
            self.assertTrue(code_kind.ABSOLUTE.match(t), t)

    def test_register_forms_are_not(self):
        for t in ("ld a, [hl]", "ld a, [hl+]", "ld [hl-], a", "ld a, [de]", "ld [bc], a",
                  "ld a, [c]", "ldh a, [rSTAT]", "ld hl, wFoo"):
            self.assertFalse(code_kind.ABSOLUTE.match(t), t)


class ScoreTests(unittest.TestCase):
    def test_moved_local_jump_target_is_ignored(self):
        # `jp z,$4005` vs `jp z,$4006`: the same branch once the code moved
        rom = bytes.fromhex("af ca 05 40 00 c9")
        ours = bytes.fromhex("af ca 06 40 00 c9")
        self.assertEqual(match_function.diff_score(ours, rom, 0x4000, 0x100), (1, 0))

    def test_other_differences_count(self):
        rom = bytes.fromhex("af cd 00 20 c9")       # call $2000: outside the function
        ours = bytes.fromhex("af cd 10 20 c9")
        self.assertEqual(match_function.diff_score(ours, rom, 0x4000, 0x100), (1, 1))


class ListingTests(unittest.TestCase):
    # as-gbz80 listing rows: address in columns 3-6, up to six bytes, the
    # unit.asm line number in 25-30; longer data wraps onto unnumbered rows
    LST = "\n".join([
        "   1294                    5746 ___CloneInstrAtCursor_end:",
        "   1294 43 41 4E 4E 4F 54  5748 \t.ascii \"CANNOT CLONE\"",
        "        20 43 4C 4F 4E 45",
        "   12A0 00                 5749 \t.db 0x00",
        "                            240 ; genLabel",
    ])

    def test_six_byte_rows_keep_their_line_number(self):
        addrs = to_rgbds.listing_addrs(self.LST)
        self.assertEqual(addrs[5748], 0x1294)
        self.assertEqual(addrs[5749], 0x12A0)
        self.assertEqual(addrs[5746], 0x1294)
        self.assertNotIn(240, addrs)                 # no address on a comment row


class OperandTests(unittest.TestCase):
    def translator(self):
        t = to_rgbds.Translator.__new__(to_rgbds.Translator)
        t.func, t.rename, t.equates, t.guards = "F", {}, {}, {}
        t.syms = {"hFoo": (None, 0xFF90)}
        return t

    def test_operands(self):
        t = self.translator()
        self.assertEqual(t.operand("(hl+)"), "[hl+]")
        self.assertEqual(t.operand("#0x12"), "$12")
        self.assertEqual(t.operand("00105$"), "F.L00105")
        self.assertEqual(t.expr("#<(00105$)"), "LOW(F.L00105)")

    def test_unknown_names_become_guarded_equates(self):
        t = self.translator()
        self.assertEqual(t.operand("(_hFoo+0)"), "[hFoo+0]")
        self.assertEqual(t.guards, {"hFoo": "$FF90"})
        with self.assertRaises(ValueError):
            t.operand("_NoSuchName")


if __name__ == "__main__":
    unittest.main()
