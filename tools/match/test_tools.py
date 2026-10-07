#!/usr/bin/env python3
"""Regressions for the Track 2 helper tools: rom_grep's pattern syntax,
code_kind's never-from-SDCC operand test and match_function's --score
address normalisation.

Run with: python3 tools/match/test_tools.py
"""

import importlib
import unittest

rom_grep = importlib.import_module("rom_grep")
code_kind = importlib.import_module("code_kind")
match_function = importlib.import_module("match_function")


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


if __name__ == "__main__":
    unittest.main()
