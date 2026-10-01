/*
 * Song screen paste -- LSDj 9.4.0, ROM bank $07:$5970-$5B0E.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit: wSongRow is declared volatile here (the row-shifting loop
 * recomputes `wSongRow + h` instead of reusing the test's value, which a
 * plain global would share), and SongWriteSramBytes is prototyped with
 * UWORD source/destination (the ROM casts both pointers).
 */

#include "lsdj.h"

extern volatile UBYTE wSongRow;             /* $C41E: this unit declares the cursor row volatile */

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
void SongWriteSramBytes(UWORD src, UWORD dst, UBYTE n);   /* asm $07:$7422; this unit casts both pointers */
void AdjustBookmarksOnInsert(UBYTE from, UBYTE to);
void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain);

void PasteSongSelection(void)
{
    UBYTE col;
    UBYTE n;
    UBYTE colOff;
    UBYTE h;
    UBYTE *src;
    UBYTE *p;
    UBYTE w;

    colOff = 0;
    w = wSelRight - wSelLeft;
    if (w == 1)
        colOff = wSelLeft - wSongCol;
    h = wSelBottom - wSelTop;
    if (!wSelLeft && wSelRight == 4)
        AdjustBookmarksOnInsert(wSongRow, wSongRow + h);
    if (wSongRow + h < 0xBF) {
        for (col = wSelLeft; col < wSelRight; col++) {
            p = (UBYTE *)0xB58C + (col - colOff);
            src = p - ((UWORD)h << 2);
            if ((UWORD)p < (UWORD)0xB290)
                DebugTrap();
            if ((UWORD)p >= (UWORD)0xB590)
                DebugTrap();
            n = (UWORD)0xC0 - (wSongRow + h);
            SongWriteSramBytes((UWORD)src, (UWORD)p, n);
        }
    }
    for (n = 0; n < h; n++) {
        for (col = wSelLeft; col < wSelRight; col++) {
            WriteSongCell(col - colOff, wSongRow, ReadClipboardCell(col, n));
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
