/*
 * The table screen and the screen map -- LSDj 9.4.0, ROM bank $05.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void TableUpdateScreen(void);

extern volatile sfr UBYTE SCX_REG;          /* $FF43 */
extern volatile sfr UBYTE SCY_REG;          /* $FF42 */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wTable;                        /* $C41B, the table being edited */
extern UWORD wTableCellAPtr;                /* $C447: CMD column base, an address held as an integer */
extern UWORD wTableCellBPtr;                /* $C449: its value column */
extern UBYTE wMapCol;                       /* $C8FF, the screen map cursor */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjectCol;                /* $C901 */
extern UBYTE wMapSynthCol;                  /* $C902 */
extern UBYTE wMapMoveDir;                   /* $C903 */
extern UBYTE wSynthCursor;                  /* $C442 */
extern char wTableCol;                      /* $C92F, a plain char (TableDrawCursor's signed switch) */
extern UBYTE wTableRow;                     /* $C930 */
extern UBYTE TableColX[];                /* $05:$42D5..: the six table columns' x, */
extern UBYTE TableColWidth[];            /* width */
extern UBYTE TableColPalette[];             /* and normal palette */
extern UBYTE wSavedCursorMode;              /* $CBD2..$CBD7: SaveSelectionState */
extern UBYTE wSavedClipType;
extern UBYTE wSavedSelLeft;
extern UBYTE wSavedSelTop;
extern UBYTE wSavedSelRight;
extern UBYTE wSavedSelBottom;

void MapUpdateTopRow(void);
void MapEnterScreen(void);
void MapStartTransition(UBYTE dir);
void MapCancelSelection(void);

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

void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */

void MapUpdateTopRow(void)
{
    if (wMapRow != 1)
        return;
    if (wMapCol) {
        if (wMapCol > 2) {
            wMapProjectCol = 2;
            wMapSynthCol = 3;
        } else {
            wMapProjectCol = 1;
            wMapSynthCol = 2;
        }
    } else {
        wMapProjectCol = 0;
        wMapSynthCol = 2;
    }
}

void MapDrawTopRow(void)
{
    UBYTE x;

    hBgDigitPalette = 4;
    hBgPalette = 1;
    GetTilemapAddr(wMapProjectCol + 15, 15);
    DrawStringIndexed('P');
    GetTilemapAddr(wMapSynthCol + 15, 15);
    DrawString("YW");
    x = wMapCol + 15;
    GetTilemapAddr(x, 17);
    DrawStringIndexed('G');
    wVramCursor = (UBYTE *)0x9A0F;
    DrawStringIndexed(wLiveMode ? 'L' : 'S');
    DrawString("CPI");
    DrawStringIndexed(wInstrument == 0x40 ? 'W' : 'T');
    DrawTextField(15, 16, 5, 1);
    DrawTextField(x, wMapRow + 15, 1, 2);
    DrawTextField(x, (wMapRow + 1) % 3 + 15, 1, 1);
    DrawTextField(x, (wMapRow + 2) % 3 + 15, 1, 1);
}

void MapEnterScreen(void)
{
    switch (wMapRow) {
    case 0:
        if (wMapCol == wMapProjectCol)
            wCurrentScreen = 9;
        else if (wMapCol == wMapSynthCol)
            wCurrentScreen = 10;
        else
            wCurrentScreen = 8;
        break;
    case 1:
        switch (wMapCol) {
        case 0:
            wCurrentScreen = 4;
            break;
        case 1:
            wCurrentScreen = 3;
            break;
        case 2:
            wCurrentScreen = 1;
            break;
        case 3:
            wCurrentScreen = 6;
            break;
        case 4:
            wCurrentScreen = wInstrument == 0x40 ? 12 : 5;
            break;
        }
        break;
    case 2:
        wCurrentScreen = 2;
        break;
    }
}

void MapStartTransition(UBYTE dir)
{
    wMapMoveDir = dir;
    SCX_REG = 0;
    SCY_REG = 0;
}

void MapCancelSelection(void)
{
    if (wCursorMode == 4)
        FarCall(5, RestoreSelectionState);
}

void MapMoveLeft(void)
{
    MapCancelSelection();
    wMapCol--;
    if (wMapCol == 0xFF)
        DebugTrap();
    MapEnterScreen();
    MapStartTransition(4);
    MapUpdateTopRow();
}

void MapMoveRight(void)
{
    MapCancelSelection();
    wMapCol++;
    MapEnterScreen();
    MapStartTransition(2);
    MapUpdateTopRow();
}

void MapMoveUp(void)
{
    MapCancelSelection();
    wMapRow--;
    if (wMapRow == 0xFF)
        DebugTrap();
    MapEnterScreen();
    MapStartTransition(1);
}

void MapMoveDown(void)
{
    MapCancelSelection();
    wMapRow++;
    if (wMapRow == 3)
        DebugTrap();
    MapEnterScreen();
    MapStartTransition(3);
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

UBYTE TableReadCmd(UBYTE step)
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

void TableWriteEnv(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    WriteSongByte(0, p + step, v);
}

void TableWriteCmd(UBYTE step, UBYTE cmd)
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

void TableWriteVal(UBYTE step, UBYTE v)
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

void TableWriteTrans(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableTransposes + ((UWORD)wTable << 4);
    WriteSongByte(1, p + step, v);
}

UBYTE TableReadEnv(UBYTE step)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    return ReadSongByte(0, p + step);
}

UBYTE TableReadVal(UBYTE step)
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

UBYTE TableReadTrans(UBYTE step)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableTransposes + ((UWORD)wTable << 4);
    return ReadSongByte(1, p + step);
}

void TableEraseCursor(void)
{
    UBYTE y, end, x;

    y = wTableRow + 2;
    if (wCursorMode == 4)
        return;
    end = TableColX[wTableCol] + TableColWidth[wTableCol];
    for (x = TableColX[wTableCol]; x < end; x++)
        DrawTextField(x, y, 1, TableColPalette[wTableCol]);
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
    hBgDigitPalette = 4;
    hBgPalette = 1;
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
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawCmdLetter(a1);
        wVramCursor += 3;
        DrawCmdLetter(a2);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor -= 4;
        DrawFieldValue(a1, wRowBufCol3[i]);
        wVramCursor += 2;
        DrawFieldValue(a2, wRowBufCol5[i]);
        wVramCursor += 0x19;
    }
    a1 = wTableRow + 2;
    i = TableColX[wTableCol];
    w = TableColWidth[wTableCol];
    pal = hGameboyType ? 3 : 2;
    DrawTextField(i, a1, w, pal);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    if (wCursorMode == 4)
        TableUpdateScreen();
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

void TableDrawCursor(void)
{
    if (wCursorMode == 4)
        TableUpdateScreen();
    else
        DrawTextField(TableColX[wTableCol], wTableRow + 2, TableColWidth[wTableCol], hGameboyType ? 3 : 2);
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

void GetSynthFieldHelp(void)
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
    wSavedSelLeft = wSelLeft;
    wSavedSelTop = wSelTop;
    wSavedSelRight = wSelRight;
    wSavedSelBottom = wSelBottom;
    wCursorMode = 4;
    wClipboardType = wCurrentScreen;
}

void RestoreSelectionState(void)
{
    wCursorMode = wSavedCursorMode;
    wClipboardType = wSavedClipType;
    wSelLeft = wSavedSelLeft;
    wSelTop = wSavedSelTop;
    wSelRight = wSavedSelRight;
    wSelBottom = wSavedSelBottom;
}
