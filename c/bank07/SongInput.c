/*
 * Song screen input -- LSDj 9.4.0, ROM bank $07:$4B3B-$56FD.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Its own unit for the same reason as SongCutCopy.c: SongCopyColumn takes
 * two UWORDs here (the ROM casts the pointer arguments; SongScreen.c defines
 * it with pointer parameters).
 */

#include "lsdj.h"

extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE SongColOffsets[];              /* $07:$4047 */
extern UBYTE wChanNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChanLiveQueued[];             /* $C300, [4] */
extern UBYTE wChanStarted[];                /* $C304, [4] */
extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wColChain[];                   /* $C44F, [4] */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wClipboardType_p1;             /* $C412 */
extern UBYTE wSongScopeDirty;               /* $C404 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wColorSet_p1;                  /* $C530 */
extern UBYTE wSelectPal_p1;                 /* $C56A */
extern UBYTE wPlayStartChan;                /* $C94C */
extern UBYTE wPlayStartRow;                 /* $C94D */
extern UBYTE wSongSelScroll;                /* $C94A */
extern UBYTE wSongSelRow;                   /* $C94B */
extern UBYTE wSongAPressState;              /* $C951 */
extern UBYTE wSongCellEmpty;                /* $C953 */
extern UBYTE wSyncTick;                     /* $CB69 */

void SongEraseCursor(void);
void SongDrawCursor(void);
void SongRedrawRow(void);
void SongScrollToCursor(void);
void SongScrollUp(void);
void SongScrollDown(void);
UBYTE SongCursorPageDown(void);
UBYTE SongCursorPageUp(void);
void SongEditSelection(void);
void SongLoadCursorChain(void);
void CutCopySongSelection(UBYTE isCut);
void PasteSongSelection(void);
UBYTE SongChanActiveMask(void);
void SongBeginSelection(void);
void SongDrawCursorRow(void);
void ToggleBookmark(UBYTE row);
void DrawChannelHeader(void);
void WriteSongCell(UBYTE col, UBYTE row, UBYTE chain);
void SongCopyColumn(UWORD p, UWORD q);    /* defined with pointers in SongScreen.c */
UBYTE IsSongChainUsed(void);                /* bank $1F */
void MarkChainUsed(void);                   /* bank 1 */
void ShowTapAddChain(void);                 /* bank 1 */
void SndAdvanceRetrig(void);                /* bank 2 */
void SndStopAllIfIdle(void);                /* bank 2 */
void StopPlayback(void);                    /* bank 2 */
void SaveSelectionState(void);              /* bank 5 */
void MapMoveUp(void);                       /* bank 5 */
void MapMoveDown(void);                     /* bank 5 */
void MapMoveRight(void);                    /* bank 5 */

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

    GetTilemapAddr(SongColOffsets[wSongCol], wSongRow + 2 - wSongScroll);
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wButtons != 0x20)
        wSongAPressState = 0;
    if (wCursorMode == 4 && wButtons == 0x43) {
        wSongCol = wSelLeft;
        wSongRow = wSelTop;
        FarCall(5, 0x747D);
        SongScrollToCursor();
        SongRedrawRow();
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
        chain = ReadSeqByte(wSongCol, wSongRow);
        wSongCellEmpty = chain == 0xFF;
        if (wSongCellEmpty) {
            chain = wColChain[wSongCol];
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
                chain = ReadSeqByte(wSongCol, v);
            } while (chain == 0xFF);
        }
    store:
        if (chain != 0xFF) {
            wColChain[wSongCol] = chain;
            DrawHexByte(chain);
            WriteSongCell(wSongCol, wSongRow, chain);
        }
        return 1;
    }
    if (wButtons == 0x01) {
        if (wSongCol == 3 || wBHeld)
            return 0;
        if (wCursorMode != 4)
            SongEraseCursor();
        wSongCol++;
        if (!wMsgStrPtr && !wMsgTimer)
            DrawChannelHeader();
        if (wCursorMode == 4)
            SongEditSelection();
        else
            SongDrawCursor();
        return 1;
    } else if (wButtons == 0x02) {
        if (!wSongCol || wBHeld)
            return 0;
        if (wCursorMode != 4)
            SongEraseCursor();
        wSongCol--;
        if (!wMsgStrPtr && !wMsgTimer)
            DrawChannelHeader();
        if (wCursorMode == 4)
            SongEditSelection();
        else
            SongDrawCursor();
        return 1;
    } else if (wButtons == 0x04) {
        if (!wSongRow)
            return 0;
        if (wCursorMode == 4 && wSelTop - wSongRow == 0x29)
            return 0;
        if (!wSongRow)
            return 0;
        if (wCursorMode != 4)
            SongEraseCursor();
        wSongRow--;
        if (wSongRow - wSongScroll == 0xFF) {
            wSongScroll--;
            SongScrollUp();
        } else if (wCursorMode != 4) {
            SongDrawCursor();
        }
        if (wCursorMode == 4)
            SongEditSelection();
        return 1;
    } else if (wButtons == 0x08) {
        if (wCursorMode == 4 && wSongRow - wSelTop == 0x29)
            return 0;
        if (wSongRow == 0xBF)
            return 0;
        if (wCursorMode != 4)
            SongEraseCursor();
        wSongRow++;
        if (wSongRow - wSongScroll == 0x10) {
            wSongScroll++;
            SongScrollDown();
        } else if (wCursorMode != 4) {
            SongDrawCursor();
        }
        if (wCursorMode == 4)
            SongEditSelection();
        return 1;
    } else if (wButtons == 0x28) {
        return SongCursorPageDown();
    } else if (wButtons == 0x24) {
        return SongCursorPageUp();
    } else if (wButtons == 0x42) {
        if (!wAPressEnabled)
            return 0;
        if (wButtonsSame)
            return 1;
        wLiveMode = !wLiveMode;
        if (wMsgStrPtr) {
            wColorSet_p1 = 0;
            wMsgStrPtr = 0;
            wMsgTimer = 0;
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
        MemSet(wChanNextSongRow, 0xFF, 4);
        MemSet(wChanLiveQueued, 0, 4);
        MemSet(wChanStarted, 0, 4);
        if (wLiveMode && wPlayMode && wPlayMode != 4) {
            wChanNextSongRow[wPlayStartChan] = wPlayStartRow;
            if (wPlayMode != 3)
                wChanLiveQueued[wPlayStartChan] = 1;
            wPlayMode = 4;
        }
        DrawSongScreen();
        if (wCursorMode == 4)
            SongEditSelection();
        return 1;
    } else if (wButtons == 0x11) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongCol, wSongRow);
        if (v < 0x7F)
            v++;
        v &= 0x7F;
        WriteSongCell(wSongCol, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x14) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongCol, wSongRow) + 0x10;
        if (v & 0x80)
            v = 0x7F;
        WriteSongCell(wSongCol, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x12) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongCol, wSongRow);
        if (v && v != 0xFF) {
            v--;
            WriteSongCell(wSongCol, wSongRow, v);
            DrawHexByte(v);
        }
        return 1;
    } else if (wButtons == 0x18) {
        if (wCursorMode == 4)
            return 0;
        wSongScopeDirty = 1;
        v = ReadSeqByte(wSongCol, wSongRow);
        if (v == 0xFF)
            v = 0;
        else if (v & 0xF0) {
            v -= 0x10;
            v &= 0x7F;
        }
        else
            v = 0;
        WriteSongCell(wSongCol, wSongRow, v);
        DrawHexByte(v);
        return 1;
    } else if (wButtons == 0x44) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    } else if (wButtons == 0x48) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    } else if (wButtons == 0x30) {
        if (!wAPressEnabled && !wBHeld)
            return 0;
        wBHeld = 0;
        cell = ReadSeqByte(wSongCol, wSongRow);
        wCursorMode = 2;
        wSelLeft = wSongCol;
        wSelRight = wSongCol + 1;
        wSelTop = 0;
        wSelBottom = 1;
        wClipboardType = 4;
        WriteClipboardCell(wSongCol, 0, cell);
        if (cell & 0x80) {
            p = (UBYTE *)0xB290 + ((UWORD)wSongRow << 2);
            p += wSongCol;
            SongCopyColumn((UWORD)(p + 4), (UWORD)p);
            WriteSongCell(wSongCol, 0xBF, 0xFF);
        } else {
            wColChain[wSongCol] = cell;
            WriteSongCell(wSongCol, wSongRow, 0xFF);
        }
        SongRedrawRow();
        return 1;
    } else if (wButtons == 0x50) {
        if (wCursorMode == 1)
            return 0;
        if (!wSelectHeld || wButtonsSame)
            return 0;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            if (wAPressEnabled || wSelLeft != wSongCol || wSelTop != wSongRow)
                CutCopySongSelection(1);
            else
                SongLoadCursorChain();
            return 0;
        }
        if (wClipboardType != 4)
            return 0;
        SongEraseCursor();
        PasteSongSelection();
        SongRedrawRow();
        return 1;
    } else if (wButtons == 0x70 && wCursorMode == 4) {
        SongLoadCursorChain();
        return 1;
    } else if (wButtons == 0xC0) {
        if (wButtonsSame && (wLiveMode || !wPlayMode))
            return 1;
        if (wLiveMode) {
            if (SongChanActiveMask())
                FarCall(2, FAR_FN(SndAdvanceRetrig));
        } else {
            if (wPlayMode)
                FarCall(2, FAR_FN(StopPlayback));
            FarCall(2, FAR_FN(StopPlayback));
        }
        return 1;
    } else if (wButtons == 0x80) {
        if (wButtonsSame || wBHeld) {
            if (wSelectPal_p1 == 2)
                wSelectPal_p1 = 1;
            return 1;
        }
        if (wLiveMode)
            FarCall(2, FAR_FN(SndStopAllIfIdle));
        else if (wSyncMode == 1 && !wPlayMode && wSyncTick != 0xFF)
            SongBeginSelection();
        else
            FarCall(2, FAR_FN(StopPlayback));
        return 1;
    } else if (wButtons == 0x82) {
        if (!wButtonsSame)
            SongBeginSelection();
        return 1;
    } else if (wButtons == 0x41) {
        if (wButtonsSame)
            return 0;
        v = wSongRow;
        while (1) {
            chain = ReadSeqByte(wSongCol, v);
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
        while (wChainRow && FarCallArg2(2, (UWORD)SndGetChainPhrase, wChain, (char)wChainRow) == 0xFF)
            wChainRow--;
        FarCall(5, MapMoveRight);
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
                SongDrawCursorRow();
            }
        }
        return 1;
    } else if (wSelectHeld && wButtons == 0x60) {
        wSelectHeld = 0;
        if (wCursorMode == 4 && wClipboardType == 4) {
            left = wSelLeft;
            right = wSongCol;
            top = wSelTop;
            bot = wSongRow;
            wClipboardType_p1 = 1;
            SortBytePair(&left, &right);
            if (left || right != 3) {
                wSelLeft = 0;
                wSongCol = 3;
                SongEditSelection();
                return 1;
            }
            SortBytePair(&top, &bot);
            if (top != wSongScroll || bot != wSongScroll + 0x0F) {
                wSelTop = wSongScroll;
                wSongRow = wSelTop + 0x0F;
                SongEditSelection();
                return 1;
            }
            CutCopySongSelection(0);
            return 1;
        }
        FarCall(5, SaveSelectionState);
        wClipboardType_p1 = 0;
        wSelLeft = wSongCol;
        wSelTop = wSongRow;
        wSongSelRow = wSongRow;
        wSongSelScroll = wSongScroll;
        ShowMessage(wCloneMode ? "SELECT / SLIM CLONE " : "SELECT / DEEP CLONE ");
        return 1;
    }
    return 0;
}
