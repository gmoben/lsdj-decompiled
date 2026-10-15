/*
 * The song screen and its bookmarks -- LSDj 9.4.0, ROM bank $07.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C, the top visible song row */
extern UBYTE wSavedSongColumn;              /* $C952 */
extern UBYTE wPartialRedrawDone;            /* $C56D */
extern UBYTE SongColumnOffsets[];           /* $07:$4047, x of each channel column */

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
void ScanSongSram(UBYTE *source, UBYTE *destination); /* asm */
void SaveSelectionState(void);              /* bank 5 */
void SoundStartRange(void);                 /* bank 2 */

void ScrollSongToCursor(void);
void DrawAllChannelStatus(void);            /* bank $1F */

extern UBYTE wPlayMarkerTimer;              /* $C950 */
extern UBYTE wChannelNextSongRow[];         /* $C2FC, [4] */
extern UBYTE wChannelSongRow[];             /* $C200, [4] */
extern UBYTE wChannelLiveQueued[];          /* $C300, [4] live-mode row queued */
extern UBYTE wChannelActive[];              /* $C0E0, [4] */
extern UBYTE wChannelStopQueued[];          /* $C304, [4] */
UBYTE MulU8(UBYTE a, UBYTE b);

extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wClonePhraseMap[];             /* $C532, [32] */
extern UBYTE wColumnChain[];                /* $C44F, [4] */
extern UBYTE wColumnPhrase[];               /* $C453, [4] */
UBYTE IsSongChainUsed(void);                /* bank $1F */
void WriteChainPhrase(void);                /* bank $1F */
void AllocateNextFreePhrase(void);          /* bank 1 */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE source, UBYTE destination); /* bank 6 */
UBYTE ReadChainTranspose(UBYTE chain, UBYTE step); /* bank 6 */
void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v); /* bank 6 */
void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain);

/* $4000: copy a song column from p to q (stride 4) until p reaches $B590
   (ScanSongSram); asserts both >= $B290, q < $B590 */
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

/* $4084: restore the cursor cell palette (1 if the row is bookmarked, else
   0) at SongColumnOffsets[wSongColumn] */
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

/* $40D0: write song[row][col] = chain (SRAM bank 0 $B290); asserts col < 4,
   row <= $BF, chain < $80 or $FF */
void WriteSongCell(UBYTE column, UBYTE row, UBYTE chain)
{
    UBYTE *p;

    if (column >= 4)
        DebugTrap();
    if (row > 0xBF)
        DebugTrap();
    if (chain != EMPTY && chain >= 0x80)
        DebugTrap();
    p = SramSequence + ((UWORD)row << 2);
    WriteSongByte(0, p + column, chain);
}

/* $4139: song screen chain clone (CLONED CHAIN / +PHRASES in deep mode): new
   chain via IsSongChainUsed, copy its 16 phrases (deep: clone each phrase
   once via the wClonePhraseMap map), transposes; returns the new chain or
   $ff */
UBYTE CloneSongChain(UBYTE chain)
{
    UBYTE v;
    UBYTE row;
    UBYTE phrase;
    UBYTE earlierRow;
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
    row = 0;
    p = wClonePhraseMap + 16;
    q = (UBYTE *)wClonePhraseMap + 16;
    while (row < 16) {
        phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, chain, row);
        if (!wCloneMode) {
            wClonePhraseMap[row] = phrase;
            if (phrase != EMPTY) {
                f = 0xFF;
                earlierRow = 0;
                while (earlierRow < row) {
                    if (!(wClonePhraseMap[earlierRow] - phrase)) {
                        f = p[earlierRow];
                        break;
                    }
                    earlierRow++;
                }
                if (f != 0xFF) {
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, row, f);
                } else {
                    wColumnPhrase[wSongColumn] = phrase;
                    n = FarCallByte(1, FAR_FN(AllocateNextFreePhrase));
                    if (n == 0xFF) {
                        FarCall(1, FAR_FN(ShowOutOfMemory));
                        return 0xFF;
                    }
                    FarCallArgument2V(6, FAR_FN(ClonePhraseAtCursor), phrase, n);
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, row, n);
                    q[row] = n;
                }
            } else {
                FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, row, 0xFF);
            }
        } else {
            FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), v, row, phrase);
        }
        phrase = FarCallArgument2(6, FAR_FN(ReadChainTranspose), chain, row);
        FarCallArgument3P(6, FAR_FN(SetChainTranspose), v, row, phrase);
        row++;
    }
    WriteSongCell(wSongColumn, wSongRow, v);
    message = wCloneMode ? "CLONED CHAIN" : "CLONED CHAIN+PHRASES";
    ShowMessage(message);
    return v;
}

/* $4395: highlight the cursor cell (palette 3 on CGB, 2 on DMG) at
   SongColumnOffsets[wSongColumn] */
void DrawSongCursor(void)
{
    UBYTE y;

    y = wSongRow + 2 - wSongScroll;
    if (y >= 0x12)
        DebugTrap();
    ColorField(SongColumnOffsets[wSongColumn], y, 2, hGameboyType ? 3 : 2);
}

/* $43E4: read song row wSongScroll from SRAM ($B290, ReadSongBytes) ->
   wSongReadBuffer (16 bytes) */
void ReadSongRow(void)
{
    UBYTE *p;

    p = SramSequence + ((unsigned int)wSongScroll << 2);
    ReadSongBytes(0, p, wSongReadBuffer);
    ReadSongBytes(0, p + 0x10, (UBYTE *)ROW_BUFFER_COLUMN(1));
    ReadSongBytes(0, p + 0x20, (UBYTE *)ROW_BUFFER_COLUMN(2));
    ReadSongBytes(0, p + 0x30, (UBYTE *)ROW_BUFFER_COLUMN(3));
}

/* $4463: draw a song row number + bookmark marker (wSongScroll window, 18
   rows) */
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
    wVramCursor = (UBYTE *)BG_MAP(0, 2);
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

/* $451E: ReadSongRow + draw the row at VRAM $9842 (palettes,
   DrawSongColumnValues/4395) */
void RedrawSongRow(void)
{
    ReadSongRow();
    DrawSongRowNumber();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(2, 2);
    DrawSongColumnValues();
    DrawSongCursor();
    wPartialRedrawDone = 1;
}

/* $4540: clamp song top-row wSongScroll so cursor wSongRow stays visible */
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

/* $4574: screen idx 4: draws the song sequence (4 channels =
   PU1/PU2/WAV/NOI) */
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

/* $4657: per-channel play-position markers from
   wChannelSongRow/wChannelNextSongRow (<-SongScreenUpdate) */
void UpdateSongPlayMarker(void)
{
    UBYTE channel;
    UBYTE next;
    UBYTE row;
    UBYTE on;
    UBYTE y;

    wPlayMarkerTimer++;
    for (channel = 0; channel < 4; channel++) {
        next = wChannelNextSongRow[channel];
        row = wChannelSongRow[channel];
        on = wPlayMarkerTimer & (wChannelLiveQueued[channel] ? 4 : 8);
        if (next != 0xFF && on) {
            y = next - wSongScroll;
            SetSpriteY(channel + 4, y);
            if (hGameboyType)
                SetSpriteAttribute(channel + 4, IsBookmarked(next));
        } else {
            ClearSpriteY(channel + 4);
        }
        if (!wChannelActive[channel]) {
            ClearSpriteY(channel);
        } else if (next == row) {
            ClearSpriteY(channel);
        } else {
            if (wChannelStopQueued[channel]) {
                if (on)
                    SetSpriteTile4(channel);
                else {
                    ClearSpriteY(channel);
                    continue;
                }
            } else {
                SetSpriteTile3(channel);
            }
            SetSpriteY(channel, row - wSongScroll);
            if (hGameboyType)
                SetSpriteAttribute(channel, IsBookmarked(row));
        }
    }
}

/* $47AA: per-frame visual update (screen idx 4) */
void SongScreenUpdate(void)
{
    UBYTE sprite;

    if (wPlayMode == PLAY_SONG) {
        UpdateSongPlayMarker();
    } else {
        for (sprite = 0; sprite != 8; sprite++)
            ClearSpriteY(sprite);
    }
}

/* $47CF: pick row draw palettes (cursor/bookmark-aware) */
void SetSongRowPalettes(UBYTE y)
{
    UBYTE bookmarked;
    UBYTE v;
    UBYTE shadow;
    UBYTE channel, current;

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
    for (channel = 0; channel < 4; channel++) {
        v = ReadSequenceByte(channel, wSongRow);
        DrawStringIndexed(' ');
        current = channel == wSongColumn;
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

/* $492D: draw cursor-row string (SetVramCursor at wSongRow-wSongScroll,
   SetSongRowPalettes) */
void DrawSongCursorRow(void)
{
    UBYTE row;

    row = wSongRow + 2 - wSongScroll;
    SetVramCursor(0, row);
    SetSongRowPalettes(row);
}

/* $494E: scroll the song grid for the row above: ScrollSongVramUp (+
   CopyVramRowUp on CGB), redraw row 2 ($9840) */
void ScrollSongUp(void)
{
    ScrollSongVramUp();
    if (hGameboyType)
        CopyVramRowUp();
    wVramCursor = (UBYTE *)BG_MAP(0, 2);
    SetSongRowPalettes(2);
    wPartialRedrawDone = 1;
}

/* $4973: scroll the song grid for the row below: ScrollSongVramDown (+
   CopyVramRowDown on CGB), redraw row 17 ($9A20) */
void ScrollSongDown(void)
{
    ScrollSongVramDown();
    if (hGameboyType)
        CopyVramRowDown();
    wVramCursor = (UBYTE *)BG_MAP(0, 17);
    SetSongRowPalettes(0x11);
    wPartialRedrawDone = 1;
}

/* $4998: scan down 1..16 from cursor wSongRow, wrapping at 0 -> $ff */
void FindNextSongRow(void)
{
    UBYTE distance;

    for (distance = 1; distance < 0x10; distance++) {
        if (distance + wSongRow == 0) {
            wSongRow = 0xFF;
            return;
        }
        if (IsBookmarked(distance + wSongRow)) {
            wSongRow = distance + wSongRow;
            return;
        }
    }
    wSongRow += 0x10;
    if (wSongRow > 0xBF)
        wSongRow = 0xBF;
}

/* $49E7: B+DOWN: move the song cursor (wSongRow) down a page (not while
   wCursorMode == 4) */
UBYTE PageSongCursorDown(void)
{
    UBYTE d;

    if (wCursorMode == CURSOR_SELECTING)
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

/* $4A25: scan up 1..16 from cursor wSongRow, wrapping */
void FindPreviousSongRow(void)
{
    UBYTE distance;

    for (distance = 1; distance < 0x10; distance++) {
        if (wSongRow - distance == 0xFF) {
            wSongRow = 0;
            return;
        }
        if (IsBookmarked(wSongRow - distance)) {
            wSongRow = wSongRow - distance;
            return;
        }
    }
    wSongRow -= 0x10;
    if (wSongRow == 0xFF)
        wSongRow = 0;
}

/* $4A79: B+UP: move the song cursor (wSongRow) up a page */
UBYTE PageSongCursorUp(void)
{
    UBYTE d;

    if (wCursorMode == CURSOR_SELECTING)
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

/* $4AB8: SaveSelectionState, seed the wSelection* rectangle from the song
   cursor */
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

/* $4AFD: read seq[wSongRow,wSongColumn] ($ff -> empty path), validate+cache
   via CloneSongChain */
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

/* $6249: bounds-checked ($aff0-$afff) WriteSongByte into the bookmarks row */
void WriteBookmarkByte(UWORD p, UBYTE v)
{
    if (p < (UWORD)0xAFF0)
        DebugTrap();
    if (p >= (UWORD)0xB000)
        DebugTrap();
    WriteSongByte(0, p, v);
}

/* $627C: bubble-sort the 16-byte song bookmarks row ($aff0, song offset
   $0FF0) */
void SortBookmarks(void)
{
    UBYTE swapped, slot, a, b;
    UBYTE *p;

    do {
        slot = 15;
        swapped = 0;
        do {
            p = SramBookmarks + slot;
            a = ReadSongByte(0, p - 1);
            b = ReadSongByte(0, p);
            if (b < a) {
                swapped = 1;
                WriteBookmarkByte(p - 1, b);
                WriteBookmarkByte(p, a);
            }
        } while (--slot);
    } while (swapped);
}

/* $6320: 1 if song row `row` is in the bookmark list (SramBookmarks, 16 rows
   in order, $FF for none) */
UBYTE IsBookmarked(UBYTE row)
{
    UBYTE slot = 0;
    UBYTE v;
    UBYTE *p;

    if (row == 0xFF)
        return 0;
    do {
        p = SramBookmarks + slot;
        v = ReadSongByte(0, p);
        if (row < v)
            return 0;
        if (row == v)
            return 1;
        slot++;
    } while (slot < 0x10);
    return 0;
}

/* $637C: append song row to bookmarks; full -> ShowMax16Marks */
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
    UBYTE slot;

    if (!IsBookmarked(row))
        return;
    for (slot = 0; slot < 0x10; slot++)
        if (row == ReadSongByte(0, SramBookmarks + slot))
            WriteBookmarkByte(SramBookmarks + slot, 0xFF);
    SortBookmarks();
}

/* $6413: song row in list ? RemoveBookmark : AddBookmark */
void ToggleBookmark(UBYTE row)
{
    if (IsBookmarked(row))
        RemoveBookmark(row);
    else
        AddBookmark(row);
}

/* $643D: FillMemory($aff0, $ff, 16) */
void ClearBookmarks(void)
{
    FillMemory(SramBookmarks, 0xFF, 0x10);
}

/* $644F: song rows [lo,hi) removed: drop bookmarks inside, shift later ones
   up, re-sort */
void AdjustBookmarksOnDelete(UBYTE low, UBYTE high)
{
    UBYTE slot, v;
    UBYTE *p;

    slot = 15;
    do {
        p = SramBookmarks + slot;
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
    } while (slot--);
    SortBookmarks();
}

/* $64CA: song rows shifted (src->dst): rebase bookmarks, drop wrapped,
   re-sort */
void AdjustBookmarksOnInsert(UBYTE from, UBYTE to)
{
    UBYTE slot, v;
    UBYTE *p;

    slot = 15;
    do {
        p = SramBookmarks + slot;
        v = ReadSongByte(0, p);
        if (v != 0xFF && v >= from) {
            v -= from;
            v += to;
            if (v > from)
                WriteBookmarkByte(p, v);
            else
                WriteBookmarkByte(p, 0xFF);
        }
    } while (slot--);
}
