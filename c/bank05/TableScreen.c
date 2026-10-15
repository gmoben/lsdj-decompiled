/*
 * The table screen and the screen map -- LSDj 9.4.0, ROM bank $05.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void DrawTableSelection(void);

extern volatile sfr UBYTE SCX_REG;          /* $FF43 */
extern volatile sfr UBYTE SCY_REG;          /* $FF42 */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
void SendLinkClipboard(void);                   /* bank $1C */
extern UBYTE wTable;                        /* $C41B, the table being edited */
extern UWORD wTableCellAPtr;                /* $C447: CMD column base, an address held as an integer */
extern UWORD wTableCellBPtr;                /* $C449: its value column */
extern UBYTE wMapCol;                       /* $C8FF, the screen map cursor */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjCol;                /* $C901 */
extern UBYTE wMapSynthCol;                  /* $C902 */
extern UBYTE wMapMoveDir;                   /* $C903 */
extern UBYTE wSynthCursor;                  /* $C442 */
extern char wTableCol;                      /* $C92F, a plain char (DrawTableCursor's signed switch) */
extern UBYTE wTableRow;                     /* $C930 */
extern UBYTE TableColX[];                /* $05:$42D5..: the six table columns' x, */
extern UBYTE TableColWidth[];            /* width */
extern UBYTE TableColPal[];             /* and normal palette */
extern UBYTE wSavedCursorMode;              /* $CBD2..$CBD7: SaveSelectionState */
extern UBYTE wSavedClipType;
extern UBYTE wSavedSelectionLeft;
extern UBYTE wSavedSelectionTop;
extern UBYTE wSavedSelectionRight;
extern UBYTE wSavedSelectionBottom;

void UpdateMapTopRow(void);
void GotoMapCursorScreen(void);
void StartMapTransition(UBYTE dir);
void CancelSelectionMode(void);

void DrawAllChanStatus(void);               /* bank $1F */
extern UBYTE wChanInstr[];                  /* $C210, [4] */
extern UBYTE wChanPitchSlots[];             /* $C250 */
extern UBYTE wChanStepCountdown[];          /* $C0C4, [4] */
extern UBYTE wChanTable[];                  /* $C204, [4] */
extern UBYTE wChanTableRow1[];              /* $C208, [4] (+4: wChanTableRow2) */
extern UBYTE wChanTableStep[];              /* $C33F, [4] */
extern UBYTE wLiveMode;                     /* $C528 */
extern UWORD wPhraseNotePtr[];              /* $C0BC, [4] */
extern UBYTE wRowBufCol2[];                 /* $C48D, [16] */
extern UBYTE wRowBufCol3[];                 /* $C49D, [16] */
extern UBYTE wRowBufCol4[];                 /* $C4AD, [16] */
extern UBYTE wRowBufCol5[];                 /* $C4BD, [16] */

void DrawFieldVal(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */

void UpdateMapTopRow(void)
{
    if (wMapRow != 1)
        return;
    if (wMapCol) {
        if (wMapCol > 2) {
            wMapProjCol = 2;
            wMapSynthCol = 3;
        } else {
            wMapProjCol = 1;
            wMapSynthCol = 2;
        }
    } else {
        wMapProjCol = 0;
        wMapSynthCol = 2;
    }
}

void DrawMapTopRow(void)
{
    UBYTE x;

    hBgDigitPal = 4;
    hBgPal = 1;
    SetVramCursor(wMapProjCol + 15, 15);
    DrawStringIndexed('P');
    SetVramCursor(wMapSynthCol + 15, 15);
    DrawString("YW");
    x = wMapCol + 15;
    SetVramCursor(x, 17);
    DrawStringIndexed('G');
    wVramCursor = (UBYTE *)0x9A0F;
    DrawStringIndexed(wLiveMode ? 'L' : 'S');
    DrawString("CPI");
    DrawStringIndexed(wInstr == 0x40 ? 'W' : 'T');
    ColorField(15, 16, 5, 1);
    ColorField(x, wMapRow + 15, 1, 2);
    ColorField(x, (wMapRow + 1) % 3 + 15, 1, 1);
    ColorField(x, (wMapRow + 2) % 3 + 15, 1, 1);
}

void GotoMapCursorScreen(void)
{
    switch (wMapRow) {
    case 0:
        if (wMapCol == wMapProjCol)
            wCurScreen = 9;
        else if (wMapCol == wMapSynthCol)
            wCurScreen = 10;
        else
            wCurScreen = 8;
        break;
    case 1:
        switch (wMapCol) {
        case 0:
            wCurScreen = 4;
            break;
        case 1:
            wCurScreen = 3;
            break;
        case 2:
            wCurScreen = 1;
            break;
        case 3:
            wCurScreen = 6;
            break;
        case 4:
            wCurScreen = wInstr == 0x40 ? 12 : 5;
            break;
        }
        break;
    case 2:
        wCurScreen = 2;
        break;
    }
}

void StartMapTransition(UBYTE dir)
{
    wMapMoveDir = dir;
    SCX_REG = 0;
    SCY_REG = 0;
}

void CancelSelectionMode(void)
{
    if (wCursorMode == 4)
        FarCall(5, RestoreSelectionState);
}

void MoveMapCursorLeft(void)
{
    CancelSelectionMode();
    wMapCol--;
    if (wMapCol == 0xFF)
        DebugTrap();
    GotoMapCursorScreen();
    StartMapTransition(4);
    UpdateMapTopRow();
}

void MoveMapCursorRight(void)
{
    CancelSelectionMode();
    wMapCol++;
    GotoMapCursorScreen();
    StartMapTransition(2);
    UpdateMapTopRow();
}

void MoveMapCursorUp(void)
{
    CancelSelectionMode();
    wMapRow--;
    if (wMapRow == 0xFF)
        DebugTrap();
    GotoMapCursorScreen();
    StartMapTransition(1);
}

void MoveMapCursorDown(void)
{
    CancelSelectionMode();
    wMapRow++;
    if (wMapRow == 3)
        DebugTrap();
    GotoMapCursorScreen();
    StartMapTransition(3);
}

void DrawCmdLetter(UBYTE cmd)
{
    UBYTE c, ok;

    ok = cmd <= 0x13;
    if (ok)
        c = FxMnemonics[cmd];
    else
        c = '?';
    DrawStringIndexed(c);
}

UBYTE ReadTableCmd(UBYTE step)
{
    UBYTE *p;
    UBYTE v, ok;

    p = (UBYTE *)wTableCellAPtr;
    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p += (UWORD)wTable << 4;
    v = ReadSongByte(1, p + step);
    ok = v <= 0x13;
    if (!ok)
        v = 0;
    return v;
}

void WriteTableEnv(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    WriteSongByte(0, p + step, v);
}

void WriteTableCmd(UBYTE step, UBYTE cmd)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellAPtr;
    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    if (cmd > 0x13)
        DebugTrap();
    p += (UWORD)wTable << 4;
    WriteSongByte(1, p + step, cmd);
}

void WriteTableVal(UBYTE step, UBYTE v)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellBPtr;
    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p += (UWORD)wTable << 4;
    WriteSongByte(1, p + step, v);
}

void WriteTableTranspose(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableTransposes + ((UWORD)wTable << 4);
    WriteSongByte(1, p + step, v);
}

UBYTE ReadTableEnv(UBYTE step)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    return ReadSongByte(0, p + step);
}

UBYTE ReadTableVal(UBYTE step)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellBPtr;
    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p += (UWORD)wTable << 4;
    return ReadSongByte(1, p + step);
}

UBYTE ReadTableTranspose(UBYTE step)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableTransposes + ((UWORD)wTable << 4);
    return ReadSongByte(1, p + step);
}

void EraseTableCursor(void)
{
    UBYTE y, end, x;

    y = wTableRow + 2;
    if (wCursorMode == 4)
        return;
    end = TableColX[wTableCol] + TableColWidth[wTableCol];
    for (x = TableColX[wTableCol]; x < end; x++)
        ColorField(x, y, 1, TableColPal[wTableCol]);
}

void ReadTableStep(void)
{
    UWORD t;
    UBYTE *p0, *p1, *p2, *p3, *p4;

    t = wTable & 0x1F;
    t <<= 4;
    p0 = (UBYTE *)0xB690 + t;
    p1 = (UBYTE *)0xB480 + t;
    p2 = (UBYTE *)0xB680 + t;
    p3 = (UBYTE *)0xBA80 + t;
    p4 = (UBYTE *)0xB880 + t;
    t += (UWORD)0xBC80;
    ReadSongBytes(0, p0, wSongReadBuf);
    ReadSongBytes(1, p1, (UBYTE *)0xC47D);
    ReadSongBytes(1, p2, (UBYTE *)0xC48D);
    ReadSongBytes(1, p4, (UBYTE *)0xC49D);
    ReadSongBytes(1, p3, (UBYTE *)0xC4AD);
    ReadSongBytes(1, (UBYTE *)t, (UBYTE *)0xC4BD);
}

void DrawTableEnv(UBYTE v)
{
    if ((v & 0x0F) == 0x0F) {
        DrawByte(v >> 4);
        DrawStringIndexed('H');
    } else {
        DrawHexByte(v);
    }
}

void DrawTableScreen(void)
{
    UBYTE i;
    UBYTE a1;
    UBYTE w;
    UBYTE a2;
    UBYTE x, pal;

    if (wTable >= 0x20)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteX(1, 4);
    SetSpriteX(2, 10);
    SetSpriteTile3(0);
    SetSpriteTile3(1);
    SetSpriteTile3(2);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    ReadTableStep();
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x9842;
    for (i = 0; i < 16; i++) {
        DrawTableEnv(wSongReadBuf[i]);
        wVramCursor++;
        DrawHexByte(wUsedTableMap[i]);
        wVramCursor += 0x1B;
    }
    wVramCursor = (UBYTE *)0x9847;
    for (i = 0; i < 16; i++) {
        a2 = wRowBufCol4[i];
        a1 = wRowBufCol2[i];
        hBgDigitPal = wShadedDigitPal;
        hBgPal = wShadedPal;
        DrawCmdLetter(a1);
        wVramCursor += 3;
        DrawCmdLetter(a2);
        hBgDigitPal = 4;
        hBgPal = 1;
        wVramCursor -= 4;
        DrawFieldVal(a1, wRowBufCol3[i]);
        wVramCursor += 2;
        DrawFieldVal(a2, wRowBufCol5[i]);
        wVramCursor += 0x19;
    }
    a1 = wTableRow + 2;
    i = TableColX[wTableCol];
    w = TableColWidth[wTableCol];
    pal = hGameboyType ? 3 : 2;
    ColorField(i, a1, w, pal);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    if (wCursorMode == 4)
        DrawTableSelection();
}

UBYTE TableInputFlag(UBYTE x)
{
    switch (x) {
    case 4:
        return 1;
    case 10:
        return wTableCol != 2;
    }
    return 0;
}

void TableScreenUpdate(void)
{
    UBYTE r0, r1, r2;
    UWORD p;
    UBYTE v;

    r0 = 0xFF;
    if (!wPlayMode || wTable != wChanTable[wSongCol]) {
        ClearSpriteY(0);
        ClearSpriteY(1);
        ClearSpriteY(2);
        return;
    }
    switch (wChanStepCountdown[wSongCol]) {
    case 0:
        r0 = (UBYTE)wPhraseNotePtr[wSongCol] & 0x0F;
        break;
    default:
        p = wPhraseNotePtr[wSongCol];
        p = p - 1;
        r0 = (UBYTE)p & 0x0F;
        break;
    case 0xFF:
        break;
    }
    if (wChanTableStep[wSongCol] == 0xFF) {
        v = wChanInstr[wSongCol];
        r1 = wChanPitchSlots[v];
        r2 = wChanPitchSlots[v + 0x40];
    } else {
        r1 = wChanTableRow1[wSongCol];
        r2 = wChanTableRow1[wSongCol + 4];
    }
    SetSpriteY(0, r0);
    SetSpriteY(1, r1);
    SetSpriteY(2, r2);
}

void DrawTableCursor(void)
{
    if (wCursorMode == 4)
        DrawTableSelection();
    else
        ColorField(TableColX[wTableCol], wTableRow + 2, TableColWidth[wTableCol], hGameboyType ? 3 : 2);
    switch (wTableCol) {
    case 2:
    case 3:
        wTableCellAPtr = 0xB680;
        wTableCellBPtr = 0xB880;
        break;
    case 4:
    case 5:
        wTableCellAPtr = 0xBA80;
        wTableCellBPtr = 0xBC80;
        break;
    }
}

extern UBYTE wTableEnvCache;                /* $C931 */
extern UBYTE wTableTransposeCache;              /* $C932 */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wPendingCmds[];                /* $C457, [16] */
extern UBYTE wPendingCmdTarget;             /* $C467 */
extern UBYTE wColLastCmd[];                 /* $C44B, [4] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
void ShowCmdHelp(void);                 /* bank 1 */
void SoundToggleSongPlayback(void);              /* bank 2 */
void SoundTogglePlayback(void);                  /* bank 2 */
void CutCopyTableSelection(UBYTE cut);
void PasteTableCells(void);
void SaveSelectionState(void);

/* $4B5D: the table screen's input, a switch on wButtons: A writes the
   envelope or transpose kept from the last edit, the column's command (the
   last one used, its help on a second press) or, on the value of an A
   command pressed twice, a free table; the directions move the cursor;
   A+RIGHT/LEFT and A+UP/DOWN step the cell by 1 and 16 (an octave, 4 on
   the noise channel, for transposes; command letters skip those tables do
   not take) over the cursor row or the selection; B+directions change
   table; A+B cuts the cell; select+A pastes (in a selection: cuts); start
   and select+start play; select+UP/LEFT/DOWN move on the screen map, DOWN
   opening the groove a G command names; B copies a selection; select+B
   selects, then widens to the column and the screen. 1 when taken */

UBYTE TableScreenInput(void)
{
    UBYTE y, y1, v;

    if (wTable >= 0x20)
        DebugTrap();
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    if (wButtons != 0x10)
        wDrawStep = 0;
    switch (wButtons) {
    case 0x43:
        if (wCursorMode == 4) {
            wTableCol = wSelectionLeft;
            wTableRow = wSelectionTop;
            FarCall(5, RestoreSelectionState);
            if (hGameboyType) {
                FarCall(1, FAR_FN(DrawScreenPalMap));
                FarCall(5, DrawMapTopRow);
            }
            DrawTableScreen();
            DrawTableCursor();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case 0x10:
        if (!wAPressEnabled || wCursorMode == 4)
            break;
        SetVramCursor(TableColX[wTableCol], wTableRow + 2);
        if (wTableCol > 3) {
            wTableCellAPtr = 0xBA80;
            wTableCellBPtr = 0xBC80;
        } else {
            wTableCellAPtr = 0xB680;
            wTableCellBPtr = 0xB880;
        }
        switch (wTableCol) {
        case 0:
            if (!ReadTableEnv((char)wTableRow)) {
                DrawTableEnv(wTableEnvCache);
                WriteTableEnv((char)wTableRow, wTableEnvCache);
            }
            break;
        case 1:
            if (!ReadTableTranspose((char)wTableRow)) {
                DrawHexByte(wTableTransposeCache);
                WriteTableTranspose((char)wTableRow, wTableTransposeCache);
            }
            break;
        case 2:
        case 4:
            wPendingCmdTarget = (wTableCol == 4 ? 1 : 0) + 1;
            v = ReadTableCmd((char)wTableRow);
            if (!v) {
                v = wColLastCmd[wSongCol];
                if (!v)
                    DebugTrap();
            } else if (wDrawStep++)
                FarCallArg(1, FAR_FN(ShowCmdHelp), v);
            if (v == 0x0A && wTableCol == 4)
                v = 1;
            wPendingCmds[wTableRow] = v;
            DrawCmdLetter(v);
            hBgDigitPal = 4;
            hBgPal = 1;
            DrawFieldVal(v, ReadTableVal((char)wTableRow));
            break;
        case 3:
        case 5:
            if (wDrawStep++ == 1) {
                v = ReadTableCmd((char)wTableRow);
                if (v == 1) {
                    v = FarCallByte(0x1F, FAR_FN(FindFreeTable));
                    if (v != 0xFF) {
                        WriteTableVal((char)wTableRow, v);
                        DrawHexByte(v);
                    }
                }
            }
            break;
        }
        return 1;
    case 0x01:
        EraseTableCursor();
        if (++wTableCol == 6)
            wTableCol--;
        DrawTableCursor();
        return 1;
    case 0x02:
        EraseTableCursor();
        if (!wTableCol)
            wTableCol++;
        wTableCol--;
        DrawTableCursor();
        return 1;
    case 0x04:
        EraseTableCursor();
        if ((char)--wTableRow == 0xFF)
            wTableRow++;
        DrawTableCursor();
        return 1;
    case 0x08:
        EraseTableCursor();
        if (++wTableRow == 0x10)
            wTableRow = 0x0F;
        DrawTableCursor();
        return 1;
    case 0x11:
        if (wCursorMode == 4) {
            y = wSelectionTop;
            y1 = (char)wTableRow;
            SortBytePair(&y, &y1);
        } else {
            y1 = (char)wTableRow;
            y = (char)wTableRow;
        }
        y1++;
        for (; y < y1; y++) {
            switch (wTableCol) {
            case 2:
            case 3:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                break;
            case 4:
            case 5:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                break;
            }
            SetVramCursor(TableColX[wTableCol], y + 2);
            switch (wTableCol) {
            case 0:
                v = ReadTableEnv(y);
                if ((v & 0x0F) != 0x0F) {
                    v++;
                    DrawTableEnv(v);
                    WriteTableEnv(y, v);
                }
                wTableEnvCache = v;
                break;
            case 1:
                v = ReadTableTranspose(y);
                v++;
                DrawHexByte(v);
                WriteTableTranspose(y, v);
                wTableTransposeCache = v;
                break;
            case 2:
            case 4:
                if (!wPendingCmds[y])
                    if (!(wPendingCmds[y] = ReadTableCmd(y)))
                        break;
                wPendingCmdTarget = (wTableCol == 4 ? 1 : 0) + 1;
                if (wPendingCmds[y] < 0x13)
                    do
                        wPendingCmds[y]++;
                    while (TableInputFlag(wPendingCmds[y]));
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawCmdLetter(wPendingCmds[y]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(ShowCmdHelp), wPendingCmds[y]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[y], ReadTableVal(y));
                break;
            case 3:
            case 5:
                v = ReadTableVal(y);
                switch (ReadTableCmd(y)) {
                case 0:
                    break;
                case 1:
                    v++;
                    if (v > 0x20)
                        v = 0x20;
                    break;
                case 7:
                    v++;
                    if (v > 0x1F)
                        v = 0x1F;
                    break;
                case 0x0C:
                    if (v == 1)
                        v = 3;
                    else
                        v = 2;
                    break;
                case 0x12:
                    if (v != 3 || wSongCol == 2)
                        v++;
                    break;
                default:
                    v++;
                }
                WriteTableVal(y, v);
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawFieldVal(ReadTableCmd(y), v);
                break;
            }
        }
        return 1;
    case 0x12:
        if (wCursorMode == 4) {
            y = wSelectionTop;
            y1 = (char)wTableRow;
            SortBytePair(&y, &y1);
        } else {
            y1 = (char)wTableRow;
            y = (char)wTableRow;
        }
        y1++;
        for (; y < y1; y++) {
            switch (wTableCol) {
            case 2:
            case 3:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                break;
            case 4:
            case 5:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                break;
            }
            SetVramCursor(TableColX[wTableCol], y + 2);
            switch (wTableCol) {
            case 0:
                v = ReadTableEnv(y);
                if (v & 0x0F) {
                    v--;
                    DrawTableEnv(v);
                    WriteTableEnv(y, v);
                }
                wTableEnvCache = v;
                break;
            case 1:
                v = ReadTableTranspose(y);
                v--;
                DrawHexByte(v);
                WriteTableTranspose(y, v);
                wTableTransposeCache = v;
                break;
            case 2:
            case 4:
                wPendingCmdTarget = (wTableCol == 4 ? 1 : 0) + 1;
                if (!wPendingCmds[y])
                    wPendingCmds[y] = ReadTableCmd(y);
                if (wPendingCmds[y] > 1)
                    do
                        wPendingCmds[y]--;
                    while (TableInputFlag(wPendingCmds[y]));
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawCmdLetter(wPendingCmds[y]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(ShowCmdHelp), wPendingCmds[y]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[y], ReadTableVal(y));
                break;
            case 3:
            case 5:
                v = ReadTableVal(y);
                switch (ReadTableCmd(y)) {
                case 1:
                case 7:
                    v--;
                    if (v > 0x1F)
                        v = 0;
                    break;
                case 0x0C:
                    if (v == 2)
                        v = 3;
                    else
                        v = 1;
                    break;
                case 0x12:
                    if (v || wSongCol == 2)
                        v--;
                    break;
                default:
                    v--;
                }
                WriteTableVal(y, v);
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawFieldVal(ReadTableCmd(y), v);
                break;
            }
        }
        return 1;
    case 0x14:
        if (wCursorMode == 4) {
            y = wSelectionTop;
            y1 = (char)wTableRow;
            SortBytePair(&y, &y1);
        } else {
            y1 = (char)wTableRow;
            y = (char)wTableRow;
        }
        y1++;
        for (; y < y1; y++) {
            switch (wTableCol) {
            case 2:
            case 3:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                break;
            case 4:
            case 5:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                break;
            }
            SetVramCursor(TableColX[wTableCol], y + 2);
            switch (wTableCol) {
            case 0:
                v = ReadTableEnv(y);
                v += 0x10;
                DrawTableEnv(v);
                WriteTableEnv(y, v);
                wTableEnvCache = v;
                break;
            case 1:
                v = ReadTableTranspose(y);
                v += wSongCol == 3 ? 4 : 12;
                DrawHexByte(v);
                WriteTableTranspose(y, v);
                wTableTransposeCache = v;
                break;
            case 2:
            case 4:
                if (!wPendingCmds[y])
                    if (!(wPendingCmds[y] = ReadTableCmd(y)))
                        break;
                wPendingCmdTarget = (wTableCol == 4 ? 1 : 0) + 1;
                if (wPendingCmds[y] < 0x13)
                    do
                        wPendingCmds[y]++;
                    while (TableInputFlag(wPendingCmds[y]));
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawCmdLetter(wPendingCmds[y]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(ShowCmdHelp), wPendingCmds[y]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[y], ReadTableVal(y));
                break;
            case 3:
            case 5:
                v = ReadTableVal(y);
                switch (ReadTableCmd(y)) {
                case 0:
                    break;
                case 1:
                    v += 0x10;
                    if (v > 0x20)
                        v = 0x20;
                    break;
                case 7:
                    v += 0x10;
                    if (v > 0x1F)
                        v = 0x1F;
                    break;
                case 0x0C:
                    v = 3;
                    break;
                case 0x12:
                    if (wSongCol == 2)
                        v += 0x10;
                    else if (v != 3)
                        v++;
                    break;
                case 0x0F:
                    v += wSongCol == 3 ? 4 : 0x10;
                    break;
                default:
                    v += 0x10;
                }
                WriteTableVal(y, v);
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawFieldVal(ReadTableCmd(y), v);
                break;
            }
        }
        return 1;
    case 0x18:
        if (wCursorMode == 4) {
            y = wSelectionTop;
            y1 = (char)wTableRow;
            SortBytePair(&y, &y1);
        } else {
            y1 = (char)wTableRow;
            y = (char)wTableRow;
        }
        y1++;
        for (; y < y1; y++) {
            switch (wTableCol) {
            case 2:
            case 3:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                break;
            case 4:
            case 5:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                break;
            }
            SetVramCursor(TableColX[wTableCol], y + 2);
            switch (wTableCol) {
            case 0:
                v = ReadTableEnv(y);
                v -= 0x10;
                DrawTableEnv(v);
                WriteTableEnv(y, v);
                wTableEnvCache = v;
                break;
            case 1:
                v = ReadTableTranspose(y);
                v -= wSongCol == 3 ? 4 : 12;
                DrawHexByte(v);
                WriteTableTranspose(y, v);
                wTableTransposeCache = v;
                break;
            case 2:
            case 4:
                wPendingCmdTarget = (wTableCol == 4 ? 1 : 0) + 1;
                if (!wPendingCmds[y])
                    wPendingCmds[y] = ReadTableCmd(y);
                if (wPendingCmds[y] > 1)
                    do
                        wPendingCmds[y]--;
                    while (TableInputFlag(wPendingCmds[y]));
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawCmdLetter(wPendingCmds[y]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(ShowCmdHelp), wPendingCmds[y]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[y], ReadTableVal(y));
                break;
            case 3:
            case 5:
                v = ReadTableVal(y);
                switch (ReadTableCmd(y)) {
                case 0:
                    break;
                case 1:
                case 7:
                    v -= 0x10;
                    if (v > 0x1F)
                        v = 0;
                    break;
                case 0x0C:
                    v = 0;
                    break;
                case 0x12:
                    if (wSongCol == 2)
                        v -= 0x10;
                    else if (v)
                        v--;
                    break;
                case 0x0F:
                    v -= wSongCol == 3 ? 4 : 0x10;
                    break;
                default:
                    v -= 0x10;
                }
                WriteTableVal(y, v);
                hBgDigitPal = wHighlightDigitPal;
                hBgPal = wHighlightPal;
                DrawFieldVal(ReadTableCmd(y), v);
                break;
            }
        }
        return 1;
    case 0x21:
        if (wTable < 0x1F)
            wTable++;
        wMessageStrPtr = 0;
        DrawTableScreen();
        return 1;
    case 0x22:
        if (wTable)
            wTable--;
        wMessageStrPtr = 0;
        DrawTableScreen();
        return 1;
    case 0x24:
        if (wTable < 0x10)
            wTable += 0x10;
        else
            wTable = 0x1F;
        wMessageStrPtr = 0;
        DrawTableScreen();
        return 1;
    case 0x28:
        if (wTable & 0x10)
            wTable -= 0x10;
        else
            wTable = 0;
        wMessageStrPtr = 0;
        DrawTableScreen();
        return 1;
    case 0x30:
        if (!wAPressEnabled)
            break;
        wAPressEnabled = 0;
        EraseTableCursor();
        wCursorMode = 2;
        wSelectionTop = wTableRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = 5;
        switch (wTableCol) {
        case 0:
            wSelectionLeft = 0;
            wSelectionRight = 1;
            wTableEnvCache = ReadTableEnv((char)wTableRow);
            WriteClipboardCell(0, (char)wTableRow, wTableEnvCache);
            WriteTableEnv((char)wTableRow, 0);
            break;
        case 1:
            wSelectionLeft = 1;
            wSelectionRight = 2;
            wTableTransposeCache = ReadTableTranspose((char)wTableRow);
            WriteClipboardCell(1, (char)wTableRow, wTableTransposeCache);
            WriteTableTranspose((char)wTableRow, 0);
            break;
        case 2:
        case 3:
            wSelectionLeft = 2;
            wSelectionRight = 4;
            wTableCellAPtr = 0xB680;
            wTableCellBPtr = 0xB880;
            v = ReadTableCmd((char)wTableRow);
            if (v)
                wColLastCmd[wSongCol] = v;
            WriteClipboardCell(2, (char)wTableRow, v);
            WriteClipboardCell(3, (char)wTableRow, ReadTableVal((char)wTableRow));
            WriteTableCmd((char)wTableRow, 0);
            WriteTableVal((char)wTableRow, 0);
            break;
        case 4:
        case 5:
            wSelectionLeft = 4;
            wSelectionRight = 6;
            wTableCellAPtr = 0xBA80;
            wTableCellBPtr = 0xBC80;
            v = ReadTableCmd((char)wTableRow);
            if (v)
                wColLastCmd[wSongCol] = v;
            WriteClipboardCell(4, (char)wTableRow, v);
            WriteClipboardCell(5, (char)wTableRow, ReadTableVal((char)wTableRow));
            WriteTableCmd((char)wTableRow, 0);
            WriteTableVal((char)wTableRow, 0);
            break;
        }
        DrawTableScreen();
        return 1;
    case 0x50:
        if (wCursorMode == 1 || !wSelectHeld)
            break;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            CutCopyTableSelection(1);
            break;
        }
        if ((wClipboardType == 5 || wClipboardType == 1) && (wTableCol || !wSelectionLeft)) {
            EraseTableCursor();
            PasteTableCells();
            DrawTableScreen();
            return 1;
        }
        break;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        FarCall(1, RedrawScreen);
        return 1;
    case 0x48: {
        UWORD a, b;

        if (wButtonsSame)
            break;
        if (wTableCol == 2 || wTableCol == 3) {
            a = (UWORD)0xB680 + ((UWORD)wTable << 4);
            a += (UWORD)wTableRow;
            if (ReadSongByte(1, a) == 7) {
                b = (UWORD)0xB880 + ((UWORD)wTable << 4);
                b += (UWORD)wTableRow;
                wGroove = ReadSongByte(1, b) & 0x1F;
            }
        } else if (wTableCol > 3) {
            b = (UWORD)0xBA80 + ((UWORD)wTable << 4);
            b += (UWORD)wTableRow;
            if (ReadSongByte(1, b) == 7) {
                b = (UWORD)0xBC80 + ((UWORD)wTable << 4);
                b += (UWORD)wTableRow;
                wGroove = ReadSongByte(1, b) & 0x1F;
            }
        }
        FarCall(5, MoveMapCursorDown);
        FarCall(1, RedrawScreen);
        return 1;
    }
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(5, FAR_FN(RedrawScreen));
        return 1;
    case 0x20:
        if (wCursorMode != 4 || !wAPressEnabled)
            break;
        CutCopyTableSelection(0);
        return 1;
    case 0x60: {
        UBYTE left, right;

        if (!wSelectHeld)
            break;
        if (wCursorMode == 4 && wClipboardType == 5) {
            left = wSelectionLeft;
            right = wTableCol;
            y = wSelectionTop;
            y1 = (char)wTableRow;
            SortBytePair(&y, &y1);
            if (y || y1 != 0x0F) {
                wSelectionTop = 0;
                wTableRow = 0x0F;
                wSelectHeld = 0;
                DrawTableSelection();
                return 1;
            }
            SortBytePair(&left, &right);
            if (left || right != 5) {
                wSelectionLeft = 0;
                wTableCol = 5;
                wSelectHeld = 0;
                DrawTableSelection();
                return 1;
            }
            wSelectHeld = 0;
            CutCopyTableSelection(0);
            return 1;
        }
        ShowMessage("SELECT");
        FarCall(5, SaveSelectionState);
        wSelectionLeft = wTableCol;
        wSelectionTop = wTableRow;
        wSelectHeld = 0;
        return 1;
    }
    }
    return 0;
}

void CutCopyTableSelection(UBYTE cut)
{
    UBYTE x, y;

    if (wClipboardType != 5)
        return;
    ShowMessage(cut ? "CUT" : "COPY");
    wCursorMode = 2;
    wSelectionRight = wTableCol;
    wSelectionBottom = wTableRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    if (wSelectionLeft == 3)
        wSelectionLeft--;
    if (wSelectionLeft == 5)
        wSelectionLeft--;
    if (wSelectionRight == 2)
        wSelectionRight++;
    if (wSelectionRight == 4)
        wSelectionRight++;
    wSelectionRight++;
    wSelectionBottom++;
    for (x = wSelectionLeft; x < wSelectionRight; x++) {
        for (y = wSelectionTop; y < wSelectionBottom; y = y + 1) {
            if (x == 6 && y)
                continue;
            wTableCol = x;
            wTableRow = y;
            EraseTableCursor();
            switch (x) {
            case 0:
                WriteClipboardCell(0, y, ReadTableEnv(y));
                if (cut)
                    WriteTableEnv(y, 0);
                break;
            case 1:
                WriteClipboardCell(1, y, ReadTableTranspose(y));
                if (cut)
                    WriteTableTranspose(y, 0);
                break;
            case 2:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                WriteClipboardCell(2, y, ReadTableCmd(y));
                if (cut)
                    WriteTableCmd(y, 0);
                break;
            case 3:
                wTableCellAPtr = 0xB680;
                wTableCellBPtr = 0xB880;
                WriteClipboardCell(3, y, ReadTableVal(y));
                if (cut)
                    WriteTableVal(y, 0);
                break;
            case 4:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                WriteClipboardCell(4, y, ReadTableCmd(y));
                if (cut)
                    WriteTableCmd(y, 0);
                break;
            case 5:
                wTableCellAPtr = 0xBA80;
                wTableCellBPtr = 0xBC80;
                WriteClipboardCell(5, y, ReadTableVal(y));
                if (cut)
                    WriteTableVal(y, 0);
                break;
            }
        }
    }
    wTableCol = wSelectionLeft;
    wTableRow = wSelectionTop;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    DrawTableScreen();
}

/* The table screen's paste: clipboard cells into rows from wTableRow on.
   A phrase selection's command pair (clipboard type 1, columns 3-4) pastes
   into the first CMD/value pair; pasting a pair into the other one swaps
   the SRAM columns (command $0A becomes 0 when it lands in the second). */
void PasteTableCells(void)
{
    UBYTE x, y;
    UBYTE swap = 0;
    UBYTE fromPhrase = 0;
    UBYTE v;

    if (wClipboardType == 1) {
        if (wSelectionLeft != 3 || wSelectionRight != 5)
            return;
        fromPhrase = 1;
        wSelectionLeft = 2;
        wSelectionRight = 4;
    }
    if (wSelectionBottom - wSelectionTop > 1)
        ShowMessage("PASTE");
    if (wSelectionLeft == 2 && (wTableCol == 4 || wTableCol == 5))
        swap = 1;
    if (wSelectionLeft == 4 && (wTableCol == 2 || wTableCol == 3))
        swap++;
    for (y = wSelectionTop; y < wSelectionBottom; y++) {
        for (x = wSelectionLeft; x < wSelectionRight; x = x + 1) {
            if (x == 6 && y)
                continue;
            switch (x) {
            case 0:
                WriteTableEnv(wTableRow, ReadClipboardCell(0, y));
                break;
            case 1:
                WriteTableTranspose(wTableRow, ReadClipboardCell(1, y));
                break;
            case 2:
                v = ReadClipboardCell(fromPhrase + 2, y);
                if (swap) {
                    wTableCellAPtr = 0xBA80;
                    wTableCellBPtr = 0xBC80;
                    if (v == 0x0A)
                        v = 0;
                } else {
                    wTableCellAPtr = 0xB680;
                    wTableCellBPtr = 0xB880;
                }
                WriteTableCmd(wTableRow, v);
                break;
            case 3:
                if (swap) {
                    wTableCellAPtr = 0xBA80;
                    wTableCellBPtr = 0xBC80;
                } else {
                    wTableCellAPtr = 0xB680;
                    wTableCellBPtr = 0xB880;
                }
                WriteTableVal(wTableRow, ReadClipboardCell(fromPhrase + 3, y));
                break;
            case 4:
                if (swap) {
                    wTableCellAPtr = 0xB680;
                    wTableCellBPtr = 0xB880;
                } else {
                    wTableCellAPtr = 0xBA80;
                    wTableCellBPtr = 0xBC80;
                }
                WriteTableCmd(wTableRow, ReadClipboardCell(4, y));
                break;
            case 5:
                if (swap) {
                    wTableCellAPtr = 0xB680;
                    wTableCellBPtr = 0xB880;
                } else {
                    wTableCellAPtr = 0xBA80;
                    wTableCellBPtr = 0xBC80;
                }
                WriteTableVal(wTableRow, ReadClipboardCell(5, y));
                break;
            }
        }
        wTableRow++;
        wTableRow = (char)wTableRow & 0x0F;     /* read into c, as PasteChainCells */
    }
    if (fromPhrase) {
        wSelectionLeft = 3;
        wSelectionRight = 5;
    }
}

void DrawTableSelection(void)
{
    UBYTE x, y, left, top, right, bottom;

    if (wClipboardType != 5)
        return;
    left = wSelectionLeft;
    right = wTableCol;
    SortBytePair(&left, &right);
    right = (char)right + 1;        /* the char casts read through c and b, */
    top = wSelectionTop;
    bottom = (char)wTableRow;       /* as if the coordinates were chars */
    SortBytePair(&top, &bottom);
    bottom = (char)bottom + 1;
    /* the ring around the rectangle back to the column palettes... */
    for (x = left ? left - 1 : 0; x < (right == 6 ? 6 : right + 1); x++) {
        for (y = top ? top - 1 : 0; y < (bottom == 0x10 ? 0x10 : bottom + 1); y = y + 1) {
            if (x < left || x >= right || y < top || y >= bottom)
                ColorField(TableColX[x], y + 2, TableColWidth[x], x ? 1 - (x & 1) : 0);
        }
    }
    /* ...and the rectangle highlighted */
    for (x = left; x < right; x++)
        for (y = top; y < bottom; y = y + 1)
            ColorField(TableColX[x], y + 2, TableColWidth[x], hGameboyType ? 3 : 2);
}

void ShowSynthFieldHelp(void)
{
    char *s;

    s = 0;
    switch (wSynthCursor) {
    case 0:
        s = "SIGNAL WAVE. W.FX USES A WAVE IN RANGE F0-FF AS INPUT";
        break;
    case 1:
        s = "LOW-PASS, HIGH-PASS, BAND-PASS OR ALL-PASS";
        break;
    case 2:
        s = "DISTORTION. CLIP TRUNCATES TO LIMIT, FOLD MIRRORS AROUND LIMIT, WRAP WRAPS AROUND VERTICALLY";
        break;
    case 3:
    case 9:
    case 15:
        s = "COMPRESS WAVE HORIZONTALLY";
        break;
    case 4:
    case 10:
        s = "SIGNAL VOLUME";
        break;
    case 5:
    case 11:
        s = "FILTER CUTOFF FREQUENCY";
        break;
    case 6:
    case 12:
        s = "RESONANCE CONTROL. BOOSTS SIGNAL AROUND CUTOFF FREQUENCY";
        break;
    case 7:
    case 13:
        s = "VERTICAL SHIFT";
        break;
    case 8:
    case 14:
        s = "LIMIT SIGNAL VERTICALLY USING DIST MODE. 0-F LOWERS VOLUME, 10-FF WRAPS LOUD SIGNALS";
        break;
    default:
        DebugTrap();
    }
    ShowMessage(s);
}

void SaveSelectionState(void)
{
    wSavedCursorMode = wCursorMode;
    wSavedClipType = wClipboardType;
    wSavedSelectionLeft = wSelectionLeft;
    wSavedSelectionTop = wSelectionTop;
    wSavedSelectionRight = wSelectionRight;
    wSavedSelectionBottom = wSelectionBottom;
    wCursorMode = 4;
    wClipboardType = wCurScreen;
}

void RestoreSelectionState(void)
{
    wCursorMode = wSavedCursorMode;
    wClipboardType = wSavedClipType;
    wSelectionLeft = wSavedSelectionLeft;
    wSelectionTop = wSavedSelectionTop;
    wSelectionRight = wSavedSelectionRight;
    wSelectionBottom = wSavedSelectionBottom;
}
