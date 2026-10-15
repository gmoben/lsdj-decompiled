/*
 * Song screen cut/copy -- LSDj 9.4.0, ROM bank $07:$5736-$5956.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit: this file's prototype of CopySongColumn takes two UWORDs
 * (the ROM casts the pointer arguments; SongScreen.c defines it with
 * pointer parameters), and the song pointers are `UBYTE * volatile` (every
 * use reloads them from the stack).
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSongSelectionRow;                   /* $C94B */
void CopySongColumn(UWORD src, UWORD dst);   /* this unit's prototype (the ROM casts the pointer args) */
void EraseSongCursor(void);
void ScrollSongToCursor(void);
void RedrawSongRow(void);
void AdjustBookmarksOnDelete(UBYTE lo, UBYTE hi);
void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain);

void CutCopySongSelection(UBYTE isCut)
{
    UBYTE n;
    UBYTE *q;
    UBYTE * volatile p;
    UBYTE d2;
    char vis;
    UBYTE v;
    UBYTE r;
    UBYTE y;
    UWORD x;
    UWORD t;
    char *msg;
    UBYTE col;

    if (wClipboardType != 4)
        return;
    wCursorMode = 2;
    wSelectionRight = wSongColumn;
    wSelectionBottom = wSongRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    if (wSelectionRight - wSelectionLeft >= 6)
        DebugTrap();
    if (wSelectionBottom - wSelectionTop >= 0x2A)
        DebugTrap();
    wSelectionRight++;
    wSelectionBottom++;
    wSongColumn = wSelectionLeft;
    while (wSongColumn - wSelectionRight) {
        if (wSongColumn >= 6)
            DebugTrap();
        wSongRow = wSelectionTop;
        for (;;) {
            d2 = wSongRow - wSelectionBottom;
            if (!d2)
                break;
            y = wSongRow - wSongScroll;
            vis = y < 0x10;
            if (vis)
                EraseSongCursor();
            v = ReadSeqByte(wSongColumn, wSongRow);
            r = wSongRow - wSelectionTop;
            WriteClipboardCell(wSongColumn, r, v);
            wSongRow++;
        }
        if (isCut) {
            n = wSelectionBottom - wSelectionTop;
            p = (UBYTE *)0xB290 + ((UWORD)wSelectionTop << 2);
            col = wSongColumn;
            p += col;
            q = p + ((UWORD)n << 2);
            x = (UWORD)p;
            CopySongColumn((UWORD)q, x);
            while (n) {
                t = n;
                x = 0xC0 - t;
                WriteSongCell(wSongColumn, x, 0xFF);
                n--;
            }
        }
        wSongColumn++;
    }
    if (isCut && !wSelectionLeft && wSelectionRight == 4)
        AdjustBookmarksOnDelete(wSelectionTop, wSelectionBottom);
    msg = isCut ? "CUT CHAINS" : "COPIED CHAINS";   /* both literals follow the function ($5957, $5962) */
    ShowMessage(msg);
    wSongColumn = wSelectionLeft;
    wSongRow = isCut ? wSelectionTop : wSongSelectionRow;
    ScrollSongToCursor();
    RedrawSongRow();
}
