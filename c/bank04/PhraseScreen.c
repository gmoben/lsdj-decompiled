/*
 * The phrase screen -- LSDj 9.4.0, ROM bank $04.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern char wPhraseCol;                     /* $C415, a plain char */
extern UBYTE wPhraseColLast[];              /* $C907, [4] */
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wColInstrument[];              /* $C468, [4] last instrument per channel */
extern UBYTE wInstrIsKit;                   /* $C90B */
UBYTE AllocNextFreeInstr(void);             /* bank $1F */
void CopyTableData(UBYTE from, UBYTE to);   /* bank 1 */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE wPhraseRowFlagsB[];            /* $C91D, [16] */
extern UBYTE wRowBufCol2[];                 /* $C48D, [16] the phrase commands */
extern UBYTE wRowBufCol3[];                 /* $C49D, [16] their values */
extern UBYTE PhraseColX0[];                 /* $04:$429E */
extern UBYTE PhraseColX1[];                 /* $04:$42A3 */

void DrawChainCell(void);                   /* bank 6 */
void DrawChainPos(void);                    /* bank 6 */
void DrawWordRow(UBYTE n);                  /* bank $1F */
void DrawAllChanStatus(void);               /* bank $1F */
void ScreenPreDraw(void);                   /* bank $1E */
void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0 */
UBYTE KitIndexToBank(UBYTE kit);            /* bank 0, asm */
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0, asm */
void DrawPhraseCursor(void);                /* bank $1F */
void DrawPhraseSelection(void);            /* bank $1F */
void CopyInstrName(void);                   /* bank 1 */
void WriteInstrName(void);                  /* bank 1 */
UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row);
UBYTE DrawInstrName(UBYTE row, UBYTE instr);
extern UBYTE wAPressEnabled;                /* $C409 */
UBYTE PhraseCursorOnEditCell(void);
void CloneInstrAtCursor(void);
UBYTE CloneTableAtCursor(void);
UBYTE PhraseReadFx(UBYTE phrase, UBYTE row);
UBYTE FindCursorInstr(void);
extern UBYTE wPrelisten;                    /* $C56E */
extern UBYTE wLiveStartCol;                 /* $C571 */
extern UBYTE wSavedLiveMode;                /* $C572 */
extern UBYTE wLiveMode;                     /* $C528 */
void StopPlayback(void);                    /* bank 2 */
void SyncSetSendMode(void);                 /* bank 0 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wNameEditBuf[];                /* $C431, [5] */
extern UBYTE wInstrRemapDirty;              /* $C9B7 */
extern UBYTE wInstrRemapTable[];            /* $C9B8, [16] */
void ClearMessageBar(void);                 /* bank 1 */
UBYTE PhraseInstrEntered(void);

void DrawChainCellFar(UBYTE x)
{
    FarCallArg2V(6, DrawChainCell, wSongCol, x);
}

void DrawPhraseCursorFar(void)
{
    FarCall(0x1F, FAR_FN(DrawPhraseCursor));
}

void DrawPhraseCmdLetter(UBYTE cmd)
{
    UBYTE c, ok;

    ok = cmd <= 0x13;
    if (ok)
        c = FxMnemonics[cmd];
    else
        c = 0x3F;
    DrawStringIndexed(c);
}

UBYTE PhraseModeTile(void)
{
    return wSongCol == 3 ? 0x78 : 0x6C;
}

UBYTE ReadCursorInstr(void)
{
    UBYTE row;

    row = wPhraseRow;
    return ReadPhraseInstr(wPhrase, row);
}

UBYTE FindCursorInstr(void)
{
    UBYTE row, v;

    for (row = wPhraseRow; row != 0xFF; row--) {
        v = ReadPhraseInstr(wPhrase, row);
        if (v <= 0x40)
            return v;
    }
    return 0xFF;
}

void PhraseResolveCursorNote(UBYTE v)
{
    UBYTE instr;

    wPhraseColLast[wSongCol] = v;
    if (wSongCol == 3 && wCursorMode != 4) {
        instr = FindCursorInstr();
        if (instr < 0x40 && ReadInstrByte(instr, 0) == 3)
            WriteInstrByte(instr, 4, v);
    }
}

void SetColInstrument(UBYTE instr)
{
    if (wSongCol >= 4)
        DebugTrap();
    if (instr <= 0x40) {
        wColInstrument[wSongCol] = instr;
        if (wSongCol == 2) {
            wInstrIsKit = instr != 0x40;
            if (wInstrIsKit)
                wInstrIsKit = ReadInstrByte(instr, 0) == 2;
        }
        WriteSongByte(1, (UWORD)wSongCol + (UWORD)0xBFC6, instr);
    }
}

void WritePhraseNote(UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == 0xFF)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    WriteSongByte(0, (UBYTE *)p, v);
}

void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (instr > 0x40 && instr != 0xFF)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    WriteSongByte(3, p + row, instr);
}

void PhraseDrawCursorGlyph(UBYTE row)
{
    UBYTE y;
    UBYTE *tbl;

    y = row + 2;
    tbl = wPhraseDrawMode ? PhraseColX1 : PhraseColX0;
    GetTilemapAddr(tbl[wPhraseCol], y);
}

UBYTE PhraseInstrEntered(void)
{
    UBYTE instr;
    UBYTE res;
    UBYTE *p;
    UBYTE i, v, n;

    instr = ReadCursorInstr();
    res = 0;
    if (instr > 0x40) {
        if (wColInstrument[wSongCol] == 0xFF) {
            SetColInstrument(FarCallByte(0x1F, FAR_FN(AllocNextFreeInstr)));
            wDrawStep++;
        }
        WritePhraseInstrument(wPhrase, (char)wPhraseRow, wColInstrument[wSongCol]);
        if (wSongCol == 3) {
            n = wColInstrument[3];
            if (n < 0x40) {
                n = ReadInstrByte(n, 4);
                if (n > 0x78)
                    n = 0x78;
                WritePhraseNote((char)wPhraseRow, n);
            }
        }
        res = 1;
    } else {
        SetColInstrument(instr);
        if (wPhraseCol == 2) {
            FarCallArg(1, FAR_FN(CopyInstrName), instr);
            for (i = 0; i < 5; i++) {
                p = &wSampleNoteString[i];
                v = wNameEditBuf[i];
                if (!v)
                    v = ' ';
                *p = v;
            }
            wSampleNoteString[5] = ' ';
            FarCall(1, FAR_FN(ClearMessageBar));
            PhraseDrawCursorGlyph((char)wPhraseRow);
        }
    }
    return res;
}

void PhraseEditUp(UBYTE row)
{
    UBYTE *p;
    UBYTE step;
    UBYTE full;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    step = 12;
    full = 0;
    if (wPhrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (wSongCol == 3) {
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
        if (wCursorMode == 4)
            return;
        v = wPhraseColLast[wSongCol];
        full = PhraseInstrEntered();
    } else {
        v += step;
        if (wCursorMode == 4 || wSongCol == 3) {
            while (PhraseModeTile() < v || !v)
                v -= PhraseModeTile();
        } else {
            while (PhraseModeTile() < v || v < step)
                v -= step;
        }
    }
    PhraseResolveCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawChainCellFar(v);
}

void PhraseEditDown(UBYTE row)
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
    if (wPhrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (!v) {
        if (wCursorMode == 4)
            return;
        v = wPhraseColLast[wSongCol];
        full = PhraseInstrEntered();
    } else {
        kitcol = wSongCol == 3;
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
        if (wCursorMode == 4 || kitcol) {
            while (PhraseModeTile() < v || !v)
                v += PhraseModeTile();
        } else {
            n = -step;
            while (n < v || !v)
                v += step;
        }
    }
    PhraseResolveCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawChainCellFar(v);
}

void PhraseEditRight(UBYTE row)
{
    UBYTE *p;
    UBYTE full;
    UWORD addr;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    full = 0;
    if (wPhrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    addr = (UWORD)p;
    v = ReadSongByte(0, addr);
    if (!v) {
        if (wCursorMode == 4)
            return;
        row = wPhraseColLast[wSongCol];
        full = PhraseInstrEntered();
    } else {
        if (v == PhraseModeTile() && wCursorMode != 4 && wSongCol != 3)
            return;
        row = v + 1;
    }
    if (PhraseModeTile() < row || !row)
        row = 1;
    PhraseResolveCursorNote(row);
    WriteSongByte(0, addr, row);
    if (full)
        DrawPhraseScreen();
    else
        DrawChainCellFar(row);
}

void PhraseEditLeft(UBYTE row)
{
    UBYTE *p;
    UBYTE full;
    UBYTE v;

    p = (UBYTE *)(row + (UWORD)0xA000);
    full = 0;
    if (wPhrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (!v) {
        if (wCursorMode == 4)
            return;
        v = wPhraseColLast[wSongCol];
        full = PhraseInstrEntered();
    } else {
        if (v == 1 && wCursorMode != 4 && wSongCol != 3)
            return;
        v--;
    }
    if (!v || PhraseModeTile() < v)
        v = PhraseModeTile();
    PhraseResolveCursorNote(v);
    WriteSongByte(0, p, v);
    if (full)
        DrawPhraseScreen();
    else
        DrawChainCellFar(v);
}

UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    return ReadSongByte(3, p + (unsigned int)row);
}

UBYTE PhraseReadNote(UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == 0xFF)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    return ReadSongByte(0, (UBYTE *)p);
}

void WritePhraseFx(UBYTE phrase, UBYTE row, UBYTE cmd)
{
    UWORD p;

    p = (UWORD)(SramPhraseFx + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (cmd > 0x13)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, cmd);
}

void WritePhraseFxVal(UBYTE phrase, UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseFxVal + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, v);
}

UBYTE PhraseReadFxVal(UBYTE phrase, UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseFxVal + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    return ReadSongByte(2, (UBYTE *)p);
}

UBYTE PhraseReadFx(UBYTE phrase, UBYTE row)
{
    UWORD p;
    UBYTE v, ok;

    p = (UWORD)(SramPhraseFx + (unsigned int)row);
    if (phrase == 0xFF)
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

UBYTE PhraseEditPlace(void)
{
    UBYTE *p;
    UBYTE v;

    p = (UBYTE *)(wPhraseRow + (UWORD)0xA000);
    if (wPhraseRow >= 0x10)
        DebugTrap();
    if (wPhrase == 0xFF)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    v = ReadSongByte(0, p);
    if (v || wCursorMode == 4) {
        PhraseResolveCursorNote(v);
        return 0;
    }
    WriteSongByte(0, p, wPhraseColLast[wSongCol]);
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
    ReadSongBytes(0, p, wSongReadBuf);
    ReadSongBytes(3, (UWORD)0xB000 + off, (UBYTE *)0xC47D);
    ReadSongBytes(2, p, (UBYTE *)0xC48D);
    ReadSongBytes(2, (UWORD)0xAFF0 + off, (UBYTE *)0xC49D);
}

void DrawPhraseSelectionFar(void)
{
    FarCall(0x1F, FAR_FN(DrawPhraseSelection));
}

UBYTE PhraseCheckColFD(UBYTE i)
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
const UBYTE PhraseRowAttrs0[13] = { 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
const UBYTE PhraseRowAttrs1[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };
const UBYTE PhraseRowAttrs2[13] = { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };

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
    UBYTE i;
    UBYTE t;
    UBYTE cur;
    UBYTE ins;
    UBYTE note;
    UWORD addr;
    UBYTE *p;

    if (wPhrase == 0xFF)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    FarCall(6, DrawChainPos);
    DrawTempoField();
    LoadPhraseRow();
    wPhraseDrawMode = 0;
    for (i = 0; i < 0x10; i++) {
        t = wUsedTableMap[i];
        if (t < 0x40 && ReadSongByte(1, ((UWORD)t << 4) + (UWORD)0xB080) == 2) {
            wPhraseDrawMode = 1;
            break;
        }
    }
    cur = 0xFF;
    for (i = 0; i < 0x10; i++) {
        ins = wUsedTableMap[i];
        switch (ins) {
        case 0x40:
            cur = 0xFF;
            wPhraseRowFlags[i] = 0xFE;
            break;
        case 0xFF:
            wPhraseRowFlags[i] = cur;
            break;
        default:
            if (ReadSongByte(1, ((UWORD)ins << 4) + (UWORD)0xB080) == 2) {
                cur = ins;
                wPhraseRowFlags[i] = ins;
            } else {
                cur = 0xFF;
                wPhraseRowFlags[i] = 0xFF;
            }
        }
        hBgDigitPalette = 4;
        hBgPalette = 1;
        GetTilemapAddr(2, i + 2);
        if (cur != 0xFF) {
            note = wSongReadBuf[i];
            addr = ((UWORD)cur << 4) + (UWORD)0xB082;
            t = ReadSongByte(1, addr);
            t = KitIndexToBank(t & 0x3F);
            wPhraseRowFlags[i] = t;
            hBgDigitPalette = wShadowDigitPal;
            hBgPalette = wShadowPal;
            if (!(note & 0xF0)) {
                DrawEmptyNote();
            } else {
                ReadKitSampleName(t, note >> 4);
                DrawNoteCell();
            }
            hBgDigitPalette = 4;
            hBgPalette = 1;
            addr = ((UWORD)cur << 4) + (UWORD)0xB089;
            t = ReadSongByte(1, addr);
            t = KitIndexToBank((char)t & 0x3F);
            wPhraseRowFlagsB[i] = t;
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
            if (ins == 0x40) {
                t = wSongReadBuf[i];
                FarCallArg(0x1F, FAR_FN(DrawWordRow), t);
            } else {
                t = wSongReadBuf[i];
                DrawChainCellFar(t);
            }
            DrawStringTile20();
        }
        if (hGameboyType) {
            p = wPhraseDrawMode ? (PhraseCheckColFD(i) ? PhraseRowAttrs1 : PhraseRowAttrs0) : PhraseRowAttrs2;
            VBK_REG = 1;
            set_bkg_tiles(2, i + 2, 13, 1, p);
            VBK_REG = 0;
        }
        if (ins == 0xFF)
            DrawString("--");
        else
            DrawHexByte(ins);
        DrawStringTile20();
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        t = wRowBufCol2[i];
        DrawPhraseCmdLetter(t);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawFieldValue(t, wRowBufCol3[i]);
        if (!wPhraseDrawMode)
            DrawString("   ");
    }
    if (!wPhraseCol && PhraseCheckColFD((char)wPhraseRow))
        wPhraseCol++;
    if (wCursorMode == 4)
        DrawPhraseSelectionFar();
    else
        DrawPhraseCursorFar();
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    FarCall(0x1E, FAR_FN(ScreenPreDraw));
}

UBYTE CursorInstrIsKit(void)
{
    UBYTE v, r;

    v = FindCursorInstr();
    if (v < 0x40)
        r = ReadInstrByte(v, 0) == 2;
    else
        r = 0;
    return r;
}

UBYTE PhraseCheckEditContext(UBYTE x)
{
    if (wSongCol != 2)
        return 0;
    if (wPhraseCol > 1)
        return 0;
    if (wSelLeft > 1)
        return 0;
    if (!CursorInstrIsKit())
        return 0;
    if (wPhraseCol == 0)
        return wSelLeft == 1;
    return ReadClipboardCell(1, x) == 0xFF;
}

void CloneInstrument(UBYTE src, UBYTE dst)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        WriteInstrByte(dst, i, ReadInstrByte(src, i));
    FarCallArg(1, FAR_FN(CopyInstrName), src);
    FarCallArg(1, FAR_FN(WriteInstrName), dst);
}

void CloneInstrAtCursor(void)
{
    UBYTE instr, bad, n;

    instr = ReadCursorInstr();
    bad = !(instr < 0x40);
    FarCall(5, FAR_FN(RestoreSelectionState));
    if (bad) {
        strcpy(wStrBuffer, "CANNOT CLONE");
        FarCall(1, DrawMessageBar);
        return;
    }
    n = FarCallByte(0x1F, FAR_FN(AllocNextFreeInstr));
    if (n == 0xFF)
        return;
    CloneInstrument(instr, n);
    SetColInstrument(n);
    WritePhraseInstrument(wPhrase, (char)wPhraseRow, n);
    DrawPhraseScreen();
    ShowMessage("CLONED INSTR");
}

UBYTE CloneTableAtCursor(void)
{
    UBYTE v, ok, t;

    v = PhraseReadFxVal(wPhrase, (char)wPhraseRow);
    ok = v < 0x20;
    FarCall(5, FAR_FN(RestoreSelectionState));
    if (!ok)
        return 0;
    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return 0;
    FarCallArg2V(1, FAR_FN(CopyTableData), v, t);
    WritePhraseFxVal(wPhrase, (char)wPhraseRow, t);
    DrawPhraseScreen();
    return 1;
}

UBYTE PhraseCloneTableCmd(void)
{
    UBYTE row;

    if (wAPressEnabled || PhraseCursorOnEditCell())
        return 0;
    if (wPhraseCol == 2) {
        CloneInstrAtCursor();
        return 1;
    }
    if (wPhraseCol == 4) {
        row = wPhraseRow;
        if (PhraseReadFx(wPhrase, row) == 1) {
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
    if (wCursorMode == 4)
        return;
    row = wPhraseRow;
    note = PhraseReadNote(row);
    if (!note)
        return;
    if (wLiveStartCol != 0xFF) {
        FarCall(2, FAR_FN(StopPlayback));
        wLiveMode = wSavedLiveMode;
    }
    if (wPlayMode)
        return;
    if (wSyncMode != 0 && wSyncMode != 1 && wSyncMode != 5)
        return;
    wSavedLiveMode = wLiveMode;
    wLiveMode = 0;
    wLiveStartCol = wSongCol;
    if (wSyncMode == 1)
        SyncSetSendMode();
    FarCall(2, FAR_FN(StopPlayback));
}

UBYTE DrawInstrName(UBYTE row, UBYTE instr)
{
    UBYTE res;
    UBYTE *p;
    UBYTE i, v, n;

    res = 0;
    FarCallArg(1, FAR_FN(CopyInstrName), instr);
    for (i = 0; i < 5; i++) {
        p = &wSampleNoteString[i];
        v = wNameEditBuf[i];
        if (!v)
            v = ' ';
        *p = v;
    }
    wSampleNoteString[5] = ' ';
    FarCall(1, FAR_FN(ClearMessageBar));
    PhraseDrawCursorGlyph(row);
    SetColInstrument(instr);
    WritePhraseInstrument(wPhrase, row, instr);
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    DrawHexByte(instr);
    if (wSongCol == 3 && instr < 0x40 && ReadInstrByte(instr, 0) == 3) {
        n = ReadInstrByte(instr, 4);
        *(UBYTE *)0xC90A = n;               /* wPhraseInstrCache, stored through the literal */
        WritePhraseNote(row, n);
        res = 1;
    }
    PrelistenCursorRow();
    wInstrRemapDirty = 1;
    wInstrRemapTable[row] = instr;
    return res;
}

UBYTE PhraseCellFlagA(UBYTE row)
{
    UBYTE r, kit, i, k2;

    i = ReadPhraseInstr(wPhrase, row);
    r = 0;
    kit = 0;
    if (i == 0xFF)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrByte(i, 0) == 2;
    } else {
        i = 0x40;
        r = 1;
    }
    if (i)
        i--;
    k2 = ReadInstrByte(i, 0);
    k2 = k2 == 2;
    r |= k2;
    r |= !(kit == k2);
    r |= DrawInstrName(row, i);
    return r;
}

UBYTE PhraseCellFlagC(UBYTE row)
{
    UBYTE r, kit, i, k2;

    i = ReadPhraseInstr(wPhrase, row);
    r = 0;
    kit = 0;
    if (i == 0xFF || i < 0x10)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrByte(i, 0) == 2;
        i -= 0x10;
    } else {
        i = 0x30;
        r = 1;
    }
    k2 = ReadInstrByte(i, 0);
    k2 = k2 == 2;
    r |= k2;
    r |= !(kit == k2);
    r |= DrawInstrName(row, i);
    return r;
}

UBYTE PrelistenEditedCell(void)
{
    UBYTE i;

    if (wSongCol != 2)
        return 0;
    i = ReadCursorInstr();
    if (i > 0x40)
        return wInstrIsKit;
    if (i == 0x40)
        return 0;
    return ReadInstrByte(i, 0) == 2;
}
