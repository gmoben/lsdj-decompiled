/*
 * The table screen and the screen map -- LSDj 9.4.0, ROM bank $05.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE SCX_REG;          /* $FF43 */
extern volatile sfr UBYTE SCY_REG;          /* $FF42 */
extern UBYTE wSelLeft;                      /* $C40D, the selection rectangle */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wTable;                        /* $C41B, the table being edited */
extern UWORD wTableCellAPtr;                /* $C447: CMD column base, an address held as an integer */
extern UWORD wTableCellBPtr;                /* $C449: its value column */
extern UBYTE wMapCol;                       /* $C8FF, the screen map cursor */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjectCol;                /* $C901 */
extern UBYTE wMapSynthCol;                  /* $C902 */
extern UBYTE wMapMoveDir;                   /* $C903 */
extern UBYTE wTableCol;                     /* $C92F */
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
extern UBYTE FxMnemonics[];                 /* $00:$0824, the command letters */

void MapUpdateTopRow(void);
void MapEnterScreen(void);
void MapStartTransition(UBYTE dir);
void MapCancelSelection(void);
void RestoreSelectionState(void);

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

void DrawTableEnv(UBYTE v)
{
    if ((v & 0x0F) == 0x0F) {
        DrawByte(v >> 4);
        DrawStringIndexed('H');
    } else {
        DrawHexByte(v);
    }
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
