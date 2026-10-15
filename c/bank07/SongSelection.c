/*
 * Song screen selection highlight -- LSDj 9.4.0, ROM bank $07:$5B1D-$5DD7.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit: SortBytePair is prototyped here with `char *` parameters
 * (match_function.py aliases SortBytePairC to the ROM routine), so the
 * `&UBYTE` locals go through cast temps -- the first is spilled below the
 * stack frame's variables, the second lands in bc, as in the ROM.
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSongPasteCol;                 /* $C94E */
extern UBYTE wSongPasteRow;                 /* $C94F */
extern UBYTE SongColOffsets[];              /* $07:$4047 */
void EraseSongCursor(void);
void SortBytePairC(char *a, char *b);      /* SortBytePair as this unit declares it */
void DrawTileRun(UBYTE x, UBYTE y, UBYTE w, UBYTE h, UBYTE tile);   /* asm */

const UBYTE SongSelectionAttr[32] = {             /* $07:$5DB8: CGB attribute 3 for the selection */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
};

void SongEditSelection(void)
{
    UBYTE i;
    UBYTE x0;
    UBYTE y0;
    UBYTE x1;
    UBYTE y1;
    UBYTE y;

    if (wClipboardType != 4)
        return;
    if (wSelectionLeft >= 4)
        DebugTrap();
    if (wSongCol >= 4)
        DebugTrap();
    x0 = wSelectionLeft;
    x1 = wSongCol;
    SortBytePairC((char *)&x0, (char *)&x1);
    y0 = wSelectionTop;
    y1 = wSongRow;
    SortBytePairC((char *)&y0, (char *)&y1);
    if (y0 < wSongScroll)
        y0 = 0;
    else
        y0 -= wSongScroll;
    if (wSongScroll + 0x0F < y1)
        y1 = 0x10;
    else
        y1 = y1 - wSongScroll + 1;
    x1++;
    wSongPasteCol = wSongCol;
    wSongPasteRow = wSongRow;
    wSongCol = x0 ? x0 - 1 : 0;
    while (wSongCol < (x1 == 4 ? 4 : x1 + 1)) {
        wSongRow = wSongScroll + (y0 ? y0 - 1 : 0);
        while (wSongRow < wSongScroll + (y1 == 0x10 ? 0x10 : y1 + 1)) {
            if (wSongCol < x0 || wSongCol >= x1 || wSongRow < wSongScroll + y0 || wSongRow >= wSongScroll + y1)
                EraseSongCursor();
            wSongRow++;
        }
        wSongCol++;
    }
    wSongCol = wSongPasteCol;
    wSongRow = wSongPasteRow;
    for (i = x0; i < x1; i++) {
        y = y0 + 2;
        if (i >= 4)
            DebugTrap();
        if (y >= 0x12)
            DebugTrap();
        if (y + y1 - y0 - 1 >= 0x12)
            DebugTrap();
        if (hGameboyType) {
            VBK_REG = 1;
            set_bkg_tiles(SongColOffsets[i], y, 2, y1 - y0, SongSelectionAttr);
            VBK_REG = 0;
        } else {
            DrawTileRun(SongColOffsets[i], y, 2, y1 - y0, 2);
        }
    }
}
