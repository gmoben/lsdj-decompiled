/*
 * Song screen input -- LSDj 9.4.0, ROM bank $07:$4B3B-$56FD.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit for the same reason as SongCutCopy.c: CopySongColumn takes
 * two UWORDs here (the ROM casts the pointer arguments; SongScreen.c defines
 * it with pointer parameters).
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE SongColumnOffsets[];              /* $07:$4047 */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelLiveQueued[];             /* $C300, [4] */
extern UBYTE wChannelStopQueued[];                /* $C304, [4] */
extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wColumnChain[];                   /* $C44F, [4] */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wQuickMarking;             /* $C412 */
extern UBYTE wSongScopeDirty;               /* $C404 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wMessageNoPause;                  /* $C530 */
extern UBYTE wMuteSoloLatch;                 /* $C56A */
extern UBYTE wPlayStartChannel;                /* $C94C */
extern UBYTE wPlayStartRow;                 /* $C94D */
extern UBYTE wSongSelectionScroll;                /* $C94A */
extern UBYTE wSongSelectionRow;                   /* $C94B */
extern UBYTE wSongAPressState;              /* $C951 */
extern UBYTE wSongCellEmpty;                /* $C953 */
extern UBYTE wSyncTick;                     /* $CB69 */

void EraseSongCursor(void);
void DrawSongCursor(void);
void RedrawSongRow(void);
void ScrollSongToCursor(void);
void ScrollSongUp(void);
void ScrollSongDown(void);
UBYTE PageSongCursorDown(void);
UBYTE PageSongCursorUp(void);
void EditSongSelection(void);
void LoadSongCursorChain(void);
void CutCopySongSelection(UBYTE isCut);
void PasteSongSelection(void);
UBYTE SongChannelActiveMask(void);
void BeginSongSelection(void);
void DrawSongCursorRow(void);
void ToggleBookmark(UBYTE row);
void DrawChannelHeader(void);
void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain);
void CopySongColumn(UWORD p, UWORD q);    /* defined with pointers in SongScreen.c */
UBYTE IsSongChainUsed(void);                /* bank $1F */
void MarkChainUsed(void);                   /* bank 1 */
void ShowTapAddChain(void);                 /* bank 1 */
void SoundLiveStopRange(void);                /* bank 2 */
void SoundStartRange(void);                /* bank 2 */
void SoundTogglePlayback(void);                    /* bank 2 */
void SaveSelectionState(void);              /* bank 5 */
void MoveMapCursorUp(void);                       /* bank 5 */
void MoveMapCursorDown(void);                     /* bank 5 */
void MoveMapCursorRight(void);                    /* bank 5 */

/*
 * The song screen's input, an if/else chain on wButtons: A writes the chain
 * last used in the column (or the next free one, or a fresh one), the
 * directions move the cursor and scroll, A+direction step the chain number,
 * A+B clears the cell (or, on an empty one, deletes the row), B+up/down page,
 * select+left toggles LIVE mode, select+right opens the chain under the
 * cursor, select+B/A/B start, cut/copy and paste selections, B twice toggles
 * a bookmark, start and select+start drive playback. 1 when the key was
 * taken.
 */
UBYTE SongScreenInput(void)
{
    UBYTE v;
    UBYTE chain;
    UBYTE cell;
    UBYTE *p;
    UBYTE left;
    UBYTE right;
    UBYTE top;
    UBYTE bot;

    SetVramCursor(SongColumnOffsets[wSongColumn], wSongRow + 2 - wSongScroll);
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wButtons != 0x20)
        wSongAPressState = 0;
    if (wCursorMode == 4 && wButtons == 0x43) {
        wSongColumn = wSelectionLeft;
        wSongRow = wSelectionTop;
        FarCall(5, 0x747D);
        ScrollSongToCursor();
        RedrawSongRow();
        ShowMessage("CANCEL SELECT");
        return 1;
    }
    if (wAPressEnabled && wButtons == 0x10) {
        if (wCursorMode == 4)
            return 0;
        if (wDrawStep++ && wSongCellEmpty) {
            chain = FarCallByte(0x1F, FAR_FN(IsSongChainUsed));
            wDrawStep = 0;
            goto store;
        }
        chain = ReadSeqByte(wSongColumn, wSongRow);
        wSongCellEmpty = chain == 0xFF;
        if (wSongCellEmpty) {
            chain = wColumnChain[wSongColumn];
            if (chain != 0xFF) {
                FarCallArg(1, FAR_FN(MarkChainUsed), chain);
                goto store;
            }
            v = wSongRow;
            do {
                if (v == 0) {
                    chain = FarCallByte(0x1F, FAR_FN(IsSongChainUsed));
                    wSongCellEmpty = 0;
                    goto store;
                }
                v--;
                chain = ReadSeqByte(wSongColumn, v);
            } while (chain == 0xFF);
        }
    store:
        if (chain != 0xFF) {
            wColumnChain[wSongColumn] = chain;
            DrawHexByte(chain);
            WriteSongCell(wSongColumn, wSongRow, chain);
        }
        return 1;
    }
    if (wButtons == 0x01) {
        if (wSongColumn == 3 || wBHeld)
            return 0;
        if (wCursorMode != 4)
            EraseSongCursor();
        wSongColumn++;
        if (!wMessageStrPtr && !wMessageTimer)
            DrawChannelHeader();
        if (wCursorMode == 4)
            EditSongSelection();
        else
            DrawSongCursor();
        return 1;
    } else if (wButtons == 0x02) {
        if (!wSongColumn || wBHeld)
            return 0;
        if (wCursorMode != 4)
            EraseSongCursor();
        wSongColumn--;
        if (!wMessageStrPtr && !wMessageTimer)
            DrawChannelHeader();
        if (wCursorMode == 4)
            EditSongSelection();
        else
            DrawSongCursor();
        return 1;
    } else if (wButtons == 0x04) {
        if (!wSongRow)
            return 0;
        if (wCursorMode == 4 && wSelectionTop - wSongRow == 0x29)
            return 0;
        if (!wSongRow)
            return 0;
        if (wCursorMode != 4)
            EraseSongCursor();
        wSongRow--;
        if (wSongRow - wSongScroll == 0xFF) {
            wSongScroll--;
            ScrollSongUp();
        } else if (wCursorMode != 4) {
            DrawSongCursor();
        }
        if (wCursorMode == 4)
            EditSongSelection();
        return 1;
    } else if (wButtons == 0x08) {
        if (wCursorMode == 4 && wSongRow - wSelectionTop == 0x29)
            return 0;
        if (wSongRow == 0xBF)
            return 0;
        if (wCursorMode != 4)
            EraseSongCursor();
        wSongRow++;
        if (wSongRow - wSongScroll == 0x10) {
            wSongScroll++;
            ScrollSongDown();
        } else if (wCursorMode != 4) {
            DrawSongCursor();
        }
        if (wCursorMode == 4)
            EditSongSelection();
        return 1;
    } else if (wButtons == 0x28) {
        return PageSongCursorDown();
    } else if (wButtons == 0x24) {
        return PageSongCursorUp();
    } else if (wButtons == 0x42) {
        if (!wAPressEnabled)
            return 0;
        if (wButtonsSame)
            return 1;
        wLiveMode = !wLiveMode;
        if (wMessageStrPtr) {
            wMessageNoPause = 0;
            wMessageStrPtr = 0;
            wMessageTimer = 0;
        }
        if (!wLiveMode) {
            ClearSpriteY(4);
            ClearSpriteY(5);
            ClearSpriteY(6);
            ClearSpriteY(7);
            SetSpriteTile3(0);
            SetSpriteTile3(1);
            SetSpriteTile3(2);
            SetSpriteTile3(3);
        }
        MemSet(wChannelNextSongRow, 0xFF, 4);
        MemSet(wChannelLiveQueued, 0, 4);
        MemSet(wChannelStopQueued, 0, 4);
        if (wLiveMode && wPlayMode && wPlayMode != 4) {
            wChannelNextSongRow[wPlayStartChannel] = wPlayStartRow;
            if (wPlayMode != 3)
                wChannelLiveQueued[wPlayStartChannel] = 1;
            wPlayMode = 4;
        }
        DrawSongScreen();
        if (wCursorMode == 4)
            EditSongSelection();
        return 1;
    } else if (wButtons == 0x11) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongColumn, wSongRow);
        if (v < 0x7F)
            v++;
        v &= 0x7F;
        WriteSongCell(wSongColumn, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x14) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongColumn, wSongRow) + 0x10;
        if (v & 0x80)
            v = 0x7F;
        WriteSongCell(wSongColumn, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x12) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongColumn, wSongRow);
        if (v && v != 0xFF) {
            v--;
            WriteSongCell(wSongColumn, wSongRow, v);
            DrawHexByte(v);
        }
        return 1;
    } else if (wButtons == 0x18) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongColumn, wSongRow);
        if (v == 0xFF)
            v = 0;
        else if (v & 0xF0) {
            v -= 0x10;
            v &= 0x7F;
        }
        else
            v = 0;
        WriteSongCell(wSongColumn, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x44) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    } else if (wButtons == 0x48) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    } else if (wButtons == 0x30) {
        if (!wAPressEnabled && !wBHeld)
            return 0;
        wBHeld = 0;
        cell = ReadSeqByte(wSongColumn, wSongRow);
        wCursorMode = 2;
        wSelectionLeft = wSongColumn;
        wSelectionRight = wSongColumn + 1;
        wSelectionTop = 0;
        wSelectionBottom = 1;
        wClipboardType = 4;
        WriteClipboardCell(wSongColumn, 0, cell);
        if (cell & 0x80) {
            p = (UBYTE *)0xB290 + ((UWORD)wSongRow << 2);
            p += wSongColumn;
            CopySongColumn((UWORD)(p + 4), (UWORD)p);
            WriteSongCell(wSongColumn, 0xBF, 0xFF);
        } else {
            wColumnChain[wSongColumn] = cell;
            WriteSongCell(wSongColumn, wSongRow, 0xFF);
        }
        RedrawSongRow();
        return 1;
    } else if (wButtons == 0x50) {
        if (wCursorMode == 1)
            return 0;
        if (!wSelectHeld || wButtonsSame)
            return 0;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            if (wAPressEnabled || wSelectionLeft != wSongColumn || wSelectionTop != wSongRow)
                CutCopySongSelection(1);
            else
                LoadSongCursorChain();
            return 0;
        }
        if (wClipboardType != 4)
            return 0;
        EraseSongCursor();
        PasteSongSelection();
        RedrawSongRow();
        return 1;
    } else if (wButtons == 0x70 && wCursorMode == 4) {
        LoadSongCursorChain();
        return 1;
    } else if (wButtons == 0xC0) {
        if (wButtonsSame && (wLiveMode || !wPlayMode))
            return 1;
        if (wLiveMode) {
            if (SongChannelActiveMask())
                FarCall(2, FAR_FN(SoundLiveStopRange));
        } else {
            if (wPlayMode)
                FarCall(2, FAR_FN(SoundTogglePlayback));
            FarCall(2, FAR_FN(SoundTogglePlayback));
        }
        return 1;
    } else if (wButtons == 0x80) {
        if (wButtonsSame || wBHeld) {
            if (wMuteSoloLatch == 2)
                wMuteSoloLatch = 1;
            return 1;
        }
        if (wLiveMode)
            FarCall(2, FAR_FN(SoundStartRange));
        else if (wSyncMode == 1 && !wPlayMode && wSyncTick != 0xFF)
            BeginSongSelection();
        else
            FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    } else if (wButtons == 0x82) {
        if (!wButtonsSame)
            BeginSongSelection();
        return 1;
    } else if (wButtons == 0x41) {
        if (wButtonsSame)
            return 0;
        v = wSongRow;
        while (1) {
            chain = ReadSeqByte(wSongColumn, v);
            if (chain < 0x80) {
                wChain = chain;
                wSongRow = v;
                break;
            }
            if (!v) {
                FarCall(1, FAR_FN(ShowTapAddChain));
                return 0;
            }
            v--;
        }
        while (wChainRow && FarCallArg2(2, (UWORD)SoundGetChainPhrase, wChain, (char)wChainRow) == 0xFF)
            wChainRow--;
        FarCall(5, MoveMapCursorRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    } else if (wButtons == 0x20) {
        if (!wAPressEnabled)
            return 0;
        if (wCursorMode == 4) {
            CutCopySongSelection(0);
            return 1;
        }
        switch (wSongAPressState) {
        case 0:
            wSongAPressState = 1;
            return 0;
        case 1:
            if (wAPressEnabled && !wButtonsSame) {
                wSongAPressState = 2;
                return 1;
            }
            return 0;
        case 2:
            if (wAPressEnabled && !wButtonsSame) {
                wSongAPressState = 0;
                ToggleBookmark(wSongRow);
                DrawSongCursorRow();
            }
        }
        return 1;
    } else if (wSelectHeld && wButtons == 0x60) {
        wSelectHeld = 0;
        if (wCursorMode == 4 && wClipboardType == 4) {
            left = wSelectionLeft;
            right = wSongColumn;
            top = wSelectionTop;
            bot = wSongRow;
            wQuickMarking = 1;
            SortBytePair(&left, &right);
            if (left || right != 3) {
                wSelectionLeft = 0;
                wSongColumn = 3;
                EditSongSelection();
                return 1;
            }
            SortBytePair(&top, &bot);
            if (top != wSongScroll || bot != wSongScroll + 0x0F) {
                wSelectionTop = wSongScroll;
                wSongRow = wSelectionTop + 0x0F;
                EditSongSelection();
                return 1;
            }
            CutCopySongSelection(0);
            return 1;
        }
        FarCall(5, SaveSelectionState);
        wQuickMarking = 0;
        wSelectionLeft = wSongColumn;
        wSelectionTop = wSongRow;
        wSongSelectionRow = wSongRow;
        wSongSelectionScroll = wSongScroll;
        ShowMessage(wCloneMode ? "SELECT / SLIM CLONE " : "SELECT / DEEP CLONE ");
        return 1;
    }
    return 0;
}
