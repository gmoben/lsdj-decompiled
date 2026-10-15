/*
 * DrawTileHexCell -- LSDj 9.4.0, ROM bank $06:$7524-$75A5 (130 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank06/DrawTileHexCell.c --target DrawTileHexCell
 *
 * Called from DrawNoteName for cell values above $77. Rebases the value by
 * -60, derives t = (v+1)/4 - 9 and a 2-character prefix HexCellPrefixPointers[v&3]
 * ("D ", "F ", "G#", "C "), then draws either prefix[0] '-' |t| (t negative)
 * or the prefix followed by t (via DrawString / DrawByte). The table and the
 * four strings sit right after the function in the ROM (same translation
 * unit); they are referenced here as externs.
 *
 * This function is why the peephole rule set grew past DrawMessageBar's six
 * rules: LSDj's compiler drops the reload of a value it just stored after an
 * ALU op (`add a,#N; ld (hl),a; [ld a,(hl)]`), drops stores that are
 * overwritten before being read (`t = v+1; t >>= 2; t -= 9` compiles to one
 * store), and keeps the one register reload that follows a pointer read
 * through the same register pair (`ld a,(bc); ld c,a; ld a,c` at $7553). See
 * docs/toolchain.md.
 *
 * Source forms that matter: three separate statements for t (a single
 * expression puts the shift in `b`); `t & 0x80` for the sign test (bit 7,a);
 * `t` unsigned (`-t` is `xor a; sbc a,(hl)`).
 */

#include "lsdj.h"

extern char *HexCellPrefixPointers[];       /* $06:$75A6, [4] -> TxtHexCellPrefix0..3 */

void DrawTileHexCell(UBYTE v)
{
    UBYTE t;

    v -= 60;
    t = v + 1;
    t >>= 2;
    t -= 9;
    if (t & 0x80) {
        DrawStringIndexed(HexCellPrefixPointers[v & 3][0]);
        DrawStringIndexed('-');
        DrawByte(-t);
    } else {
        wChainScratchPointer = HexCellPrefixPointers[v & 3];
        DrawString(wChainScratchPointer);
        DrawByte(t);
    }
}
