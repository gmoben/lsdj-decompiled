/*
 * The song screen and its bookmarks -- LSDj 9.4.0, ROM bank $07.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wSongScroll;                   /* $C41C, the top visible song row */
extern UBYTE wPartialRedrawDone;            /* $C56D */

void SongReadCellValue(void);
UBYTE SongEnterChain(UBYTE chain);
void SongDrawCellHw(void);
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

void SongRedrawRow(void)
{
    SongReadRow();
    SongDrawRowNum();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9842;
    SongDrawColumnValues();
    SongDrawCellHw();
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
    SongReadCellValue();
    SongFindNextRow();
    d = wSongRow - wSongScroll;
    if (d >= 0x10) {
        wSongScroll += 0x10;
        d = wSongScroll & 0xF0;
        if (d >= 0xB0)
            wSongScroll = 0xB0;
        SongRedrawRow();
    } else {
        SongDrawCellHw();
    }
    return 1;
}

UBYTE SongCursorPageUp(void)
{
    UBYTE d;

    if (wCursorMode == 4)
        return 0;
    SongReadCellValue();
    SongFindPrevRow();
    if (wSongRow < wSongScroll) {
        wSongScroll -= 0x10;
        d = wSongScroll & 0xF0;
        if (d == 0xF0)
            wSongScroll = 0;
        SongRedrawRow();
    } else {
        SongDrawCellHw();
    }
    return 1;
}

void SongLoadCursorChain(void)
{
    UBYTE c;

    c = ReadSeqByte(wSongCol, wSongRow);
    if (c == 0xFF)
        return;
    c &= 0x7F;
    SongReadCellValue();
    c = SongEnterChain(c);
    if (c != 0xFF)
        DrawHexByte(c);
    SongDrawCellHw();
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
