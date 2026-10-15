#!/usr/bin/env python3
"""Focused regressions for the recovered 16-bit comparison pass, the
switch-chain reload pass and the hl reload after a word test.

Run with: python3 tools/match/test_peep.py
"""

import importlib
import re
import unittest

peep = importlib.import_module("peep")
gb_cmp16 = peep.gb_cmp16


def execute(lines, value):
    """Execute the comparison tests' small SM83 subset, including HL/SP effects."""
    regs = dict.fromkeys("abcdehl", 0)
    sp = 0xC000
    memory = {sp + 6: value & 0xFF, sp + 7: value >> 8}
    carry = 0
    zero = False
    branch = False

    def pair(name):
        return regs[name[0]] << 8 | regs[name[1]]

    def put_pair(name, value):
        regs[name[0]], regs[name[1]] = (value >> 8) & 0xFF, value & 0xFF

    def get(operand):
        if operand.startswith("#"):
            return sum(int(term, 0) for term in operand[1:].split("+"))
        if operand == "(hl)":
            return memory.get(pair("hl"), 0xA5)
        return regs[operand]

    for raw in lines:
        text = raw.split(";")[0].strip()
        if not text or text.endswith(":"):
            continue
        parts = text.split(None, 1)
        op = parts[0]
        args = parts[1].split(",") if len(parts) == 2 else []
        if op == "lda":
            put_pair(args[0], sp + int(args[1].split("(")[0]))
        elif op == "ld":
            target, source = args
            v = get(source)
            if target == "(hl)":
                memory[pair("hl")] = v
            elif len(target) == 2:
                put_pair(target, v)
            else:
                regs[target] = v
        elif op in ("inc", "dec"):
            put_pair(args[0], pair(args[0]) + (1 if op == "inc" else -1))
        elif op == "xor":
            regs["a"] ^= get(args[1])
            carry = 0
        elif op == "sbc":
            v = regs["a"] - get(args[1]) - carry
            regs["a"], carry = v & 0xFF, int(v < 0)
        elif op == "add":
            v = pair("hl") + pair(args[1])
            put_pair("hl", v)
            carry = int(v > 0xFFFF)
        elif op == "rla":
            v = regs["a"]
            regs["a"], carry = ((v << 1) | carry) & 0xFF, v >> 7
        elif op == "bit":
            zero = not (get(args[1]) >> int(args[0]) & 1)
        elif op == "jp":
            branch = {"c": bool(carry), "nc": not carry, "z": zero, "nz": not zero}[args[0]]
            if branch:
                break
        else:
            raise AssertionError(f"unsupported test instruction: {text}")
    return branch, memory


class Compare16Tests(unittest.TestCase):
    def source(self, threshold, ending):
        return [
            "_test:", "\tlda\thl,7(sp)", "\txor\ta,a", "\tdec\thl",
            "\tld\ta,(hl)", f"\tsbc\ta,#0x{threshold & 0xFF:02X}",
            "\tinc\thl", "\tld\ta,(hl)", f"\tsbc\ta,#0x{threshold >> 8:02X}",
        ] + ending

    def test_materialized_value_and_destination(self):
        ending = ["\tld\ta,#0x00", "\trla", "\tinc\thl", "\tld\t(hl),a"]
        for threshold in (1, 11, 0x100, 0x8000, 0xFFFF):
            source = self.source(threshold, ending)
            result, count = gb_cmp16(source)
            self.assertEqual(count, 1)
            for value in (0, threshold - 1, threshold, min(threshold + 1, 0xFFFF), 0xFFFF):
                with self.subTest(threshold=threshold, value=value):
                    self.assertEqual(execute(source, value), execute(result, value))
                    self.assertEqual(execute(result, value)[1][0xC008], int(value < threshold))

    def test_branch_polarity_and_stack_store_after_high_byte_fetch(self):
        for condition in ("c", "nc"):
            source = self.source(11, [f"\tjp\t{condition},001done$", "\tinc\thl",
                                     "\tld\t(hl),#0x04"])
            result, count = gb_cmp16(source)
            self.assertEqual(count, 1)
            for value in (0, 10, 11, 12, 0xFFFF):
                with self.subTest(condition=condition, value=value):
                    self.assertEqual(execute(source, value), execute(result, value))

    def test_nonzero_threshold_carry_identity_exhaustively(self):
        # Check the emitted threshold and carry inversion against unsigned <
        # across every 16-bit input, not just the addresses seen in this ROM.
        for threshold in (1, 11, 0x100, 0x8000, 0xFFFF):
            result, count = gb_cmp16(self.source(threshold, ["\tld\ta,#0x00", "\trla"]))
            self.assertEqual(count, 1)
            match = re.search(r"ld\s+hl,#0x([0-9A-F]+)", "\n".join(result))
            assert match is not None
            addend = int(match.group(1), 16)
            for value in range(0x10000):
                self.assertEqual(1 ^ int(value + addend > 0xFFFF), int(value < threshold))

    def test_other_fetch_forms_preserve_boolean_stores(self):
        for value in (0, 10, 11, 12, 0xFFFF):
            stack_compare = ["\tld\ta,(hl)", "\tsbc\ta,#0x0B", "\tinc\thl",
                             "\tld\ta,(hl)", "\tsbc\ta,#0x00"]
            sources = [
                ["\txor\ta,a", "\tlda\thl,6(sp)"] + stack_compare,
                ["\tlda\thl,6(sp)", "\txor\ta,a"] + stack_compare,
            ]
            for low, high in (("c", "b"), ("e", "d")):
                sources.append([
                    f"\tld\t{low},#0x{value & 0xFF:02X}",
                    f"\tld\t{high},#0x{value >> 8:02X}",
                    "\tlda\thl,7(sp)", "\txor\ta,a", f"\tld\ta,{low}",
                    "\tsbc\ta,#0x0B", f"\tld\ta,{high}", "\tsbc\ta,#0x00",
                ])
            for source in sources:
                source += ["\tld\ta,#0x00", "\trla", "\tinc\thl", "\tld\t(hl),a"]
                result, count = gb_cmp16(source)
                self.assertEqual(count, 1)
                self.assertEqual(execute(source, value), execute(result, value))

    def test_global_materialization_is_left_unchanged(self):
        source = ["\txor\ta,a", "\tld\thl,#0xC006", "\tld\ta,(hl)",
                  "\tsbc\ta,#0x0B", "\tinc\thl", "\tld\ta,(hl)",
                  "\tsbc\ta,#0x00", "\tld\ta,#0x00", "\trla"]
        self.assertEqual(gb_cmp16(source), (source, 0))

    def test_fetch_stepping_up_from_the_byte_below(self):
        # SpeechInstrInput: hl still at the loop counter (sp+5) below the word
        for condition in ("c", "nc"):
            source = ["\tlda\thl,5(sp)", "\txor\ta,a", "\tinc\thl", "\tld\ta,(hl)",
                      "\tsbc\ta,#0xD0", "\tinc\thl", "\tld\ta,(hl)", "\tsbc\ta,#0xBD",
                      f"\tjp\t{condition},001done$", "\tdec\thl", "\tld\t(hl),#0x04"]
            result, count = gb_cmp16(source)
            self.assertEqual(count, 1)
            self.assertIn("\tadd\thl,de", result)
            for value in (0, 0xBDCF, 0xBDD0, 0xBDD1, 0xFFFF):
                with self.subTest(condition=condition, value=value):
                    self.assertEqual(execute(source, value), execute(result, value))

    def test_global_branch_reloads_hl_for_the_next_use(self):
        # CompressSong: `wCompressDst < 0x7FFF` then `wCompressDst + 1`; stock SDCC
        # steps back from the high byte, which the add form has clobbered
        source = ["\txor\ta,a", "\tld\thl,#0xC006 + 0", "\tld\ta,(hl)",
                  "\tsbc\ta,#0xFF", "\tinc\thl", "\tld\ta,(hl)",
                  "\tsbc\ta,#0x7F", "\tjp\tnc,001skip$", "\tdec\thl",
                  "\tld\ta,(hl)", "\tinc\thl", "\tinc\thl", "\tld\t(hl),a"]
        result, count = gb_cmp16(source)
        self.assertEqual(count, 1)
        jump = result.index("\tjp\tc,001skip$")
        self.assertEqual(result[jump + 1], "\tld\thl,#0xC006")
        for value in (0, 0x1234, 0x7FFE, 0x7FFF, 0xFFFF):
            with self.subTest(value=value):
                self.assertEqual(execute(source, value), execute(result, value))

    def test_promoted_global_byte_in_a_pair_reloads_hl_for_the_next_use(self):
        # WordScreenInput: `(UWORD)wSpeechCursorRow < 13` promotes the byte
        # into bc and leaves hl at it for the `wSpeechCursorRow++` that follows
        source = ["\tld\thl,#_wSpeechCursorRow", "\tld\tc,(hl)", "\tld\tb,#0x00",
                  "\txor\ta,a", "\tld\ta,c", "\tsbc\ta,#0x0D", "\tld\ta,b",
                  "\tsbc\ta,#0x00", "\tjp\tnc,001skip$", "\tinc\t(hl)"]
        result, count = gb_cmp16(source)
        self.assertEqual(count, 1)
        self.assertIn("\tadd\thl,bc", result)
        jump = result.index("\tjp\tc,001skip$")
        self.assertEqual(result[jump + 1:], ["\tld\thl,#_wSpeechCursorRow", "\tinc\t(hl)"])

    def test_zero_threshold_and_intervening_label_are_not_rewritten(self):
        for ending in (["\tld\ta,#0x00", "\trla"], ["\tjp\tnc,001done$"]):
            source = self.source(0, ending)
            self.assertEqual(gb_cmp16(source), (source, 0))
        source = self.source(11, ["001entry$:", "\tld\ta,#0x00", "\trla"])
        self.assertEqual(gb_cmp16(source), (source, 0))


class SignedCompare16Tests(unittest.TestCase):
    """SDCC 2.2.1's signed word compares with a constant, rewritten to the add
    form on the sign-flipped operand (Abs16, DrawNumber, DrawCrashScreen,
    FxCmd_P_Step)."""

    VALUES = (-0x8000, -0x7FFF, -17, -16, -15, -2, -1, 0, 1, 9, 10, 11, 15, 16, 17, 0x7FFE, 0x7FFF)
    LIMITS = (-0x8000, -0x7FFF, -16, -1, 1, 10, 16, 0x7FFF)

    @staticmethod
    def flipped(k):
        k &= 0xFFFF
        return ((k >> 8) ^ 0x80), k & 0xFF

    def check(self, source):
        result, count = gb_cmp16(source)
        self.assertEqual(count, 1)
        self.assertIn("\tadd\thl,de", result)
        for v in self.VALUES:
            with self.subTest(value=v):
                self.assertEqual(execute(source, v)[0], execute(result, v)[0])
        return result

    def test_constant_right_on_the_stack(self):
        for k in self.LIMITS[1:]:             # x < -0x8000: never true is left alone
            hh, ll = self.flipped(k)
            for cc in ("c", "nc"):
                with self.subTest(k=k, cc=cc):
                    self.check(["\tlda\thl,7(sp)", "\tld\ta,(hl)", "\txor\ta,#0x80", "\tld\te,a",
                                f"\tld\td,#0x{hh:02X}", "\tdec\thl", "\tld\ta,(hl)",
                                f"\tsbc\ta,#0x{ll:02X}", "\tld\ta,e", "\tsbc\ta,d",
                                f"\tjp\t{cc},001done$"])

    def test_constant_left_on_the_stack(self):
        for k in self.LIMITS[:-1]:            # K < 0x7FFF: never true is left alone
            hh, ll = self.flipped(k)
            for cc in ("c", "nc"):
                with self.subTest(k=k, cc=cc):
                    self.check([f"\tld\te,#0x{hh:02X}", "\tlda\thl,7(sp)", "\tld\ta,(hl)",
                                "\txor\ta,#0x80", "\tld\td,a", f"\tld\ta,#0x{ll:02X}", "\tdec\thl",
                                "\tsbc\ta,(hl)", "\tld\ta,e", "\tsbc\ta,d", f"\tjp\t{cc},001done$"])

    def test_constant_in_registers(self):
        for k in self.LIMITS:
            hh, ll = self.flipped(k)
            for v in self.VALUES:
                load = [f"\tld\tc,#0x{v & 0xFF:02X}", f"\tld\tb,#0x{(v >> 8) & 0xFF:02X}"]
                for body in (["\tld\ta,b", "\txor\ta,#0x80", "\tld\te,a", f"\tld\td,#0x{hh:02X}",
                              "\tld\ta,c", f"\tsbc\ta,#0x{ll:02X}", "\tld\ta,e", "\tsbc\ta,d"],
                             [f"\tld\te,#0x{hh:02X}", "\tld\ta,b", "\txor\ta,#0x80", "\tld\td,a",
                              f"\tld\ta,#0x{ll:02X}", "\tsbc\ta,c", "\tld\ta,e", "\tsbc\ta,d"]):
                    source = load + body + ["\tjp\tnc,001done$"]
                    result, count = gb_cmp16(source)
                    left = body[0].startswith("\tld\te,#")
                    if k == (0x7FFF if left else -0x8000):
                        self.assertEqual(count, 0)    # never true: left alone
                        continue
                    with self.subTest(k=k, v=v, left=left):
                        self.assertEqual(count, 1)
                        self.assertEqual(execute(source, 0)[0], execute(result, 0)[0])

    def test_zero_after_the_compare_comment(self):
        for cc in ("z", "nz"):
            source = ["; genCmpLt", "; AOP_STK for ", "\tlda\thl,7(sp)", "\tld\ta,(hl)",
                      "\tbit\t7,a", f"\tjp\t{cc},001done$"]
            # LSDj's compiler kept the bit test; GBDK's library build added
            self.assertEqual(gb_cmp16(source), (source, 0))
            result, count = gb_cmp16(source, zero=True)
            self.assertEqual(count, 1)
            self.assertIn("\tld\thl,#0x8000", result)
            for v in self.VALUES:
                with self.subTest(cc=cc, value=v):
                    self.assertEqual(execute(source, v)[0], execute(result, v)[0])

    def test_bit_test_without_the_comment_stays(self):
        # `if (w & 0x8000)` is the same bit test, from genAnd
        source = ["; genAnd", "\tlda\thl,7(sp)", "\tld\ta,(hl)", "\tbit\t7,a", "\tjp\tz,001done$"]
        self.assertEqual(gb_cmp16(source, zero=True), (source, 0))

    def test_negation_after_the_compare_reloads_hl(self):
        # Abs16: `xor a,a; ld hl,sp+4; ld a,$00; sbc a,[hl]` -- genUminus
        # fetches its operand before loading the zero
        source = ["; genCmpLt", "\tlda\thl,7(sp)", "\tld\ta,(hl)", "\tbit\t7,a",
                  "\tjp\tz,001done$", "\txor\ta,a", "\tdec\thl", "\tld\ta,#0x00",
                  "\tsbc\ta,(hl)"]
        result, count = gb_cmp16(source, zero=True)
        self.assertEqual(count, 1)
        self.assertEqual(result[-4:], ["\txor\ta,a", "\tlda\thl,6(sp)", "\tld\ta,#0x00",
                                       "\tsbc\ta,(hl)"])


class SwitchChainTests(unittest.TestCase):
    LOAD = ["\tld\thl,#_wButtons + 0", "\tld\ta,(hl)"]

    def chain(self, label="00201$:"):
        return (self.LOAD + ["\tcp\ta,#0x01", "\tjp\tz,00107$", label]
                + self.LOAD + ["\tcp\ta,#0x02", "\tjp\tz,00111$"])

    def test_reload_after_unreferenced_label_is_dropped(self):
        out, n = peep.switch_chain(self.chain(), set())
        self.assertEqual(n, 1)
        self.assertEqual(out, self.LOAD + ["\tcp\ta,#0x01", "\tjp\tz,00107$", "00201$:",
                                           "\tcp\ta,#0x02", "\tjp\tz,00111$"])

    def test_shared_boolean_case_reloads_a_only(self):
        # SynthScreenInput: case 0x10 tests the CSE'd `wButtons == 0x10` in c
        source = (self.LOAD + ["\tcp\ta,#0x08", "\tjp\tz,00107$", "00418$:", "\txor\ta,a",
                               "\tor\ta,c", "\tjp\tnz,00114$"]
                  + self.LOAD + ["\tcp\ta,#0x11", "\tjp\tz,00111$"])
        out, n = peep.switch_chain(source, set())
        self.assertEqual(n, 1)
        self.assertEqual(out, source[:8] + ["\tld\ta,(hl)", "\tcp\ta,#0x11", "\tjp\tz,00111$"])
        other = source[:8] + ["\tld\thl,#_wButtonsSame + 0"] + source[9:]
        self.assertEqual(peep.switch_chain(other, set()), (other, 0))

    def test_jump_target_or_other_global_keeps_the_reload(self):
        source = self.chain()
        self.assertEqual(peep.switch_chain(source, {"0:00201$"}), (source, 0))
        other = [l.replace("_wButtons", "_wButtonsSame") if i >= 5 else l
                 for i, l in enumerate(source)]
        self.assertEqual(peep.switch_chain(other, set()), (other, 0))

    def test_int_case_widens_from_a(self):
        # InstrScreenInput's kit fields: `case (int)9:` copies the variable
        # into bc for a 16-bit compare; LSDj takes it from `a`
        head = self.LOAD + ["\tcp\ta,#0x08", "\tjp\tz,00107$"]
        tail = ["\tld\tb,#0x00", "\tld\ta,c", "\tcp\ta,#0x09"]
        for reload in (self.LOAD[:1], []):
            out, n = peep.switch_chain(head + reload + ["\tld\tc,(hl)"] + tail, set())
            self.assertEqual(n, 1)
            self.assertEqual(out, head + ["\tld\tc,a"] + tail)
        other = head + ["\tld\thl,#_wButtonsSame + 0", "\tld\tc,(hl)"] + tail
        self.assertEqual(peep.switch_chain(other, set()), (other, 0))



class WordTestHlTests(unittest.TestCase):
    """A stack word tested by genIfx loses hl (DrawInstrFieldHelp); the same
    bytes from genCmpEq keep it (GetInstrEnvBytes)."""

    def source(self, marker):
        return [marker, "; AOP_STK for _f_s_1_1", "\tlda\thl,0(sp)", "\tld\ta,(hl+)",
                "\tor\ta,(hl)", "\tjp\tz,00168$", "; genIpush", "\tdec\thl",
                "\tld\ta,(hl+)", "\tld\th,(hl)", "\tld\tl,a", "\tpush\thl"]

    def test_ifx_test_reloads_hl(self):
        out, n = peep.ifx_word_hl(self.source("; genIfx"))
        self.assertEqual(n, 1)
        self.assertEqual(out[7], "\tlda\thl,0(sp)")
        # and track_a leaves that reload alone
        tracked, _ = peep.track_a(out, set())
        self.assertNotIn("\tdec\thl", tracked)

    def test_compare_keeps_hl(self):
        source = self.source("; genCmpEq")
        self.assertEqual(peep.ifx_word_hl(source), (source, 0))
        reloaded = source[:7] + ["\tlda\thl,0(sp)"] + source[8:]
        tracked, _ = peep.track_a(reloaded, set())
        self.assertEqual(tracked[7], "\tdec\thl")


class HlRegisterTests(unittest.TestCase):
    """After `ld c,(hl)` the byte at hl is c: a reread becomes `ld a,c`, even
    across a store through de (StartScreenPlayback), but not across one
    through hl or a change of c."""

    INDEX = ["\tld\thl,#_wSongCol + 0", "\tld\tc,(hl)", "\tld\ta,#<_wChanActive",
             "\tadd\ta,c", "\tld\te,a", "\tld\ta,#>_wChanActive", "\tadc\ta,#0x00",
             "\tld\td,a", "\tld\ta,#0x01"]
    STORE = ["\tld\thl,#_wPlayStartChan + 0", "\tld\t(hl),a"]

    def test_store_through_de_keeps_the_register(self):
        source = self.INDEX + ["\tld\t(de),a", "\tld\ta,(hl)"] + self.STORE
        out, _ = peep.track_a(source, set())
        self.assertEqual(out[10], "\tld\ta,c")

    def test_store_through_hl_or_new_register_value_rereads(self):
        through_hl = self.INDEX + ["\tld\t(hl),a", "\tld\ta,(hl)"] + self.STORE
        self.assertNotIn("\tld\ta,c", peep.track_a(through_hl, set())[0][9:])
        changed = self.INDEX + ["\tld\t(de),a", "\tinc\tc", "\tld\ta,(hl)"] + self.STORE
        self.assertEqual(peep.track_a(changed, set())[0][11], "\tld\ta,(hl)")

    def test_conditional_jump_ends_it_for_a_global_only(self):
        # FxCmd_E_Pu1: `wEnvSpeedPu1 = v; if (v) wEnvCounterPu1 = wEnvSpeedPu1;`
        test = ["\txor\ta,a", "\tor\ta,c", "\tjp\tz,00102$", "\tld\ta,(hl)"]
        source = ["\tld\thl,#_wEnvSpeedPu1 + 0", "\tld\t(hl),c"] + test
        self.assertEqual(peep.track_a(source, set())[0][5], "\tld\ta,(hl)")
        stack = ["\tlda\thl,2(sp)", "\tld\t(hl),c"] + test
        self.assertEqual(peep.track_a(stack, set())[0][5], "\tld\ta,c")

    def test_copy_of_a_stored_stack_byte_survives_a_bit_test(self):
        # InstrScreenInput's kit flags: `v = ReadCurInstrByte(5); if (!(v &
        # 64)) v |= 0x40;` -- the byte is copied, tested and ORed unreloaded
        source = ["\tlda\thl,7(sp)", "\tld\t(hl),e", "\tlda\tsp,1(sp)", "\tld\tc,(hl)",
                  "\tld\ta,c", "\tbit\t6,a", "\tjp\tnz,00102$", "\tld\ta,(hl)",
                  "\tor\ta,#0x40", "\tld\t(hl),a"]
        out, _ = peep.track_a(source, set())
        self.assertEqual(out[3:8], ["\tld\tc,e", "\tld\ta,c", "\tbit\t6,a", "\tjp\tnz,00102$",
                                    "\tor\ta,#0x40"])
        # an equality test rereads it (PhraseScreenInput's `if (k)`)
        tested = ["\tlda\thl,0(sp)", "\tld\t(hl),a", "\tor\ta,a", "\tjp\tz,00102$",
                  "\tld\ta,(hl)", "\tadd\ta,#0x10"]
        self.assertEqual(peep.track_a(tested, set())[0][4], "\tld\ta,(hl)")

    def test_a_literal_store_through_hl_keeps_a_only_when_equal(self):
        # ResetWorkState: `a` held 1, six `g = 0` stores, then `cell[0] = 1`
        # reloads the 1; TogglePlayback: `a` held 0, `wSongPlaying = 0`, then
        # `hSoundCh = 0` stores `a` unreloaded
        reload = ["\tld\ta,#0x01", "\tld\t(de),a", "\tld\thl,#_g", "\tld\t(hl),#0x00",
                  "\tld\tde,#_cell", "\tld\ta,#0x01", "\tld\t(de),a"]
        self.assertEqual(peep.track_a(reload, set())[0], reload)
        keep = ["\tld\ta,#0x00", "\tld\t(de),a", "\tld\thl,#_g", "\tld\t(hl),#0x00",
                "\tld\tde,#_cell", "\tld\ta,#0x00", "\tld\t(de),a"]
        self.assertEqual(peep.track_a(keep, set())[0], keep[:5] + keep[6:])


class HramReuseTests(unittest.TestCase):
    """track_hram: a reload into the register holding the byte goes; no copy
    from another register; `a` serves in straight-line code and across an
    ordering test; a store through a pointer loaded from memory ends the
    reuse of a byte carried across a jump."""

    READ_C = ["\tldh\ta,(_hSoundCh+0)", "\tld\tc,a"]
    READ_B = ["\tldh\ta,(_hSoundCh+0)", "\tld\tb,a"]
    INDEX_C = ["\tld\ta,#<_wArr", "\tadd\ta,c", "\tld\te,a", "\tld\ta,#>_wArr",
               "\tadc\ta,#0x00", "\tld\td,a", "\tld\ta,#0x00", "\tld\t(de),a"]

    def run_pass(self, lines):
        return peep.track_hram(lines, set())[0]

    def test_reload_into_the_holding_register_goes_even_after_a_store(self):
        out = self.run_pass(self.READ_C + self.INDEX_C + self.READ_C + self.INDEX_C)
        self.assertEqual(out, self.READ_C + self.INDEX_C + self.INDEX_C)

    def test_pair_goes_when_a_dies_a_few_lines_on(self):
        # SndStartChanFromRow: the 16-bit index of wChanPhrase[hSoundCh]
        word = ["\tld\tde,#_wChanPhrase", "\tld\tl,c", "\tld\th,#0x00", "\tadd\thl,de", "\tld\ta,l"]
        out = self.run_pass(self.READ_C + self.INDEX_C + self.READ_C + word)
        self.assertEqual(out, self.READ_C + self.INDEX_C + word)
        kept = self.READ_C + self.INDEX_C + self.READ_C + ["\tld\tde,#_wX", "\tpush\taf"]
        self.assertEqual(self.run_pass(kept), kept)

    def test_no_copy_from_another_register(self):
        source = self.READ_C + self.INDEX_C + self.READ_B + ["\tld\tde,#_wArr"]
        self.assertEqual(self.run_pass(source), source)

    def test_a_serves_only_across_an_ordering_test(self):
        ordering = ["\tldh\ta,(_hSoundCh+0)", "\tcp\ta,#0x04", "\tjp\tnc,00109$"] + self.READ_C
        self.assertEqual(self.run_pass(ordering), ordering[:3] + ["\tld\tc,a"])
        equality = ["\tldh\ta,(_hSoundCh+0)", "\tcp\ta,#0x02", "\tjp\tnz,00109$",
                    "\tldh\ta,(_hSoundCh+0)", "\tadd\ta,a"]
        self.assertEqual(self.run_pass(equality), equality)

    def test_a_serves_in_straight_line_code(self):
        # FxCmd_F_Wav's `step = hFxValS`: genCast reads the sfr again for
        # the sign; no compiled code in the ROM rereads a byte a holds
        widen = ["\tldh\ta,(_hFxValueS+0)", "\tlda\thl,2(sp)", "\tld\t(hl),a",
                 "\tldh\ta,(_hFxValueS+0)", "\trla", "\tsbc\ta,a"]
        self.assertEqual(self.run_pass(widen), widen[:3] + widen[4:])
        changed = ["\tldh\ta,(_hFxValueS+0)", "\tadd\ta,#0x01", "\tld\tc,a",
                   "\tldh\ta,(_hFxValueS+0)", "\trla"]
        self.assertEqual(self.run_pass(changed), changed)

    def test_store_through_a_loaded_pointer_ends_a_carried_byte(self):
        # SndUpdateChan's D: c read before `if (d)`, then `+=` through a
        # stack pointer, then a reread; StartSongPlayback's `*active = 1` after an
        # in-block read keeps it
        store = ["\tlda\thl,0(sp)", "\tld\te,(hl)", "\tinc\thl", "\tld\td,(hl)", "\tld\t(de),a"]
        test = ["\txor\ta,a", "\tor\ta,b", "\tjp\tz,00105$"]
        carried = self.READ_C + test + store + self.READ_C + self.INDEX_C
        self.assertEqual(self.run_pass(carried), carried)
        in_block = self.READ_C + store + self.READ_C + self.INDEX_C
        self.assertEqual(self.run_pass(in_block), self.READ_C + store + self.INDEX_C)



class DeadLoadTest(unittest.TestCase):
    """Any side-effect-free `ld r,X` rewritten unread in its block goes,
    and so does the `inc hl` that fed it (FileUpgradeV0Kit's narrowed
    16-bit `&`: the ROM keeps no dead register load of any kind)."""

    def test_dead_high_byte_load_and_its_step_go(self):
        src = ["_F:", "\tld\ta,(hl)", "\tand\ta,#0xDF", "\tld\tb,a", "\tinc\thl", "\tld\tc,(hl)",
               "\tlda\thl,6(sp)", "\tld\t(hl),b", "\tlda\thl,4(sp)", "\tld\tc,(hl)", "\tld\tb,#0x00",
               "\tld\ta,c", "\tret"]
        out, n = peep.dead_stores(src, set())
        self.assertEqual(n, 1)
        self.assertNotIn("\tld\tc,(hl)", out[:7])
        out2, n2 = peep.dead_hl_steps(out, set())
        self.assertEqual(n2, 1)
        self.assertNotIn("\tinc\thl", out2)

    def test_loads_with_side_effects_or_reads_stay(self):
        src = ["_F:", "\tld\tc,(hl+)", "\tld\tc,#0x01", "\tld\tb,(hl)", "\tld\ta,b", "\tld\tb,#0x02", "\tret"]
        self.assertEqual(peep.dead_stores(src, set()), (src, 0))
        step = ["_F:", "\tinc\thl", "\tld\ta,(hl)", "\tlda\thl,2(sp)", "\tret"]
        self.assertEqual(peep.dead_hl_steps(step, set()), (step, 0))


class ChainedBitwiseTest(unittest.TestCase):
    """A bitwise result kept in a for the next literal op (FileUpgradeV3Kit)."""

    HEAD = ["_F:", "\tpush\tbc", "\tld\tb,c", "\tld\ta,b", "\tand\ta,#0xF9"]

    def test_store_dead_across_a_call_is_dropped(self):
        tail = ["\tor\ta,#0x01", "\tld\tc,a", "\tpush\taf", "\tinc\tsp",
                "\tcall\t_FileWriteInstrByte", "\tlda\tsp,2(sp)", "00101$:", "\tpop\tbc", "\tret"]
        out, n = peep.chained_bitwise(self.HEAD + ["\tld\tb,a"] + tail)
        self.assertEqual((out, n), (self.HEAD + tail, 1))

    def test_store_dead_through_a_jump_is_dropped(self):
        # FileUpgradeV3Pulse's cases join at the write
        tail = ["\tor\ta,#0x10", "\tld\tc,a", "\tjp\t00105$", "00104$:", "\tld\tc,#0x00",
                "00105$:", "\tld\ta,c", "\tpush\taf", "\tpop\tbc", "\tret"]
        out, n = peep.chained_bitwise(self.HEAD + ["\tld\tb,a"] + tail)
        self.assertEqual((out, n), (self.HEAD + tail, 1))

    def test_store_read_later_or_on_a_branch_stays(self):
        read = self.HEAD + ["\tld\tb,a", "\tor\ta,#0x01", "\tld\tc,a", "\tld\ta,b", "\tret"]
        self.assertEqual(peep.chained_bitwise(read), (read, 0))
        branch = self.HEAD + ["\tld\tb,a", "\tor\ta,#0x01", "\tjp\tz,00103$", "\tpop\tbc",
                              "\tret", "00103$:", "\tld\ta,b", "\tret"]
        self.assertEqual(peep.chained_bitwise(branch), (branch, 0))

    def test_d_and_e_die_at_a_call_and_cp_is_not_chained(self):
        de = self.HEAD + ["\tld\te,a", "\txor\ta,#0x0F", "\tcall\t_G", "\tld\ta,e", "\tret"]
        self.assertEqual(peep.chained_bitwise(de)[1], 1)
        cp = self.HEAD + ["\tld\tb,a", "\tcp\ta,#0x02", "\tret"]
        self.assertEqual(peep.chained_bitwise(cp), (cp, 0))

class IncrementalApplyTest(unittest.TestCase):
    """`apply` (a rule re-attempts only the positions a replacement could
    have changed) gives the reference full-scan matcher's output: restart
    rules, a non-restart rule continuing past its replacement, a
    `labelRefCount` rule enabled by a jump another rule removed, and a
    dropped trampoline label whose conditional jumps are retargeted."""

    RULES = """
replace restart { ld a,%1 ; ld %2,a } by { ld %2,%1 }
replace { ld %1,%2 ; ld %1,%3 } by { ld %1,%3 }
replace restart { jp %1 ; %1: } by { %1: }
replace restart { %1: } by { } if labelRefCount %1 0
replace restart { jp %1 ; %2: ; jp %3 } by { jp %1 ; %2: ; jp %3 } if labelRefCount %2 99
replace restart { or a,a ; jp z,%1 ; %2: ; jp %3 } by { or a,a ; jp nz,%3 ; %2: ; jp %3 }
"""

    def src(self, n):
        out = ["_F:"]
        for k in range(n):
            out += [f"\tld\ta,(hl)", f"\tld\tc,a", f"\tld\tc,#{k}", f"\tld\tc,b",
                    f"\tjp\t{100 + k}$", f"{100 + k}$:", "", f"\tor\ta,a", f"\tjp\tz,{200 + k}$",
                    f"{300 + k}$:", f"\tjp\t{400 + k}$", f"{200 + k}$:", f"\tld\ta,c", "; comment",
                    f"\tjp\tnz,{300 + k}$", f"{400 + k}$:"]
        out.append("\tret")
        return out

    def test_matches_full_scan(self):
        rules = peep.parse_rules(self.RULES.replace(" ; ", "\n"))
        self.assertEqual(len(rules), 6)
        src = self.src(12)
        full, n_full = peep.apply_full(rules, src)
        inc, n_inc = peep.apply(rules, src)
        self.assertGreater(n_full, 12)
        self.assertEqual(inc, full)
        self.assertEqual(n_inc, n_full)

    def test_ruleless_and_empty(self):
        rules = peep.parse_rules(self.RULES.replace(" ; ", "\n"))
        self.assertEqual(peep.apply(rules, []), ([], 0))
        self.assertEqual(peep.apply([], ["\tnop"]), (["\tnop"], 0))


if __name__ == "__main__":
    unittest.main()
