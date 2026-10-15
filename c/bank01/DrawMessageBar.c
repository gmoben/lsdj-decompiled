/*
 * DrawMessageBar -- LSDj 9.4.0, ROM bank $01:$74F7-$75BC (198 bytes)
 *
 * MATCHING C: compiled with the period GBDK 2.95-3 / SDCC 2.2.1 plus the
 * recovered LSDj peephole rules (tools/match/lsdj.peep), assembled with
 * as-gbz80 and linked at $74F7, this is byte-identical to the ROM. Verify:
 *
 *     tools/match/match_function.py --c c/bank01/DrawMessageBar.c \
 *         --target DrawMessageBar
 *
 * What it does: pad the transient message in wStrBuf to 20 characters
 * (spaces, then a NUL), point wMessageStrPtr at it, on CGB write the
 * MessageBarAttrRow attributes to the top tilemap row (VRAM bank 1), select the
 * "selected" draw palettes, draw the 20 characters at $9800, and arm the
 * 50-frame message timer.
 *
 * Source-level details the match depends on (see docs/toolchain.md):
 *  - HRAM/IO bytes stored with `ldh` are `extern volatile sfr` (GBDK 2.95's
 *    USE_SFR_FOR_REG style); a plain `*(UBYTE *)0xFFxx` store compiles to
 *    `ld de,#0xFFxx; ld (de),a`, and testing one in an `if` crashes SDCC 2.2.1.
 *  - hGameboyType is read through hl (`xor a; ld hl,...; or a,(hl)`), i.e. an
 *    ordinary extern variable that the link places at $FF90, not an sfr.
 *  - The attribute-row write is GBDK's own set_bkg_tiles(). Calls are direct
 *    (GBDK 2.95-3's default small model; plain 2.95 needs `nonbanked`).
 *  - `&wStrBuf[0]` (or a cast) makes SDCC load the address into de first,
 *    as the ROM does; plain `wStrBuf` stores it as two immediates.
 *  - `if (!wStrBuf[i])` gives the ROM's direct `jp nz` to the loop step;
 *    `== 0` produces a jp/jr trampoline instead.
 */

#include "lsdj.h"

extern UBYTE MessageBarAttrRow[];               /* $01:$7484, 20 x $04 */

void DrawMessageBar(void)
{
    UBYTE i;
    UBYTE *saved;

    wMessagePanelShown = 0;
    wMessageStrPtr = &wStrBuf[0];
    for (i = 0; i < 20; i++) {
        if (!wStrBuf[i]) {
            while (i < 20)
                wStrBuf[i++] = ' ';
            wStrBuf[i] = 0;
            break;
        }
    }
    if (hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0, 0, 20, 1, MessageBarAttrRow);
        VBK_REG = 0;
    }
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    saved = wVramCursor;
    wVramCursor = (UBYTE *)0x9800;
    for (i = 0; i < 20; i++)
        DrawStringIndexed(wMessageStrPtr[i]);
    wVramCursor = saved;
    wMessageTimer = 50;
}
