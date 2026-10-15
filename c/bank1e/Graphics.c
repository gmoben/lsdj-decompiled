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
extern UWORD wTileSlotPointer;              /* $C9B5 */
extern UBYTE TilesUiIcons[];                /* $1E:$42C8 */
/* the fonts' tile blocks, each with its highlight and shaded variant
   ($1E:$54C0, $6356, $462A) */
extern UBYTE TilesFontY2K[], TilesFontY2KHighlight[], TilesFontY2KShaded[];
extern UBYTE TilesFontZero[], TilesFontZeroHighlight[], TilesFontZeroShaded[];
extern UBYTE TilesFontKiko[], TilesFontKikoHighlight[], TilesFontKikoShaded[];
void ResetDisplayState(void);               /* $00:$339A */
UWORD LoadTilesDispatch(UBYTE *tiles);      /* $00:$337D */
void RequestTileSlot(UWORD slot);           /* $00:$338B */
void FillVramRegions(void);
void CopyVramTile(UWORD source, UWORD destination); /* $1E:$7A4B, parked: one tile, src to dst */
void WriteVramByteSafe(UWORD address, UBYTE v); /* $00:$17EC */
void WriteVramByteSafeC(UWORD address, char v); /* WriteVramByteSafe with a char value (aliased by match_function.py): the cast copy of the scratch byte is CopyVramTile's register occupant */
extern UBYTE wVramCopyScratch;              /* $C9B4 */

/* $4000: the top row's column labels for the screen about to be drawn
   (raw tile indices through IdentityCharacterMap; nothing on the wave screen) */

void DrawScreenColumnLabels(void)
{
    if (wCurrentScreen == SCREEN_WAVE)
        return;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(2, 1);
    DrawString(TxtSpaces13);
    wStringSource = IdentityCharacterMap;
    hBgPalette = hGameboyType ? 0x48 : 0xD2;
    switch (wCurrentScreen) {
    case SCREEN_PHRASE:
        if (wPhraseDrawMode) {
            wVramCursor = (UBYTE *)BG_MAP(2, 1);
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            wVramCursor = (UBYTE *)BG_MAP(9, 1);
            DrawStringIndexed(0x13);
            DrawStringIndexed(0x14);
            DrawStringIndexed(0x15);
            DrawStringIndexed(0x16);
            DrawStringIndexed(0x17);
        } else {
            wVramCursor = (UBYTE *)BG_MAP(2, 1);
            DrawStringIndexed(0x10);
            DrawStringIndexed(0x11);
            DrawStringIndexed(0x12);
            wVramCursor = (UBYTE *)BG_MAP(6, 1);
            DrawStringIndexed(0x13);
            DrawStringIndexed(0x14);
            DrawStringIndexed(0x15);
            DrawStringIndexed(0x16);
            DrawStringIndexed(0x17);
        }
        break;
    case SCREEN_CHAIN:
        wVramCursor = (UBYTE *)BG_MAP(2, 1);
        DrawStringIndexed(0x18);
        DrawStringIndexed(0x19);
        wVramCursor = (UBYTE *)BG_MAP(5, 1);
        DrawStringIndexed(0x1A);
        DrawStringIndexed(0x1B);
        break;
    case SCREEN_TABLE:
        wVramCursor = (UBYTE *)BG_MAP(2, 1);
        DrawStringIndexed(0x1C);
        DrawStringIndexed(0x1D);
        wVramCursor = (UBYTE *)BG_MAP(5, 1);
        DrawStringIndexed(0x1A);
        DrawStringIndexed(0x1B);
        DrawStringIndexed(0x16);
        DrawStringIndexed(0x17);
        wVramCursor = (UBYTE *)BG_MAP(11, 1);
        DrawStringIndexed(0x16);
        DrawStringIndexed(0x17);
        break;
    case SCREEN_GROOVE:
        wVramCursor = (UBYTE *)BG_MAP(2, 1);
        DrawStringIndexed(0x1E);
        DrawStringIndexed(0x1F);
        DrawStringIndexed(0x20);
        DrawStringIndexed(0x21);
        DrawStringIndexed(0x22);
        break;
    case SCREEN_WORD:
        wVramCursor = (UBYTE *)BG_MAP(2, 1);
        DrawStringIndexed(0x23);
        DrawStringIndexed(0x24);
        wVramCursor = (UBYTE *)BG_MAP(6, 1);
        DrawStringIndexed(0x25);
        DrawStringIndexed(0x26);
        break;
    case SCREEN_SONG:
        wVramCursor = (UBYTE *)BG_MAP(3, 1);
        DrawStringIndexed(0x27);
        DrawStringIndexed(0x28);
        wVramCursor = (UBYTE *)BG_MAP(6, 1);
        DrawStringIndexed(0x27);
        DrawStringIndexed(0x29);
        wVramCursor = (UBYTE *)BG_MAP(9, 1);
        DrawStringIndexed(0x2A);
        DrawStringIndexed(0x2B);
        wVramCursor = (UBYTE *)BG_MAP(12, 1);
        DrawStringIndexed(0x2C);
        DrawStringIndexed(0x2D);
        break;
    }
    wStringSource = AsciiCharacterMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
}

/* $79FA: blank the right third of the tile rows (two VRAM ranges) */
void FillVramRegions(void)
{
    UWORD address;

    for (address = 0x9480; address != (UWORD)BG_MAP(0, 0); address += 2)
        WriteVramByteSafe(address, 0xFF);
    for (address = 0x8800; address != (UWORD)0x88D0; address += 2)
        WriteVramByteSafe(address, 0xFF);
}

/* $7A38: wait until the LCD lets the CPU at VRAM, then read a byte */
UBYTE ReadVramByte(UBYTE *p)
{
    while (STAT_REG & 2)
        ;
    return *p;
}

/* $7A4B: copy one tile (16 bytes) from VRAM address src to dst through
   the scratch byte, one WriteVramByteSafe per byte. The value argument is
   a char: its cast copy of the scratch byte is evaluated before dst++
   (arguments right to left), owns c, and is evicted into the global by
   the pointer copy -- which is why the ROM's second pair is [b c] and the
   push still reads the scratch byte from memory. */
void CopyVramTile(UWORD source, UWORD destination)
{
    UBYTE byte;

    for (byte = 0; byte != 0x10; byte++) {
        wVramCopyScratch = ReadVramByte((UBYTE *)source++);
        WriteVramByteSafeC(destination++, wVramCopyScratch);
    }
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
        wShadedDigitPalette = 4;
        wHighlightDigitPalette = 4;
        wShadedPalette = 1;
        wHighlightPalette = 1;
        a = TilesFontY2K;
        switch (wFont) {
        case FONT_ZERO:
            a = TilesFontZero;
            break;
        case FONT_KIKO:
            a = TilesFontKiko;
            break;
        }
        wTileSlotPointer = LoadTilesDispatch(a);
    } else {
        a = TilesFontY2K;
        b = TilesFontY2KHighlight;
        c = TilesFontY2KShaded;
        switch (wFont) {
        case FONT_ZERO:
            a = TilesFontZero;
            b = TilesFontZeroHighlight;
            c = TilesFontZeroShaded;
            break;
        case FONT_KIKO:
            a = TilesFontKiko;
            b = TilesFontKikoHighlight;
            c = TilesFontKikoShaded;
            break;
        }
        wTileSlotPointer = LoadTilesDispatch(a);
        LoadTilesDispatch((UBYTE *)(UWORD)c);  /* each through a cast temp in bc */
        LoadTilesDispatch((UBYTE *)(UWORD)b);
        if (wColorSet & 2)
            FillVramRegions();
    }
    LoadTilesDispatch(TilesUiIcons);
    CopyVramTile(0x9020, 0x8030);
    CopyVramTile(0x9280, 0x8040);
    _asm ei _endasm;
    RequestTileSlot(wTileSlotPointer);
}
