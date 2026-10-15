/*
 * The song screen and its bookmarks -- LSDj 9.4.0, ROM bank $07.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C, the top visible song row */
extern UBYTE wSavedSongColumn;                 /* $C952 */
extern UBYTE wPartialRedrawDone;            /* $C56D */
extern UBYTE SongColumnOffsets[];              /* $07:$4047, x of each channel column */

void EraseSongCursor(void);
UBYTE CloneSongChain(UBYTE chain);
void DrawSongCursor(void);
void ReadSongRow(void);
void DrawSongRowNumber(void);
void DrawSongColumnValues(void);
void RedrawSongRow(void);
void UpdateSongPlayMarker(void);
void SetSongRowPalettes(UBYTE row);
void ScrollSongVramUp(void);
void ScrollSongVramDown(void);
void CopyVramRowUp(void);
void CopyVramRowDown(void);
void FindNextSongRow(void);
void FindPreviousSongRow(void);
void WriteBookmarkByte(UWORD p, UBYTE v);
void SortBookmarks(void);
UBYTE IsBookmarked(UBYTE row);
void AddBookmark(UBYTE row);
void RemoveBookmark(UBYTE row);
void ShowMax16Marks(void);                  /* bank 1 */
void ScanSongSram(UBYTE *source, UBYTE *destination);  /* asm */
void SaveSelectionState(void);              /* bank 5 */
void SoundStartRange(void);                /* bank 2 */

void ScrollSongToCursor(void);
void DrawAllChannelStatus(void);               /* bank $1F */

extern UBYTE wPlayMarkerTimer;              /* $C950 */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChannelLiveQueued[];             /* $C300, [4] live-mode row queued */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wChannelStopQueued[];                /* $C304, [4] */
UBYTE MulU8(UBYTE a, UBYTE b);

extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wClonePhraseMap[];             /* $C532, [32] */
extern UBYTE wColumnChain[];                   /* $C44F, [4] */
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
UBYTE IsSongChainUsed(void);                /* bank $1F */
void WriteChainPhrase(void);                /* bank $1F */
void AllocNextFreePhrase(void);             /* bank 1 */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE source, UBYTE destination);    /* bank 6 */
UBYTE ReadChainTranspose(UBYTE chain, UBYTE step);     /* bank 6 */
void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v);   /* bank 6 */
void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain);

void CopySongColumn(UBYTE *p, UBYTE *q)
{
    if (q >= (UBYTE *)0xB590)
        DebugTrap();
    if (p < SramSequence)
        DebugTrap();
    if (q < SramSequence)
        DebugTrap();
    ScanSongSram(p, q);
}

void EraseSongCursor(void)
{
    UBYTE x, y, palette;

    x = SongColumnOffsets[wSongColumn];
    y = wSongRow + 2 - wSongScroll;
    palette = IsBookmarked(wSongRow);
    if (y >= 0x12)
        DebugTrap();
    ColorField(x, y, 2, palette);
}

void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain)
{
    UBYTE *p;

    if (column >= 4)
        DebugTrap();
    if (row > 0xBF)
        DebugTrap();
    if (chain != 0xFF && chain >= 0x80)
        DebugTrap();
    p = SramSequence + ((UWORD)row << 2);
    WriteSongByte(0, p + column, chain);
}

UBYTE CloneSongChain(UBYTE chain)
{
    UBYTE v;
    UBYTE i;
    UBYTE phrase;
    UBYTE j;
    UBYTE f;
    UBYTE n;
    UBYTE *p;
    UBYTE *q;
    char *message;

    FarCall(5, RestoreSelectionState);
    wColumnChain[wSongColumn] = chain;
    v = FarCallByte(0x1F, FAR_FN(IsSongChainUsed));
    if (v == 0xFF)
        return 0xFF;
    i = 0;
    p = wClonePhraseMap + 16;
    q = (UBYTE *)wClonePhraseMap + 16;
    while (i < 16) {
        phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, chain, i);
        if (!wCloneMode) {
            wClonePhraseMap[i] = phrase;
            if (phrase != 0xFF) {
                f = 0xFF;
                j = 0;
                while (j < i) {
                    if (!(wClonePhraseMap[j] - phrase)) {
                        f = p[j];
                        break;
                    }
                    j++;
                }
                if (f != 0xFF) {
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, i, f);
                } else {
                    wColumnPhrase[wSongColumn] = phrase;
                    n = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
                    if (n == 0xFF) {
                        FarCall(1, FAR_FN(ShowOutOfMemory));
                        return 0xFF;
                    }
                    FarCallArgument2V(6, FAR_FN(ClonePhraseAtCursor), phrase, n);
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, i, n);
                    q[i] = n;
                }
            } else {
                FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, i, 0xFF);
            }
        } else {
            FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, i, phrase);
        }
        phrase = FarCallArgument2(6, FAR_FN(ReadChainTranspose), chain, i);
        FarCallArgument3P(6, FAR_FN(SetChainTranspose), v, i, phrase);
        i++;
    }
    WriteSongCell(wSongColumn, wSongRow, v);
    message = wCloneMode ? "CLONED CHAIN" : "CLONED CHAIN+PHRASES";
    ShowMessage(message);
    return v;
}

void DrawSongCursor(void)
{
    UBYTE y;

    y = wSongRow + 2 - wSongScroll;
    if (y >= 0x12)
        DebugTrap();
    ColorField(SongColumnOffsets[wSongColumn], y, 2, hGameboyType ? 3 : 2);
}

void ReadSongRow(void)
{
    UBYTE *p;

    p = SramSequence + ((unsigned int)wSongScroll << 2);
    ReadSongBytes(0, p, wSongReadBuffer);
    ReadSongBytes(0, p + 0x10, (UBYTE *)0xC47D);
    ReadSongBytes(0, p + 0x20, (UBYTE *)0xC48D);
    ReadSongBytes(0, p + 0x30, (UBYTE *)0xC49D);
}

void DrawSongRowNumber(void)
{
    UBYTE y;
    UBYTE row, palette;

    if (hGameboyType) {
        row = wSongScroll;
        for (y = 2; y != 18; y++, row++) {
            palette = (row & 4) ? 1 : 2;
            ColorField(0, y, 2, palette);
            ColorField(2, y, 12, IsBookmarked(row));
        }
    }
    wVramCursor = (UBYTE *)0x9840;
    row = wSongScroll;
    y = 16;
    while (y--) {
        if ((BYTE)row & 4) {
            hBgDigitPalette = wShadedDigitPalette;
            hBgPalette = wShadedPalette;
        } else {
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
        }
        DrawHexByte(row);
        wVramCursor += 0x1E;
        row++;
    }
}

void RedrawSongRow(void)
{
    ReadSongRow();
    DrawSongRowNumber();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9842;
    DrawSongColumnValues();
    DrawSongCursor();
    wPartialRedrawDone = 1;
}

void ScrollSongToCursor(void)
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
    ScrollSongToCursor();
    RedrawSongRow();
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    DrawTempoField();
    FarCall(5, DrawMapTopRow);
}

void UpdateSongPlayMarker(void)
{
    UBYTE i;
    UBYTE next;
    UBYTE row;
    UBYTE on;
    UBYTE y;

    wPlayMarkerTimer++;
    for (i = 0; i < 4; i++) {
        next = wChannelNextSongRow[i];
        row = wChannelSongRow[i];
        on = wPlayMarkerTimer & (wChannelLiveQueued[i] ? 4 : 8);
        if (next != 0xFF && on) {
            y = next - wSongScroll;
            SetSpriteY(i + 4, y);
            if (hGameboyType)
                SetSpriteAttribute(i + 4, IsBookmarked(next));
        } else {
            ClearSpriteY(i + 4);
        }
        if (!wChannelActive[i]) {
            ClearSpriteY(i);
        } else if (next == row) {
            ClearSpriteY(i);
        } else {
            if (wChannelStopQueued[i]) {
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
                SetSpriteAttribute(i, IsBookmarked(row));
        }
    }
}

void SongScreenUpdate(void)
{
    UBYTE i;

    if (wPlayMode == 4) {
        UpdateSongPlayMarker();
    } else {
        for (i = 0; i != 8; i++)
            ClearSpriteY(i);
    }
}

void SetSongRowPalettes(UBYTE y)
{
    UBYTE bookmarked;
    UBYTE v;
    UBYTE shadow;
    UBYTE i, current;

    bookmarked = IsBookmarked(wSongRow);
    current = wSongScroll + y - 2;
    shadow = current & (UBYTE)4;
    if (shadow) {
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
    } else {
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
    }
    DrawHexByte(wSongRow);
    if (bookmarked) {
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
    } else {
        hBgDigitPalette = 4;
        hBgPalette = 1;
    }
    for (i = 0; i < 4; i++) {
        v = ReadSequenceByte(i, wSongRow);
        DrawStringIndexed(' ');
        current = i == wSongColumn;
        if (current) {
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
        }
        if (v & 0x80)
            DrawString("--");                   /* the literal lands right after the function ($492A) */
        else
            DrawHexByte(v);
        if (current) {
            if (bookmarked) {
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
            } else {
                hBgDigitPalette = 4;
                hBgPalette = 1;
            }
        }
    }
    if (hGameboyType) {
        ColorField(0, y, 2, shadow ? 1 : 2);
        ColorField(2, y, 12, bookmarked);
        ColorField(MulU8(wSongColumn, 3) + 3, y, 2, 3);
    }
}

void DrawSongCursorRow(void)
{
    UBYTE row;

    row = wSongRow + 2 - wSongScroll;
    SetVramCursor(0, row);
    SetSongRowPalettes(row);
}

void ScrollSongUp(void)
{
    ScrollSongVramUp();
    if (hGameboyType)
        CopyVramRowUp();
    wVramCursor = (UBYTE *)0x9840;
    SetSongRowPalettes(2);
    wPartialRedrawDone = 1;
}

void ScrollSongDown(void)
{
    ScrollSongVramDown();
    if (hGameboyType)
        CopyVramRowDown();
    wVramCursor = (UBYTE *)0x9A20;
    SetSongRowPalettes(0x11);
    wPartialRedrawDone = 1;
}

void FindNextSongRow(void)
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

UBYTE PageSongCursorDown(void)
{
    UBYTE d;

    if (wCursorMode == 4)
        return 0;
    EraseSongCursor();
    FindNextSongRow();
    d = wSongRow - wSongScroll;
    if (d >= 0x10) {
        wSongScroll += 0x10;
        d = wSongScroll & 0xF0;
        if (d >= 0xB0)
            wSongScroll = 0xB0;
        RedrawSongRow();
    } else {
        DrawSongCursor();
    }
    return 1;
}

void FindPreviousSongRow(void)
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

UBYTE PageSongCursorUp(void)
{
    UBYTE d;

    if (wCursorMode == 4)
        return 0;
    EraseSongCursor();
    FindPreviousSongRow();
    if (wSongRow < wSongScroll) {
        wSongScroll -= 0x10;
        d = wSongScroll & 0xF0;
        if (d == 0xF0)
            wSongScroll = 0;
        RedrawSongRow();
    } else {
        DrawSongCursor();
    }
    return 1;
}

void BeginSongSelection(void)
{
    wSavedSongColumn = wSongColumn;
    FarCall(5, SaveSelectionState);
    wSelectionLeft = 0;
    wSongColumn = 3;
    FarCall(2, FAR_FN(SoundStartRange));
    FarCall(5, RestoreSelectionState);
    wSongColumn = wSavedSongColumn;
}

void LoadSongCursorChain(void)
{
    UBYTE c;

    c = ReadSequenceByte(wSongColumn, wSongRow);
    if (c == 0xFF)
        return;
    c &= 0x7F;
    EraseSongCursor();
    c = CloneSongChain(c);
    if (c != 0xFF)
        DrawHexByte(c);
    DrawSongCursor();
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

/* Clear every bookmark slot holding row, then re-sort. The address is
   written out in both calls: CSE makes it one converted temporary in one
   slot, which spills i as the ROM does (a named UBYTE *p gets a slot of its
   own beside the conversion's; a UWORD p keeps i in c). */
void RemoveBookmark(UBYTE row)
{
    UBYTE i;

    if (!IsBookmarked(row))
        return;
    for (i = 0; i < 0x10; i++)
        if (row == ReadSongByte(0, SramBookmarks + i))
            WriteBookmarkByte(SramBookmarks + i, 0xFF);
    SortBookmarks();
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
    FillMemory(SramBookmarks, 0xFF, 0x10);
}

void AdjustBookmarksOnDelete(UBYTE low, UBYTE high)
{
    UBYTE i, v;
    UBYTE *p;

    i = 15;
    do {
        p = SramBookmarks + i;
        v = ReadSongByte(0, p);
        if (v != 0xFF && v >= low) {
            if (v < high) {
                WriteBookmarkByte(p, 0xFF);
            } else {
                v -= high;
                v += low;
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
