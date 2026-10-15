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
extern UBYTE wSelectionTop;                 /* $C40E */
extern UBYTE wSelectionRight;               /* $C40F */
extern UBYTE wSelectionBottom;              /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSongSelectionRow;             /* $C94B */
void CopySongColumn(UWORD source, UWORD destination); /* this unit's prototype (the ROM casts the pointer args) */
void EraseSongCursor(void);
void ScrollSongToCursor(void);
void RedrawSongRow(void);
void AdjustBookmarksOnDelete(UBYTE low, UBYTE high);
void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain);

/* $5736: copy the song selection to the clipboard; when `isCut`, each column
   below closes up over it, the end filled with $FF, and the bookmarks follow
   (AdjustBookmarksOnDelete); "CUT CHAINS" or "COPIED CHAINS" */
void CutCopySongSelection(UBYTE isCut)
{
    UBYTE rowsLeft;
    UBYTE *q;
    UBYTE * volatile p;
    UBYTE d2;
    char vis;
    UBYTE v;
    UBYTE r;
    UBYTE y;
    UWORD x;
    UWORD t;
    char *message;
    UBYTE column;

    if (wClipboardType != SCREEN_SONG)
        return;
    wCursorMode = CURSOR_COPIED;
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
            v = ReadSequenceByte(wSongColumn, wSongRow);
            r = wSongRow - wSelectionTop;
            WriteClipboardCell(wSongColumn, r, v);
            wSongRow++;
        }
        if (isCut) {
            rowsLeft = wSelectionBottom - wSelectionTop;
            p = SramSequence + ((UWORD)wSelectionTop << 2);
            column = wSongColumn;
            p += column;
            q = p + ((UWORD)rowsLeft << 2);
            x = (UWORD)p;
            CopySongColumn((UWORD)q, x);
            while (rowsLeft) {
                t = rowsLeft;
                x = 0xC0 - t;
                WriteSongCell(wSongColumn, x, 0xFF);
                rowsLeft--;
            }
        }
        wSongColumn++;
    }
    if (isCut && !wSelectionLeft && wSelectionRight == CHANNELS)
        AdjustBookmarksOnDelete(wSelectionTop, wSelectionBottom);
    message = isCut ? "CUT CHAINS" : "COPIED CHAINS";   /* both literals follow the function ($5957, $5962) */
    ShowMessage(message);
    wSongColumn = wSelectionLeft;
    wSongRow = isCut ? wSelectionTop : wSongSelectionRow;
    ScrollSongToCursor();
    RedrawSongRow();
}
