/*
 * The song screen and its bookmarks -- LSDj 9.4.0, ROM bank $07.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wSongScroll;                   /* $C41C, the top visible song row */
extern UBYTE wSelLeft;                      /* $C40D */
extern UBYTE wSavedSongCol;                 /* $C952 */
extern UBYTE wPartialRedrawDone;            /* $C56D */
extern UBYTE SongColOffsets[];              /* $07:$4047, x of each channel column */

void SongEraseCursor(void);
UBYTE SongEnterChain(UBYTE chain);
void SongDrawCursor(void);
void SongReadRow(void);
void SongDrawRowNum(void);
void SongDrawColumnValues(void);
void SongRedrawRow(void);
void SongUpdatePlayMarker(void);
void SongRowPalettes(UBYTE row);
void SongScrollVramUp(void);
void SongScrollVramDown(void);
void VramCopyRowUp(void);
void VramCopyRowDown(void);
void SongFindNextRow(void);
void SongFindPrevRow(void);
void WriteBookmarkByte(UBYTE *p, UBYTE v);
void SortBookmarks(void);
UBYTE IsBookmarked(UBYTE row);
void AddBookmark(UBYTE row);
void RemoveBookmark(UBYTE row);
void ShowMax16Marks(void);                  /* bank 1 */
void SongScanSram(UBYTE *src, UBYTE *dst);  /* asm */
void SaveSelectionState(void);              /* bank 5 */
void RestoreSelectionState(void);           /* bank 5 */
void SndStopAllIfIdle(void);                /* bank 2 */

void SongCopyColumn(UBYTE *p, UBYTE *q)
{
    if (q >= (UBYTE *)0xB590)
        DebugTrap();
    if (p < SramSequence)
        DebugTrap();
    if (q < SramSequence)
        DebugTrap();
    SongScanSram(p, q);
}

void SongEraseCursor(void)
{
    UBYTE x, y, pal;

    x = SongColOffsets[wSongCol];
    y = wSongRow + 2 - wSongScroll;
    pal = IsBookmarked(wSongRow);
    if (y >= 0x12)
        DebugTrap();
    DrawTextField(x, y, 2, pal);
}

void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain)
{
    UBYTE *p;

    if (col >= 4)
        DebugTrap();
    if (row > 0xBF)
        DebugTrap();
    if (chain != 0xFF && chain >= 0x80)
        DebugTrap();
    p = SramSequence + ((UWORD)row << 2);
    WriteSongByte(0, p + col, chain);
}

void SongDrawCursor(void)
{
    UBYTE y;

    y = wSongRow + 2 - wSongScroll;
    if (y >= 0x12)
        DebugTrap();
    DrawTextField(SongColOffsets[wSongCol], y, 2, hGameboyType ? 3 : 2);
}

void SongRedrawRow(void)
{
    SongReadRow();
    SongDrawRowNum();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9842;
    SongDrawColumnValues();
    SongDrawCursor();
    wPartialRedrawDone = 1;
}

void SongScrollToCursor(void)
{
    UBYTE d;

    if (wSongRow < wSongScroll) {
        wSongScroll = wSongRow;
    } else {
        d = wSongRow - wSongScroll;
        if (d & 0xF0)
            wSongScroll = wSongRow - 15;
    }
}

void SongScreenUpdate(void)
{
    UBYTE i;

    if (wPlayMode == 4) {
        SongUpdatePlayMarker();
    } else {
        for (i = 0; i != 8; i++)
            ClearSpriteY(i);
    }
}

void SongDrawCursorRow(void)
{
    UBYTE row;

    row = wSongRow + 2 - wSongScroll;
    GetTilemapAddr(0, row);
    SongRowPalettes(row);
}

void SongScrollUp(void)
{
    SongScrollVramUp();
    if (hGameboyType)
        VramCopyRowUp();
    wVramCursor = (UBYTE *)0x9840;
    SongRowPalettes(2);
    wPartialRedrawDone = 1;
}

void SongScrollDown(void)
{
    SongScrollVramDown();
    if (hGameboyType)
        VramCopyRowDown();
    wVramCursor = (UBYTE *)0x9A20;
    SongRowPalettes(0x11);
    wPartialRedrawDone = 1;
}

UBYTE SongCursorPageDown(void)
{
    UBYTE d;

    if (wCursorMode == 4)
        return 0;
    SongEraseCursor();
    SongFindNextRow();
    d = wSongRow - wSongScroll;
    if (d >= 0x10) {
        wSongScroll += 0x10;
        d = wSongScroll & 0xF0;
        if (d >= 0xB0)
            wSongScroll = 0xB0;
        SongRedrawRow();
    } else {
        SongDrawCursor();
    }
    return 1;
}

UBYTE SongCursorPageUp(void)
{
    UBYTE d;

    if (wCursorMode == 4)
        return 0;
    SongEraseCursor();
    SongFindPrevRow();
    if (wSongRow < wSongScroll) {
        wSongScroll -= 0x10;
        d = wSongScroll & 0xF0;
        if (d == 0xF0)
            wSongScroll = 0;
        SongRedrawRow();
    } else {
        SongDrawCursor();
    }
    return 1;
}

void SongBeginSelection(void)
{
    wSavedSongCol = wSongCol;
    FarCall(5, SaveSelectionState);
    wSelLeft = 0;
    wSongCol = 3;
    FarCall(2, FAR_FN(SndStopAllIfIdle));
    FarCall(5, RestoreSelectionState);
    wSongCol = wSavedSongCol;
}

void SongLoadCursorChain(void)
{
    UBYTE c;

    c = ReadSeqByte(wSongCol, wSongRow);
    if (c == 0xFF)
        return;
    c &= 0x7F;
    SongEraseCursor();
    c = SongEnterChain(c);
    if (c != 0xFF)
        DrawHexByte(c);
    SongDrawCursor();
}

void WriteBookmarkByte(UBYTE *p, UBYTE v)
{
    if (p < SramBookmarks)
        DebugTrap();
    if (p >= (UBYTE *)0xB000)
        DebugTrap();
    WriteSongByte(0, p, v);
}

void AddBookmark(UBYTE row)
{
    if (ReadSongByte(0, SramBookmarks + 15) != 0xFF) {
        FarCall(1, FAR_FN(ShowMax16Marks));
    } else {
        WriteBookmarkByte(SramBookmarks + 15, row);
        SortBookmarks();
    }
}

void ToggleBookmark(UBYTE row)
{
    if (IsBookmarked(row))
        RemoveBookmark(row);
    else
        AddBookmark(row);
}

void ClearBookmarks(void)
{
    MemSet(SramBookmarks, 0xFF, 0x10);
}
