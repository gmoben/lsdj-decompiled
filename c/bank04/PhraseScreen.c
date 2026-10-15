/*
 * The phrase screen -- LSDj 9.4.0, ROM bank $04.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern char wPhraseColumn;                     /* $C415, a plain char */
extern UBYTE wPhraseColumnLast[];              /* $C907, [4] */
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wColumnInstrument[];              /* $C468, [4] last instrument per channel */
extern UBYTE wInstrumentIsKit;                   /* $C90B */
UBYTE AllocateNextFreeInstrument(void);             /* bank $1F */
void CopyTableData(UBYTE from, UBYTE to);   /* bank 1 */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE wPhraseRowFlagsB[];            /* $C91D, [16] */
extern UBYTE wRowBufferColumn2[];                 /* $C48D, [16] the phrase commands */
extern UBYTE wRowBufferColumn3[];                 /* $C49D, [16] their values */
extern UBYTE PhraseColumnX0[];                 /* $04:$429E */
extern UBYTE PhraseColumnX1[];                 /* $04:$42A3 */

void DrawNoteName(void);                   /* bank 6 */
void DrawChainPosition(void);                    /* bank 6 */
void DrawWordRow(UBYTE n);                  /* bank $1F */
void DrawAllChannelStatus(void);               /* bank $1F */
void DrawScreenColumnLabels(void);                   /* bank $1E */
void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0 */
UBYTE KitIndexToBank(UBYTE kit);            /* bank 0, asm */
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0, asm */
void DrawPhraseCursor(void);                /* bank $1F */
void DrawPhraseSelection(void);            /* bank $1F */
void CopyInstrumentName(void);                   /* bank 1 */
void WriteInstrumentName(void);                  /* bank 1 */
UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row);
UBYTE DrawInstrumentName(UBYTE row, UBYTE instrument);
extern UBYTE wAPressEnabled;                /* $C409 */
UBYTE PhraseCursorOnEditCell(void);
void CloneInstrumentAtCursor(void);
UBYTE CloneTableAtCursor(void);
UBYTE ReadPhraseCommand(UBYTE phrase, UBYTE row);
UBYTE FindCursorInstrument(void);
extern UBYTE wPrelisten;                    /* $C56E */
extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UBYTE wSavedLiveMode;                /* $C572 */
extern UBYTE wLiveMode;                     /* $C528 */
void SoundTogglePlayback(void);                    /* bank 2 */
void SyncSetSendMode(void);                 /* bank 0 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wNameEditBuffer[];                /* $C431, [5] */
extern UBYTE wInstrumentRemapDirty;              /* $C9B7 */
extern UBYTE wInstrumentRemapTable[];            /* $C9B8, [16] */
void ClearMessageBar(void);                 /* bank 1 */
UBYTE CommitPhraseInstrument(void);
extern UBYTE wPhraseInstrumentCache[];           /* $C90A */
extern UBYTE wKeyboardHeldNote;                  /* $CB7E */
extern UBYTE wTable;                        /* $C41B */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wMuteSoloLatch;                /* $C56A */
extern UBYTE wPendingCommands[];                /* $C457, [16] */
extern UBYTE wPendingCommandTarget;             /* $C467 */
extern UBYTE wColumnLastCommand[];                 /* $C44B, [4] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wPhraseNoteIsKitA;             /* $C92D: the pasted note's instrument is a kit (column 0) */
extern UBYTE wPhraseNoteIsKitB;             /* $C92E: (column 1) */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE CommandPrelistenFlags[];            /* $04:$57C0, [20] */
extern UBYTE PhraseStepOnes[];              /* $04:$54E3, [4][20] */
void ShowCommandHelp(void);                 /* bank 1 */
UBYTE PhraseCellFlagB(UBYTE row);
UBYTE PhraseCellFlagD(UBYTE row);
void ErasePhraseCursor(void);          /* bank $1F */
void HasEnterableColumnRight(void);            /* bank $1F */
void HasEnterableColumnLeft(void);             /* bank $1F */
void CutCopyPhraseSelection(void);          /* bank $1F */
void MoveMapCursorLeft(void);                     /* $05:$4233 */
void MoveMapCursorRight(void);                    /* $05:$4253 */
void MoveMapCursorUp(void);                       /* $05:$426A */
void MoveMapCursorDown(void);                     /* $05:$4287 */
void SoundToggleSongPlayback(void);              /* $02:$78C4 */
void SaveSelectionState(void);              /* $05:$743F */

void DrawNoteNameFar(UBYTE x)
{
    FarCallArgument2V(6, DrawNoteName, wSongColumn, x);
}

void DrawPhraseCursorFar(void)
{
    FarCall(0x1F, FAR_FN(DrawPhraseCursor));
}

void DrawPhraseCommandLetter(UBYTE command)
{
    UBYTE c, ok;

    ok = command <= COMMAND_Z;
    if (ok)
        c = CommandMnemonics[command];
    else
        c = 0x3F;
    DrawStringIndexed(c);
}

UBYTE PhraseModeTile(void)
{
    return wSongColumn == CHANNEL_NOI ? 0x78 : 0x6C;
}

UBYTE ReadCursorInstrument(void)
{
    UBYTE row;

    row = wPhraseRow;
    return ReadPhraseInstrument(wPhrase, row);
}

UBYTE FindCursorInstrument(void)
{
    UBYTE row, v;

    for (row = wPhraseRow; row != 0xFF; row--) {
        v = ReadPhraseInstrument(wPhrase, row);
        if (v <= 0x40)
            return v;
    }
    return 0xFF;
}

void ResolvePhraseCursorNote(UBYTE v)
{
    UBYTE instrument;

    wPhraseColumnLast[wSongColumn] = v;
    if (wSongColumn == CHANNEL_NOI && wCursorMode != CURSOR_SELECTING) {
        instrument = FindCursorInstrument();
        if (instrument < INSTRUMENTS
                && ReadInstrumentByte(instrument, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_NOISE)
            WriteInstrumentByte(instrument, NOISE_BYTE_LAST_NOTE, v);
    }
}

void SetColumnInstrument(UBYTE instrument)
{
    if (wSongColumn >= 4)
        DebugTrap();
    if (instrument <= INSTRUMENT_SPEECH) {
        wColumnInstrument[wSongColumn] = instrument;
        if (wSongColumn == CHANNEL_WAV) {
            wInstrumentIsKit = instrument != INSTRUMENT_SPEECH;
            if (wInstrumentIsKit)
                wInstrumentIsKit
                    = ReadInstrumentByte(instrument, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
        }
        WriteSongByte(1, (UWORD)wSongColumn + (UWORD)SRAM_PROPERTY_COLUMN_INSTRUMENTS, instrument);
    }
}

void WritePhraseNote(UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == EMPTY)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    WriteSongByte(0, (UBYTE *)p, v);
}

void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instrument)
{
    UBYTE *p;

    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (instrument > INSTRUMENT_SPEECH && instrument != EMPTY)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    WriteSongByte(3, p + row, instrument);
}

void DrawPhraseCursorGlyph(UBYTE row)
{
    UBYTE y;
    UBYTE *tbl;

    y = row + 2;
    tbl = wPhraseDrawMode ? PhraseColumnX1 : PhraseColumnX0;
    SetVramCursor(tbl[wPhraseColumn], y);
}

UBYTE CommitPhraseInstrument(void)
{
    UBYTE instrument;
    UBYTE result;
    UBYTE *p;
    UBYTE letter, v, n;

    instrument = ReadCursorInstrument();
    result = 0;
    if (instrument > INSTRUMENT_SPEECH) {
        if (wColumnInstrument[wSongColumn] == 0xFF) {
            SetColumnInstrument(FarCallByte(0x1F, FAR_FN(AllocateNextFreeInstrument)));
            wDrawStep++;
        }
        WritePhraseInstrument(wPhrase, (char)wPhraseRow, wColumnInstrument[wSongColumn]);
        if (wSongColumn == CHANNEL_NOI) {
            n = wColumnInstrument[3];
            if (n < 0x40) {
                n = ReadInstrumentByte(n, NOISE_BYTE_LAST_NOTE);
                if (n > 0x78)
                    n = 0x78;
                WritePhraseNote((char)wPhraseRow, n);
            }
        }
        result = 1;
    } else {
        SetColumnInstrument(instrument);
        if (wPhraseColumn == PHRASE_COLUMN_INSTRUMENT) {
            FarCallArgument(1, FAR_FN(CopyInstrumentName), instrument);
            for (letter = 0; letter < 5; letter++) {
                p = &wSampleNoteString[letter];
                v = wNameEditBuffer[letter];
                if (!v)
                    v = ' ';
                *p = v;
            }
            wSampleNoteString[5] = ' ';
            FarCall(1, FAR_FN(ClearMessageBar));
            DrawPhraseCursorGlyph((char)wPhraseRow);
        }
    }
    return result;
}

void EditPhraseCellUp(UBYTE row)
{
    UBYTE *p;
    UBYTE step;
    UBYTE full;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    step = 12;
    full = 0;
    if (wPhrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (wSongColumn == CHANNEL_NOI) {
        switch (v) {
        case 0x3A:
        case 0x3B:
        case 0x76:
        case 0x77:
        case 0x78:
            step = 1;
            break;
        case 0x38:
        case 0x39:
        case 0x74:
        case 0x75:
            step = 2;
            break;
        case 0x36:
        case 0x37:
        case 0x72:
        case 0x73:
            step = 3;
            break;
        default:
            step = 4;
            break;
        }
    }
    if (!v) {
        if (wCursorMode == CURSOR_SELECTING)
            return;
        v = wPhraseColumnLast[wSongColumn];
        full = CommitPhraseInstrument();
    } else {
        v += step;
        if (wCursorMode == CURSOR_SELECTING || wSongColumn == CHANNEL_NOI) {
            while (PhraseModeTile() < v || !v)
                v -= PhraseModeTile();
        } else {
            while (PhraseModeTile() < v || v < step)
                v -= step;
        }
    }
    ResolvePhraseCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawNoteNameFar(v);
}

void EditPhraseCellDown(UBYTE row)
{
    UBYTE *p;
    UBYTE step;
    UBYTE full;
    UBYTE v;
    UBYTE kitcol;
    UBYTE t, n;

    p = (UBYTE *)(row + (UWORD)0xA000);
    step = 12;
    full = 0;
    if (wPhrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (!v) {
        if (wCursorMode == CURSOR_SELECTING)
            return;
        v = wPhraseColumnLast[wSongColumn];
        full = CommitPhraseInstrument();
    } else {
        kitcol = wSongColumn == CHANNEL_NOI;
        if (kitcol) {
            if (v > 0x3C)
                t = v + 0xC4;
            else
                t = v;
            switch (t) {
            case 3:
            case 0x39:
            case 0x3F:
                step = 3;
                break;
            case 2:
            case 0x3A:
            case 0x3E:
                step = 2;
                break;
            case 1:
            case 0x3B:
            case 0x3C:
            case 0x3D:
                step = 1;
                break;
            default:
                step = 4;
                break;
            }
        }
        v -= step;
        if (wCursorMode == CURSOR_SELECTING || kitcol) {
            while (PhraseModeTile() < v || !v)
                v += PhraseModeTile();
        } else {
            n = -step;
            while (n < v || !v)
                v += step;
        }
    }
    ResolvePhraseCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawNoteNameFar(v);
}

void EditPhraseCellRight(UBYTE row)
{
    UBYTE *p;
    UBYTE full;
    UWORD address;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    full = 0;
    if (wPhrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    address = (UWORD)p;
    v = ReadSongByte(0, address);
    if (!v) {
        if (wCursorMode == CURSOR_SELECTING)
            return;
        row = wPhraseColumnLast[wSongColumn];
        full = CommitPhraseInstrument();
    } else {
        if (v == PhraseModeTile() && wCursorMode != CURSOR_SELECTING && wSongColumn != CHANNEL_NOI)
            return;
        row = v + 1;
    }
    if (PhraseModeTile() < row || !row)
        row = 1;
    ResolvePhraseCursorNote(row);
    WriteSongByte(0, address, row);
    if (full)
        DrawPhraseScreen();
    else
        DrawNoteNameFar(row);
}

void EditPhraseCellLeft(UBYTE row)
{
    UBYTE *p;
    UBYTE full;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    full = 0;
    if (wPhrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (!v) {
        if (wCursorMode == CURSOR_SELECTING)
            return;
        v = wPhraseColumnLast[wSongColumn];
        full = CommitPhraseInstrument();
    } else {
        if (v == 1 && wCursorMode != CURSOR_SELECTING && wSongColumn != CHANNEL_NOI)
            return;
        v--;
    }
    if (!v || PhraseModeTile() < v)
        v = PhraseModeTile();
    ResolvePhraseCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawNoteNameFar(v);
}

UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row)
{
    UBYTE *p;

    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    return ReadSongByte(3, p + (unsigned int)row);
}

UBYTE ReadPhraseNote(UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == EMPTY)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    return ReadSongByte(0, (UBYTE *)p);
}

void WritePhraseCommand(UBYTE phrase, UBYTE row, UBYTE command)
{
    UWORD p;

    p = (UWORD)(SramPhraseCommands + (unsigned int)row);
    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (command > COMMAND_Z)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, command);
}

void WritePhraseCommandValue(UBYTE phrase, UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseCommandValues + (unsigned int)row);
    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, v);
}

UBYTE ReadPhraseCommandValue(UBYTE phrase, UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseCommandValues + (unsigned int)row);
    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    return ReadSongByte(2, (UBYTE *)p);
}

UBYTE ReadPhraseCommand(UBYTE phrase, UBYTE row)
{
    UWORD p;
    UBYTE v, ok;

    p = (UWORD)(SramPhraseCommands + (unsigned int)row);
    if (phrase == EMPTY)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    v = ReadSongByte(2, (UBYTE *)p);
    ok = v <= 0x13;
    if (!ok)
        v = 0;
    return v;
}

/* the caller passes the phrase and row; the body reads wPhrase/wPhraseRow */

UBYTE PlacePhraseCell(UBYTE phrase, UBYTE row)
{
    UBYTE *p;
    UBYTE v;

    p = (UBYTE *)(wPhraseRow + (UWORD)0xA000);
    if (wPhraseRow >= 0x10)
        DebugTrap();
    if (wPhrase == EMPTY)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (v || wCursorMode == CURSOR_SELECTING) {
        ResolvePhraseCursorNote(v);
        return 0;
    }
    WriteSongByte(0, p, wPhraseColumnLast[wSongColumn]);
    return 1;
}

void DrawEmptyNote(void)
{
    DrawString("---");
}

void LoadPhraseRow(void)
{
    volatile UWORD p;
    UWORD off;

    off = wPhrase;
    off <<= 4;
    p = (UWORD)0xA000 + off;
    ReadSongBytes(0, p, wSongReadBuffer);
    ReadSongBytes(3, (UWORD)SRAM_PHRASE_INSTRUMENTS + off, (UBYTE *)ROW_BUFFER_COLUMN(1));
    ReadSongBytes(2, p, (UBYTE *)ROW_BUFFER_COLUMN(2));
    ReadSongBytes(2, (UWORD)SRAM_PHRASE_COMMAND_VALUES + off, (UBYTE *)ROW_BUFFER_COLUMN(3));
}

void DrawPhraseSelectionFar(void)
{
    FarCall(0x1F, FAR_FN(DrawPhraseSelection));
}

UBYTE PhraseRowIsSentinel(UBYTE i)
{
    UBYTE v;

    v = wPhraseRowFlags[i];
    return v > 0xFD;
}

void DrawNoteCell(void)
{
    DrawString(wSampleNoteString[0] ? wSampleNoteString : "OFF");
}

/* CGB attribute rows for DrawPhraseScreen; SDCC places them right after its
   code ($04:$50D8), ahead of its string literals */
const UBYTE PhraseRowAttributes0[13] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
const UBYTE PhraseRowAttributes1[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
const UBYTE PhraseRowAttributes2[13] = { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };

/*
 * Screen 1, the phrase grid. A phrase that plays a kit on any step
 * (wPhraseDrawMode) gets the two-sample layout: each step's instrument
 * carries forward to the following empty steps, and steps under a kit show
 * the left and right sample names (their kit banks recorded in
 * wPhraseRowFlags / wPhraseRowFlagsB) instead of a note. Then the
 * instrument, command letter and value columns, the CGB row attributes, and
 * the cursor, channel status and column labels.
 */
void DrawPhraseScreen(void)
{
    UBYTE row;
    UBYTE t;
    UBYTE current;
    UBYTE instrument;
    UBYTE note;
    UWORD address;
    UBYTE *p;

    if (wPhrase == EMPTY)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    FarCall(6, DrawChainPosition);
    DrawTempoField();
    LoadPhraseRow();
    wPhraseDrawMode = 0;
    for (row = 0; row < 0x10; row++) {
        t = wUsedTableMap[row];
        if (t < 0x40 && ReadSongByte(1, ((UWORD)t << 4) + (UWORD)SRAM_INSTRUMENTS) == 2) {
            wPhraseDrawMode = 1;
            break;
        }
    }
    current = 0xFF;
    for (row = 0; row < 0x10; row++) {
        instrument = wUsedTableMap[row];
        switch (instrument) {
        case 0x40:
            current = 0xFF;
            wPhraseRowFlags[row] = 0xFE;
            break;
        case 0xFF:
            wPhraseRowFlags[row] = current;
            break;
        default:
            if (ReadSongByte(1, ((UWORD)instrument << 4) + (UWORD)SRAM_INSTRUMENTS) == 2) {
                current = instrument;
                wPhraseRowFlags[row] = instrument;
            } else {
                current = 0xFF;
                wPhraseRowFlags[row] = 0xFF;
            }
        }
        hBgDigitPalette = 4;
        hBgPalette = 1;
        SetVramCursor(2, row + 2);
        if (current != 0xFF) {
            note = wSongReadBuffer[row];
            address = ((UWORD)current << 4) + (UWORD)0xB082;
            t = ReadSongByte(1, address);
            t = KitIndexToBank(t & 0x3F);
            wPhraseRowFlags[row] = t;
            hBgDigitPalette = wShadedDigitPalette;
            hBgPalette = wShadedPalette;
            if (!(note & 0xF0)) {
                DrawEmptyNote();
            } else {
                ReadKitSampleName(t, note >> 4);
                DrawNoteCell();
            }
            hBgDigitPalette = 4;
            hBgPalette = 1;
            address = ((UWORD)current << 4) + (UWORD)0xB089;
            t = ReadSongByte(1, address);
            t = KitIndexToBank((char)t & 0x3F);
            wPhraseRowFlagsB[row] = t;
            if (!(note & 0x0F)) {
                DrawEmptyNote();
            } else {
                ReadKitSampleName(t, note & 0x0F);
                DrawNoteCell();
            }
            DrawStringTile20();
        } else {
            if (wPhraseDrawMode)
                DrawString("   ");
            if (instrument == INSTRUMENT_SPEECH) {
                t = wSongReadBuffer[row];
                FarCallArgument(0x1F, FAR_FN(DrawWordRow), t);
            } else {
                t = wSongReadBuffer[row];
                DrawNoteNameFar(t);
            }
            DrawStringTile20();
        }
        if (hGameboyType) {
            p = wPhraseDrawMode ? (PhraseRowIsSentinel(row) ? PhraseRowAttributes1 : PhraseRowAttributes0) : PhraseRowAttributes2;
            VBK_REG = 1;
            set_bkg_tiles(2, row + 2, 13, 1, p);
            VBK_REG = 0;
        }
        if (instrument == EMPTY)
            DrawString("--");
        else
            DrawHexByte(instrument);
        DrawStringTile20();
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        t = wRowBufferColumn2[row];
        DrawPhraseCommandLetter(t);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawFieldValue(t, wRowBufferColumn3[row]);
        if (!wPhraseDrawMode)
            DrawString("   ");
    }
    if (!wPhraseColumn && PhraseRowIsSentinel((char)wPhraseRow))
        wPhraseColumn++;
    if (wCursorMode == CURSOR_SELECTING)
        DrawPhraseSelectionFar();
    else
        DrawPhraseCursorFar();
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    FarCall(0x1E, FAR_FN(DrawScreenColumnLabels));
}

UBYTE CursorInstrumentIsKit(void)
{
    UBYTE v, r;

    v = FindCursorInstrument();
    if (v < 0x40)
        r = ReadInstrumentByte(v, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
    else
        r = 0;
    return r;
}

UBYTE CheckPhraseEditContext(UBYTE x)
{
    if (wSongColumn != CHANNEL_WAV)
        return 0;
    if (wPhraseColumn > PHRASE_COLUMN_NOTE)
        return 0;
    if (wSelectionLeft > PHRASE_COLUMN_NOTE)
        return 0;
    if (!CursorInstrumentIsKit())
        return 0;
    if (wPhraseColumn == PHRASE_COLUMN_LEFT_NOTE)
        return wSelectionLeft == PHRASE_COLUMN_NOTE;
    return ReadClipboardCell(1, x) == 0xFF;
}

void PastePhraseSelection(void)
{
    char column;
    UBYTE row;
    UBYTE savedLeft, savedRight;
    char shift;
    UBYTE v;
    UBYTE context;
    UBYTE c4, c3;

    savedLeft = wSelectionLeft;
    savedRight = wSelectionRight;
    shift = 0;
    if (wClipboardType == SCREEN_TABLE) {
        switch (wSelectionRight - wSelectionLeft) {
        default:
            return;
        case 2:
            switch (wSelectionLeft) {
            case PHRASE_COLUMN_INSTRUMENT:
                shift = -1;
                break;
            case PHRASE_COLUMN_VALUE:
                shift = 1;
                break;
            default:
                return;
            }
            wSelectionLeft = 3;
            wSelectionRight = 5;
        }
    }
    row = wSelectionTop;
    c4 = shift + 4;
    c3 = shift + 3;
    for (; row < wSelectionBottom; row++) {
        for (column = wSelectionRight - 1; !(column < (char)wSelectionLeft); column--) {
            context = CheckPhraseEditContext(row);
            if (context) {
                if (column < 2)
                    column ^= 1;
            }
            switch (column) {
            case 0:
                wPhraseNoteIsKitA = CursorInstrumentIsKit();
                if (wPhraseNoteIsKitA) {
                    v = ReadClipboardCell(context, row);
                    if (v == 0xFF)
                        break;
                    v <<= 4;
                    v = (ReadPhraseNote(wPhraseRow) & 0x0F) + v;
                    WritePhraseNote(wPhraseRow, v);
                }
                break;
            case 1:
                v = ReadClipboardCell(context ^ 1, row);
                if (!(wSongColumn == CHANNEL_NOI)) {
                    if (v == 0xFF)
                        break;
                }
                wPhraseNoteIsKitB = CursorInstrumentIsKit();
                if (wPhraseNoteIsKitB)
                    v = (ReadPhraseNote(wPhraseRow) & 0xF0) + v;
                WritePhraseNote(wPhraseRow, v);
                break;
            case 2:
                WritePhraseInstrument(wPhrase, wPhraseRow, ReadClipboardCell(2, row));
                break;
            case 3:
                WritePhraseCommand(wPhrase, wPhraseRow, ReadClipboardCell(c3, row));
                break;
            case 4:
                WritePhraseCommandValue(wPhrase, wPhraseRow, ReadClipboardCell(c4, row));
                break;
            }
            if (context) {
                if (column < 2)
                    column ^= 1;
            }
        }
        wPhraseRow++;
        if ((char)wPhraseRow == 0x10)
            wPhraseRow = 0;
    }
    if (shift) {
        wSelectionLeft = savedLeft;
        wSelectionRight = savedRight;
    }
    if (wSelectionBottom - wSelectionTop > 1)
        ShowMessage("PASTE");
}

void CloneInstrument(UBYTE source, UBYTE destination)
{
    UBYTE field;

    for (field = 0; field < 0x10; field++)
        WriteInstrumentByte(destination, field, ReadInstrumentByte(source, field));
    FarCallArgument(1, FAR_FN(CopyInstrumentName), source);
    FarCallArgument(1, FAR_FN(WriteInstrumentName), destination);
}

void CloneInstrumentAtCursor(void)
{
    UBYTE instrument, bad, n;

    instrument = ReadCursorInstrument();
    bad = !(instrument < INSTRUMENTS);
    FarCall(5, FAR_FN(RestoreSelectionState));
    if (bad) {
        strcpy(wStringBuffer, "CANNOT CLONE");
        FarCall(1, DrawMessageBar);
        return;
    }
    n = FarCallByte(0x1F, FAR_FN(AllocateNextFreeInstrument));
    if (n == 0xFF)
        return;
    CloneInstrument(instrument, n);
    SetColumnInstrument(n);
    WritePhraseInstrument(wPhrase, (char)wPhraseRow, n);
    DrawPhraseScreen();
    ShowMessage("CLONED INSTR");
}

UBYTE CloneTableAtCursor(void)
{
    UBYTE v, ok, t;

    v = ReadPhraseCommandValue(wPhrase, (char)wPhraseRow);
    ok = v < 0x20;
    FarCall(5, FAR_FN(RestoreSelectionState));
    if (!ok)
        return 0;
    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return 0;
    FarCallArgument2V(1, FAR_FN(CopyTableData), v, t);
    WritePhraseCommandValue(wPhrase, (char)wPhraseRow, t);
    DrawPhraseScreen();
    return 1;
}

UBYTE CloneAtPhraseCursor(void)
{
    UBYTE row;

    if (wAPressEnabled || PhraseCursorOnEditCell())
        return 0;
    if (wPhraseColumn == PHRASE_COLUMN_INSTRUMENT) {
        CloneInstrumentAtCursor();
        return 1;
    }
    if (wPhraseColumn == PHRASE_COLUMN_VALUE) {
        row = wPhraseRow;
        if (ReadPhraseCommand(wPhrase, row) == COMMAND_A) {
            if (CloneTableAtCursor())
                ShowMessage("CLONED TABLE");
            return 1;
        }
    }
    return 0;
}

void PrelistenCursorRow(void)
{
    UBYTE row, note;

    if (!wPrelisten)
        return;
    if (wCursorMode == CURSOR_SELECTING)
        return;
    row = wPhraseRow;
    note = ReadPhraseNote(row);
    if (!note)
        return;
    if (wLiveStartColumn != 0xFF) {
        FarCall(2, FAR_FN(SoundTogglePlayback));
        wLiveMode = wSavedLiveMode;
    }
    if (wPlayMode)
        return;
    if (wSyncMode != SYNC_OFF && wSyncMode != SYNC_LSDJ && wSyncMode != SYNC_KEYBOARD)
        return;
    wSavedLiveMode = wLiveMode;
    wLiveMode = 0;
    wLiveStartColumn = wSongColumn;
    if (wSyncMode == SYNC_LSDJ)
        SyncSetSendMode();
    FarCall(2, FAR_FN(SoundTogglePlayback));
}

UBYTE DrawInstrumentName(UBYTE row, UBYTE instrument)
{
    UBYTE result;
    UBYTE *p;
    UBYTE letter, v, n;

    result = 0;
    FarCallArgument(1, FAR_FN(CopyInstrumentName), instrument);
    for (letter = 0; letter < 5; letter++) {
        p = &wSampleNoteString[letter];
        v = wNameEditBuffer[letter];
        if (!v)
            v = ' ';
        *p = v;
    }
    wSampleNoteString[5] = ' ';
    FarCall(1, FAR_FN(ClearMessageBar));
    DrawPhraseCursorGlyph(row);
    SetColumnInstrument(instrument);
    WritePhraseInstrument(wPhrase, row, instrument);
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    DrawHexByte(instrument);
    if (wSongColumn == CHANNEL_NOI && instrument < INSTRUMENTS
            && ReadInstrumentByte(instrument, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_NOISE) {
        n = ReadInstrumentByte(instrument, NOISE_BYTE_LAST_NOTE);
        *(UBYTE *)0xC90A = n;               /* wPhraseInstrumentCache, stored through the literal */
        WritePhraseNote(row, n);
        result = 1;
    }
    PrelistenCursorRow();
    wInstrumentRemapDirty = 1;
    wInstrumentRemapTable[row] = instrument;
    return result;
}

UBYTE PhraseCellFlagA(UBYTE row)
{
    UBYTE r, kit, i, k2;

    i = ReadPhraseInstrument(wPhrase, row);
    r = 0;
    kit = 0;
    if (i == 0xFF)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
    } else {
        i = 0x40;
        r = 1;
    }
    if (i)
        i--;
    k2 = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE);
    k2 = k2 == 2;
    r |= k2;
    r |= !(kit == k2);
    r |= DrawInstrumentName(row, i);
    return r;
}

/* As PhraseCellFlagA for the instrument after this one. r is only ever
   assigned in the branches, and the ROM keeps it in its slot for every
   access (`volatile`); as a plain local SDCC folds `r = k2` into the next
   `|=`, starts r there and spills the instrument instead. */
UBYTE PhraseCellFlagB(UBYTE row)
{
    volatile UBYTE r;
    UBYTE kit, i, k2;

    i = ReadPhraseInstrument(wPhrase, row);
    kit = 0;
    if (i == 0xFF)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
        i++;
    } else
        i = 0x40;
    if (i < 0x40) {
        k2 = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE);
        k2 = k2 == 2;
        r = k2;
        r |= !(kit == k2);
    } else
        r = 1;
    r |= DrawInstrumentName(row, i);
    return r;
}

UBYTE PhraseCellFlagC(UBYTE row)
{
    UBYTE r, kit, i, k2;

    i = ReadPhraseInstrument(wPhrase, row);
    r = 0;
    kit = 0;
    if (i == 0xFF || i < 0x10)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
        i -= 0x10;
    } else {
        i = 0x30;
        r = 1;
    }
    k2 = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE);
    k2 = k2 == 2;
    r |= k2;
    r |= !(kit == k2);
    r |= DrawInstrumentName(row, i);
    return r;
}

/* As PhraseCellFlagB for the instrument 0x10 further on. */
UBYTE PhraseCellFlagD(UBYTE row)
{
    volatile UBYTE r;
    UBYTE kit, i, k2;

    i = ReadPhraseInstrument(wPhrase, row);
    if (i > 0x30)
        return 0;
    kit = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
    i += 0x10;
    if (i < 0x40) {
        k2 = ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE);
        k2 = k2 == 2;
        r = k2;
        r |= !(kit == k2);
    } else
        r = 1;
    r |= DrawInstrumentName(row, i);
    return r;
}

UBYTE PrelistenEditedCell(void)
{
    UBYTE i;

    if (wSongColumn != CHANNEL_WAV)
        return 0;
    i = ReadCursorInstrument();
    if (i > 0x40)
        return wInstrumentIsKit;
    if (i == 0x40)
        return 0;
    return ReadInstrumentByte(i, INSTRUMENT_BYTE_TYPE) == INSTRUMENT_KIT;
}

/* $5BA6: the phrase screen's input, an if chain on wButtons: A places the
   column's last note/instrument/command (a held keyboard note first; a kit
   note per sample nibble), a second A opens a new instrument or table or
   the command's help; the directions move the cursor, UP/DOWN running on
   into the chain's previous/next phrase; A+RIGHT/LEFT and A+UP/DOWN step the
   cell by 1 and by an octave/16 (kit samples skip empty slots, command
   letters those the channel lacks, values clamp per command: A G O S W)
   over the cursor row or the selection; B+LEFT/RIGHT change channel,
   B+UP/DOWN the chain row; A+B cuts the cell; select+A pastes (in a
   selection: clones the instrument or A command's table under the cursor,
   else cuts), select+(B,A) clones; start and select+start play;
   select+directions move on the screen map, DOWN on a G command opening
   its groove and RIGHT on an A command its table; B copies a selection;
   select+B selects, then widens to the screen. 1 when taken */

UBYTE PhraseScreenInput(void)
{
    UBYTE v, row, rowEnd, field, redraw, keyNote, placed, mask, oldPhrase;
    UBYTE instrument, left, right;
    UBYTE table;
    char chainRow;

    redraw = 0;
    if (wPhrase == EMPTY)
        DebugTrap();
    if ((UBYTE)(char)wPhraseRow >= 0x10)
        DebugTrap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawPhraseCursorGlyph((char)wPhraseRow);
    if (wButtons != J_A)
        wDrawStep = 0;
    if (wCursorMode == CURSOR_SELECTING && wButtons == J_SELECT_TAP) {
        wPhraseColumn = wSelectionLeft;
        wPhraseRow = wSelectionTop;
        FarCall(5, 0x747D);
        DrawPhraseScreen();
        ShowMessage("CANCEL SELECT");
        return 1;
    }
    if (wAPressEnabled && wButtons == J_A) {
        keyNote = wKeyboardHeldNote;
        placed = 1;
        if (keyNote) {
            wPhraseColumn = PHRASE_COLUMN_NOTE;
            if (wSongColumn == CHANNEL_NOI) {
                mask = wPhraseInstrumentCache[0];
                if (keyNote & 0xF0)
                    mask &= 0x0F;
                else
                    mask &= 0xF0;
                keyNote |= mask;
            } else if (!PhraseRowIsSentinel(wPhraseRow) && (keyNote & 0xF0)) {
                keyNote >>= 4;
                wPhraseColumn = PHRASE_COLUMN_LEFT_NOTE;
            }
            WritePhraseNote((char)wPhraseRow, 0);
            wPhraseColumnLast[wSongColumn] = keyNote;
        }
        wPendingCommandTarget = 0;
        if (wCursorMode == CURSOR_SELECTING)
            return 0;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        switch (wPhraseColumn) {
        case PHRASE_COLUMN_LEFT_NOTE:
        case PHRASE_COLUMN_NOTE:
            if (!PrelistenEditedCell())
                placed = PlacePhraseCell(wPhrase, wPhraseRow);
            else {
                v = ReadPhraseNote((char)wPhraseRow);
                if (wPhraseColumn == PHRASE_COLUMN_LEFT_NOTE) {
                    if (!(v & 0xF0)) {
                        v += wPhraseColumnLast[wSongColumn] << 4;
                        if (!(v & 0xF0))
                            v += 0x10;
                        WritePhraseNote((char)wPhraseRow, v);
                    } else
                        wPhraseColumnLast[wSongColumn] = v >> 4;
                } else if (!(v & 0x0F)) {
                    v += wPhraseColumnLast[wSongColumn] & 0x0F;
                    if (!(v & 0x0F))
                        ++v;
                    WritePhraseNote((char)wPhraseRow, v);
                } else
                    wPhraseColumnLast[wSongColumn] = v & 0x0F;
            }
            hBgDigitPalette = 4;
            hBgPalette = 1;
            redraw = 1;
        case PHRASE_COLUMN_INSTRUMENT:
            if (wPhraseColumn == PHRASE_COLUMN_INSTRUMENT && wDrawStep++ == 1) {
                SetColumnInstrument(FarCallByte(0x1F, FAR_FN(AllocateNextFreeInstrument)));
                redraw = 1;
            } else if (placed)
                redraw |= CommitPhraseInstrument();
            else
                SetColumnInstrument(ReadCursorInstrument());
            PrelistenCursorRow();
            ++redraw;
            break;
        case PHRASE_COLUMN_COMMAND:
            v = ReadPhraseCommand(wPhrase, (char)wPhraseRow);
            if (!v) {
                v = wColumnLastCommand[wSongColumn];
                if (!v)
                    DebugTrap();
            } else if (wDrawStep++)
                FarCallArgument(1, FAR_FN(ShowCommandHelp), v);
            wPendingCommands[wPhraseRow] = v;
            DrawPhraseCommandLetter(v);
            hBgDigitPalette = 4;
            hBgPalette = 1;
            DrawFieldValue(v, ReadPhraseCommandValue(wPhrase, (char)wPhraseRow));
            break;
        case PHRASE_COLUMN_VALUE:
            if (wDrawStep++ == 1 && ReadPhraseCommand(wPhrase, (char)wPhraseRow) == COMMAND_A) {
                v = FarCallByte(0x1F, FAR_FN(FindFreeTable));
                if (v != 0xFF) {
                    WritePhraseCommandValue(wPhrase, (char)wPhraseRow, v);
                    DrawHexByte(v);
                }
            }
            if (CommandPrelistenFlags[ReadPhraseCommand(wPhrase, (char)wPhraseRow)])
                PrelistenCursorRow();
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == J_RIGHT) {
        if (wCursorMode != CURSOR_SELECTING)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        if (wPhraseColumn == PHRASE_COLUMN_VALUE)
            wPhraseColumn--;
        wPhraseColumn++;
        if (wCursorMode == CURSOR_SELECTING)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == J_LEFT) {
        switch (wPhraseColumn) {
        case PHRASE_COLUMN_LEFT_NOTE:
            return 1;
        case PHRASE_COLUMN_NOTE:
            if (PhraseRowIsSentinel((char)wPhraseRow) && (wCursorMode != CURSOR_SELECTING || !wPhraseDrawMode))
                return 1;
        }
        if (wCursorMode != CURSOR_SELECTING)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseColumn--;
        if (wCursorMode == CURSOR_SELECTING)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == J_UP) {
        if (wCursorMode == CURSOR_SELECTING && !wPhraseRow)
            return 0;
        if (wCursorMode != CURSOR_SELECTING)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseRow--;
        if (!wPhraseColumn && PhraseRowIsSentinel((char)wPhraseRow) && wCursorMode != CURSOR_SELECTING)
            wPhraseColumn++;
        if ((char)wPhraseRow == 0xFF) {
            if (wChainRow) {
                wChainRow--;
                wPhraseRow = 0x0F;
                v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, (char)wChainRow);
                if (v == 0xFF) {
                    wPhraseRow = 0;
                    wChainRow++;
                } else {
                    wPhrase = v;
                    DrawPhraseScreen();
                }
            } else
                wPhraseRow = 0;
        }
        if (wCursorMode == CURSOR_SELECTING)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == J_DOWN) {
        if (wCursorMode == CURSOR_SELECTING && (char)wPhraseRow == 0x0F)
            return 0;
        if (wCursorMode != CURSOR_SELECTING)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseRow++;
        if (!wPhraseColumn && PhraseRowIsSentinel((char)wPhraseRow) && wCursorMode != CURSOR_SELECTING)
            wPhraseColumn++;
        if (wPhraseRow == 0x10) {
            wPhraseRow = 0x0F;
            chainRow = wChainRow;
            if (chainRow != 0x0F) {
                v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, chainRow + 1);
                if (v != 0xFF) {
                    wPhrase = v;
                    wChainRow = chainRow + 1;
                    wPhraseRow = 0;
                    DrawPhraseScreen();
                }
            }
        }
        if (wCursorMode == CURSOR_SELECTING)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == (J_A | J_RIGHT)) {
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            DrawPhraseCursorGlyph(row);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            switch (wPhraseColumn) {
            case PHRASE_COLUMN_LEFT_NOTE:
                v = ReadPhraseNote(row);
                field = v & 0xF0;
                if (field) {
                    field += 0x10;
                    if (!field)
                        field = 0xF0;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    wPhraseColumnLast[wSongColumn] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_NOTE:
                if (PhraseRowIsSentinel(row)) {
                    v = ReadPhraseInstrument(wPhrase, row);
                    if (v == 0x40) {
                        v = ReadPhraseNote(row) + 1;
                        if (v <= 0x0E) {
                            FarCallArgument(0x1F, FAR_FN(DrawWordRow), v);
                            WritePhraseNote(row, v);
                        }
                    } else
                        EditPhraseCellRight(row);
                } else {
                    v = ReadPhraseNote(row);
                    field = v & 0x0F;
                    if (field) {
                        if (++field == 0x10)
                            --field;
                        v &= 0xF0;
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        WritePhraseNote(row, v + field);
                        wPhraseColumnLast[wSongColumn] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_INSTRUMENT:
                redraw |= PhraseCellFlagB(row);
                redraw |= wCursorMode == CURSOR_SELECTING;
                break;
            case PHRASE_COLUMN_COMMAND:
                do {
                    if (!wPendingCommands[row]) {
                        if (!(wPendingCommands[row] = ReadPhraseCommand(wPhrase, row)))
                            break;
                        wPendingCommandTarget = 0;
                    }
                    if (wPendingCommands[row] < 0x13)
                        wPendingCommands[row]++;
                    else
                        wPendingCommands[row] = 0x13;
                } while (!PhraseStepOnes[(UWORD)wSongColumn * 20 + wPendingCommands[row]]);
                if (wPendingCommands[row]) {
                    DrawPhraseCommandLetter(wPendingCommands[row]);
                    if (wMessageStringPointer || wMessageTimer)
                        FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawFieldValue(wPendingCommands[row], ReadPhraseCommandValue(wPhrase, row));
                }
                break;
            case PHRASE_COLUMN_VALUE:
                v = ReadPhraseCommandValue(wPhrase, row);
                field = ReadPhraseCommand(wPhrase, row);
                switch (field) {
                case 0:
                    break;
                case 7:
                    ++v;
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
                    if (wSongColumn == CHANNEL_WAV || v != 3)
                        ++v;
                    break;
                case 1:
                    if (v >= 0x20) {
                        v = 0x20;
                        break;
                    }
                default:
                    ++v;
                }
                WritePhraseCommandValue(wPhrase, row, v);
                DrawFieldValue(field, v);
                if (CommandPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == (J_A | J_UP)) {
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            DrawPhraseCursorGlyph(row);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            switch (wPhraseColumn) {
            case PHRASE_COLUMN_LEFT_NOTE:
                v = ReadPhraseNote(row);
                field = v & 0xF0;
                if (field) {
                    field += 0x20;
                    if (field < 0x20)
                        field = 0xF0;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    wPhraseColumnLast[wSongColumn] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_NOTE:
                if (PhraseRowIsSentinel(row)) {
                    v = ReadPhraseInstrument(wPhrase, row);
                    if (v == 0x40) {
                        v = ReadPhraseNote(row) + 2;
                        if (v > 0x0E)
                            v = 0x0E;
                        FarCallArgument(0x1F, FAR_FN(DrawWordRow), v);
                        WritePhraseNote(row, v);
                    } else
                        EditPhraseCellUp(row);
                } else {
                    v = ReadPhraseNote(row);
                    field = v & 0x0F;
                    if (field) {
                        field += 2;
                        if (field > 0x0F)
                            field = 0x0F;
                        v &= 0xF0;
                        WritePhraseNote(row, v + field);
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        wPhraseColumnLast[wSongColumn] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_INSTRUMENT:
                redraw |= PhraseCellFlagD(row);
                redraw |= wCursorMode == CURSOR_SELECTING;
                break;
            case PHRASE_COLUMN_COMMAND:
                do {
                    if (!wPendingCommands[row]) {
                        if (!(wPendingCommands[row] = ReadPhraseCommand(wPhrase, row)))
                            break;
                        wPendingCommandTarget = 0;
                    }
                    if (wPendingCommands[row] < 0x13)
                        wPendingCommands[row]++;
                    else
                        wPendingCommands[row] = 0x13;
                } while (!PhraseStepOnes[(UWORD)wSongColumn * 20 + wPendingCommands[row]]);
                if (wPendingCommands[row]) {
                    DrawPhraseCommandLetter(wPendingCommands[row]);
                    if (wMessageStringPointer || wMessageTimer)
                        FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawFieldValue(wPendingCommands[row], ReadPhraseCommandValue(wPhrase, row));
                }
                break;
            case PHRASE_COLUMN_VALUE:
                v = ReadPhraseCommandValue(wPhrase, row);
                field = ReadPhraseCommand(wPhrase, row);
                switch (field) {
                case 0:
                    break;
                case 7:
                    v += 0x10;
                    if (v > 0x1F)
                        v = 0x1F;
                    break;
                case 0x12:
                    if (wSongColumn == CHANNEL_WAV)
                        v += 0x10;
                    else if (v != 3)
                        ++v;
                    break;
                case 0x0C:
                    v = 3;
                    break;
                case 0x0F:
                    v += wSongColumn == CHANNEL_NOI ? 4 : 0x10;
                    break;
                case 1:
                    if (v >= 0x10) {
                        v = 0x20;
                        break;
                    }
                default:
                    v += 0x10;
                }
                WritePhraseCommandValue(wPhrase, row, v);
                DrawFieldValue(field, v);
                if (CommandPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == (J_A | J_LEFT)) {
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            DrawPhraseCursorGlyph(row);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            switch (wPhraseColumn) {
            case PHRASE_COLUMN_LEFT_NOTE:
                v = ReadPhraseNote(row);
                field = v & 0xF0;
                if (field) {
                    field -= 0x10;
                    if (!field || field == 0xF0)
                        field = 0x10;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    while (!wSampleNoteString[0]) {
                        --field;
                        WritePhraseNote(row, v + (field << 4));
                        ReadKitSampleName(wPhraseRowFlags[row], field);
                    }
                    wPhraseColumnLast[wSongColumn] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_NOTE:
                if (PhraseRowIsSentinel(row)) {
                    v = ReadPhraseInstrument(wPhrase, row);
                    if (v == 0x40) {
                        v = ReadPhraseNote(row) - 1;
                        if (v) {
                            FarCallArgument(0x1F, FAR_FN(DrawWordRow), v);
                            WritePhraseNote(row, v);
                        }
                    } else
                        EditPhraseCellLeft(row);
                } else {
                    v = ReadPhraseNote(row);
                    field = v & 0x0F;
                    if (field) {
                        --field;
                        if (!field || field == 0xFF)
                            field = 1;
                        v &= 0xF0;
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        while (!wSampleNoteString[0]) {
                            --field;
                            ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        }
                        WritePhraseNote(row, v + field);
                        wPhraseColumnLast[wSongColumn] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_INSTRUMENT:
                redraw |= PhraseCellFlagA(row);
                redraw |= wCursorMode == CURSOR_SELECTING;
                break;
            case PHRASE_COLUMN_COMMAND:
                do {
                    if (!wPendingCommands[row]) {
                        wPendingCommands[row] = ReadPhraseCommand(wPhrase, row);
                        wPendingCommandTarget = 0;
                    }
                    if (wPendingCommands[row] > 1)
                        wPendingCommands[row]--;
                } while (!PhraseStepOnes[(UWORD)wSongColumn * 20 + wPendingCommands[row]]);
                DrawPhraseCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadPhraseCommandValue(wPhrase, row));
                break;
            case PHRASE_COLUMN_VALUE:
                v = ReadPhraseCommandValue(wPhrase, row);
                field = ReadPhraseCommand(wPhrase, row);
                switch (field) {
                case 0:
                    break;
                case 7:
                    if (v)
                        --v;
                    break;
                case 0x12:
                    if (v || wSongColumn == CHANNEL_WAV)
                        --v;
                    break;
                case 0x0C:
                    v = v == 2 ? 3 : 1;
                    break;
                case 1:
                    if (!v || v > 0x20) {
                        v = 0;
                        break;
                    }
                default:
                    --v;
                }
                WritePhraseCommandValue(wPhrase, row, v);
                DrawFieldValue(field, v);
                if (CommandPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == (J_A | J_DOWN)) {
        if (wCursorMode == CURSOR_SELECTING) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            DrawPhraseCursorGlyph(row);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            switch (wPhraseColumn) {
            case PHRASE_COLUMN_LEFT_NOTE:
                v = ReadPhraseNote(row);
                field = v & 0xF0;
                if (field) {
                    field -= 0x20;
                    if (!field || field > 0xD0)
                        field = 0x10;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    while (!wSampleNoteString[0]) {
                        --field;
                        WritePhraseNote(row, v + (field << 4));
                        ReadKitSampleName(wPhraseRowFlags[row], field);
                    }
                    wPhraseColumnLast[wSongColumn] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_NOTE:
                if (PhraseRowIsSentinel(row)) {
                    v = ReadPhraseInstrument(wPhrase, row);
                    if (v == 0x40) {
                        v = ReadPhraseNote(row) - 2;
                        if (!v || v >= 0x0E)
                            v = 1;
                        FarCallArgument(0x1F, FAR_FN(DrawWordRow), v);
                        WritePhraseNote(row, v);
                    } else
                        EditPhraseCellDown(row);
                } else {
                    v = ReadPhraseNote(row);
                    field = v & 0x0F;
                    if (field) {
                        field -= 2;
                        if (!field || field > 0x0F)
                            field = 1;
                        v &= 0xF0;
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        while (!wSampleNoteString[0]) {
                            --field;
                            ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        }
                        WritePhraseNote(row, v + field);
                        wPhraseColumnLast[wSongColumn] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case PHRASE_COLUMN_INSTRUMENT:
                redraw |= PhraseCellFlagC(row);
                redraw |= wCursorMode == CURSOR_SELECTING;
                break;
            case PHRASE_COLUMN_COMMAND:
                do {
                    if (!wPendingCommands[row]) {
                        wPendingCommands[row] = ReadPhraseCommand(wPhrase, row);
                        wPendingCommandTarget = 0;
                    }
                    if (wPendingCommands[row] > 1)
                        wPendingCommands[row]--;
                } while (!PhraseStepOnes[(UWORD)wSongColumn * 20 + wPendingCommands[row]]);
                DrawPhraseCommandLetter(wPendingCommands[row]);
                if (wMessageStringPointer || wMessageTimer)
                    FarCallArgument(1, FAR_FN(ShowCommandHelp), wPendingCommands[row]);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawFieldValue(wPendingCommands[row], ReadPhraseCommandValue(wPhrase, row));
                break;
            case PHRASE_COLUMN_VALUE:
                v = ReadPhraseCommandValue(wPhrase, row);
                field = ReadPhraseCommand(wPhrase, row);
                switch (field) {
                case 0:
                    break;
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
                        --v;
                    break;
                case 0x0F:
                    v -= wSongColumn == CHANNEL_NOI ? 4 : 0x10;
                    break;
                case 1:
                    if (v < 0x10 || v > 0x20) {
                        v = 0;
                        break;
                    }
                default:
                    v -= 0x10;
                }
                WritePhraseCommandValue(wPhrase, row, v);
                DrawFieldValue(field, v);
                if (CommandPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wBHeld && wButtons == (J_B | J_RIGHT)) {
        wBHeld = 0;
        FarCall(0x1F, FAR_FN(HasEnterableColumnRight));
        return 1;
    }
    if (wBHeld && wButtons == (J_B | J_LEFT)) {
        wBHeld = 0;
        FarCall(0x1F, FAR_FN(HasEnterableColumnLeft));
        return 1;
    }
    if (wBHeld && wButtons == (J_B | J_UP)) {
        wBHeld = 0;
        wMessageStringPointer = 0;
        if (wChainRow) {
            oldPhrase = wPhrase;
            wChainRow--;
            wPhrase = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, wChainRow);
            if (wPhrase == EMPTY) {
                wPhrase = oldPhrase;
                wChainRow++;
            }
            DrawPhraseScreen();
        } else if (wAPressEnabled) {
            wPhraseRow = 0;
            DrawPhraseScreen();
        }
        return 1;
    }
    if (wBHeld && wButtons == (J_B | J_DOWN)) {
        wBHeld = 0;
        wMessageStringPointer = 0;
        if ((char)wChainRow == 0x0F)
            return 1;
        v = FarCallArgument2P(2, FAR_FN(SoundGetChainPhrase), wChain, wChainRow + 1);
        if (v != 0xFF) {
            wPhrase = v;
            wChainRow = wChainRow + 1;
            DrawPhraseScreen();
        }
        return 1;
    }
    if (wButtons == (J_B | J_A)) {
        if (!wAPressEnabled)
            return 0;
        wAPressEnabled = 0;
        wCursorMode = CURSOR_COPIED;
        wSelectionTop = wPhraseRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = SCREEN_PHRASE;
        switch (wPhraseColumn) {
        case PHRASE_COLUMN_LEFT_NOTE:
            wSelectionLeft = 0;
            wSelectionRight = 3;
            v = ReadPhraseNote((char)wPhraseRow);
            if (v & 0xF0)
                wPhraseColumnLast[wSongColumn] = v >> 4;
            WriteClipboardCell(0, (char)wPhraseRow, v >> 4);
            WriteClipboardCell(1, (char)wPhraseRow, 0xFF);
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstrument());
            WritePhraseNote((char)wPhraseRow, v & 0x0F);
            v = ReadCursorInstrument();
            SetColumnInstrument(v);
            break;
        case PHRASE_COLUMN_NOTE:
            SetColumnInstrument(ReadCursorInstrument());
            wSelectionLeft = 1;
            wSelectionRight = 3;
            v = ReadPhraseNote((char)wPhraseRow);
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstrument());
            if (PhraseRowIsSentinel((char)wPhraseRow)) {
                if (v)
                    wPhraseColumnLast[wSongColumn] = v;
                WriteClipboardCell(1, (char)wPhraseRow, v);
                WritePhraseNote((char)wPhraseRow, 0);
                WritePhraseInstrument(wPhrase, (char)wPhraseRow, 0xFF);
            } else {
                if (v & 0x0F)
                    wPhraseColumnLast[wSongColumn] = v & 0x0F;
                WriteClipboardCell(1, (char)wPhraseRow, v & 0x0F);
                WritePhraseNote((char)wPhraseRow, v & 0xF0);
            }
            break;
        case PHRASE_COLUMN_INSTRUMENT:
            wSelectionLeft = 2;
            wSelectionRight = 3;
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstrument());
            WritePhraseInstrument(wPhrase, (char)wPhraseRow, 0xFF);
            SetColumnInstrument(ReadClipboardCell(2, (char)wPhraseRow));
            break;
        case PHRASE_COLUMN_COMMAND:
        case PHRASE_COLUMN_VALUE:
            wSelectionLeft = 3;
            wSelectionRight = 5;
            v = ReadPhraseCommand(wPhrase, (char)wPhraseRow);
            WriteClipboardCell(3, (char)wPhraseRow, v);
            if (v)
                wColumnLastCommand[wSongColumn] = v;
            WriteClipboardCell(4, (char)wPhraseRow, ReadPhraseCommandValue(wPhrase, (char)wPhraseRow));
            WritePhraseCommand(wPhrase, (char)wPhraseRow, 0);
            WritePhraseCommandValue(wPhrase, (char)wPhraseRow, 0);
        }
        DrawPhraseScreen();
        return 1;
    }
    if (wButtons == (J_SELECT | J_A)) {
        if (wCursorMode == CURSOR_NORMAL || !wSelectHeld)
            return 0;
        if (wClipboardType != SCREEN_PHRASE && wClipboardType != SCREEN_TABLE)
            return 0;
        if (wCursorMode == CURSOR_SELECTING) {
            wSelectHeld = 0;
            if (CloneAtPhraseCursor())
                return 1;
            FarCallArgument(0x1F, FAR_FN(CutCopyPhraseSelection), 1);
            return 0;
        }
        if (wClipboardType == SCREEN_PHRASE)
            switch (wPhraseColumn) {
            case PHRASE_COLUMN_LEFT_NOTE:
            case PHRASE_COLUMN_NOTE:
                if (wSelectionLeft > PHRASE_COLUMN_NOTE)
                    return 0;
                break;
            case PHRASE_COLUMN_INSTRUMENT:
                if (wSelectionLeft != PHRASE_COLUMN_INSTRUMENT)
                    return 0;
                break;
            case PHRASE_COLUMN_COMMAND:
            case PHRASE_COLUMN_VALUE:
                if (wSelectionLeft < PHRASE_COLUMN_COMMAND)
                    return 0;
            }
        FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        PastePhraseSelection();
        DrawPhraseScreen();
        return 1;
    }
    if (wButtons == (J_SELECT | J_B | J_A) && wCursorMode == CURSOR_SELECTING)
        return CloneAtPhraseCursor();
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
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    }
    if (wButtons == (J_SELECT | J_LEFT)) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == (J_SELECT | J_DOWN)) {
        if (wButtonsSame)
            return 0;
        if (wPhraseColumn > PHRASE_COLUMN_INSTRUMENT
                && ReadPhraseCommand(wPhrase, (char)wPhraseRow) == COMMAND_G)
            wGroove = ReadPhraseCommandValue(wPhrase, (char)wPhraseRow);
        FarCall(5, MoveMapCursorDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == (J_SELECT | J_UP)) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == (J_SELECT | J_RIGHT)) {
        if (wButtonsSame)
            return 0;
        instrument = FindCursorInstrument();
        if (instrument <= INSTRUMENT_SPEECH) {
            SetColumnInstrument(instrument);
            wInstrument = instrument;
            if (wInstrument == INSTRUMENT_SPEECH) {
                wSpeechRow = ReadPhraseNote((char)wPhraseRow) - 1;
                if (wSpeechRow >= 0x0E)
                    wSpeechRow = 0;
            }
        } else
            instrument = wInstrument;
        if (wPhraseColumn > PHRASE_COLUMN_INSTRUMENT
                && ReadPhraseCommand(wPhrase, (char)wPhraseRow) == COMMAND_A) {
            table = ReadPhraseCommandValue(wPhrase, (char)wPhraseRow);
            if (table < 0x20) {
                wTable = table;
                wInstrument = EMPTY;
                FarCall(5, MoveMapCursorRight);
                FarCall(5, MoveMapCursorRight);
                FarCall(7, FAR_FN(RedrawScreen));
                wInstrument = instrument;
                return 1;
            }
        }
        FarCall(5, MoveMapCursorRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wCursorMode == CURSOR_SELECTING && wButtons == J_B) {
        if (!wAPressEnabled)
            return 0;
        FarCallArgument(0x1F, FAR_FN(CutCopyPhraseSelection), 0);
        return 1;
    }
    if (wButtons == (J_SELECT | J_B)) {
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == CURSOR_SELECTING && wClipboardType == SCREEN_PHRASE) {
            left = wSelectionLeft;
            right = wPhraseColumn;
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
            if (row || rowEnd != 0x0F) {
                wSelectionTop = 0;
                wPhraseRow = 0x0F;
                wSelectHeld = 0;
                DrawPhraseSelectionFar();
                return 1;
            }
            SortBytePair(&left, &right);
            if (left || right != 4) {
                wSelectionLeft = 0;
                wPhraseColumn = PHRASE_COLUMN_VALUE;
                wSelectHeld = 0;
                DrawPhraseSelectionFar();
                return 1;
            }
            wSelectHeld = 0;
            FarCallArgument(0x1F, FAR_FN(CutCopyPhraseSelection), 0);
            return 1;
        }
        FarCall(5, SaveSelectionState);
        wSelectionLeft = wPhraseColumn;
        wSelectionTop = wPhraseRow;
        wSelectHeld = 0;
        ShowMessage("SELECT");
        return 1;
    }
    return 0;
}
