#!/usr/bin/env python3
"""Regressions for the Track 2 helper tools: rom_grep's pattern syntax,
code_kind's never-from-SDCC operand test, match_function's --score
address normalisation, to_rgbds's reading of as-gbz80 listings and asxxxx
operands, and which ROM-address operands disasm.py / to_rgbds.py print as
labels.

Run with: python3 tools/match/test_tools.py
"""

import importlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))      # tools/

rom_grep = importlib.import_module("rom_grep")
code_kind = importlib.import_module("code_kind")
match_function = importlib.import_module("match_function")
to_rgbds = importlib.import_module("to_rgbds")
disasm = importlib.import_module("disasm")


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


class PointerTests(unittest.TestCase):
    HOME = {0x0BFE: "FarCallByte", 0x2F21: "DrawString"}

    @staticmethod
    def rows(*items):
        return [(a, b"", t, r) for a, t, r in items]

    def test_banked_call_target_is_in_the_callee_bank(self):
        rows = self.rows((0x5855, "ld bc, $4157", None), (0x5858, "push bc", None),
                         (0x5859, "ld a, $1f", None), (0x585B, "push af", None),
                         (0x585C, "inc sp", None), (0x585D, "call $0bfe", 0x0BFE))
        ptrs, _ = disasm.rom_pointers(rows, 0x03, {}, self.HOME)
        self.assertEqual(ptrs, {0x5855: (0x1F, 0x4157)})

    def test_trampoline_bank_is_tracked(self):
        rows = self.rows((0x0EEA, "ld hl, $2000", None), (0x0EED, "ld [hl], $07", None),
                         (0x0EEF, "call $68a2", 0x68A2))
        ptrs, notes = disasm.rom_pointers(rows, 0, {}, self.HOME)
        self.assertEqual(ptrs, {0x0EEA: "rROMB0", 0x0EEF: (0x07, 0x68A2)})
        self.assertEqual(notes, [])

    def test_unknown_bank_is_a_note(self):
        rows = self.rows((0x20B6, "ld hl, $4000", None), (0x20B9, "ld a, [hl+]", None))
        ptrs, notes = disasm.rom_pointers(rows, 0, {}, self.HOME)
        self.assertEqual((ptrs, len(notes)), ({}, 1))

    def test_cmp16_constant_stays_a_number(self):
        rows = self.rows((0x47D5, "ld hl, $5000", None), (0x47D8, "add hl, de", None),
                         (0x47D9, "jp c, $4800", 0x4800))
        self.assertEqual(disasm.rom_pointers(rows, 0x01, {}, self.HOME)[0], {})

    def test_own_bank_pointer_and_mbc_write(self):
        rows = self.rows((0x45C4, "ld hl, $473e", None), (0x45C7, "push hl", None),
                         (0x45C8, "call $2f21", 0x2F21),
                         (0x45DA, "ld hl, $4000", None), (0x45DD, "ld [hl], $00", None))
        ptrs, _ = disasm.rom_pointers(rows, 0x1F, {}, self.HOME)
        self.assertEqual(ptrs, {0x45C4: (0x1F, 0x473E), 0x45DA: "rRAMB"})

    def test_small_numbers_from_other_banks_stay_numbers(self):
        # `ld de, $0200` in bank $1D computes $200 - x: not TimesTwoTable
        rows = self.rows((0x5ACB, "ld de, $0200", None), (0x5ACE, "dec hl", None))
        self.assertEqual(disasm.rom_pointers(rows, 0x1D, {}, {0x0200: "TimesTwoTable"})[0], {})

    def test_unlabelled_jr_is_relative(self):
        rows = [(0x14D6, b"\x30\xff", "jr nc, $14d7", 0x14D7)]
        self.assertEqual(disasm.render(rows, False, {}), "\tjr nc, @ + 1")


class TranslatedPointerTests(unittest.TestCase):
    def translator(self, source=""):
        t = to_rgbds.Translator.__new__(to_rgbds.Translator)
        t.named = to_rgbds.source_addresses(source, {0x6E0C: "TxtHelpPan"})
        return t

    def test_macro_named_address_stored_byte_wise(self):
        t = self.translator("#define TxtHelpPan         ((char *)0x6E0C)\n")
        rows = [(1, "I", "ld [hl], $0C"), (3, "I", "inc hl"), (4, "I", "ld [hl], $6E")]
        self.assertEqual([r[2] for r in t.rom_operands(rows)],
                         ["ld [hl], LOW(TxtHelpPan)", "inc hl", "ld [hl], HIGH(TxtHelpPan)"])

    def test_macro_must_match_the_sym(self):
        self.assertEqual(to_rgbds.source_addresses("#define Foo ((char *)0x6E0C)\n",
                                                   {0x6E0C: "TxtHelpPan"}), {})

    def test_banked_call_by_address(self):
        rows = [(1, "I", "ld hl, $747D"), (4, "I", "push hl"), (5, "I", "ld a, $05"),
                (7, "I", "push af"), (8, "I", "inc sp"), (9, "I", "call FarCall")]
        self.assertEqual(self.translator().rom_operands(rows)[0][2], "ld hl, RestoreSelectionState")


if __name__ == "__main__":
    unittest.main()
