/* Bank $1E: the top row's column labels and the VRAM helpers (ROM order).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#include "lsdj.h"

extern UBYTE wPhraseDrawMode;               /* $C90C */
extern char TxtSpaces13[];                  /* $1E:$79EC, 13 spaces */

extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
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
