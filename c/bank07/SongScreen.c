/*
 * The song screen and its bookmarks -- LSDj 9.4.0, ROM bank $07.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C, the top visible song row */
extern UBYTE wSavedSongCol;                 /* $C952 */
extern UBYTE wPartialRedrawDone;            /* $C56D */
extern UBYTE SongColOffsets[];              /* $07:$4047, x of each channel column */

void SongEraseCursor(void);
UBYTE SongCloneChain(UBYTE chain);
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
void WriteBookmarkByte(UWORD p, UBYTE v);
void SortBookmarks(void);
UBYTE IsBookmarked(UBYTE row);
void AddBookmark(UBYTE row);
void RemoveBookmark(UBYTE row);
void ShowMax16Marks(void);                  /* bank 1 */
void SongScanSram(UBYTE *src, UBYTE *dst);  /* asm */
void SaveSelectionState(void);              /* bank 5 */
void SndStopAllIfIdle(void);                /* bank 2 */

void SongScrollToCursor(void);
void DrawAllChanStatus(void);               /* bank $1F */

extern UBYTE wPlayMarkerTimer;              /* $C950 */
extern UBYTE wChanNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChanLiveQueued[];             /* $C300, [4] live-mode row queued */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wChanStarted[];                /* $C304, [4] */
UBYTE MulU8(UBYTE a, UBYTE b);

extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wClonePhraseMap[];             /* $C532, [32] */
extern UBYTE wColChain[];                   /* $C44F, [4] */
extern UBYTE wColPhrase[];                  /* $C453, [4] */
UBYTE IsSongChainUsed(void);                /* bank $1F */
void WriteChainPhrase(void);                /* bank $1F */
void AllocNextFreePhrase(void);             /* bank 1 */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE src, UBYTE dst);    /* bank 6 */
UBYTE ChainReadTrans(UBYTE chain, UBYTE step);     /* bank 6 */
void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v);   /* bank 6 */
void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain);

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

UBYTE SongCloneChain(UBYTE chain)
{
    UBYTE v;
    UBYTE i;
    UBYTE ph;
    UBYTE j;
    UBYTE f;
    UBYTE n;
    UBYTE *p;
    UBYTE *q;
    char *msg;

    FarCall(5, RestoreSelectionState);
    wColChain[wSongCol] = chain;
    v = FarCallByte(0x1F, FAR_FN(IsSongChainUsed));
    if (v == 0xFF)
        return 0xFF;
    i = 0;
    p = wClonePhraseMap + 16;
    q = (UBYTE *)wClonePhraseMap + 16;
    while (i < 16) {
        ph = FarCallArg2(2, (UWORD)SndGetChainPhrase, chain, i);
        if (!wCloneMode) {
            wClonePhraseMap[i] = ph;
            if (ph != 0xFF) {
                f = 0xFF;
                j = 0;
                while (j < i) {
                    if (!(wClonePhraseMap[j] - ph)) {
                        f = p[j];
                        break;
                    }
                    j++;
                }
                if (f != 0xFF) {
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), v, i, f);
                } else {
                    wColPhrase[wSongCol] = ph;
                    n = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
                    if (n == 0xFF) {
                        FarCall(1, FAR_FN(ShowOutOfMemory));
                        return 0xFF;
                    }
                    FarCallArg2V(6, FAR_FN(ClonePhraseAtCursor), ph, n);
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), v, i, n);
                    q[i] = n;
                }
            } else {
                FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), v, i, 0xFF);
            }
        } else {
            FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), v, i, ph);
        }
        ph = FarCallArg2(6, FAR_FN(ChainReadTrans), chain, i);
        FarCallArg3P(6, FAR_FN(SetChainTranspose), v, i, ph);
        i++;
    }
    WriteSongCell(wSongCol, wSongRow, v);
    msg = wCloneMode ? "CLONED CHAIN" : "CLONED CHAIN+PHRASES";
    ShowMessage(msg);
    return v;
}

void SongDrawCursor(void)
{
    UBYTE y;

    y = wSongRow + 2 - wSongScroll;
    if (y >= 0x12)
        DebugTrap();
    DrawTextField(SongColOffsets[wSongCol], y, 2, hGameboyType ? 3 : 2);
}

void SongReadRow(void)
{
    UBYTE *p;

    p = SramSequence + ((unsigned int)wSongScroll << 2);
    ReadSongBytes(0, p, wSongReadBuf);
    ReadSongBytes(0, p + 0x10, (UBYTE *)0xC47D);
    ReadSongBytes(0, p + 0x20, (UBYTE *)0xC48D);
    ReadSongBytes(0, p + 0x30, (UBYTE *)0xC49D);
}

void SongDrawRowNum(void)
{
    UBYTE y;
    UBYTE row, pal;

    if (hGameboyType) {
        row = wSongScroll;
        for (y = 2; y != 18; y++, row++) {
            pal = (row & 4) ? 1 : 2;
            DrawTextField(0, y, 2, pal);
            DrawTextField(2, y, 12, IsBookmarked(row));
        }
    }
    wVramCursor = (UBYTE *)0x9840;
    row = wSongScroll;
    y = 16;
    while (y--) {
        if ((BYTE)row & 4) {
            hBgDigitPalette = wShadowDigitPal;
            hBgPalette = wShadowPal;
        } else {
            hBgDigitPalette = wSelectDigitPal;
            hBgPalette = wSelectPal;
        }
        DrawHexByte(row);
        wVramCursor += 0x1E;
        row++;
    }
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

void DrawSongScreen(void)
{
    FarCall(1, FAR_FN(DrawTopBar));
    SetSpriteX(0, 2);
    SetSpriteX(1, 5);
    SetSpriteX(2, 8);
    SetSpriteX(3, 11);
    SetSpriteTile3(0);
    SetSpriteTile3(1);
    SetSpriteTile3(2);
    SetSpriteTile3(3);
    SetSpriteX(4, 2);
    SetSpriteX(5, 5);
    SetSpriteX(6, 8);
    SetSpriteX(7, 11);
    SetSpriteTile3(4);
    SetSpriteTile3(5);
    SetSpriteTile3(6);
    SetSpriteTile3(7);
    SongScrollToCursor();
    SongRedrawRow();
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    DrawTempoField();
    FarCall(5, MapDrawTopRow);
}

void SongUpdatePlayMarker(void)
{
    UBYTE i;
    UBYTE next;
    UBYTE row;
    UBYTE on;
    UBYTE y;

    wPlayMarkerTimer++;
    for (i = 0; i < 4; i++) {
        next = wChanNextSongRow[i];
        row = wChanSongRow[i];
        on = wPlayMarkerTimer & (wChanLiveQueued[i] ? 4 : 8);
        if (next != 0xFF && on) {
            y = next - wSongScroll;
            SetSpriteY(i + 4, y);
            if (hGameboyType)
                SetSpriteAttr(i + 4, IsBookmarked(next));
        } else {
            ClearSpriteY(i + 4);
        }
        if (!wChanActive[i]) {
            ClearSpriteY(i);
        } else if (next == row) {
            ClearSpriteY(i);
        } else {
            if (wChanStarted[i]) {
                if (on)
                    SetSpriteTile4(i);
                else {
                    ClearSpriteY(i);
                    continue;
                }
            } else {
                SetSpriteTile3(i);
            }
            SetSpriteY(i, row - wSongScroll);
            if (hGameboyType)
                SetSpriteAttr(i, IsBookmarked(row));
        }
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

void SongRowPalettes(UBYTE y)
{
    UBYTE bm;
    UBYTE v;
    UBYTE shadow;
    UBYTE i, cur;

    bm = IsBookmarked(wSongRow);
    cur = wSongScroll + y - 2;
    shadow = cur & (UBYTE)4;
    if (shadow) {
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
    } else {
        hBgDigitPalette = wSelectDigitPal;
        hBgPalette = wSelectPal;
    }
    DrawHexByte(wSongRow);
    if (bm) {
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
    } else {
        hBgDigitPalette = 4;
        hBgPalette = 1;
    }
    for (i = 0; i < 4; i++) {
        v = ReadSeqByte(i, wSongRow);
        DrawStringIndexed(' ');
        cur = i == wSongCol;
        if (cur) {
            hBgDigitPalette = wSelectDigitPal;
            hBgPalette = wSelectPal;
        }
        if (v & 0x80)
            DrawString("--");                   /* the literal lands right after the function ($492A) */
        else
            DrawHexByte(v);
        if (cur) {
            if (bm) {
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
            } else {
                hBgDigitPalette = 4;
                hBgPalette = 1;
            }
        }
    }
    if (hGameboyType) {
        DrawTextField(0, y, 2, shadow ? 1 : 2);
        DrawTextField(2, y, 12, bm);
        DrawTextField(MulU8(wSongCol, 3) + 3, y, 2, 3);
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

void SongFindNextRow(void)
{
    UBYTE i;

    for (i = 1; i < 0x10; i++) {
        if (i + wSongRow == 0) {
            wSongRow = 0xFF;
            return;
        }
        if (IsBookmarked(i + wSongRow)) {
            wSongRow = i + wSongRow;
            return;
        }
    }
    wSongRow += 0x10;
    if (wSongRow > 0xBF)
        wSongRow = 0xBF;
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

void SongFindPrevRow(void)
{
    UBYTE i;

    for (i = 1; i < 0x10; i++) {
        if (wSongRow - i == 0xFF) {
            wSongRow = 0;
            return;
        }
        if (IsBookmarked(wSongRow - i)) {
            wSongRow = wSongRow - i;
            return;
        }
    }
    wSongRow -= 0x10;
    if (wSongRow == 0xFF)
        wSongRow = 0;
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
    c = SongCloneChain(c);
    if (c != 0xFF)
        DrawHexByte(c);
    SongDrawCursor();
}

void WriteBookmarkByte(UWORD p, UBYTE v)
{
    if (p < (UWORD)0xAFF0)
        DebugTrap();
    if (p >= (UWORD)0xB000)
        DebugTrap();
    WriteSongByte(0, p, v);
}

void SortBookmarks(void)
{
    UBYTE swapped, i, a, b;
    UBYTE *p;

    do {
        i = 15;
        swapped = 0;
        do {
            p = SramBookmarks + i;
            a = ReadSongByte(0, p - 1);
            b = ReadSongByte(0, p);
            if (b < a) {
                swapped = 1;
                WriteBookmarkByte(p - 1, b);
                WriteBookmarkByte(p, a);
            }
        } while (--i);
    } while (swapped);
}

UBYTE IsBookmarked(UBYTE row)
{
    UBYTE i = 0;
    UBYTE v;
    UBYTE *p;

    if (row == 0xFF)
        return 0;
    do {
        p = SramBookmarks + i;
        v = ReadSongByte(0, p);
        if (row < v)
            return 0;
        if (row == v)
            return 1;
        i++;
    } while (i < 0x10);
    return 0;
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

void AdjustBookmarksOnDelete(UBYTE lo, UBYTE hi)
{
    UBYTE i, v;
    UBYTE *p;

    i = 15;
    do {
        p = SramBookmarks + i;
        v = ReadSongByte(0, p);
        if (v != 0xFF && v >= lo) {
            if (v < hi) {
                WriteBookmarkByte(p, 0xFF);
            } else {
                v -= hi;
                v += lo;
                WriteBookmarkByte(p, v);
            }
        }
    } while (i--);
    SortBookmarks();
}

void AdjustBookmarksOnInsert(UBYTE from, UBYTE to)
{
    UBYTE i, v;
    UBYTE *p;

    i = 15;
    do {
        p = SramBookmarks + i;
        v = ReadSongByte(0, p);
        if (v != 0xFF && v >= from) {
            v -= from;
            v += to;
            if (v > from)
                WriteBookmarkByte(p, v);
            else
                WriteBookmarkByte(p, 0xFF);
        }
    } while (i--);
}
