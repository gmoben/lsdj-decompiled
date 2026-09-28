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
 * What it does: pad the transient message in wStrBuffer to 20 characters
 * (spaces, then a NUL), point wMsgStrPtr at it, on CGB write the
 * MsgBarAttrRow attributes to the top tilemap row (VRAM bank 1), select the
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
 *  - `&wStrBuffer[0]` (or a cast) makes SDCC load the address into de first,
 *    as the ROM does; plain `wStrBuffer` stores it as two immediates.
 *  - `if (!wStrBuffer[i])` gives the ROM's direct `jp nz` to the loop step;
 *    `== 0` produces a jp/jr trampoline instead.
 */

#ifdef __clang__  /* editor tooling only: SDCC 2.2.1 keyword (never set by SDCC) */
#define sfr
#endif

typedef unsigned char UBYTE;

extern volatile sfr UBYTE VBK_REG;          /* $FF4F rVBK */
extern volatile sfr UBYTE hBgPalette;       /* $FF8C */
extern volatile sfr UBYTE hBgDigitPalette;  /* $FF8D */
extern UBYTE hGameboyType;                  /* $FF90 */

extern UBYTE wStrBuffer[];                  /* $C6A9, 20 chars + NUL */
extern UBYTE *wMsgStrPtr;                   /* $C7AA */
extern UBYTE wMsgPanelShown;                /* $C7AC */
extern UBYTE wMsgTimer;                     /* $C7A9 */
extern UBYTE wSelectDigitPal;               /* $C568 */
extern UBYTE wSelectPal;                    /* $C569 */
extern UBYTE *wVramCursor;                  /* $C573 */
extern UBYTE MsgBarAttrRow[];               /* $01:$7484, 20 x $04 */

/* GBDK 2.95 <gb/gb.h>; bank 0 $39CD is the library routine, byte-exact */
void set_bkg_tiles(UBYTE x, UBYTE y, UBYTE w, UBYTE h, unsigned char *tiles);
void DrawStringIndexed(UBYTE c);

void DrawMessageBar(void)
{
    UBYTE i;
    UBYTE *saved;

    wMsgPanelShown = 0;
    wMsgStrPtr = &wStrBuffer[0];
    for (i = 0; i < 20; i++) {
        if (!wStrBuffer[i]) {
            while (i < 20)
                wStrBuffer[i++] = ' ';
            wStrBuffer[i] = 0;
            break;
        }
    }
    if (hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0, 0, 20, 1, MsgBarAttrRow);
        VBK_REG = 0;
    }
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    saved = wVramCursor;
    wVramCursor = (UBYTE *)0x9800;
    for (i = 0; i < 20; i++)
        DrawStringIndexed(wMsgStrPtr[i]);
    wVramCursor = saved;
    wMsgTimer = 50;
}
