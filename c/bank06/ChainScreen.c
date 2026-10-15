/*
 * The chain screen -- LSDj 9.4.0, ROM bank $06.
 *
 * Its rows and transposes, the draw and the update, the cursor, phrase open
 * and clone, the input, cut/copy/paste and the selection.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wChanChain[];                  /* $C170, [4] */
extern UBYTE wChanChainRow[];               /* $C17C, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainCol;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE ChainColX[];                  /* the chain screen's two columns: x */
void DrawAllChanStatus(void);               /* bank $1F */
extern char TxtClonedPhrase[];              /* $06:$5899 "CLONED PHRASE" */
extern char TxtDashes2[];                   /* $06:$5A5E " --" */
extern UBYTE wColPhrase[];                  /* $C453, [4] */
void DrawChainPos(void);
void LoadChainRow(void);
void AllocNextFreePhrase(void);             /* bank 1 */
void WriteChainPhrase(void);                /* bank $1F */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE src, UBYTE dst);
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void SaveSelectionState(void);              /* $05:$743F */
void MoveMapCursorLeft(void);                     /* $05:$4233 */
void MoveMapCursorUp(void);                       /* $05:$426A */
void MoveMapCursorDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);                 /* $02:$78C4 */
void TogglePlayback(void);                    /* $02:$7C4C */
void SendLinkClipboard(void);                   /* bank $1C */

UBYTE GetCursorChain(void)
{
    return ReadSeqByte(wSongCol, wSongRow);
}

UBYTE LoadChainCursorPhrase(void)
{
    UBYTE row, ph;

    row = wChainRow;
    for (;;) {
        ph = FarCallArg2(2, (UWORD)SndGetChainPhrase, wChain, row);
        if (ph != 0xFF) {
            wPhrase = ph;
            wChainRow = row;
            return 1;
        }
        if (!row)
            return 0;
        row--;
    }
}

UBYTE ChainScreenEnter(UBYTE col)
{
    UBYTE chain, row;

    chain = wChain;
    row = wSongRow;
    if (col >= 4)
        DebugTrap();
    wMessageStrPtr = 0;
    if (wLiveMode && wChanActive[col]) {
        wChain = wChanChain[col];
        wSongRow = wChanSongRow[col];
    } else {
        wChain = ReadSeqByte(col, wSongRow);
        while (wChain == 0xFF && wSongRow)
            wChain = ReadSeqByte(col, --wSongRow);
    }
    if (wChain & 0x80) {
        wChain = chain;
        wSongRow = row;
        return 0;
    }
    wSongCol = col;
    DrawChainScreen();
    return 1;
}

void DrawChainPos(void)
{
    UBYTE r;

    wVramCursor = (UBYTE *)0x9851;
    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    DrawString("CP");
    r = wChainRow;
    DrawByte(r);
}

void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramChainTransposes + ((UWORD)chain << 4);
    WriteSongByte(1, p + row, v);
}

UBYTE ReadChainTranspose(UBYTE chain, UBYTE step)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainTransposes + ((UWORD)chain << 4);
    return ReadSongByte(1, p + step);
}

void ClonePhraseAtCursor(UBYTE src, UBYTE dst)
{
    UBYTE *s1, *d1, *s2, *d2, *s3, *d3, *s4, *d4;
    UWORD soff, doff;
    UBYTE v;
    UBYTE i;

    soff = (UWORD)src << 4;
    doff = (UWORD)dst << 4;
    if (wCurScreen == 3)
        ShowMessage(TxtClonedPhrase);
    if (src == 0xFF)
        DebugTrap();
    if (dst == 0xFF)
        DebugTrap();
    s1 = (UBYTE *)0xA000 + soff;
    s2 = (UBYTE *)0xB000 + soff;
    s3 = (UBYTE *)0xA000 + soff;
    s4 = (UBYTE *)0xAFF0 + soff;
    d1 = (UBYTE *)0xA000 + doff;
    d2 = (UBYTE *)0xB000 + doff;
    d3 = (UBYTE *)0xA000 + doff;
    d4 = (UBYTE *)0xAFF0 + doff;
    for (i = 0; i < 16; i++) {
        v = ReadSongByte(0, s1++);
        WriteSongByte(0, d1++, v);
        v = ReadSongByte(3, s2++);
        WriteSongByte(3, d2++, v);
        v = ReadSongByte(2, s3++);
        WriteSongByte(2, d3++, v);
        v = ReadSongByte(2, s4++);
        WriteSongByte(2, d4++, v);
    }
}

void EraseChainCursor(void)
{
    UBYTE r, c;

    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    c = wChainCol;
    ColorField(ChainColX[wChainCol], wChainRow + 2, 2, c && hGameboyType);
}

void LoadChainRow(void)
{
    UBYTE *p, *q;

    p = SramChainPhrases + ((UWORD)wChain << 4);
    q = SramChainTransposes + ((UWORD)wChain << 4);
    ReadSongBytes(1, p, wSongReadBuf);
    ReadSongBytes(1, q, wUsedTableMap);
}

void DrawChainScreen(void)
{
    UBYTE y;
    UBYTE i, v, pal, r;

    if (wChain & 0x80)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    DrawChainPos();
    LoadChainRow();
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x9841;
    for (i = 0;; i++) {
        v = wSongReadBuf[i];
        if (v == 0xFF) {
            DrawString(TxtDashes2);
        } else {
            DrawStringTile20();
            DrawHexByte(v);
        }
        if (i == 15)
            break;
        wVramCursor += 0x1D;
    }
    wVramCursor = (UBYTE *)0x9845;
    for (i = 0;; i++) {
        DrawHexByte(wUsedTableMap[i]);
        if (i == 15)
            break;
        wVramCursor += 0x1E;
    }
    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    pal = hGameboyType ? 3 : 2;
    y = wChainRow + 2;
    ColorField(ChainColX[wChainCol], y, 2, pal);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
}

void ChainScreenUpdate(void)
{
    if ((wPlayMode == 4 || wPlayMode == 3) && wChanActive[wSongCol]
            && wChain == wChanChain[wSongCol])
        SetSpriteY(0, wChanChainRow[wSongCol]);
    else
        ClearSpriteY(0);
}

void DrawChainCursor(void)
{
    ColorField(ChainColX[wChainCol], wChainRow + 2, 2, hGameboyType ? 3 : 2);
}

void OpenChainPhrase(void)
{
    UBYTE ph;
    UBYTE n;

    ph = FarCallArg2(2, (UWORD)SndGetChainPhrase, wChain, (char)wChainRow);
    FarCall(5, 0x747D);                 /* RestoreSelectionState, bank 5, by address */
    if (ph == 0xFF)
        return;
    wColPhrase[wSongCol] = ph;
    n = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
    if (n != 0xFF) {
        ClonePhraseAtCursor(ph, n);
        FarCallArg3(0x1F, (UWORD)WriteChainPhrase, wChain, (char)wChainRow, n);
        SetVramCursor(2, wChainRow + 2);
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        DrawHexByte(n);
    } else {
        FarCall(1, FAR_FN(ShowOutOfMemory));
    }
}

extern UBYTE wChainCellVal;               /* $C945 */
extern UBYTE wChainCellEmpty;               /* $C946 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wQuickMarking;                 /* $C412 */
extern UBYTE wMuteSoloLatch;                /* $C56A */
extern UBYTE wChainScopeDirty;              /* $C403 */
void MoveMapCursorRight(void);                    /* $05:$4253 */
void ShowTapAddPhrase(void);                /* bank 1 */
void MarkPhraseUsed(void);                  /* bank 1 */
void CutCopyChainSelection(UBYTE cut);
void PasteChainCells(void);
void DrawChainSelection(void);

/* $5BD0: the chain screen's input, an if chain on wButtons: A writes a
   phrase (the column's last, or a new one) or the transpose kept from the
   last edit; the directions move the cursor; A+RIGHT/UP/LEFT/DOWN step the
   phrase (by 1 / 16) or the transpose (by 1 / an octave, 4 on the noise
   channel) under the cursor or across the selection; B+LEFT/RIGHT change
   channel, B+UP/DOWN the song row; A+B cuts the cell; select+A pastes (or,
   in a selection, cuts); start and select+start play; select+directions
   move on the screen map; B in a selection copies it; select+B selects,
   then widens to the column and the screen. 1 when the key was taken */

UBYTE ChainScreenInput(void)
{
    UBYTE x, y, x1, y0, y1, v, left, right;
    UBYTE isA;
    char cx, cy;

    if (wChain & 0x80)
        DebugTrap();
    isA = wButtons == 0x10;
    if (!isA)
        wDrawStep = 0;
    if (wCursorMode == 4 && wButtons == 0x43) {
        wChainCol = wSelectionLeft;
        wChainRow = wSelectionTop;
        FarCall(5, 0x747D);
        if (hGameboyType) {
            FarCall(1, 0x6CD7);
            FarCall(5, 0x4040);
        }
        DrawChainScreen();
        ShowMessage("CANCEL SELECT");
        DrawChainCursor();
        return 1;
    }
    hBgDigitPal = 4;
    hBgPal = 1;
    if (wAPressEnabled && isA) {
        if (wCursorMode == 4)
            return 0;
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        SetVramCursor(ChainColX[wChainCol], wChainRow + 2);
        switch (wChainCol) {
        case 0:
            if (wDrawStep++ && wChainCellEmpty) {
                v = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
                wDrawStep = 0;
            } else {
                v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, wChainRow);
                wChainCellEmpty = v == 0xFF;
                if (wChainCellEmpty) {
                    v = wColPhrase[wSongCol];
                    if (v == 0xFF) {
                        v = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
                        wChainCellEmpty = 0;
                    } else
                        FarCallArg(1, FAR_FN(MarkPhraseUsed), v);
                }
            }
            if (v != 0xFF) {
                FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, (char)wChainRow, v);
                DrawHexByte(v);
                wColPhrase[wSongCol] = v;
            }
            return 1;
        case 1:
            if (!ReadChainTranspose(wChain, (char)wChainRow)) {
                SetChainTranspose(wChain, (char)wChainRow, wChainCellVal);
                DrawHexByte(wChainCellVal);
                return 1;
            }
        }
    }
    if (wButtons == 0x01) {
        if (wChainCol == 1)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainCol++;
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == 0x02) {
        if (!wChainCol)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainCol--;
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == 0x04) {
        if (!wChainRow)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainRow--;
        DrawChainPos();
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == 0x08) {
        if (wChainRow == 0x0F)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainRow++;
        DrawChainPos();
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == 0x11) {
        cx = wChainCol;
        x1 = cx;
        cy = wChainRow;
        y1 = cy;
        if (wCursorMode == 4) {
            x = wSelectionLeft;
            y0 = wSelectionTop;
        } else {
            x = cx;
            y0 = cy;
        }
        SortBytePair(&x, &x1);
        SortBytePair(&y0, &y1);
        x1++;
        y1++;
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        wChainScopeDirty = 1;
        for (; x < x1; x++) {
            for (y = y0; y < y1; y++) {
                switch (x) {
                case 0:
                    v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y);
                    if (v != 0xFE) {
                        v = v + 1;
                        FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, v);
                        SetVramCursor(ChainColX[0], y + 2);
                        DrawHexByte(v);
                    }
                    break;
                case 1:
                    SetVramCursor(ChainColX[1], y + 2);
                    v = ReadChainTranspose(wChain, y) + 1;
                    SetChainTranspose(wChain, y, v);
                    DrawHexByte(v);
                    wChainCellVal = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == 0x14) {
        cx = wChainCol;
        x1 = cx;
        cy = wChainRow;
        y1 = cy;
        if (wCursorMode == 4) {
            x = wSelectionLeft;
            y0 = wSelectionTop;
        } else {
            x = cx;
            y0 = cy;
        }
        SortBytePair(&x, &x1);
        SortBytePair(&y0, &y1);
        x1++;
        y1++;
        wChainScopeDirty = 1;
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        for (; x < x1; x++) {
            for (y = y0; y < y1; y++) {
                switch (x) {
                case 0:
                    v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y);
                    if (v == 0xFF)
                        v = wColPhrase[wSongCol];
                    else if (v >= 0xEF)
                        v = 0xFE;
                    else
                        v = v + 0x10;
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, v);
                    SetVramCursor(ChainColX[0], y + 2);
                    DrawHexByte(v);
                    break;
                case 1:
                    SetVramCursor(ChainColX[1], y + 2);
                    v = ReadChainTranspose(wChain, y);
                    v = v + (wSongCol == 3 ? 4 : 12);
                    SetChainTranspose(wChain, y, v);
                    DrawHexByte(v);
                    wChainCellVal = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == 0x12) {
        cx = wChainCol;
        x1 = cx;
        cy = wChainRow;
        y1 = cy;
        if (wCursorMode == 4) {
            x = wSelectionLeft;
            y0 = wSelectionTop;
        } else {
            x = cx;
            y0 = cy;
        }
        SortBytePair(&x, &x1);
        SortBytePair(&y0, &y1);
        x1++;
        y1++;
        wChainScopeDirty = 1;
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        for (; x < x1; x++) {
            for (y = y0; y < y1; y++) {
                switch (x) {
                case 0:
                    v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y);
                    if (v) {
                        v = v - 1;
                        FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, v);
                        SetVramCursor(ChainColX[0], y + 2);
                        DrawHexByte(v);
                    }
                    break;
                case 1:
                    SetVramCursor(ChainColX[1], y + 2);
                    v = ReadChainTranspose(wChain, y) - 1;
                    SetChainTranspose(wChain, y, v);
                    DrawHexByte(v);
                    wChainCellVal = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == 0x18) {
        cx = wChainCol;
        x1 = cx;
        cy = wChainRow;
        y1 = cy;
        if (wCursorMode == 4) {
            x = wSelectionLeft;
            y0 = wSelectionTop;
        } else {
            x = cx;
            y0 = cy;
        }
        SortBytePair(&x, &x1);
        SortBytePair(&y0, &y1);
        x1++;
        y1++;
        wChainScopeDirty = 1;
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
        for (; x < x1; x++) {
            for (y = y0; y < y1; y++) {
                switch (x) {
                case 0:
                    SetVramCursor(ChainColX[0], y + 2);
                    v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y);
                    if (v == 0xFF)
                        v = wColPhrase[wSongCol];
                    else if (v < 0x10)
                        v = 0;
                    else
                        v = v - 0x10;
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, v);
                    DrawHexByte(v);
                    break;
                case 1:
                    SetVramCursor(ChainColX[1], y + 2);
                    v = ReadChainTranspose(wChain, y);
                    v = v - (wSongCol == 3 ? 4 : 12);
                    SetChainTranspose(wChain, y, v);
                    DrawHexByte(v);
                    wChainCellVal = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wBHeld && wButtons == 0x21) {
        wBHeld = 0;
        for (x = wSongCol + 1; x < 4; ++x)
            if (ChainScreenEnter(x))
                return 1;
        return 0;
    }
    if (wBHeld && wButtons == 0x22) {
        wBHeld = 0;
        for (x = wSongCol - 1; x < 4; --x)
            if (ChainScreenEnter(x))
                return 1;
        return 0;
    }
    if (wBHeld && wButtons == 0x24) {
        wBHeld = 0;
        wMessageStrPtr = 0;
        if (!wSongRow) {
            if (wAPressEnabled) {
                EraseChainCursor();
                wChainRow = 0;
                DrawChainScreen();
            }
            return 0;
        }
        wSongRow--;
        wChain = GetCursorChain();
        if (wChain & 0x80) {
            wSongRow++;
            wChain = GetCursorChain();
            if (wChain & 0x80)
                wChain = 0;
        }
        DrawChainScreen();
        return 1;
    }
    if (wBHeld && wButtons == 0x28) {
        wBHeld = 0;
        wMessageStrPtr = 0;
        if (wSongRow == 0xFF)
            return 0;
        wSongRow++;
        if ((wSongRow - wSongScroll) & 0xF0)
            wSongScroll = wSongRow & 0xF0;
        wChain = GetCursorChain();
        if (wChain & 0x80) {
            wSongRow--;
            wChain = GetCursorChain();
            if (wChain & 0x80)
                wChain = 0;
        }
        DrawChainScreen();
        return 1;
    }
    if (wButtons == 0x30) {
        if (!wAPressEnabled)
            return 0;
        wAPressEnabled = 0;
        wCursorMode = 2;
        wSelectionLeft = wChainCol;
        wSelectionRight = 2;
        wSelectionTop = wChainRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = 3;
        if (!wChainCol) {
            v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, (char)wChainRow);
            wColPhrase[wSongCol] = v;
            WriteClipboardCell(0, (char)wChainRow, v);
        }
        wChainCellVal = ReadChainTranspose(wChain, (char)wChainRow);
        WriteClipboardCell(1, (char)wChainRow, wChainCellVal);
        if (!wChainCol)
            FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, (char)wChainRow, 0xFF);
        else
            SetChainTranspose(wChain, (char)wChainRow, 0);
        DrawChainScreen();
        return 1;
    }
    if (wButtons == 0x50) {
        if (wCursorMode == 1)
            return 0;
        if (wClipboardType != 3)
            return 0;
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            if (wAPressEnabled || wSelectionLeft != wChainCol || wSelectionTop != wChainRow)
                CutCopyChainSelection(1);
            else
                OpenChainPhrase();
            return 0;
        }
        EraseChainCursor();
        PasteChainCells();
        DrawChainScreen();
        return 1;
    }
    if (wButtons == 0x70 && wCursorMode == 4) {
        OpenChainPhrase();
        return 1;
    }
    if (wButtons == 0xC0) {
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    }
    if (wButtons == 0x80) {
        if (wButtonsSame || wBHeld) {
            if (wMuteSoloLatch == 2)
                wMuteSoloLatch = 1;
            return 1;
        }
        if (!wPlayMode && FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, (char)wChainRow) == 0xFF
                && FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, 0) == 0xFF)
            return 0;
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    }
    if (wButtons == 0x41) {
        if (wButtonsSame)
            return 0;
        if (!LoadChainCursorPhrase()) {
            FarCall(1, FAR_FN(ShowTapAddPhrase));
            return 0;
        }
        FarCall(5, MoveMapCursorRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x42) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x44) {
        if (wButtonsSame)
            return 0;
        LoadChainCursorPhrase();
        FarCall(5, MoveMapCursorUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x48) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wCursorMode == 4 && wButtons == 0x20) {
        if (!wAPressEnabled)
            return 0;
        CutCopyChainSelection(0);
        return 1;
    }
    if (wButtons == 0x60) {
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == 4 && wClipboardType == 3) {
            left = wSelectionLeft;
            right = (char)wChainCol;
            y0 = wSelectionTop;
            y1 = (char)wChainRow;
            wQuickMarking = 1;
            SortBytePair(&y0, &y1);
            if (y0 || y1 != 0x0F) {
                wSelectionTop = 0;
                wChainRow = 0x0F;
                wSelectHeld = 0;
                DrawChainSelection();
                return 1;
            }
            SortBytePair(&left, &right);
            if (left || right != 1) {
                wSelectionLeft = 0;
                wChainCol = 1;
                wSelectHeld = 0;
                DrawChainSelection();
                return 1;
            }
            wSelectHeld = 0;
            CutCopyChainSelection(0);
            return 1;
        }
        FarCall(5, SaveSelectionState);
        wQuickMarking = 0;
        wSelectionLeft = wChainCol;
        wSelectionTop = wChainRow;
        wSelectHeld = 0;
        ShowMessage("SELECT");
        return 1;
    }
    return 0;
}

void CutCopyChainSelection(UBYTE cut)
{
    UBYTE x, y;

    if (wClipboardType != 3)
        return;
    wCursorMode = 2;
    wSelectionRight = wChainCol;
    wSelectionBottom = wChainRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    wSelectionRight++;
    wSelectionBottom++;
    for (x = wSelectionLeft; x < wSelectionRight; x++) {
        for (y = wSelectionTop; y < wSelectionBottom; y = y + 1) {
            wChainCol = x;
            wChainRow = y;
            EraseChainCursor();
            switch (x) {
            case 0:
                WriteClipboardCell(0, y, FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y));
                if (cut)
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, 0xFF);
                break;
            case 1:
                WriteClipboardCell(1, y, ReadChainTranspose(wChain, y));
                if (cut)
                    SetChainTranspose(wChain, y, 0);
                break;
            }
        }
    }
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    wChainCol = wSelectionLeft;
    wChainRow = wSelectionTop;
    ShowMessage(cut ? "CUT " : "COPY");
    DrawChainScreen();
}

void PasteChainCells(void)
{
    UBYTE x, y;

    for (y = wSelectionTop; y < wSelectionBottom; y++) {
        for (x = wSelectionLeft; x < wSelectionRight; x = x + 1) {
            switch (x) {
            case 0:
                FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, wChainRow, ReadClipboardCell(0, y));
                break;
            case 1:
                SetChainTranspose(wChain, wChainRow, ReadClipboardCell(1, y));
                break;
            }
        }
        wChainRow++;
        wChainRow = (char)wChainRow & 0x0F;     /* read into c, like OpenChainPhrase's (char)wChainRow */
    }
    if (wSelectionBottom - wSelectionTop > 1)
        ShowMessage("PASTE");
}

void DrawChainSelection(void)
{
    UBYTE x, y, left, top, right, bottom;
    UBYTE col;

    if (wClipboardType != 3)
        return;
    col = wChainCol;                /* into c for the test only, as DrawPhraseSelection */
    if (wSelectionLeft < col) {
        left = wSelectionLeft;
        right = wChainCol + 1;
    } else {
        right = wSelectionLeft + 1;
        left = wChainCol;
    }
    if (wSelectionTop < wChainRow) {
        top = wSelectionTop;
        bottom = wChainRow + 1;
    } else {
        bottom = wSelectionTop + 1;
        top = wChainRow;
    }
    /* both columns' cells around the rectangle back to their own palettes... */
    for (x = 0; x < 2; x++) {
        for (y = top ? top - 1 : 0; y < (bottom == 0x10 ? 0x10 : bottom + 1); y = y + 1) {
            if (x < left || x >= right || y < top || y >= bottom)
                ColorField(ChainColX[x], y + 2, 2, hGameboyType ? x : 0);
        }
    }
    /* ...and the rectangle highlighted */
    for (x = left; x < right; x++)
        for (y = top; y < bottom; y = y + 1)
            ColorField(ChainColX[x], y + 2, 2, hGameboyType ? 3 : 2);
}
