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

extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wChannelChain[];                  /* $C170, [4] */
extern UBYTE wChannelChainRow[];               /* $C17C, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainColumn;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE ChainColumnX[];                  /* the chain screen's two columns: x */
void DrawAllChannelStatus(void);               /* bank $1F */
extern char TxtClonedPhrase[];              /* $06:$5899 "CLONED PHRASE" */
extern char TxtDashes2[];                   /* $06:$5A5E " --" */
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
void DrawChainPosition(void);
void LoadChainRow(void);
void AllocateNextFreePhrase(void);             /* bank 1 */
void WriteChainPhrase(void);                /* bank $1F */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE source, UBYTE destination);
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
void SoundToggleSongPlayback(void);                 /* $02:$78C4 */
void SoundTogglePlayback(void);                    /* $02:$7C4C */
void SendLinkClipboard(void);                   /* bank $1C */

UBYTE GetCursorChain(void)
{
    return ReadSequenceByte(wSongColumn, wSongRow);
}

UBYTE LoadChainCursorPhrase(void)
{
    UBYTE row, phrase;

    row = wChainRow;
    for (;;) {
        phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, wChain, row);
        if (phrase != 0xFF) {
            wPhrase = phrase;
            wChainRow = row;
            return 1;
        }
        if (!row)
            return 0;
        row--;
    }
}

UBYTE ChainScreenEnter(UBYTE column)
{
    UBYTE chain, row;

    chain = wChain;
    row = wSongRow;
    if (column >= 4)
        DebugTrap();
    wMessageStringPointer = 0;
    if (wLiveMode && wChannelActive[column]) {
        wChain = wChannelChain[column];
        wSongRow = wChannelSongRow[column];
    } else {
        wChain = ReadSequenceByte(column, wSongRow);
        while (wChain == 0xFF && wSongRow)
            wChain = ReadSequenceByte(column, --wSongRow);
    }
    if (wChain & 0x80) {
        wChain = chain;
        wSongRow = row;
        return 0;
    }
    wSongColumn = column;
    DrawChainScreen();
    return 1;
}

void DrawChainPosition(void)
{
    UBYTE r;

    wVramCursor = (UBYTE *)BG_MAP(17, 2);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
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

void ClonePhraseAtCursor(UBYTE source, UBYTE destination)
{
    UBYTE *s1, *d1, *s2, *d2, *s3, *d3, *s4, *d4;
    UWORD sourceOffset, destinationOffset;
    UBYTE v;
    UBYTE row;

    sourceOffset = (UWORD)source << 4;
    destinationOffset = (UWORD)destination << 4;
    if (wCurrentScreen == SCREEN_CHAIN)
        ShowMessage(TxtClonedPhrase);
    if (source == 0xFF)
        DebugTrap();
    if (destination == 0xFF)
        DebugTrap();
    s1 = (UBYTE *)0xA000 + sourceOffset;
    s2 = SramPhraseInstruments + sourceOffset;
    s3 = (UBYTE *)0xA000 + sourceOffset;
    s4 = (UBYTE *)0xAFF0 + sourceOffset;
    d1 = (UBYTE *)0xA000 + destinationOffset;
    d2 = SramPhraseInstruments + destinationOffset;
    d3 = (UBYTE *)0xA000 + destinationOffset;
    d4 = (UBYTE *)0xAFF0 + destinationOffset;
    for (row = 0; row < 16; row++) {
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
    c = wChainColumn;
    ColorField(ChainColumnX[wChainColumn], wChainRow + 2, 2, c && hGameboyType);
}

void LoadChainRow(void)
{
    UBYTE *p, *q;

    p = SramChainPhrases + ((UWORD)wChain << 4);
    q = SramChainTransposes + ((UWORD)wChain << 4);
    ReadSongBytes(1, p, wSongReadBuffer);
    ReadSongBytes(1, q, wUsedTableMap);
}

void DrawChainScreen(void)
{
    UBYTE y;
    UBYTE row, v, palette, r;

    if (wChain & 0x80)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    DrawChainPosition();
    LoadChainRow();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(1, 2);
    for (row = 0;; row++) {
        v = wSongReadBuffer[row];
        if (v == 0xFF) {
            DrawString(TxtDashes2);
        } else {
            DrawStringTile20();
            DrawHexByte(v);
        }
        if (row == 15)
            break;
        wVramCursor += 0x1D;
    }
    wVramCursor = (UBYTE *)BG_MAP(5, 2);
    for (row = 0;; row++) {
        DrawHexByte(wUsedTableMap[row]);
        if (row == 15)
            break;
        wVramCursor += 0x1E;
    }
    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    palette = hGameboyType ? 3 : 2;
    y = wChainRow + 2;
    ColorField(ChainColumnX[wChainColumn], y, 2, palette);
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
}

void ChainScreenUpdate(void)
{
    if ((wPlayMode == 4 || wPlayMode == 3) && wChannelActive[wSongColumn]
            && wChain == wChannelChain[wSongColumn])
        SetSpriteY(0, wChannelChainRow[wSongColumn]);
    else
        ClearSpriteY(0);
}

void DrawChainCursor(void)
{
    ColorField(ChainColumnX[wChainColumn], wChainRow + 2, 2, hGameboyType ? 3 : 2);
}

void OpenChainPhrase(void)
{
    UBYTE phrase;
    UBYTE n;

    phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, wChain, (char)wChainRow);
    FarCall(5, 0x747D);                 /* RestoreSelectionState, bank 5, by address */
    if (phrase == 0xFF)
        return;
    wColumnPhrase[wSongColumn] = phrase;
    n = FarCallByte(1, FAR_FN(AllocateNextFreePhrase));
    if (n != 0xFF) {
        ClonePhraseAtCursor(phrase, n);
        FarCallArgument3(0x1F, (UWORD)WriteChainPhrase, wChain, (char)wChainRow, n);
        SetVramCursor(2, wChainRow + 2);
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        DrawHexByte(n);
    } else {
        FarCall(1, FAR_FN(ShowOutOfMemory));
    }
}

extern UBYTE wChainCellValue;               /* $C945 */
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
    UBYTE column, row, columnEnd, rowStart, rowEnd, v, left, right;
    UBYTE isA;
    char cursorColumn, cursorRow;

    if (wChain & 0x80)
        DebugTrap();
    isA = wButtons == J_A;
    if (!isA)
        wDrawStep = 0;
    if (wCursorMode == 4 && wButtons == J_SELECT_TAP) {
        wChainColumn = wSelectionLeft;
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
    hBgDigitPalette = 4;
    hBgPalette = 1;
    if (wAPressEnabled && isA) {
        if (wCursorMode == 4)
            return 0;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        SetVramCursor(ChainColumnX[wChainColumn], wChainRow + 2);
        switch (wChainColumn) {
        case 0:
            if (wDrawStep++ && wChainCellEmpty) {
                v = FarCallByte(1, FAR_FN(AllocateNextFreePhrase));
                wDrawStep = 0;
            } else {
                v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, wChainRow);
                wChainCellEmpty = v == 0xFF;
                if (wChainCellEmpty) {
                    v = wColumnPhrase[wSongColumn];
                    if (v == 0xFF) {
                        v = FarCallByte(1, FAR_FN(AllocateNextFreePhrase));
                        wChainCellEmpty = 0;
                    } else
                        FarCallArgument(1, FAR_FN(MarkPhraseUsed), v);
                }
            }
            if (v != 0xFF) {
                FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, (char)wChainRow, v);
                DrawHexByte(v);
                wColumnPhrase[wSongColumn] = v;
            }
            return 1;
        case 1:
            if (!ReadChainTranspose(wChain, (char)wChainRow)) {
                SetChainTranspose(wChain, (char)wChainRow, wChainCellValue);
                DrawHexByte(wChainCellValue);
                return 1;
            }
        }
    }
    if (wButtons == J_RIGHT) {
        if (wChainColumn == 1)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainColumn++;
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == J_LEFT) {
        if (!wChainColumn)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainColumn--;
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == J_UP) {
        if (!wChainRow)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainRow--;
        DrawChainPosition();
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == J_DOWN) {
        if (wChainRow == 0x0F)
            return 0;
        if (wCursorMode != 4)
            EraseChainCursor();
        wChainRow++;
        DrawChainPosition();
        if (wCursorMode == 4)
            DrawChainSelection();
        else
            DrawChainCursor();
        return 1;
    }
    if (wButtons == (J_A | J_RIGHT)) {
        cursorColumn = wChainColumn;
        columnEnd = cursorColumn;
        cursorRow = wChainRow;
        rowEnd = cursorRow;
        if (wCursorMode == 4) {
            column = wSelectionLeft;
            rowStart = wSelectionTop;
        } else {
            column = cursorColumn;
            rowStart = cursorRow;
        }
        SortBytePair(&column, &columnEnd);
        SortBytePair(&rowStart, &rowEnd);
        columnEnd++;
        rowEnd++;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        wChainScopeDirty = 1;
        for (; column < columnEnd; column++) {
            for (row = rowStart; row < rowEnd; row++) {
                switch (column) {
                case 0:
                    v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, row);
                    if (v != 0xFE) {
                        v = v + 1;
                        FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, row, v);
                        SetVramCursor(ChainColumnX[0], row + 2);
                        DrawHexByte(v);
                    }
                    break;
                case 1:
                    SetVramCursor(ChainColumnX[1], row + 2);
                    v = ReadChainTranspose(wChain, row) + 1;
                    SetChainTranspose(wChain, row, v);
                    DrawHexByte(v);
                    wChainCellValue = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == (J_A | J_UP)) {
        cursorColumn = wChainColumn;
        columnEnd = cursorColumn;
        cursorRow = wChainRow;
        rowEnd = cursorRow;
        if (wCursorMode == 4) {
            column = wSelectionLeft;
            rowStart = wSelectionTop;
        } else {
            column = cursorColumn;
            rowStart = cursorRow;
        }
        SortBytePair(&column, &columnEnd);
        SortBytePair(&rowStart, &rowEnd);
        columnEnd++;
        rowEnd++;
        wChainScopeDirty = 1;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        for (; column < columnEnd; column++) {
            for (row = rowStart; row < rowEnd; row++) {
                switch (column) {
                case 0:
                    v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, row);
                    if (v == 0xFF)
                        v = wColumnPhrase[wSongColumn];
                    else if (v >= 0xEF)
                        v = 0xFE;
                    else
                        v = v + 0x10;
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, row, v);
                    SetVramCursor(ChainColumnX[0], row + 2);
                    DrawHexByte(v);
                    break;
                case 1:
                    SetVramCursor(ChainColumnX[1], row + 2);
                    v = ReadChainTranspose(wChain, row);
                    v = v + (wSongColumn == CHANNEL_NOI ? 4 : 12);
                    SetChainTranspose(wChain, row, v);
                    DrawHexByte(v);
                    wChainCellValue = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == (J_A | J_LEFT)) {
        cursorColumn = wChainColumn;
        columnEnd = cursorColumn;
        cursorRow = wChainRow;
        rowEnd = cursorRow;
        if (wCursorMode == 4) {
            column = wSelectionLeft;
            rowStart = wSelectionTop;
        } else {
            column = cursorColumn;
            rowStart = cursorRow;
        }
        SortBytePair(&column, &columnEnd);
        SortBytePair(&rowStart, &rowEnd);
        columnEnd++;
        rowEnd++;
        wChainScopeDirty = 1;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        for (; column < columnEnd; column++) {
            for (row = rowStart; row < rowEnd; row++) {
                switch (column) {
                case 0:
                    v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, row);
                    if (v) {
                        v = v - 1;
                        FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, row, v);
                        SetVramCursor(ChainColumnX[0], row + 2);
                        DrawHexByte(v);
                    }
                    break;
                case 1:
                    SetVramCursor(ChainColumnX[1], row + 2);
                    v = ReadChainTranspose(wChain, row) - 1;
                    SetChainTranspose(wChain, row, v);
                    DrawHexByte(v);
                    wChainCellValue = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wButtons == (J_A | J_DOWN)) {
        cursorColumn = wChainColumn;
        columnEnd = cursorColumn;
        cursorRow = wChainRow;
        rowEnd = cursorRow;
        if (wCursorMode == 4) {
            column = wSelectionLeft;
            rowStart = wSelectionTop;
        } else {
            column = cursorColumn;
            rowStart = cursorRow;
        }
        SortBytePair(&column, &columnEnd);
        SortBytePair(&rowStart, &rowEnd);
        columnEnd++;
        rowEnd++;
        wChainScopeDirty = 1;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        for (; column < columnEnd; column++) {
            for (row = rowStart; row < rowEnd; row++) {
                switch (column) {
                case 0:
                    SetVramCursor(ChainColumnX[0], row + 2);
                    v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, row);
                    if (v == 0xFF)
                        v = wColumnPhrase[wSongColumn];
                    else if (v < 0x10)
                        v = 0;
                    else
                        v = v - 0x10;
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, row, v);
                    DrawHexByte(v);
                    break;
                case 1:
                    SetVramCursor(ChainColumnX[1], row + 2);
                    v = ReadChainTranspose(wChain, row);
                    v = v - (wSongColumn == CHANNEL_NOI ? 4 : 12);
                    SetChainTranspose(wChain, row, v);
                    DrawHexByte(v);
                    wChainCellValue = v;
                    break;
                }
            }
        }
        return 1;
    }
    if (wBHeld && wButtons == (J_B | J_RIGHT)) {
        wBHeld = 0;
        for (column = wSongColumn + 1; column < 4; ++column)
            if (ChainScreenEnter(column))
                return 1;
        return 0;
    }
    if (wBHeld && wButtons == (J_B | J_LEFT)) {
        wBHeld = 0;
        for (column = wSongColumn - 1; column < 4; --column)
            if (ChainScreenEnter(column))
                return 1;
        return 0;
    }
    if (wBHeld && wButtons == (J_B | J_UP)) {
        wBHeld = 0;
        wMessageStringPointer = 0;
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
    if (wBHeld && wButtons == (J_B | J_DOWN)) {
        wBHeld = 0;
        wMessageStringPointer = 0;
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
    if (wButtons == (J_B | J_A)) {
        if (!wAPressEnabled)
            return 0;
        wAPressEnabled = 0;
        wCursorMode = 2;
        wSelectionLeft = wChainColumn;
        wSelectionRight = 2;
        wSelectionTop = wChainRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = SCREEN_CHAIN;
        if (!wChainColumn) {
            v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, (char)wChainRow);
            wColumnPhrase[wSongColumn] = v;
            WriteClipboardCell(0, (char)wChainRow, v);
        }
        wChainCellValue = ReadChainTranspose(wChain, (char)wChainRow);
        WriteClipboardCell(1, (char)wChainRow, wChainCellValue);
        if (!wChainColumn)
            FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, (char)wChainRow, 0xFF);
        else
            SetChainTranspose(wChain, (char)wChainRow, 0);
        DrawChainScreen();
        return 1;
    }
    if (wButtons == (J_SELECT | J_A)) {
        if (wCursorMode == 1)
            return 0;
        if (wClipboardType != SCREEN_CHAIN)
            return 0;
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            if (wAPressEnabled || wSelectionLeft != wChainColumn || wSelectionTop != wChainRow)
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
    if (wButtons == (J_SELECT | J_B | J_A) && wCursorMode == 4) {
        OpenChainPhrase();
        return 1;
    }
    if (wButtons == (J_SELECT | J_START)) {
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    }
    if (wButtons == J_START) {
        if (wButtonsSame || wBHeld) {
            if (wMuteSoloLatch == 2)
                wMuteSoloLatch = 1;
            return 1;
        }
        if (!wPlayMode && FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, (char)wChainRow) == 0xFF
                && FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, 0) == 0xFF)
            return 0;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    }
    if (wButtons == (J_SELECT | J_RIGHT)) {
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
    if (wButtons == (J_SELECT | J_LEFT)) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == (J_SELECT | J_UP)) {
        if (wButtonsSame)
            return 0;
        LoadChainCursorPhrase();
        FarCall(5, MoveMapCursorUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == (J_SELECT | J_DOWN)) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wCursorMode == 4 && wButtons == J_B) {
        if (!wAPressEnabled)
            return 0;
        CutCopyChainSelection(0);
        return 1;
    }
    if (wButtons == (J_SELECT | J_B)) {
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == 4 && wClipboardType == SCREEN_CHAIN) {
            left = wSelectionLeft;
            right = (char)wChainColumn;
            rowStart = wSelectionTop;
            rowEnd = (char)wChainRow;
            wQuickMarking = 1;
            SortBytePair(&rowStart, &rowEnd);
            if (rowStart || rowEnd != 0x0F) {
                wSelectionTop = 0;
                wChainRow = 0x0F;
                wSelectHeld = 0;
                DrawChainSelection();
                return 1;
            }
            SortBytePair(&left, &right);
            if (left || right != 1) {
                wSelectionLeft = 0;
                wChainColumn = 1;
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
        wSelectionLeft = wChainColumn;
        wSelectionTop = wChainRow;
        wSelectHeld = 0;
        ShowMessage("SELECT");
        return 1;
    }
    return 0;
}

void CutCopyChainSelection(UBYTE cut)
{
    UBYTE column, row;

    if (wClipboardType != SCREEN_CHAIN)
        return;
    wCursorMode = 2;
    wSelectionRight = wChainColumn;
    wSelectionBottom = wChainRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    wSelectionRight++;
    wSelectionBottom++;
    for (column = wSelectionLeft; column < wSelectionRight; column++) {
        for (row = wSelectionTop; row < wSelectionBottom; row = row + 1) {
            wChainColumn = column;
            wChainRow = row;
            EraseChainCursor();
            switch (column) {
            case 0:
                WriteClipboardCell(0, row, FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, row));
                if (cut)
                    FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, row, 0xFF);
                break;
            case 1:
                WriteClipboardCell(1, row, ReadChainTranspose(wChain, row));
                if (cut)
                    SetChainTranspose(wChain, row, 0);
                break;
            }
        }
    }
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    wChainColumn = wSelectionLeft;
    wChainRow = wSelectionTop;
    ShowMessage(cut ? "CUT " : "COPY");
    DrawChainScreen();
}

void PasteChainCells(void)
{
    UBYTE column, row;

    for (row = wSelectionTop; row < wSelectionBottom; row++) {
        for (column = wSelectionLeft; column < wSelectionRight; column = column + 1) {
            switch (column) {
            case 0:
                FarCallArgument3P(0x1F, FAR_FN(WriteChainPhrase), wChain, wChainRow, ReadClipboardCell(0, row));
                break;
            case 1:
                SetChainTranspose(wChain, wChainRow, ReadClipboardCell(1, row));
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
    UBYTE column, row, left, top, right, bottom;
    UBYTE cursorColumn;

    if (wClipboardType != SCREEN_CHAIN)
        return;
    cursorColumn = wChainColumn;                /* into c for the test only, as DrawPhraseSelection */
    if (wSelectionLeft < cursorColumn) {
        left = wSelectionLeft;
        right = wChainColumn + 1;
    } else {
        right = wSelectionLeft + 1;
        left = wChainColumn;
    }
    if (wSelectionTop < wChainRow) {
        top = wSelectionTop;
        bottom = wChainRow + 1;
    } else {
        bottom = wSelectionTop + 1;
        top = wChainRow;
    }
    /* both columns' cells around the rectangle back to their own palettes... */
    for (column = 0; column < 2; column++) {
        for (row = top ? top - 1 : 0; row < (bottom == 0x10 ? 0x10 : bottom + 1); row = row + 1) {
            if (column < left || column >= right || row < top || row >= bottom)
                ColorField(ChainColumnX[column], row + 2, 2, hGameboyType ? column : 0);
        }
    }
    /* ...and the rectangle highlighted */
    for (column = left; column < right; column++)
        for (row = top; row < bottom; row = row + 1)
            ColorField(ChainColumnX[column], row + 2, 2, hGameboyType ? 3 : 2);
}
