/*
 * Song screen cut/copy -- LSDj 9.4.0, ROM bank $07:$5736-$5956.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit: this file's prototype of SongCopyColumn takes two UWORDs
 * (the ROM casts the pointer arguments; SongScreen.c defines it with
 * pointer parameters), and the song pointers are `UBYTE * volatile` (every
 * use reloads them from the stack).
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSongSelRow;                   /* $C94B */
void SongCopyColumn(UWORD src, UWORD dst);   /* this unit's prototype (the ROM casts the pointer args) */
void SongEraseCursor(void);
void SongScrollToCursor(void);
void SongRedrawRow(void);
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
    wSelRight = wSongCol;
    wSelBottom = wSongRow;
    SortBytePair(&wSelLeft, &wSelRight);
    SortBytePair(&wSelTop, &wSelBottom);
    if (wSelRight - wSelLeft >= 6)
        DebugTrap();
    if (wSelBottom - wSelTop >= 0x2A)
        DebugTrap();
    wSelRight++;
    wSelBottom++;
    wSongCol = wSelLeft;
    while (wSongCol - wSelRight) {
        if (wSongCol >= 6)
            DebugTrap();
        wSongRow = wSelTop;
        for (;;) {
            d2 = wSongRow - wSelBottom;
            if (!d2)
                break;
            y = wSongRow - wSongScroll;
            vis = y < 0x10;
            if (vis)
                SongEraseCursor();
            v = ReadSeqByte(wSongCol, wSongRow);
            r = wSongRow - wSelTop;
            WriteClipboardCell(wSongCol, r, v);
            wSongRow++;
        }
        if (isCut) {
            n = wSelBottom - wSelTop;
            p = (UBYTE *)0xB290 + ((UWORD)wSelTop << 2);
            col = wSongCol;
            p += col;
            q = p + ((UWORD)n << 2);
            x = (UWORD)p;
            SongCopyColumn((UWORD)q, x);
            while (n) {
                t = n;
                x = 0xC0 - t;
                WriteSongCell(wSongCol, x, 0xFF);
                n--;
            }
        }
        wSongCol++;
    }
    if (isCut && !wSelLeft && wSelRight == 4)
        AdjustBookmarksOnDelete(wSelTop, wSelBottom);
    msg = isCut ? "CUT CHAINS" : "COPIED CHAINS";   /* both literals follow the function ($5957, $5962) */
    ShowMessage(msg);
    wSongCol = wSelLeft;
    wSongRow = isCut ? wSelTop : wSongSelRow;
    SongScrollToCursor();
    SongRedrawRow();
}
