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
split_c = importlib.import_module("split_c")
rename = importlib.import_module("rename")
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


class AllocDirectiveTests(unittest.TestCase):
    def test_comments_become_function_scoped_directives(self):
        src = ("/* lsdj-alloc(WordScreenInput): BUSYC=1-93 */\n"
               "/* notes...\n * lsdj-alloc(CountFileBlocks): NOSPILLAT=iTemp0:27 SPILLAT=iTemp0:28\n */\n"
               "void f(void) {}\n")
        self.assertEqual(match_function.alloc_directives(src),
                         "WordScreenInput:BUSYC=1-93 CountFileBlocks:NOSPILLAT=iTemp0:27 "
                         "CountFileBlocks:SPILLAT=iTemp0:28")
        self.assertEqual(match_function.alloc_directives("/* no directive */"), "")


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
        self.assertEqual(t.operand("00105$"), "\x0000105\x00")   # named by resolve_labels
        self.assertEqual(t.expr("#<(00105$)"), "LOW(\x0000105\x00)")

    def test_unknown_names_become_guarded_equates(self):
        t = self.translator()
        self.assertEqual(t.operand("(_hFoo+0)"), "[hFoo]")
        self.assertEqual(t.operand("#_hFoo + 0 + 1"), "hFoo + 1")
        self.assertEqual(t.guards, {"hFoo": "$FF90"})
        with self.assertRaises(ValueError):
            t.operand("_NoSuchName")


class LabelTests(unittest.TestCase):
    """SDCC's labels take the .sym names at their addresses, written from
    where they are used; fall-through labels go; the rest are named."""

    def translator(self, labels, targets, texts=None):
        t = to_rgbds.Translator.__new__(to_rgbds.Translator)
        t.func, t.labels, t.targets, t.texts = "F", labels, targets, texts or {}
        t.globals = sorted((a, n) for a, n in labels.items() if not n.startswith("."))
        return t

    def test_sym_names_scoped_references_and_dropped_labels(self):
        labels = {0x4000: "F", 0x4010: ".notSong", 0x4020: "G", 0x4030: ".tail"}
        t = self.translator(labels, {"00105": 0x4010, "00106": 0x4012, "00107": 0x4030})
        rows = [(0x4000, "I", "jp nz, \x0000105\x00"), (0x4010, "L", "\x0000105\x00:"),
                (0x4012, "L", "\x0000106\x00:"), (0x4020, "I", "jp \x0000105\x00"),
                (0x4022, "I", "jp \x0000107\x00")]
        self.assertEqual(t.resolve_labels(rows), [
            (0x4000, "I", "jp nz, .notSong"), (0x4020, "I", "jp F.notSong"),
            (0x4022, "I", "jp .tail")])

    def test_jump_table_and_literal_names(self):
        t = self.translator({0x4000: "F"}, {"00109": 0x4008, "__str_3": 0x4020},
                            {"__str_3": "SAVE SONG?"})
        rows = [(0x4000, "I", "ldh a, [hSoundChannel+0]"), (0x4002, "I", "ld e, a"),
                (0x4003, "I", "ld d, $00"), (0x4005, "I", "ld hl, \x0000109\x00"),
                (0x4008, "L", "\x0000109\x00:"), (0x4008, "I", "jp $4100"),
                (0x400B, "I", "jp $4110"), (0x4010, "I", "ld hl, \x00__str_3\x00"),
                (0x4020, "L", "\x00__str_3\x00:")]
        out = [text for _a, _k, text in t.resolve_labels(rows)]
        self.assertIn("ld hl, .soundChannelCases", out)
        self.assertIn(".soundChannelCases:", out)
        self.assertIn("ld hl, .strSaveSong", out)
        self.assertIn(".strSaveSong:", out)


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


class BytePointerTests(unittest.TestCase):
    """Addresses the code loads a byte at a time."""
    HOME = {0x179A: "FarCall"}

    @staticmethod
    def rows(*texts, start=0x6000):
        out, a = [], start
        for t in texts:
            ref = int(t.split("$")[1], 16) if t.startswith("call $") else None
            out.append((a, b"", t, ref))
            a += 2
        return out

    def test_banked_call_target_in_two_bytes(self):
        rows = self.rows("ld c, $fe", "ld b, $6e", "push bc", "ld a, $1f", "push af",
                         "inc sp", "call $179a")
        ptrs, _ = disasm.rom_pointers(rows, 0x04, {}, self.HOME,
                                      lambda b: {0x6EFE: "ErasePhraseCursor"} if b == 0x1F else {})
        self.assertEqual(ptrs, {0x6000: (0x1F, 0x6EFE, "LOW"), 0x6002: (0x1F, 0x6EFE, "HIGH")})

    def test_banked_call_target_through_register_copies(self):
        rows = self.rows("ld b, $fe", "ld c, $6e", "ld l, b", "ld h, c", "push hl", "ld a, $1f",
                         "push af", "inc sp", "call $179a")
        ptrs, _ = disasm.rom_pointers(rows, 0x04, {}, self.HOME,
                                      lambda b: {0x6EFE: "ErasePhraseCursor"} if b == 0x1F else {})
        self.assertEqual(ptrs, {0x6000: (0x1F, 0x6EFE, "LOW"), 0x6002: (0x1F, 0x6EFE, "HIGH")})

    def test_table_indexed_by_a_byte_and_by_a_word(self):
        for adc in ("adc a, $00", "adc a, b"):
            rows = self.rows("ld a, $f0", "add a, c", "ld e, a", "ld a, $64", adc, "ld d, a")
            ptrs, _ = disasm.rom_pointers(rows, 0x1F, {}, self.HOME)
            self.assertEqual(ptrs, {0x6000: (0x1F, 0x64F0, "LOW"), 0x6006: (0x1F, 0x64F0, "HIGH")}, adc)

    def test_address_plus_a_constant(self):
        # wStringSource = AsciiCharacterMap - $20, a byte at a time
        rows = self.rows("ld a, $6f", "add a, $e0", "ld hl, $c564", "ld [hl], a", "ld a, $08",
                         "adc a, $ff", start=0x3C48)
        ptrs, _ = disasm.rom_pointers(rows, 0, {}, self.HOME)
        self.assertEqual(ptrs, {0x3C48: (0, 0x086F, "LOW"), 0x3C50: (0, 0x086F, "HIGH")})

    def test_spilled_function_pointer(self):
        # SDCC stores a banked call's target to a stack slot a byte at a time
        for setup in (["ld hl, $0005", "ld [hl], $65", "inc hl", "ld [hl], $6f"],
                      ["ld b, $65", "ld c, $6f", "ld hl, $0003", "ld [hl], b", "inc hl", "ld [hl], c"]):
            rows = self.rows(*setup, "push hl", "ld a, $02", "push af", "inc sp", "call $0c8f")
            ptrs, _ = disasm.rom_pointers(rows, 0x06, {}, {0x0C8F: "FarCallArgument2"},
                                          lambda b: {0x6F65: "SoundGetChainPhrase"} if b == 0x02 else {})
            lo = next(a for a, r in zip(range(0x6000, 0x7000, 2), rows) if r[2] in ("ld [hl], $65", "ld b, $65"))
            hi = next(a for a, r in zip(range(0x6000, 0x7000, 2), rows) if r[2] in ("ld [hl], $6f", "ld c, $6f"))
            self.assertEqual(ptrs, {lo: (0x02, 0x6F65, "LOW"), hi: (0x02, 0x6F65, "HIGH")}, setup)

    def test_stored_constant_without_a_name_stays(self):
        rows = self.rows("ld hl, $c69a", "ld [hl], $49", "inc hl", "ld [hl], $07")
        self.assertEqual(disasm.rom_pointers(rows, 0x02, {}, self.HOME)[0], {})

    def test_switch_cases_jumping_to_a_shared_banked_call(self):
        # case: ld c/b, then `jp .farCall`; .farCall: push bc / ld a, $1f / ... / call FarCall
        rows = self.rows("ld c, $3d", "ld b, $42", "jp $6008",
                         "push bc", "ld a, $1f", "push af", "inc sp", "call $179a")
        rows[2] = (rows[2][0], b"", "jp $6006", 0x6006)
        ptrs, _ = disasm.rom_pointers(rows, 0x03, {}, self.HOME,
                                      lambda b: {0x423D: "DrawPulseInstrument"} if b == 0x1F else {})
        self.assertEqual(ptrs, {0x6000: (0x1F, 0x423D, "LOW"), 0x6002: (0x1F, 0x423D, "HIGH")})

    def test_ram_word_arithmetic_is_not_a_pointer(self):
        rows = self.rows("ld a, [hl]", "add a, $14", "ld [hl+], a", "ld a, [hl]", "adc a, $00",
                         "ld [hl], a")
        self.assertEqual(disasm.rom_pointers(rows, 0x07, {}, self.HOME)[0], {})

    def test_pointer_table_rows(self):
        rows = [(0x4058, b"", "dw $4098", None), (0x405A, b"", "dw $0000", None)]
        ptrs, notes = disasm.rom_pointers(rows, 0x1B, {}, self.HOME, table_bank={0x4058: 0x1B, 0x405A: 0x1B})
        self.assertEqual((ptrs, notes), ({0x4058: (0x1B, 0x4098)}, []))


class ShiftTestTests(unittest.TestCase):
    def test_padding_goes_before_the_labels_and_comes_out_of_the_fill(self):
        shift_test = importlib.import_module("shift_test")
        text = "\n".join(['SECTION "x", ROMX[$4000], BANK[$01]', "Foo:", ".loop:",
                         "\tnop                          ; $4000: 00",
                         "\tdb $ff, $ff, $ff, $ff        ; $4001: ff ff ff ff",
                         "\trst $38                      ; $4005: ff"])
        out = shift_test.insert_padding(text, 0x4000, 3).splitlines()
        self.assertEqual(out[1], "\tds 3, $ff ; shift test")
        self.assertEqual(out[2:4], ["Foo:", ".loop:"])
        self.assertEqual(out[-1], "")          # the rst $38 byte
        self.assertTrue(out[-2].startswith("\tdb $ff, $ff") and out[-2].count("$ff") == 2)


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


class SplitCTests(unittest.TestCase):
    def test_declared_names(self):
        d = split_c.declared
        self.assertEqual(d("extern char AlphanumericCharacters[][11];   /* $1B:$4C6A, the name keyboard */"), {"AlphanumericCharacters"})
        self.assertEqual(d("extern UBYTE wColumnPhrase[];      /* $C453, [4] */"), {"wColumnPhrase"})
        self.assertEqual(d("extern UBYTE wSyncRun, wSyncHandshake;"), {"wSyncRun", "wSyncHandshake"})
        self.assertEqual(d("void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */"), {"ReadKitSampleName"})
        self.assertEqual(d("extern void (*wFieldHandler)(UBYTE v);"), {"wFieldHandler"})
        self.assertEqual(d("#define TxtHelpPan ((char *)0x6E0C)"), {"TxtHelpPan"})

    def test_header_wraps_the_description(self):
        h = split_c.header("The status panel", "Updated every frame on most screens: each channel's "
                           "playing note (a kit's sample name or a note name) and the sync status.", "06")
        self.assertTrue(h.startswith("/*\n * The status panel -- LSDj 9.4.0, ROM bank $06.\n *\n * Updated"))
        self.assertLessEqual(max(map(len, h.splitlines())), 80)


class FunctionStartTests(unittest.TestCase):
    def test_a_start_label_the_symbol_table_truncated(self):
        # as-gbz80 keeps 32 characters of a symbol: the table loses `_start`
        lst = "\n".join(["   0BB9                    3976 ___ShortName_start:",
                         "   0E26                    4678 ___AVeryLongFunctionNameHere_start:",
                         "  0 ___AVeryLongFunctionNameHere_s   0E26 R",
                         "  0 ___ShortName_start               0BB9 R"])
        self.assertEqual(match_function.function_starts(lst), [0x0BB9, 0x0E26])


class RelabelTests(unittest.TestCase):
    # made-up names: a real one would follow the next rename into this file
    SYM = "\n".join(["4000 DrawFooCell", "4002 .drawFooCellFar", "4004 .loadFooDigitPal", "4006 .checkIsFOOb",
                     "4008 .fooRowPalettes", "400A FooRowPalettes", "400C .loadFooDigitPal"])

    def relabel(self, text, pairs):
        cores = {rename.label_core(n) for n in ("DrawFooCell", "FooRowPalettes", "wFooDigitPal", "wIsFOO", "wFooRow")}
        return rename.relabel_text(text, pairs, cores | {rename.label_core(n) for _, n in pairs})

    def test_labels_follow_their_global(self):
        text, n = self.relabel(self.SYM, [("DrawFooCell", "DrawBarName"), ("wFooDigitPal", "wBarDigitPal"),
                                          ("wIsFOO", "wIsFoo"), ("wFooRow", "wFooPos")])
        self.assertEqual(n, 4)
        self.assertEqual(text.split("\n")[1:5], ["4002 .drawBarNameFar", "4004 .loadBarDigitPal",
                                                 "4006 .checkIsFoob", "4008 .fooRowPalettes"])
        self.assertTrue(text.endswith("400C .loadBarDigitPal"))   # another scope, the same rename

    def test_names_c_sees_keep_within_as_gbz80s_symbol_length(self):
        cnames = {"FooBar", "DrawFooBazQuuxCorgeGraultGarply", "wFooBazQuuxCorgeGraultGarplyWaldo1"}
        errors = rename.limit_errors([("FooBar", "DrawFooBarBazQuuxCorgeGrault"),       # 28: a function
                                      ("wFooBazQuuxCorgeGraultGarplyWaldo1", "wFooBazQuuxCorgeGraultGarplyWaldo2X")],
                                     cnames, {"FooBar"})
        self.assertEqual(len(errors), 2)
        self.assertIn("at most 27", errors[0])
        self.assertIn("at most 31", errors[1])
        self.assertEqual(rename.limit_errors([("FooBar", "DrawFooBar")], cnames, {"FooBar"}), [])

    def test_a_collision_in_a_scope_stops_the_rename(self):
        with self.assertRaises(SystemExit):
            self.relabel("4000 Foo\n4002 .loadQuuxCursor\n4004 .loadQuuxMarker", [("wQuuxMarker", "wQuuxCursor")])

if __name__ == "__main__":
    unittest.main()
