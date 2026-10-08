/* Bank $1E: the top row's column labels, the font loader and the VRAM
 * helpers (ROM order).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#include "lsdj.h"

extern UBYTE wPhraseDrawMode;               /* $C90C */
extern char TxtSpaces13[];                  /* $1E:$79EC, 13 spaces */

extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wColorSet;                     /* $C52F */
extern UWORD wTileSlotPtr;                  /* $C9B5 */
extern UBYTE TilesUiIcons[];                /* $1E:$42C8 */
extern UBYTE TilesFont0A[], TilesFont0B[], TilesFont0C[];   /* $1E:$54C0.. */
extern UBYTE TilesFont1A[], TilesFont1B[], TilesFont1C[];   /* $1E:$6356.. */
extern UBYTE TilesFont2A[], TilesFont2B[], TilesFont2C[];   /* $1E:$462A.. */
void ResetDisplayState(void);               /* $00:$339A */
UWORD LoadTilesDispatch(UBYTE *tiles);      /* $00:$337D */
void RequestTileSlot(UWORD slot);           /* $00:$338B */
void GfxFillVramRegions(void);
void GfxCopyVramRegion(UWORD src, UWORD dst);  /* $1E:$7A4B, parked: one tile, src to dst */
void WriteVramByteSafe(UWORD addr, UBYTE v);    /* $00:$17EC */

/* $4000: the top row's column labels for the screen about to be drawn
   (raw tile indices through IdentityCharMap; nothing on the wave screen) */

void ScreenPreDraw(void)
{
    if (wCurrentScreen == 8)
        return;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9822;
    DrawString(TxtSpaces13);
    wStringSrc = IdentityCharMap;
    hBgPalette = hGameboyType ? 0x48 : 0xD2;
    switch (wCurrentScreen) {
    case 1:
        if (wPhraseDrawMode) {
            wVramCursor = (UBYTE *)0x9822;
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            wVramCursor = (UBYTE *)0x9829;
            DrawStringIndexed(0x13);
            DrawStringIndexed(0x14);
            DrawStringIndexed(0x15);
            DrawStringIndexed(0x16);
            DrawStringIndexed(0x17);
        } else {
            wVramCursor = (UBYTE *)0x9822;
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            wVramCursor = (UBYTE *)0x9826;
            DrawStringIndexed(0x13);
            DrawStringIndexed(0x14);
            DrawStringIndexed(0x15);
            DrawStringIndexed(0x16);
            DrawStringIndexed(0x17);
        }
        break;
    case 3:
        wVramCursor = (UBYTE *)0x9822;
        DrawStringIndexed(0x18);
        DrawStringIndexed(0x19);
        wVramCursor = (UBYTE *)0x9825;
        DrawStringIndexed(0x1A);
        DrawStringIndexed(0x1B);
        break;
    case 5:
        wVramCursor = (UBYTE *)0x9822;
        DrawStringIndexed(0x1C);
        DrawStringIndexed(0x1D);
        wVramCursor = (UBYTE *)0x9825;
        DrawStringIndexed(0x1A);
        DrawStringIndexed(0x1B);
        DrawStringIndexed(0x16);
        DrawStringIndexed(0x17);
        wVramCursor = (UBYTE *)0x982B;
        DrawStringIndexed(0x16);
        DrawStringIndexed(0x17);
        break;
    case 2:
        wVramCursor = (UBYTE *)0x9822;
        DrawStringIndexed(0x1E);
        DrawStringIndexed(0x1F);
        DrawStringIndexed(0x20);
        DrawStringIndexed(0x21);
        DrawStringIndexed(0x22);
        break;
    case 0x0C:
        wVramCursor = (UBYTE *)0x9822;
        DrawStringIndexed(0x23);
        DrawStringIndexed(0x24);
        wVramCursor = (UBYTE *)0x9826;
        DrawStringIndexed(0x25);
        DrawStringIndexed(0x26);
        break;
    case 4:
        wVramCursor = (UBYTE *)0x9823;
        DrawStringIndexed(0x27);
        DrawStringIndexed(0x28);
        wVramCursor = (UBYTE *)0x9826;
        DrawStringIndexed(0x27);
        DrawStringIndexed(0x29);
        wVramCursor = (UBYTE *)0x9829;
        DrawStringIndexed(0x2A);
        DrawStringIndexed(0x2B);
        wVramCursor = (UBYTE *)0x982C;
        DrawStringIndexed(0x2C);
        DrawStringIndexed(0x2D);
        break;
    }
    wStringSrc = AsciiCharMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
}

/* $79FA: blank the right third of the tile rows (two VRAM ranges) */
void GfxFillVramRegions(void)
{
    UWORD p;

    for (p = 0x9480; p != (UWORD)0x9800; p += 2)
        WriteVramByteSafe(p, 0xFF);
    for (p = 0x8800; p != (UWORD)0x88D0; p += 2)
        WriteVramByteSafe(p, 0xFF);
}

/* $7A38: wait until the LCD lets the CPU at VRAM, then read a byte */
UBYTE GfxReadVramByte(UBYTE *p)
{
    while (STAT_REG & 2)
        ;
    return *p;
}

/* $7AA0: the font (wFont) and UI tiles into VRAM. On CGB the palettes are
   fixed and one block per font suffices; DMG takes three blocks and can
   blank the right third (colour set bit 1). */
void LoadFontTiles(void)
{
    UBYTE *a, *b, *c;

    ResetDisplayState();
    _asm di _endasm;
    if (hGameboyType) {
        wShadowDigitPal = 4;
        wSelectDigitPal = 4;
        wShadowPal = 1;
        wSelectPal = 1;
        a = TilesFont0A;
        switch (wFont) {
        case 1:
            a = TilesFont1A;
            break;
        case 2:
            a = TilesFont2A;
            break;
        }
        wTileSlotPtr = LoadTilesDispatch(a);
    } else {
        a = TilesFont0A;
        b = TilesFont0B;
        c = TilesFont0C;
        switch (wFont) {
        case 1:
            a = TilesFont1A;
            b = TilesFont1B;
            c = TilesFont1C;
            break;
        case 2:
            a = TilesFont2A;
            b = TilesFont2B;
            c = TilesFont2C;
            break;
        }
        wTileSlotPtr = LoadTilesDispatch(a);
        LoadTilesDispatch((UBYTE *)(UWORD)c);  /* each through a cast temp in bc */
        LoadTilesDispatch((UBYTE *)(UWORD)b);
        if (wColorSet & 2)
            GfxFillVramRegions();
    }
    LoadTilesDispatch(TilesUiIcons);
    GfxCopyVramRegion(0x9020, 0x8030);
    GfxCopyVramRegion(0x9280, 0x8040);
    _asm ei _endasm;
    RequestTileSlot(wTileSlotPtr);
}
