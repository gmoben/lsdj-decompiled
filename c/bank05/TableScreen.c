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
extern UWORD wTableCellAPointer;                /* $C447: CMD column base, an address held as an integer */
extern UWORD wTableCellBPointer;                /* $C449: its value column */
extern UBYTE wMapColumn;                       /* $C8FF, the screen map cursor */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjectColumn;                /* $C901 */
extern UBYTE wMapSynthColumn;                  /* $C902 */
extern UBYTE wMapMoveDirection;                   /* $C903 */
extern UBYTE wSynthCursor;                  /* $C442 */
extern char wTableColumn;                      /* $C92F, a plain char (DrawTableCursor's signed switch) */
extern UBYTE wTableRow;                     /* $C930 */
extern UBYTE TableColumnX[];                /* $05:$42D5..: the six table columns' x, */
extern UBYTE TableColumnWidth[];            /* width */
extern UBYTE TableColumnPalette[];             /* and normal palette */
extern UBYTE wSavedCursorMode;              /* $CBD2..$CBD7: SaveSelectionState */
extern UBYTE wSavedClipboardType;
extern UBYTE wSavedSelectionLeft;
extern UBYTE wSavedSelectionTop;
extern UBYTE wSavedSelectionRight;
extern UBYTE wSavedSelectionBottom;

void UpdateMapTopRow(void);
void GotoMapCursorScreen(void);
void StartMapTransition(UBYTE direction);
void CancelSelectionMode(void);

void DrawAllChannelStatus(void);               /* bank $1F */
extern UBYTE wChannelInstrument[];                  /* $C210, [4] */
extern UBYTE wInstrumentTableRow1[];             /* $C250, [64]; wInstrumentTableRow2's [64] follow */
extern UBYTE wChannelStepCountdown[];          /* $C0C4, [4] */
extern UBYTE wChannelTable[];                  /* $C204, [4] */
extern UBYTE wChannelTableRow1[];              /* $C208, [4] (+4: wChannelTableRow2) */
extern UBYTE wChannelTableStep[];              /* $C33F, [4] */
extern UBYTE wLiveMode;                     /* $C528 */
extern UWORD wPhraseNotePointer[];              /* $C0BC, [4] */
extern UBYTE wRowBufferColumn2[];                 /* $C48D, [16] */
extern UBYTE wRowBufferColumn3[];                 /* $C49D, [16] */
extern UBYTE wRowBufferColumn4[];                 /* $C4AD, [16] */
extern UBYTE wRowBufferColumn5[];                 /* $C4BD, [16] */

void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */

void UpdateMapTopRow(void)
{
    if (wMapRow != 1)
        return;
    if (wMapColumn) {
        if (wMapColumn > 2) {
            wMapProjectColumn = 2;
            wMapSynthColumn = 3;
        } else {
            wMapProjectColumn = 1;
            wMapSynthColumn = 2;
        }
    } else {
        wMapProjectColumn = 0;
        wMapSynthColumn = 2;
    }
}

void DrawMapTopRow(void)
{
    UBYTE x;

    hBgDigitPalette = 4;
    hBgPalette = 1;
    SetVramCursor(wMapProjectColumn + 15, 15);
    DrawStringIndexed('P');
    SetVramCursor(wMapSynthColumn + 15, 15);
    DrawString("YW");
    x = wMapColumn + 15;
    SetVramCursor(x, 17);
    DrawStringIndexed('G');
    wVramCursor = (UBYTE *)BG_MAP(15, 16);
    DrawStringIndexed(wLiveMode ? 'L' : 'S');
    DrawString("CPI");
    DrawStringIndexed(wInstrument == 0x40 ? 'W' : 'T');
    ColorField(15, 16, 5, 1);
    ColorField(x, wMapRow + 15, 1, 2);
    ColorField(x, (wMapRow + 1) % 3 + 15, 1, 1);
    ColorField(x, (wMapRow + 2) % 3 + 15, 1, 1);
}

void GotoMapCursorScreen(void)
{
    switch (wMapRow) {
    case 0:
        if (wMapColumn == wMapProjectColumn)
            wCurrentScreen = SCREEN_PROJECT;
        else if (wMapColumn == wMapSynthColumn)
            wCurrentScreen = SCREEN_SYNTH;
        else
            wCurrentScreen = SCREEN_WAVE;
        break;
    case 1:
        switch (wMapColumn) {
        case 0:
            wCurrentScreen = SCREEN_SONG;
            break;
        case 1:
            wCurrentScreen = SCREEN_CHAIN;
            break;
        case 2:
            wCurrentScreen = SCREEN_PHRASE;
            break;
        case 3:
            wCurrentScreen = SCREEN_INSTRUMENT;
            break;
        case 4:
            wCurrentScreen = wInstrument == 0x40 ? 12 : 5;
            break;
        }
        break;
    case 2:
        wCurrentScreen = SCREEN_GROOVE;
        break;
    }
}

void StartMapTransition(UBYTE direction)
{
    wMapMoveDirection = direction;
    SCX_REG = 0;
    SCY_REG = 0;
}

void CancelSelectionMode(void)
{
    if (wCursorMode == CURSOR_SELECTING)
        FarCall(5, RestoreSelectionState);
}

void MoveMapCursorLeft(void)
{
    CancelSelectionMode();
    wMapColumn--;
    if (wMapColumn == 0xFF)
        DebugTrap();
    GotoMapCursorScreen();
    StartMapTransition(4);
    UpdateMapTopRow();
}

void MoveMapCursorRight(void)
{
    CancelSelectionMode();
    wMapColumn++;
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

void DrawCommandLetter(UBYTE command)
{
    UBYTE c, ok;

    ok = command <= COMMAND_Z;
    if (ok)
        c = CommandMnemonics[command];
    else
        c = '?';
    DrawStringIndexed(c);
}

UBYTE ReadTableCommand(UBYTE step)
{
    UBYTE *p;
    UBYTE v, ok;

    p = (UBYTE *)wTableCellAPointer;
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

void WriteTableEnvelope(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    WriteSongByte(0, p + step, v);
}

void WriteTableCommand(UBYTE step, UBYTE command)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellAPointer;
    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    if (command > COMMAND_Z)
        DebugTrap();
    p += (UWORD)wTable << 4;
    WriteSongByte(1, p + step, command);
}

void WriteTableValue(UBYTE step, UBYTE v)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellBPointer;
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

UBYTE ReadTableEnvelope(UBYTE step)
{
    UBYTE *p;

    if (step >= 0x10)
        DebugTrap();
    if (wTable >= 0x20)
        DebugTrap();
    p = SramTableEnvelopes + ((UWORD)wTable << 4);
    return ReadSongByte(0, p + step);
}

UBYTE ReadTableValue(UBYTE step)
{
    UBYTE *p;

    p = (UBYTE *)wTableCellBPointer;
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
    if (wCursorMode == CURSOR_SELECTING)
        return;
    end = TableColumnX[wTableColumn] + TableColumnWidth[wTableColumn];
    for (x = TableColumnX[wTableColumn]; x < end; x++)
        ColorField(x, y, 1, TableColumnPalette[wTableColumn]);
}

void ReadTableStep(void)
{
    UWORD t;
    UBYTE *p0, *p1, *p2, *p3, *p4;

    t = wTable & 0x1F;
    t <<= 4;
    p0 = SramTableEnvelopes + t;
    p1 = SramTableTransposes + t;
    p2 = SramTableCommands1 + t;
    p3 = SramTableCommands2 + t;
    p4 = SramTableCommandValues1 + t;
    t += (UWORD)SRAM_TABLE_COMMAND_VALUES2;
    ReadSongBytes(0, p0, wSongReadBuffer);
    ReadSongBytes(1, p1, (UBYTE *)ROW_BUFFER_COLUMN(1));
    ReadSongBytes(1, p2, (UBYTE *)ROW_BUFFER_COLUMN(2));
    ReadSongBytes(1, p4, (UBYTE *)ROW_BUFFER_COLUMN(3));
    ReadSongBytes(1, p3, (UBYTE *)ROW_BUFFER_COLUMN(4));
    ReadSongBytes(1, (UBYTE *)t, (UBYTE *)ROW_BUFFER_COLUMN(5));
}

void DrawTableEnvelope(UBYTE v)
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
    UBYTE step;
    UBYTE a1;
    UBYTE w;
    UBYTE a2;
    UBYTE x, palette;

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
    wVramCursor = (UBYTE *)BG_MAP(2, 2);
    for (step = 0; step < 16; step++) {
        DrawTableEnvelope(wSongReadBuffer[step]);
        wVramCursor++;
        DrawHexByte(wUsedTableMap[step]);
        wVramCursor += 0x1B;
    }
    wVramCursor = (UBYTE *)BG_MAP(7, 2);
    for (step = 0; step < 16; step++) {
        a2 = wRowBufferColumn4[step];
        a1 = wRowBufferColumn2[step];
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawCommandLetter(a1);
        wVramCursor += 3;
        DrawCommandLetter(a2);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor -= 4;
        DrawFieldValue(a1, wRowBufferColumn3[step]);
        wVramCursor += 2;
        DrawFieldValue(a2, wRowBufferColumn5[step]);
        wVramCursor += 0x19;
    }
    a1 = wTableRow + 2;
    step = TableColumnX[wTableColumn];
    w = TableColumnWidth[wTableColumn];
    palette = hGameboyType ? 3 : 2;
    ColorField(step, a1, w, palette);
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    if (wCursorMode == CURSOR_SELECTING)
        DrawTableSelection();
}

UBYTE TableInputFlag(UBYTE x)
{
    switch (x) {
    case 4:
        return 1;
    case 10:
        return wTableColumn != TABLE_COLUMN_COMMAND1;
    }
    return 0;
}

void TableScreenUpdate(void)
{
    UBYTE r0, r1, r2;
    UWORD p;
    UBYTE v;

    r0 = 0xFF;
    if (!wPlayMode || wTable != wChannelTable[wSongColumn]) {
        ClearSpriteY(0);
        ClearSpriteY(1);
        ClearSpriteY(2);
        return;
    }
    switch (wChannelStepCountdown[wSongColumn]) {
    case 0:
        r0 = (UBYTE)wPhraseNotePointer[wSongColumn] & 0x0F;
        break;
    default:
        p = wPhraseNotePointer[wSongColumn];
        p = p - 1;
        r0 = (UBYTE)p & 0x0F;
        break;
    case 0xFF:
        break;
    }
    if (wChannelTableStep[wSongColumn] == 0xFF) {
        v = wChannelInstrument[wSongColumn];
        r1 = wInstrumentTableRow1[v];
        r2 = wInstrumentTableRow1[v + 0x40];   /* wInstrumentTableRow2[v], spelt so for the ROM's add a,$40 */
    } else {
        r1 = wChannelTableRow1[wSongColumn];
        r2 = wChannelTableRow1[wSongColumn + 4];
    }
    SetSpriteY(0, r0);
    SetSpriteY(1, r1);
    SetSpriteY(2, r2);
}

void DrawTableCursor(void)
{
    if (wCursorMode == CURSOR_SELECTING)
        DrawTableSelection();
    else
        ColorField(TableColumnX[wTableColumn], wTableRow + 2, TableColumnWidth[wTableColumn], hGameboyType ? 3 : 2);
    switch (wTableColumn) {
    case TABLE_COLUMN_COMMAND1:
    case TABLE_COLUMN_VALUE1:
        wTableCellAPointer = SRAM_TABLE_COMMANDS1;
        wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
        break;
    case TABLE_COLUMN_COMMAND2:
    case TABLE_COLUMN_VALUE2:
        wTableCellAPointer = SRAM_TABLE_COMMANDS2;
        wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
        break;
    }
}

extern UBYTE wTableEnvelopeCache;                /* $C931 */
extern UBYTE wTableTransposeCache;              /* $C932 */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wPendingCommands[];                /* $C457, [16] */
extern UBYTE wPendingCommandTarget;             /* $C467 */
extern UBYTE wColumnLastCommand[];                 /* $C44B, [4] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
void ShowCommandHelp(void);                 /* bank 1 */
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
    UBYTE row, rowEnd, v;

    if (wTable >= 0x20)
        DebugTrap();
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wButtons != J_A)
        wDrawStep = 0;
    switch (wButtons) {
    case J_SELECT_TAP:
        if (wCursorMode == CURSOR_SELECTING) {
            wTableColumn = wSelectionLeft;
            wTableRow = wSelectionTop;
            FarCall(5, RestoreSelectionState);
            if (hGameboyType) {
                FarCall(1, FAR_FN(DrawScreenPaletteMap));
                FarCall(5, DrawMapTopRow);
            }
            DrawTableScreen();
            DrawTableCursor();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case J_A:
        if (!wAPressEnabled || wCursorMode == CURSOR_SELECTING)
            break;
        SetVramCursor(TableColumnX[wTableColumn], wTableRow + 2);
        if (wTableColumn > TABLE_COLUMN_VALUE1) {
            wTableCellAPointer = SRAM_TABLE_COMMANDS2;
            wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
        } else {
            wTableCellAPointer = SRAM_TABLE_COMMANDS1;
            wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
        }
        switch (wTableColumn) {
        case TABLE_COLUMN_VOLUME:
            if (!ReadTableEnvelope((char)wTableRow)) {
                DrawTableEnvelope(wTableEnvelopeCache);
                WriteTableEnvelope((char)wTableRow, wTableEnvelopeCache);
            }
            break;
        case TABLE_COLUMN_TRANSPOSE:
            if (!ReadTableTranspose((char)wTableRow)) {
                DrawHexByte(wTableTransposeCache);
                WriteTableTranspose((char)wTableRow, wTableTransposeCache);
            }
            break;
        case TABLE_COLUMN_COMMAND1:
        case TABLE_COLUMN_COMMAND2:
            wPendingCommandTarget = (wTableColumn == TABLE_COLUMN_COMMAND2 ? 1 : 0) + 1;
            v = ReadTableCommand((char)wTableRow);
            if (!v) {
                v = wColumnLastCommand[wSongColumn];
                if (!v)
                    DebugTrap();
            } else if (wDrawStep++)
                FarCallArgument(1, FAR_FN(ShowCommandHelp), v);
            if (v == 0x0A && wTableColumn == TABLE_COLUMN_COMMAND2)
                v = 1;
            wPendingCommands[wTableRow] = v;
            DrawCommandLetter(v);
            hBgDigitPalette = 4;
            hBgPalette = 1;
            DrawFieldValue(v, ReadTableValue((char)wTableRow));
            break;
        case TABLE_COLUMN_VALUE1:
        case TABLE_COLUMN_VALUE2:
            if (wDrawStep++ == 1) {
                v = ReadTableCommand((char)wTableRow);
                if (v == 1) {
                    v = FarCallByte(0x1F, FAR_FN(FindFreeTable));
                    if (v != 0xFF) {
                        WriteTableValue((char)wTableRow, v);
                        DrawHexByte(v);
                    }
                }
            }
            break;
        }
        return 1;
    case J_RIGHT:
        EraseTableCursor();
        if (++wTableColumn == TABLE_COLUMNS)
            wTableColumn--;
        DrawTableCursor();
        return 1;
    case J_LEFT:
        EraseTableCursor();
        if (!wTableColumn)
            wTableColumn++;
        wTableColumn--;
        DrawTableCursor();
        return 1;
    case J_UP:
        EraseTableCursor();
        if ((char)--wTableRow == 0xFF)
            wTableRow++;
        DrawTableCursor();
        return 1;
    case J_DOWN:
        EraseTableCursor();
        if (++wTableRow == 0x10)
            wTableRow = 0x0F;
        DrawTableCursor();
        return 1;
    case (J_A | J_RIGHT):
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wTableRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wTableRow;
            row = (char)wTableRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            switch (wTableColumn) {
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_VALUE1:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                break;
            case TABLE_COLUMN_COMMAND2:
            case TABLE_COLUMN_VALUE2:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                break;
            }
            SetVramCursor(TableColumnX[wTableColumn], row + 2);
            switch (wTableColumn) {
            case TABLE_COLUMN_VOLUME:
                v = ReadTableEnvelope(row);
                if ((v & 0x0F) != 0x0F) {
                    v++;
                    DrawTableEnvelope(v);
                    WriteTableEnvelope(row, v);
                }
                wTableEnvelopeCache = v;
                break;
            case TABLE_COLUMN_TRANSPOSE:
                v = ReadTableTranspose(row);
                v++;
                DrawHexByte(v);
                WriteTableTranspose(row, v);
                wTableTransposeCache = v;
                break;
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_COMMAND2:
                if (!wPendingCommands[row])
                    if (!(wPendingCommands[row] = ReadTableCommand(row)))
                        break;
                wPendingCommandTarget = (wTableColumn == TABLE_COLUMN_COMMAND2 ? 1 : 0) + 1;
                if (wPendingCommands[row] < 0x13)
                    do
                        wPendingCommands[row]++;
                    while (TableInputFlag(wPendingCommands[row]));
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadTableValue(row));
                break;
            case TABLE_COLUMN_VALUE1:
            case TABLE_COLUMN_VALUE2:
                v = ReadTableValue(row);
                switch (ReadTableCommand(row)) {
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
                    if (v != 3 || wSongColumn == CHANNEL_WAV)
                        v++;
                    break;
                default:
                    v++;
                }
                WriteTableValue(row, v);
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawFieldValue(ReadTableCommand(row), v);
                break;
            }
        }
        return 1;
    case (J_A | J_LEFT):
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wTableRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wTableRow;
            row = (char)wTableRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            switch (wTableColumn) {
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_VALUE1:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                break;
            case TABLE_COLUMN_COMMAND2:
            case TABLE_COLUMN_VALUE2:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                break;
            }
            SetVramCursor(TableColumnX[wTableColumn], row + 2);
            switch (wTableColumn) {
            case TABLE_COLUMN_VOLUME:
                v = ReadTableEnvelope(row);
                if (v & 0x0F) {
                    v--;
                    DrawTableEnvelope(v);
                    WriteTableEnvelope(row, v);
                }
                wTableEnvelopeCache = v;
                break;
            case TABLE_COLUMN_TRANSPOSE:
                v = ReadTableTranspose(row);
                v--;
                DrawHexByte(v);
                WriteTableTranspose(row, v);
                wTableTransposeCache = v;
                break;
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_COMMAND2:
                wPendingCommandTarget = (wTableColumn == TABLE_COLUMN_COMMAND2 ? 1 : 0) + 1;
                if (!wPendingCommands[row])
                    wPendingCommands[row] = ReadTableCommand(row);
                if (wPendingCommands[row] > 1)
                    do
                        wPendingCommands[row]--;
                    while (TableInputFlag(wPendingCommands[row]));
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadTableValue(row));
                break;
            case TABLE_COLUMN_VALUE1:
            case TABLE_COLUMN_VALUE2:
                v = ReadTableValue(row);
                switch (ReadTableCommand(row)) {
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
                    if (v || wSongColumn == CHANNEL_WAV)
                        v--;
                    break;
                default:
                    v--;
                }
                WriteTableValue(row, v);
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawFieldValue(ReadTableCommand(row), v);
                break;
            }
        }
        return 1;
    case (J_A | J_UP):
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wTableRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wTableRow;
            row = (char)wTableRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            switch (wTableColumn) {
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_VALUE1:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                break;
            case TABLE_COLUMN_COMMAND2:
            case TABLE_COLUMN_VALUE2:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                break;
            }
            SetVramCursor(TableColumnX[wTableColumn], row + 2);
            switch (wTableColumn) {
            case TABLE_COLUMN_VOLUME:
                v = ReadTableEnvelope(row);
                v += 0x10;
                DrawTableEnvelope(v);
                WriteTableEnvelope(row, v);
                wTableEnvelopeCache = v;
                break;
            case TABLE_COLUMN_TRANSPOSE:
                v = ReadTableTranspose(row);
                v += wSongColumn == CHANNEL_NOI ? 4 : 12;
                DrawHexByte(v);
                WriteTableTranspose(row, v);
                wTableTransposeCache = v;
                break;
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_COMMAND2:
                if (!wPendingCommands[row])
                    if (!(wPendingCommands[row] = ReadTableCommand(row)))
                        break;
                wPendingCommandTarget = (wTableColumn == TABLE_COLUMN_COMMAND2 ? 1 : 0) + 1;
                if (wPendingCommands[row] < 0x13)
                    do
                        wPendingCommands[row]++;
                    while (TableInputFlag(wPendingCommands[row]));
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadTableValue(row));
                break;
            case TABLE_COLUMN_VALUE1:
            case TABLE_COLUMN_VALUE2:
                v = ReadTableValue(row);
                switch (ReadTableCommand(row)) {
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
                    if (wSongColumn == CHANNEL_WAV)
                        v += 0x10;
                    else if (v != 3)
                        v++;
                    break;
                case 0x0F:
                    v += wSongColumn == CHANNEL_NOI ? 4 : 0x10;
                    break;
                default:
                    v += 0x10;
                }
                WriteTableValue(row, v);
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawFieldValue(ReadTableCommand(row), v);
                break;
            }
        }
        return 1;
    case (J_A | J_DOWN):
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wTableRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wTableRow;
            row = (char)wTableRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            switch (wTableColumn) {
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_VALUE1:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                break;
            case TABLE_COLUMN_COMMAND2:
            case TABLE_COLUMN_VALUE2:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                break;
            }
            SetVramCursor(TableColumnX[wTableColumn], row + 2);
            switch (wTableColumn) {
            case TABLE_COLUMN_VOLUME:
                v = ReadTableEnvelope(row);
                v -= 0x10;
                DrawTableEnvelope(v);
                WriteTableEnvelope(row, v);
                wTableEnvelopeCache = v;
                break;
            case TABLE_COLUMN_TRANSPOSE:
                v = ReadTableTranspose(row);
                v -= wSongColumn == CHANNEL_NOI ? 4 : 12;
                DrawHexByte(v);
                WriteTableTranspose(row, v);
                wTableTransposeCache = v;
                break;
            case TABLE_COLUMN_COMMAND1:
            case TABLE_COLUMN_COMMAND2:
                wPendingCommandTarget = (wTableColumn == TABLE_COLUMN_COMMAND2 ? 1 : 0) + 1;
                if (!wPendingCommands[row])
                    wPendingCommands[row] = ReadTableCommand(row);
                if (wPendingCommands[row] > 1)
                    do
                        wPendingCommands[row]--;
                    while (TableInputFlag(wPendingCommands[row]));
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadTableValue(row));
                break;
            case TABLE_COLUMN_VALUE1:
            case TABLE_COLUMN_VALUE2:
                v = ReadTableValue(row);
                switch (ReadTableCommand(row)) {
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
                    if (wSongColumn == CHANNEL_WAV)
                        v -= 0x10;
                    else if (v)
                        v--;
                    break;
                case 0x0F:
                    v -= wSongColumn == CHANNEL_NOI ? 4 : 0x10;
                    break;
                default:
                    v -= 0x10;
                }
                WriteTableValue(row, v);
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawFieldValue(ReadTableCommand(row), v);
                break;
            }
        }
        return 1;
    case (J_B | J_RIGHT):
        if (wTable < 0x1F)
            wTable++;
        wMessageStringPointer = 0;
        DrawTableScreen();
        return 1;
    case (J_B | J_LEFT):
        if (wTable)
            wTable--;
        wMessageStringPointer = 0;
        DrawTableScreen();
        return 1;
    case (J_B | J_UP):
        if (wTable < 0x10)
            wTable += 0x10;
        else
            wTable = 0x1F;
        wMessageStringPointer = 0;
        DrawTableScreen();
        return 1;
    case (J_B | J_DOWN):
        if (wTable & 0x10)
            wTable -= 0x10;
        else
            wTable = 0;
        wMessageStringPointer = 0;
        DrawTableScreen();
        return 1;
    case (J_B | J_A):
        if (!wAPressEnabled)
            break;
        wAPressEnabled = 0;
        EraseTableCursor();
        wCursorMode = CURSOR_COPIED;
        wSelectionTop = wTableRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = SCREEN_TABLE;
        switch (wTableColumn) {
        case TABLE_COLUMN_VOLUME:
            wSelectionLeft = 0;
            wSelectionRight = 1;
            wTableEnvelopeCache = ReadTableEnvelope((char)wTableRow);
            WriteClipboardCell(0, (char)wTableRow, wTableEnvelopeCache);
            WriteTableEnvelope((char)wTableRow, 0);
            break;
        case TABLE_COLUMN_TRANSPOSE:
            wSelectionLeft = 1;
            wSelectionRight = 2;
            wTableTransposeCache = ReadTableTranspose((char)wTableRow);
            WriteClipboardCell(1, (char)wTableRow, wTableTransposeCache);
            WriteTableTranspose((char)wTableRow, 0);
            break;
        case TABLE_COLUMN_COMMAND1:
        case TABLE_COLUMN_VALUE1:
            wSelectionLeft = 2;
            wSelectionRight = 4;
            wTableCellAPointer = SRAM_TABLE_COMMANDS1;
            wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
            v = ReadTableCommand((char)wTableRow);
            if (v)
                wColumnLastCommand[wSongColumn] = v;
            WriteClipboardCell(2, (char)wTableRow, v);
            WriteClipboardCell(3, (char)wTableRow, ReadTableValue((char)wTableRow));
            WriteTableCommand((char)wTableRow, 0);
            WriteTableValue((char)wTableRow, 0);
            break;
        case TABLE_COLUMN_COMMAND2:
        case TABLE_COLUMN_VALUE2:
            wSelectionLeft = 4;
            wSelectionRight = 6;
            wTableCellAPointer = SRAM_TABLE_COMMANDS2;
            wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
            v = ReadTableCommand((char)wTableRow);
            if (v)
                wColumnLastCommand[wSongColumn] = v;
            WriteClipboardCell(4, (char)wTableRow, v);
            WriteClipboardCell(5, (char)wTableRow, ReadTableValue((char)wTableRow));
            WriteTableCommand((char)wTableRow, 0);
            WriteTableValue((char)wTableRow, 0);
            break;
        }
        DrawTableScreen();
        return 1;
    case (J_SELECT | J_A):
        if (wCursorMode == CURSOR_NORMAL || !wSelectHeld)
            break;
        if (wCursorMode == CURSOR_SELECTING) {
            wSelectHeld = 0;
            CutCopyTableSelection(1);
            break;
        }
        if ((wClipboardType == SCREEN_TABLE || wClipboardType == SCREEN_PHRASE)
                && (wTableColumn || !wSelectionLeft)) {
            EraseTableCursor();
            PasteTableCells();
            DrawTableScreen();
            return 1;
        }
        break;
    case (J_SELECT | J_START):
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case (J_SELECT | J_UP):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        FarCall(1, RedrawScreen);
        return 1;
    case (J_SELECT | J_DOWN): {
        UWORD a, b;

        if (wButtonsSame)
            break;
        if (wTableColumn == TABLE_COLUMN_COMMAND1 || wTableColumn == TABLE_COLUMN_VALUE1) {
            a = (UWORD)SRAM_TABLE_COMMANDS1 + ((UWORD)wTable << 4);
            a += (UWORD)wTableRow;
            if (ReadSongByte(1, a) == 7) {
                b = (UWORD)SRAM_TABLE_COMMAND_VALUES1 + ((UWORD)wTable << 4);
                b += (UWORD)wTableRow;
                wGroove = ReadSongByte(1, b) & 0x1F;
            }
        } else if (wTableColumn > TABLE_COLUMN_VALUE1) {
            b = (UWORD)SRAM_TABLE_COMMANDS2 + ((UWORD)wTable << 4);
            b += (UWORD)wTableRow;
            if (ReadSongByte(1, b) == 7) {
                b = (UWORD)SRAM_TABLE_COMMAND_VALUES2 + ((UWORD)wTable << 4);
                b += (UWORD)wTableRow;
                wGroove = ReadSongByte(1, b) & 0x1F;
            }
        }
        FarCall(5, MoveMapCursorDown);
        FarCall(1, RedrawScreen);
        return 1;
    }
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(5, FAR_FN(RedrawScreen));
        return 1;
    case J_B:
        if (wCursorMode != CURSOR_SELECTING || !wAPressEnabled)
            break;
        CutCopyTableSelection(0);
        return 1;
    case (J_SELECT | J_B): {
        UBYTE left, right;

        if (!wSelectHeld)
            break;
        if (wCursorMode == CURSOR_SELECTING && wClipboardType == SCREEN_TABLE) {
            left = wSelectionLeft;
            right = wTableColumn;
            row = wSelectionTop;
            rowEnd = (char)wTableRow;
            SortBytePair(&row, &rowEnd);
            if (row || rowEnd != 0x0F) {
                wSelectionTop = 0;
                wTableRow = 0x0F;
                wSelectHeld = 0;
                DrawTableSelection();
                return 1;
            }
            SortBytePair(&left, &right);
            if (left || right != 5) {
                wSelectionLeft = 0;
                wTableColumn = TABLE_COLUMN_VALUE2;
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
        wSelectionLeft = wTableColumn;
        wSelectionTop = wTableRow;
        wSelectHeld = 0;
        return 1;
    }
    }
    return 0;
}

void CutCopyTableSelection(UBYTE cut)
{
    UBYTE column, row;

    if (wClipboardType != SCREEN_TABLE)
        return;
    ShowMessage(cut ? "CUT" : "COPY");
    wCursorMode = CURSOR_COPIED;
    wSelectionRight = wTableColumn;
    wSelectionBottom = wTableRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    if (wSelectionLeft == TABLE_COLUMN_VALUE1)
        wSelectionLeft--;
    if (wSelectionLeft == TABLE_COLUMN_VALUE2)
        wSelectionLeft--;
    if (wSelectionRight == TABLE_COLUMN_COMMAND1)
        wSelectionRight++;
    if (wSelectionRight == TABLE_COLUMN_COMMAND2)
        wSelectionRight++;
    wSelectionRight++;
    wSelectionBottom++;
    for (column = wSelectionLeft; column < wSelectionRight; column++) {
        for (row = wSelectionTop; row < wSelectionBottom; row = row + 1) {
            if (column == 6 && row)
                continue;
            wTableColumn = column;
            wTableRow = row;
            EraseTableCursor();
            switch (column) {
            case 0:
                WriteClipboardCell(0, row, ReadTableEnvelope(row));
                if (cut)
                    WriteTableEnvelope(row, 0);
                break;
            case 1:
                WriteClipboardCell(1, row, ReadTableTranspose(row));
                if (cut)
                    WriteTableTranspose(row, 0);
                break;
            case 2:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                WriteClipboardCell(2, row, ReadTableCommand(row));
                if (cut)
                    WriteTableCommand(row, 0);
                break;
            case 3:
                wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                WriteClipboardCell(3, row, ReadTableValue(row));
                if (cut)
                    WriteTableValue(row, 0);
                break;
            case 4:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                WriteClipboardCell(4, row, ReadTableCommand(row));
                if (cut)
                    WriteTableCommand(row, 0);
                break;
            case 5:
                wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                WriteClipboardCell(5, row, ReadTableValue(row));
                if (cut)
                    WriteTableValue(row, 0);
                break;
            }
        }
    }
    wTableColumn = wSelectionLeft;
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
    UBYTE column, row;
    UBYTE swap = 0;
    UBYTE fromPhrase = 0;
    UBYTE v;

    if (wClipboardType == SCREEN_PHRASE) {
        if (wSelectionLeft != TABLE_COLUMN_VALUE1 || wSelectionRight != TABLE_COLUMN_VALUE2)
            return;
        fromPhrase = 1;
        wSelectionLeft = 2;
        wSelectionRight = 4;
    }
    if (wSelectionBottom - wSelectionTop > 1)
        ShowMessage("PASTE");
    if (wSelectionLeft == TABLE_COLUMN_COMMAND1
            && (wTableColumn == TABLE_COLUMN_COMMAND2 || wTableColumn == TABLE_COLUMN_VALUE2))
        swap = 1;
    if (wSelectionLeft == TABLE_COLUMN_COMMAND2
            && (wTableColumn == TABLE_COLUMN_COMMAND1 || wTableColumn == TABLE_COLUMN_VALUE1))
        swap++;
    for (row = wSelectionTop; row < wSelectionBottom; row++) {
        for (column = wSelectionLeft; column < wSelectionRight; column = column + 1) {
            if (column == 6 && row)
                continue;
            switch (column) {
            case 0:
                WriteTableEnvelope(wTableRow, ReadClipboardCell(0, row));
                break;
            case 1:
                WriteTableTranspose(wTableRow, ReadClipboardCell(1, row));
                break;
            case 2:
                v = ReadClipboardCell(fromPhrase + 2, row);
                if (swap) {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                    if (v == 0x0A)
                        v = 0;
                } else {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                }
                WriteTableCommand(wTableRow, v);
                break;
            case 3:
                if (swap) {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                } else {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                }
                WriteTableValue(wTableRow, ReadClipboardCell(fromPhrase + 3, row));
                break;
            case 4:
                if (swap) {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                } else {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                }
                WriteTableCommand(wTableRow, ReadClipboardCell(4, row));
                break;
            case 5:
                if (swap) {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS1;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES1;
                } else {
                    wTableCellAPointer = SRAM_TABLE_COMMANDS2;
                    wTableCellBPointer = SRAM_TABLE_COMMAND_VALUES2;
                }
                WriteTableValue(wTableRow, ReadClipboardCell(5, row));
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
    UBYTE column, row, left, top, right, bottom;

    if (wClipboardType != SCREEN_TABLE)
        return;
    left = wSelectionLeft;
    right = wTableColumn;
    SortBytePair(&left, &right);
    right = (char)right + 1;        /* the char casts read through c and b, */
    top = wSelectionTop;
    bottom = (char)wTableRow;       /* as if the coordinates were chars */
    SortBytePair(&top, &bottom);
    bottom = (char)bottom + 1;
    /* the ring around the rectangle back to the column palettes... */
    for (column = left ? left - 1 : 0; column < (right == 6 ? 6 : right + 1); column++) {
        for (row = top ? top - 1 : 0; row < (bottom == 0x10 ? 0x10 : bottom + 1); row = row + 1) {
            if (column < left || column >= right || row < top || row >= bottom)
                ColorField(TableColumnX[column], row + 2, TableColumnWidth[column], column ? 1 - (column & 1) : 0);
        }
    }
    /* ...and the rectangle highlighted */
    for (column = left; column < right; column++)
        for (row = top; row < bottom; row = row + 1)
            ColorField(TableColumnX[column], row + 2, TableColumnWidth[column], hGameboyType ? 3 : 2);
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
    wSavedClipboardType = wClipboardType;
    wSavedSelectionLeft = wSelectionLeft;
    wSavedSelectionTop = wSelectionTop;
    wSavedSelectionRight = wSelectionRight;
    wSavedSelectionBottom = wSelectionBottom;
    wCursorMode = CURSOR_SELECTING;
    wClipboardType = wCurrentScreen;
}

void RestoreSelectionState(void)
{
    wCursorMode = wSavedCursorMode;
    wClipboardType = wSavedClipboardType;
    wSelectionLeft = wSavedSelectionLeft;
    wSelectionTop = wSavedSelectionTop;
    wSelectionRight = wSavedSelectionRight;
    wSelectionBottom = wSavedSelectionBottom;
}
