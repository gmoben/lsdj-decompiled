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
name_locals_full = importlib.import_module("name_locals_full")
disasm = importlib.import_module("disasm")
gfx = importlib.import_module("gfx")
kits = importlib.import_module("kits")
gen_main = importlib.import_module("gen_main")
symfile = importlib.import_module("symfile")
regflow = importlib.import_module("regflow")
sdcc4 = importlib.import_module("sdcc4")


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
        # `ld de, $0200` in bank $1D computes $200 - x: not VibratoShapes
        rows = self.rows((0x5ACB, "ld de, $0200", None), (0x5ACE, "dec hl", None))
        self.assertEqual(disasm.rom_pointers(rows, 0x1D, {}, {0x0200: "VibratoShapes"})[0], {})

    def test_unlabelled_jr_is_relative(self):
        rows = [(0x14D6, b"\x30\xff", "jr nc, $14d7", 0x14D7)]
        self.assertEqual(disasm.render(rows, False, {}), "\tjr nc, @ + 1")


class FillTests(unittest.TestCase):
    """$FF runs are padding: `ds n, $ff`, and the one at the end of a bank pads
    to the bank's end from wherever the content before it ends."""

    def test_long_ff_runs_are_ds_short_ones_stay_bytes(self):
        data = bytes([1, 2]) + b"\xff" * 16 + bytes([3]) + b"\xff" * 15
        rows = disasm._emit_data(data, 0, len(data), 0x4000)
        self.assertEqual([(a, t) for a, _b, t, _r in rows][:3],
                         [(0x4000, "db $01, $02"), (0x4002, "ds 16, $ff"), (0x4012, rows[2][2])])
        self.assertFalse(rows[2][2].startswith("ds"))
        self.assertEqual(b"".join(b for _a, b, _t, _r in rows), data)

    def test_bank_tail_pads_to_the_end_of_the_bank(self):
        data = bytes([0xC9]) + b"\xff" * 0x3FFF
        rows = disasm.disassemble(data, 0, len(data), 0x4000, [(1, len(data))])
        self.assertEqual([t for _a, _b, t, _r in rows],
                         ["ret", "; Free space: 16,383 bytes to the end of the bank.", "ds $8000 - @, $ff"])
        self.assertEqual(rows[1][3], disasm.NOTE)


class ForkTests(unittest.TestCase):
    """A fork's build (verify_all.py --fork): changed code replaces its ROM
    range, its own label kept and the original's labels inside it gone; new
    code goes before the bank's free space."""

    def test_changed_code_keeps_only_its_own_label(self):
        data = bytes([0x00, 0x00, 0x00, 0xC9])                  # nop, nop, nop | Next: ret
        changed = (0, 3, [(0x4000, "I", "ld a, $01"), (0x4002, "I", "ret")], True)
        rows = disasm.disassemble(data, 0, len(data), 0x4000, [], set(), [], [changed])
        text = disasm.render(rows, False, {0x4000: "Func", 0x4002: ".inner", 0x4003: "Next"})
        self.assertEqual(text.split("\n"), ["Func:", "\tld a, $01", "\tret", "Next:", "\tret"])

    def test_new_code_goes_before_the_free_space(self):
        data = bytes([0xC9]) + b"\xff" * 0x3FFF
        new = (disasm.APPEND, disasm.APPEND, [(0, "L", "NewFunc:"), (0, "I", "ret")], True)
        rows = disasm.disassemble(data, 0, len(data), 0x4000, [(1, len(data))], set(), [], [new])
        self.assertEqual([t for _a, _b, t, _r in rows],
                         ["ret", "", "NewFunc:", "\tret", "",
                          "; Free space: 16,383 bytes to the end of the bank.", "ds $8000 - @, $ff"])


class SdccFourTests(unittest.TestCase):
    """A fork's new C (sdcc4.py): SDCC 4's asxxxx output as RGBDS rows, the
    file's static data under names of its own, and nothing the build lacks."""

    ASM = """\
\t.module t
\t.optsdcc -msm83 sdcccall(0)
\t.globl _Tick
\t.area _CODE
_Tick::
\tldhl\tsp,\t#2
\tld\ta, (hl+)
\tld\tc, a
\tld\ta, #<(_wX)
\tadd\ta, c
\tld\t(#_rNR51), a
\tjr\tnz, 00102$
\tld\thl, #_Table + 0
\tjp\t(hl)
00102$:
\tadd\tsp, #-3
\tret
_Table:
\t.db #0x11\t; 17
\t.db #0x22
"""

    def test_rows(self):
        rows = sdcc4.Translation(self.ASM, "t", {"wX", "rNR51"}).rows()
        self.assertEqual([text for _a, _k, text in rows],
                         ["Tick:", "ld hl, sp+2", "ld a, [hl+]", "ld c, a", "ld a, LOW(wX)", "add a, c",
                          "ld [rNR51], a", "jr nz, .L00102", "ld hl, t_Table", "jp hl", ".L00102:",
                          "add sp, -3", "ret", "t_Table:", "db $11", "db $22"])

    def test_refuses_the_runtime_variables_and_unknown_names(self):
        for body in ("\tcall\t__mulint\n",                       # SDCC 4's runtime library
                     "\tld\ta, (#_wNowhere)\n",                   # a name the build doesn't define
                     "\t.area _DATA\n_counter:\n\t.ds 1\n"):     # a variable of its own
            asm = "\t.globl _F\n\t.area _CODE\n_F::\n" + body
            with self.assertRaises(ValueError, msg=body):
                sdcc4.Translation(asm, "t", set()).rows()


class TileBlockTests(unittest.TestCase):
    """A tile block: header, the character map the loader skips, 2bpp tiles
    (tools/gfx.py), rendered as bytes and an INCBIN of gfx/<name>.png."""

    BLOCK = bytes([0x01, 2]) + bytes(range(128)) + bytes([0xFF, 0x00] * 8) + bytes([0x0F, 0xF0] * 8)

    def test_names_and_sizes(self):
        self.assertEqual([gfx.asset_name(n) for n in ("TilesFontY2KHighlight", "TilesUiIcons", "TilesFontZero")],
                         ["font_y2k_highlight", "ui_icons", "font_zero"])
        self.assertEqual(gfx.block_size(bytes([1, 71])), 2 + 128 + 71 * 16)
        self.assertEqual((gfx.prefix_size(2), gfx.prefix_size(0)), (0, 256))
        self.assertEqual(gfx.block_tiles(self.BLOCK), self.BLOCK[130:])

    def test_pixels_and_png(self):
        image = gfx.tile_pixels(gfx.block_tiles(self.BLOCK))
        self.assertEqual((len(image), len(image[0])), (8, 128))
        self.assertEqual(image[0][:16], [1] * 8 + [2] * 4 + [1] * 4)
        png = gfx.png_bytes(image)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(png[16:26], bytes([0, 0, 0, 128, 0, 0, 0, 8, 2, 3]))   # 128x8, 2 bits, indexed

    def test_extract_only_when_missing(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            png, tiles = Path(d) / "gfx" / "font_test.png", gfx.block_tiles(self.BLOCK)
            self.assertEqual(gfx.extract_png(png, tiles), "written")
            self.assertEqual(gfx.extract_png(png, tiles), "same")
            png.write_bytes(png.read_bytes() + b"edit")
            self.assertEqual(gfx.extract_png(png, tiles), "differs")
            self.assertTrue(png.read_bytes().endswith(b"edit"))      # an edit is never overwritten

    def test_block_rows(self):
        rows = disasm._emit_tiles(self.BLOCK, 0, len(self.BLOCK), 0x54C0, "font_test")
        texts = [t for _a, _b, t, _r in rows]
        self.assertEqual(texts[0], "; 2 tiles from gfx/font_test.png, after a character map (the tile for each ASCII code)")
        self.assertEqual((texts[1], len(texts)), ("db $01, 2", 11))
        self.assertEqual(texts[-1], 'INCBIN "build/gfx/font_test.2bpp", 0, 2 * 16')
        self.assertEqual(b"".join(b for _a, b, _t, _r in rows), self.BLOCK)


class TableTests(unittest.TestCase):
    """Typed tables ([N] TYPE / [R][C] TYPE in the .sym): a row of elements a
    line, split where a label inside starts -- inside a word, into bytes."""

    def test_rows_of_a_width_split_at_labels_inside(self):
        data = bytes(range(60))
        rows = disasm.disassemble(data, 0, 60, 0x5000, [(0, 60)], {45}, tables=[(0, 60, 20, "UBYTE")])
        self.assertEqual([(a - 0x5000, len(b)) for a, b, _t, _r in rows], [(0, 20), (20, 20), (40, 5), (45, 15)])

    def test_types(self):
        self.assertEqual(disasm._table_row(bytes([0xF4, 0x00, 0x1F]), "BYTE"), "db -12, 0, 31")
        self.assertEqual(disasm._table_row(bytes([0x2F, 0xB3, 0x14, 0x07]), "UWORD"), "dw $b32f, $0714")
        self.assertEqual(disasm._table_row(bytes([0xFF, 0x7F, 0x00, 0x54]), "RGB"), "RGB 31,31,31, 0,0,21")
        self.assertEqual(disasm._table_row(bytes([0x00, 0x80]), "RGB"), "dw $8000")

    def test_a_label_inside_a_word_splits_it(self):
        data = bytes(range(10))
        rows = disasm.disassemble(data, 0, 10, 0x4000, [(0, 10)], {5}, tables=[(0, 10, 8, "UWORD")])
        self.assertEqual([t for _a, _b, t, _r in rows], ["dw $0100, $0302", "db $04", "db $05", "dw $0706, $0908"])


class TextTests(unittest.TestCase):
    """Text prints a line per string: a newline ends the line inside the
    string, a NUL as `, 0`, other bytes as numbers; matched C's strings too."""

    def test_lines_terminators_and_other_bytes(self):
        data = b'CHAIN\n\n<A>..{x}\n' + b"NOI\x00" + bytes([0x82]) + b'"Q"\x00'
        rows = disasm._emit_text(data, 0, len(data), 0x430E)
        self.assertEqual([t for _a, _b, t, _r in rows],
                         ['db "CHAIN\\n"', 'db "\\n"', 'db "<A>..\\{x\\}\\n"', 'db "NOI", 0', 'db $82, "\\"Q\\"", 0'])
        self.assertEqual(b"".join(b for _a, b, _t, _r in rows), data)

    def test_c_strings_split_at_a_label_inside(self):
        t = to_rgbds.Translator.__new__(to_rgbds.Translator)
        t.labels = {0x456C: ".strOk"}
        rows = t.text_rows(0x4566, "ROM...OK!")
        self.assertEqual([r[2] for r in rows], ['db "ROM..."', 'db "OK!"'])


class KitTests(unittest.TestCase):
    """A kit bank (tools/kits.py): boundary words, then the samples, 4-bit,
    as WAVs the build converts back."""

    @staticmethod
    def bank(*sizes):
        words, at = [], 0x4060
        for n in sizes:
            words.append(at)
            at += n
        words += [at] + [0] * (16 - len(sizes))
        head = b"".join(w.to_bytes(2, "little") for w in words) + b"BD-SD-" + b"\0--" * 14 + b"TR-808" + bytes(8)
        body = bytes(i & 0x7F for i in range(at - 0x4060))
        return head + body + b"\xff" * (0x4000 - len(head) - len(body))

    def test_boundaries(self):
        self.assertEqual(kits.boundaries(self.bank(0x100, 0x20)), [0x4060, 0x4160, 0x4180])
        broken = bytearray(self.bank(0x100, 0x20))
        broken[0x3FFF] = 0
        self.assertIsNone(kits.boundaries(bytes(broken)))           # not $FF after the samples

    def test_wav_round_trip_and_other_inputs(self):
        import tempfile, wave
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.wav"
            p.write_bytes(kits.wav_bytes(bytes([0x0F, 0x80])))
            self.assertEqual(kits.pcm_bytes(p), bytes([0x0F, 0x80]))
            with wave.open(str(p), "wb") as w:                      # 16-bit stereo, an odd count
                w.setnchannels(2); w.setsampwidth(2); w.setframerate(kits.RATE)
                w.writeframes(b"".join(v.to_bytes(2, "little", signed=True) * 2 for v in (32767, -32768, 0)))
            self.assertEqual(kits.pcm_bytes(p), bytes([0xF0, 0x88]))

    def test_names_as_text(self):
        self.assertEqual(gen_main.db_text(b"BD-"), 'db "BD-"')
        self.assertEqual(gen_main.db_text(b"\0--"), 'db 0, "--"')
        self.assertEqual(gen_main.db_text(b'\x80"'), 'db $80, "\\""')


class DocsTests(unittest.TestCase):
    def test_brace_names_resolve(self):
        """`Prefix{A,B}Suffix` in the docs names symbols the renames cannot
        see as whole words: every expansion must be a current name."""
        import re
        root = Path(__file__).resolve().parents[2]
        names = set()
        for p in (root / "src").glob("bank*.sym"):
            names |= set(re.findall(r"^[0-9A-F]{4} ([A-Za-z_][\w.]*)", p.read_text(), re.M))
        for p in (root / "constants").glob("*.inc"):
            names |= set(re.findall(r"^DEF ([A-Za-z_]\w*)", p.read_text(), re.M))
        for p in (root / "c").rglob("*.[ch]"):
            names |= set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", p.read_text()))
        missing = []
        for f in list((root / "docs").glob("*.md")) + [root / "AGENTS.md", root / "README.md"]:
            for m in re.finditer(r"`([A-Za-z_]\w*)\{([\w,]+)\}(\w*)`", f.read_text()):
                missing += [f"{f.name}: {m.group(1)}{x}{m.group(3)}" for x in m.group(2).split(",")
                            if m.group(1) + x + m.group(3) not in names]
        self.assertEqual(missing, [])


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

    def test_pointer_stored_through_a(self):
        # SoundNoteToPeriod: wChannelPeriodTablePointer = DrumPeriodTable, a byte at a time
        rows = self.rows("ld a, $b9", "ld [hl+], a", "ld c, a", "ld a, $09", "ld [hl], a", "ld b, a")
        ptrs, _ = disasm.rom_pointers(rows, 0x02, {}, {0x09B9: "DrumPeriodTable"})
        self.assertEqual(ptrs, {0x6000: (0, 0x09B9, "LOW"), 0x6006: (0, 0x09B9, "HIGH")})
        # the same store of a RAM address (wNotePeriods) is no ROM name: it stays numbers
        rows = self.rows("ld a, $28", "ld [hl+], a", "ld c, a", "ld a, $cf", "ld [hl], a", "ld b, a")
        self.assertEqual(disasm.rom_pointers(rows, 0x02, {}, {0x09B9: "DrumPeriodTable"})[0], {})

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

    def test_compare_constant_versus_indexed_pointer(self):
        self.assertTrue(to_rgbds.is_compare16(["add hl, de", "jp c, .done"]))
        self.assertTrue(to_rgbds.is_compare16(["add hl, bc", "ld a, $00"]))
        self.assertFalse(to_rgbds.is_compare16(["add hl, de", "ld a, [hl]"]))   # base + index

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


class BranchConditionTests(unittest.TestCase):
    def test_a_glue_block_named_after_the_test_that_reaches_it(self):
        bc = name_locals_full.branch_condition
        self.assertEqual(bc(["\tcp a, $80"], "nz"), ".not80")
        self.assertEqual(bc(["\tbit 7, a"], "z"), ".bit7Clear")
        self.assertEqual(bc(["\tld a, [wKitLevel]", "\tor a, a"], "z"), ".kitLevelZero")
        self.assertIsNone(bc(["\tcp a, [hl]"], "z"))               # no constant: no honest name


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

    def test_a_module_path_keeps_its_name(self):
        self.assertEqual(rename.replace_names("see c/bank00/FooBar.c: FooBar() calls wFooBar",
                                              {"FooBar": "BarFoo", "wFooBar": "wBarFoo"}),
                         "see c/bank00/FooBar.c: BarFoo() calls wBarFoo")

    def test_a_collision_in_a_scope_stops_the_rename(self):
        with self.assertRaises(SystemExit):
            self.relabel("4000 Foo\n4002 .loadQuuxCursor\n4004 .loadQuuxMarker", [("wQuuxMarker", "wQuuxCursor")])


class SymLayoutTests(unittest.TestCase):
    def test_sym_comments_in_one_column(self):
        text = ("; --- a group\n4000 Foo ; the foo\n4002 .loop\t  \n"
                "4004 DrawFooBarBazQuuxCorgeGrault ; longer than the column\n")
        self.assertEqual(symfile.align_sym(text),
                         "; --- a group\n" + "4000 Foo".ljust(32) + "; the foo\n4002 .loop\n"
                         "4004 DrawFooBarBazQuuxCorgeGrault ; longer than the column\n")

    def test_ram_equ_and_comments_in_columns(self):
        text = "DEF wFoo EQU $C401 ; the foo\n   ; continued\nDEF FOO_INC EQU 1\n"
        self.assertEqual(symfile.align_ram(text),
                         "DEF wFoo".ljust(38) + "EQU $C401".ljust(11) + "; the foo\n"
                         + " " * 49 + "; continued\n" + "DEF FOO_INC".ljust(38) + "EQU 1\n")

    def test_c_declaration_comments_in_one_column(self):
        text = ("extern UBYTE wChain;   /* $C418 */\n"
                "void DrawTileRun(UBYTE x, UBYTE y, UBYTE w, UBYTE h, UBYTE tile);   /* asm */\n"
                "    x = 1;   /* a statement keeps its spacing */\n")
        self.assertEqual(symfile.align_c(text),
                         "extern UBYTE wChain;".ljust(44) + "/* $C418 */\n"
                         "void DrawTileRun(UBYTE x, UBYTE y, UBYTE w, UBYTE h, UBYTE tile); /* asm */\n"
                         "    x = 1;   /* a statement keeps its spacing */\n")

    def test_the_curated_files_are_aligned(self):
        self.assertEqual([p.name for p in symfile.unaligned()], [], "run tools/symfile.py")

    def test_a_bank_names_an_address_once(self):
        self.assertEqual(symfile.duplicates(), [])


class RegflowTests(unittest.TestCase):
    """Register contracts over a made-up bank 0: routines by address."""

    def contracts(self, routines: dict[int, bytes]):
        rom = bytearray([0xFF]) * 0x4000
        for addr, code in routines.items():
            rom[addr:addr + len(code)] = code
        prog = regflow.Program.__new__(regflow.Program)
        prog.rom, prog.ops, prog.comments = bytes(rom), {}, {}
        prog.names = {(0, a): f"R{a:04X}" for a in routines}
        prog.entries = set(prog.names)
        addrs = sorted(routines)
        prog.ends = {(0, a): (addrs + [0x4000])[i + 1] for i, a in enumerate(addrs)}
        prog.by_name = {n: k for k, n in prog.names.items()}
        an = regflow.Analysis(prog)
        an.run()
        return {a: an.contracts[(0, a)] for a in routines}

    def test_a_register_pushed_and_popped_back_is_preserved(self):
        c = self.contracts({0x100: bytes([0xC5, 0x06, 0x01, 0xC1, 0xC9])})[0x100]   # push bc; ld b,1; pop bc; ret
        self.assertEqual((c.inputs, c.may), (set(), set()))

    def test_inputs_through_a_move_and_a_call(self):
        c = self.contracts({0x100: bytes([0x78, 0xCD, 0x00, 0x02, 0xC9]),          # ld a,b; call $200; ret
                            0x200: bytes([0x77, 0xC9])})                           # ld [hl],a; ret
        self.assertEqual(c[0x100].inputs, {"b", "h", "l"})
        self.assertEqual(c[0x200].inputs, {"a", "h", "l"})
        self.assertEqual(c[0x100].may, {"a"})

    def test_out_is_what_a_caller_reads(self):
        c = self.contracts({0x100: bytes([0xCD, 0x00, 0x02, 0x73, 0xC9]),          # call $200; ld [hl],e; ret
                            0x200: bytes([0x1E, 0x05, 0x16, 0x00, 0xC9])})         # ld e,5; ld d,0; ret
        self.assertEqual(regflow.describe(c[0x200]), "in: -; out: e; clobbers: d")

    def test_a_value_nothing_reads_is_not_an_output(self):
        # call $200; or a,a; ret -- `or a` only clears carry, and no caller reads the flags
        c = self.contracts({0x100: bytes([0xCD, 0x00, 0x02, 0xB7, 0xC9]),
                            0x200: bytes([0x3E, 0x01, 0xC9])})                       # ld a,1; ret
        self.assertEqual(c[0x200].outputs, set())

    def test_a_register_saved_around_a_loop_is_not_read(self):
        # call $200; .loop: push bc; call $300; pop bc; dec b; jr nz .loop; ret -- c only rides along
        c = self.contracts({0x100: bytes([0xCD, 0x00, 0x02, 0xC5, 0xCD, 0x00, 0x03, 0xC1, 0x05, 0x20, 0xF8, 0xC9]),
                            0x200: bytes([0x0E, 0x01, 0xC9]),                       # ld c,1; ret
                            0x300: bytes([0xC9])})
        self.assertEqual(c[0x200].outputs, set())

    def test_a_byte_argument_pushes_a_not_the_flags(self):
        # ld a,1; push af; inc sp; call $200; inc sp; ret -- the flags byte is dropped
        c = self.contracts({0x100: bytes([0x3E, 0x01, 0xF5, 0x33, 0xCD, 0x00, 0x02, 0x33, 0xC9]),
                            0x200: bytes([0xC9])})
        self.assertEqual(c[0x100].inputs, set())
        self.assertNotIn("partial", regflow.describe(c[0x100]))

    def test_a_jump_table_reaches_every_entry(self):
        # ld hl,$300; jp hl -- a constant; $300: jp $400; $400: ld b,c; ret
        c = self.contracts({0x100: bytes([0x21, 0x00, 0x03, 0xE9]), 0x300: bytes([0xC3, 0x00, 0x04]),
                            0x400: bytes([0x41, 0xC9])})
        self.assertEqual((c[0x100].inputs, c[0x100].may), ({"c"}, {"b", "h", "l"}))

    def test_reading_the_callers_frame_is_a_stack_input(self):
        c = self.contracts({0x100: bytes([0xF8, 0x02, 0x7E, 0xC9])})[0x100]          # ld hl,sp+2; ld a,[hl]; ret
        self.assertTrue(c.stack_args)
        self.assertTrue(regflow.describe(c).startswith("in: stack;"))

    def test_an_unknown_jp_hl_is_partial(self):
        c = self.contracts({0x100: bytes([0x7E, 0x6F, 0xE9])})[0x100]                # ld a,[hl]; ld l,a; jp hl
        self.assertIn("jp hl", c.partial)


if __name__ == "__main__":
    unittest.main()
