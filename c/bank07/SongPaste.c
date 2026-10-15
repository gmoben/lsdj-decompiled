/*
 * Song screen paste -- LSDj 9.4.0, ROM bank $07:$5970-$5B0E.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit: wSongRow is declared volatile here (the row-shifting loop
 * recomputes `wSongRow + h` instead of reusing the test's value, which a
 * plain global would share), and WriteSongSramBytes is prototyped with
 * UWORD source/destination (the ROM casts both pointers).
 */

#include "lsdj.h"

extern volatile UBYTE wSongRow;             /* $C41E: this unit declares the cursor row volatile */

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wSelectionTop;                 /* $C40E */
extern UBYTE wSelectionRight;               /* $C40F */
extern UBYTE wSelectionBottom;              /* $C410 */
void WriteSongSramBytes(UWORD source, UWORD destination, UBYTE n); /* asm $07:$7422; this unit casts both pointers */
void AdjustBookmarksOnInsert(UBYTE from, UBYTE to);
void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain);

/* $5970: paste the clipboard rows into the song grid at the cursor: shifts
   the columns down (WriteSongSramBytes) to make room, writes the cells,
   PASTED CHAINS */
void PasteSongSelection(void)
{
    UBYTE column;
    UBYTE n;
    UBYTE columnOffset;
    UBYTE h;
    UBYTE *source;
    UBYTE *p;
    UBYTE w;

    columnOffset = 0;
    w = wSelectionRight - wSelectionLeft;
    if (w == 1)
        columnOffset = wSelectionLeft - wSongColumn;
    h = wSelectionBottom - wSelectionTop;
    if (!wSelectionLeft && wSelectionRight == CHANNELS)
        AdjustBookmarksOnInsert(wSongRow, wSongRow + h);
    if (wSongRow + h < 0xBF) {
        for (column = wSelectionLeft; column < wSelectionRight; column++) {
            p = (UBYTE *)0xB58C + (column - columnOffset);
            source = p - ((UWORD)h << 2);
            if ((UWORD)p < (UWORD)0xB290)
                DebugTrap();
            if ((UWORD)p >= (UWORD)0xB590)
                DebugTrap();
            n = (UWORD)0xC0 - (wSongRow + h);
            WriteSongSramBytes((UWORD)source, (UWORD)p, n);
        }
    }
    for (n = 0; n < h; n++) {
        for (column = wSelectionLeft; column < wSelectionRight; column++) {
            WriteSongCell(column - columnOffset, wSongRow, ReadClipboardCell(column, n));
        }
        if (wSongRow == 0xBF)
            break;
        wSongRow++;
        if (!((wSongRow - wSongScroll) & 0xF0))
            continue;
        wSongScroll++;
    }
    if (w > 1 || h > 1)
        ShowMessage("PASTED CHAINS");           /* the literal follows the function ($5B0F) */
}
