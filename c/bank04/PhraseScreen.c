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
extern UBYTE wColInstr[];              /* $C468, [4] last instrument per channel */
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
void DrawFieldVal(UBYTE kind, UBYTE v);   /* bank 0 */
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
void TogglePlayback(void);                    /* bank 2 */
void SyncSetSendMode(void);                 /* bank 0 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wNameEditBuf[];                /* $C431, [5] */
extern UBYTE wInstrRemapDirty;              /* $C9B7 */
extern UBYTE wInstrRemapTable[];            /* $C9B8, [16] */
void ClearMessageBar(void);                 /* bank 1 */
UBYTE PhraseInstrEntered(void);
extern UBYTE wPhraseInstrCache[];           /* $C90A */
extern UBYTE wKbdHeldNote;                  /* $CB7E */
extern UBYTE wTable;                        /* $C41B */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wMuteSoloLatch;                /* $C56A */
extern UBYTE wPendingCmds[];                /* $C457, [16] */
extern UBYTE wPendingCmdTarget;             /* $C467 */
extern UBYTE wColLastCmd[];                 /* $C44B, [4] */
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
extern UBYTE FxPrelistenFlags[];            /* $04:$57C0, [20] */
extern UBYTE PhraseStepOnes[];              /* $04:$54E3, [4][20] */
void LoadCmdHelp(void);                 /* bank 1 */
UBYTE PhraseCellFlagB(UBYTE row);
UBYTE PhraseCellFlagD(UBYTE row);
void ErasePhraseCursor(void);          /* bank $1F */
void HasEnterableColRight(void);            /* bank $1F */
void HasEnterableColLeft(void);             /* bank $1F */
void CutCopyPhraseSelection(void);          /* bank $1F */
void MapMoveLeft(void);                     /* $05:$4233 */
void MapMoveRight(void);                    /* $05:$4253 */
void MapMoveUp(void);                       /* $05:$426A */
void MapMoveDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);              /* $02:$78C4 */
void SaveSelectionState(void);              /* $05:$743F */

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

void SetColInstr(UBYTE instr)
{
    if (wSongCol >= 4)
        DebugTrap();
    if (instr <= 0x40) {
        wColInstr[wSongCol] = instr;
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

void WritePhraseInstr(UBYTE phrase, UBYTE row, UBYTE instr)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (instr > 0x40 && instr != 0xFF)
        DebugTrap();
    p = SramPhraseInstrs + ((UWORD)phrase << 4);
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
        if (wColInstr[wSongCol] == 0xFF) {
            SetColInstr(FarCallByte(0x1F, FAR_FN(AllocNextFreeInstr)));
            wDrawStep++;
        }
        WritePhraseInstr(wPhrase, (char)wPhraseRow, wColInstr[wSongCol]);
        if (wSongCol == 3) {
            n = wColInstr[3];
            if (n < 0x40) {
                n = ReadInstrByte(n, 4);
                if (n > 0x78)
                    n = 0x78;
                WritePhraseNote((char)wPhraseRow, n);
            }
        }
        res = 1;
    } else {
        SetColInstr(instr);
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
    p = SramPhraseInstrs + ((UWORD)phrase << 4);
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

/* the caller passes the phrase and row; the body reads wPhrase/wPhraseRow */

UBYTE PhraseEditPlace(UBYTE phrase, UBYTE row)
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
        hBgDigitPal = 4;
        hBgPal = 1;
        GetTilemapAddr(2, i + 2);
        if (cur != 0xFF) {
            note = wSongReadBuf[i];
            addr = ((UWORD)cur << 4) + (UWORD)0xB082;
            t = ReadSongByte(1, addr);
            t = KitIndexToBank(t & 0x3F);
            wPhraseRowFlags[i] = t;
            hBgDigitPal = wShadowDigitPal;
            hBgPal = wShadowPal;
            if (!(note & 0xF0)) {
                DrawEmptyNote();
            } else {
                ReadKitSampleName(t, note >> 4);
                DrawNoteCell();
            }
            hBgDigitPal = 4;
            hBgPal = 1;
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
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
        t = wRowBufCol2[i];
        DrawPhraseCmdLetter(t);
        hBgDigitPal = 4;
        hBgPal = 1;
        DrawFieldVal(t, wRowBufCol3[i]);
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
    if (wSelectionLeft > 1)
        return 0;
    if (!CursorInstrIsKit())
        return 0;
    if (wPhraseCol == 0)
        return wSelectionLeft == 1;
    return ReadClipboardCell(1, x) == 0xFF;
}

void PastePhraseSelection(void)
{
    char x;
    UBYTE y;
    UBYTE savedLeft, savedRight;
    char shift;
    UBYTE v;
    UBYTE ctx;
    UBYTE c4, c3;

    savedLeft = wSelectionLeft;
    savedRight = wSelectionRight;
    shift = 0;
    if (wClipboardType == 5) {
        switch (wSelectionRight - wSelectionLeft) {
        default:
            return;
        case 2:
            switch (wSelectionLeft) {
            case 2:
                shift = -1;
                break;
            case 4:
                shift = 1;
                break;
            default:
                return;
            }
            wSelectionLeft = 3;
            wSelectionRight = 5;
        }
    }
    y = wSelectionTop;
    c4 = shift + 4;
    c3 = shift + 3;
    for (; y < wSelectionBottom; y++) {
        for (x = wSelectionRight - 1; !(x < (char)wSelectionLeft); x--) {
            ctx = PhraseCheckEditContext(y);
            if (ctx) {
                if (x < 2)
                    x ^= 1;
            }
            switch (x) {
            case 0:
                wPhraseNoteIsKitA = CursorInstrIsKit();
                if (wPhraseNoteIsKitA) {
                    v = ReadClipboardCell(ctx, y);
                    if (v == 0xFF)
                        break;
                    v <<= 4;
                    v = (PhraseReadNote(wPhraseRow) & 0x0F) + v;
                    WritePhraseNote(wPhraseRow, v);
                }
                break;
            case 1:
                v = ReadClipboardCell(ctx ^ 1, y);
                if (!(wSongCol == 3)) {
                    if (v == 0xFF)
                        break;
                }
                wPhraseNoteIsKitB = CursorInstrIsKit();
                if (wPhraseNoteIsKitB)
                    v = (PhraseReadNote(wPhraseRow) & 0xF0) + v;
                WritePhraseNote(wPhraseRow, v);
                break;
            case 2:
                WritePhraseInstr(wPhrase, wPhraseRow, ReadClipboardCell(2, y));
                break;
            case 3:
                WritePhraseFx(wPhrase, wPhraseRow, ReadClipboardCell(c3, y));
                break;
            case 4:
                WritePhraseFxVal(wPhrase, wPhraseRow, ReadClipboardCell(c4, y));
                break;
            }
            if (ctx) {
                if (x < 2)
                    x ^= 1;
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

void CloneInstr(UBYTE src, UBYTE dst)
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
        strcpy(wStrBuf, "CANNOT CLONE");
        FarCall(1, DrawMessageBar);
        return;
    }
    n = FarCallByte(0x1F, FAR_FN(AllocNextFreeInstr));
    if (n == 0xFF)
        return;
    CloneInstr(instr, n);
    SetColInstr(n);
    WritePhraseInstr(wPhrase, (char)wPhraseRow, n);
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
        FarCall(2, FAR_FN(TogglePlayback));
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
    FarCall(2, FAR_FN(TogglePlayback));
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
    SetColInstr(instr);
    WritePhraseInstr(wPhrase, row, instr);
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
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

/* As PhraseCellFlagA for the instrument after this one. r is only ever
   assigned in the branches, and the ROM keeps it in its slot for every
   access (`volatile`); as a plain local SDCC folds `r = k2` into the next
   `|=`, starts r there and spills the instrument instead. */
UBYTE PhraseCellFlagB(UBYTE row)
{
    volatile UBYTE r;
    UBYTE kit, i, k2;

    i = ReadPhraseInstr(wPhrase, row);
    kit = 0;
    if (i == 0xFF)
        return 0;
    if (i < 0x40) {
        kit = ReadInstrByte(i, 0) == 2;
        i++;
    } else
        i = 0x40;
    if (i < 0x40) {
        k2 = ReadInstrByte(i, 0);
        k2 = k2 == 2;
        r = k2;
        r |= !(kit == k2);
    } else
        r = 1;
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

/* As PhraseCellFlagB for the instrument 0x10 further on. */
UBYTE PhraseCellFlagD(UBYTE row)
{
    volatile UBYTE r;
    UBYTE kit, i, k2;

    i = ReadPhraseInstr(wPhrase, row);
    if (i > 0x30)
        return 0;
    kit = ReadInstrByte(i, 0) == 2;
    i += 0x10;
    if (i < 0x40) {
        k2 = ReadInstrByte(i, 0);
        k2 = k2 == 2;
        r = k2;
        r |= !(kit == k2);
    } else
        r = 1;
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
    UBYTE instr, left, right;
    UBYTE table;
    char chainRow;

    redraw = 0;
    if (wPhrase == 0xFF)
        DebugTrap();
    if ((UBYTE)(char)wPhraseRow >= 0x10)
        DebugTrap();
    hBgDigitPal = 4;
    hBgPal = 1;
    PhraseDrawCursorGlyph((char)wPhraseRow);
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wCursorMode == 4 && wButtons == 0x43) {
        wPhraseCol = wSelectionLeft;
        wPhraseRow = wSelectionTop;
        FarCall(5, 0x747D);
        DrawPhraseScreen();
        ShowMessage("CANCEL SELECT");
        return 1;
    }
    if (wAPressEnabled && wButtons == 0x10) {
        keyNote = wKbdHeldNote;
        placed = 1;
        if (keyNote) {
            wPhraseCol = 1;
            if (wSongCol == 3) {
                mask = wPhraseInstrCache[0];
                if (keyNote & 0xF0)
                    mask &= 0x0F;
                else
                    mask &= 0xF0;
                keyNote |= mask;
            } else if (!PhraseCheckColFD(wPhraseRow) && (keyNote & 0xF0)) {
                keyNote >>= 4;
                wPhraseCol = 0;
            }
            WritePhraseNote((char)wPhraseRow, 0);
            wPhraseColLast[wSongCol] = keyNote;
        }
        wPendingCmdTarget = 0;
        if (wCursorMode == 4)
            return 0;
        hBgDigitPal = wSelectDigitPal;
        hBgPal = wSelectPal;
        switch (wPhraseCol) {
        case 0:
        case 1:
            if (!PrelistenEditedCell())
                placed = PhraseEditPlace(wPhrase, wPhraseRow);
            else {
                v = PhraseReadNote((char)wPhraseRow);
                if (wPhraseCol == 0) {
                    if (!(v & 0xF0)) {
                        v += wPhraseColLast[wSongCol] << 4;
                        if (!(v & 0xF0))
                            v += 0x10;
                        WritePhraseNote((char)wPhraseRow, v);
                    } else
                        wPhraseColLast[wSongCol] = v >> 4;
                } else if (!(v & 0x0F)) {
                    v += wPhraseColLast[wSongCol] & 0x0F;
                    if (!(v & 0x0F))
                        ++v;
                    WritePhraseNote((char)wPhraseRow, v);
                } else
                    wPhraseColLast[wSongCol] = v & 0x0F;
            }
            hBgDigitPal = 4;
            hBgPal = 1;
            redraw = 1;
        case 2:
            if (wPhraseCol == 2 && wDrawStep++ == 1) {
                SetColInstr(FarCallByte(0x1F, FAR_FN(AllocNextFreeInstr)));
                redraw = 1;
            } else if (placed)
                redraw |= PhraseInstrEntered();
            else
                SetColInstr(ReadCursorInstr());
            PrelistenCursorRow();
            ++redraw;
            break;
        case 3:
            v = PhraseReadFx(wPhrase, (char)wPhraseRow);
            if (!v) {
                v = wColLastCmd[wSongCol];
                if (!v)
                    DebugTrap();
            } else if (wDrawStep++)
                FarCallArg(1, FAR_FN(LoadCmdHelp), v);
            wPendingCmds[wPhraseRow] = v;
            DrawPhraseCmdLetter(v);
            hBgDigitPal = 4;
            hBgPal = 1;
            DrawFieldVal(v, PhraseReadFxVal(wPhrase, (char)wPhraseRow));
            break;
        case 4:
            if (wDrawStep++ == 1 && PhraseReadFx(wPhrase, (char)wPhraseRow) == 1) {
                v = FarCallByte(0x1F, FAR_FN(FindFreeTable));
                if (v != 0xFF) {
                    WritePhraseFxVal(wPhrase, (char)wPhraseRow, v);
                    DrawHexByte(v);
                }
            }
            if (FxPrelistenFlags[PhraseReadFx(wPhrase, (char)wPhraseRow)])
                PrelistenCursorRow();
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x01) {
        if (wCursorMode != 4)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        if (wPhraseCol == 4)
            wPhraseCol--;
        wPhraseCol++;
        if (wCursorMode == 4)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == 0x02) {
        switch (wPhraseCol) {
        case 0:
            return 1;
        case 1:
            if (PhraseCheckColFD((char)wPhraseRow) && (wCursorMode != 4 || !wPhraseDrawMode))
                return 1;
        }
        if (wCursorMode != 4)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseCol--;
        if (wCursorMode == 4)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == 0x04) {
        if (wCursorMode == 4 && !wPhraseRow)
            return 0;
        if (wCursorMode != 4)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseRow--;
        if (!wPhraseCol && PhraseCheckColFD((char)wPhraseRow) && wCursorMode != 4)
            wPhraseCol++;
        if ((char)wPhraseRow == 0xFF) {
            if (wChainRow) {
                wChainRow--;
                wPhraseRow = 0x0F;
                v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, (char)wChainRow);
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
        if (wCursorMode == 4)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == 0x08) {
        if (wCursorMode == 4 && (char)wPhraseRow == 0x0F)
            return 0;
        if (wCursorMode != 4)
            FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        wPhraseRow++;
        if (!wPhraseCol && PhraseCheckColFD((char)wPhraseRow) && wCursorMode != 4)
            wPhraseCol++;
        if (wPhraseRow == 0x10) {
            wPhraseRow = 0x0F;
            chainRow = wChainRow;
            if (chainRow != 0x0F) {
                v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, chainRow + 1);
                if (v != 0xFF) {
                    wPhrase = v;
                    wChainRow = chainRow + 1;
                    wPhraseRow = 0;
                    DrawPhraseScreen();
                }
            }
        }
        if (wCursorMode == 4)
            DrawPhraseSelectionFar();
        else
            DrawPhraseCursorFar();
        return 1;
    }
    if (wButtons == 0x11) {
        if (wCursorMode == 4) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            PhraseDrawCursorGlyph(row);
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            switch (wPhraseCol) {
            case 0:
                v = PhraseReadNote(row);
                field = v & 0xF0;
                if (field) {
                    field += 0x10;
                    if (!field)
                        field = 0xF0;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    wPhraseColLast[wSongCol] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case 1:
                if (PhraseCheckColFD(row)) {
                    v = ReadPhraseInstr(wPhrase, row);
                    if (v == 0x40) {
                        v = PhraseReadNote(row) + 1;
                        if (v <= 0x0E) {
                            FarCallArg(0x1F, FAR_FN(DrawWordRow), v);
                            WritePhraseNote(row, v);
                        }
                    } else
                        PhraseEditRight(row);
                } else {
                    v = PhraseReadNote(row);
                    field = v & 0x0F;
                    if (field) {
                        if (++field == 0x10)
                            --field;
                        v &= 0xF0;
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        WritePhraseNote(row, v + field);
                        wPhraseColLast[wSongCol] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case 2:
                redraw |= PhraseCellFlagB(row);
                redraw |= wCursorMode == 4;
                break;
            case 3:
                do {
                    if (!wPendingCmds[row]) {
                        if (!(wPendingCmds[row] = PhraseReadFx(wPhrase, row)))
                            break;
                        wPendingCmdTarget = 0;
                    }
                    if (wPendingCmds[row] < 0x13)
                        wPendingCmds[row]++;
                    else
                        wPendingCmds[row] = 0x13;
                } while (!PhraseStepOnes[(UWORD)wSongCol * 20 + wPendingCmds[row]]);
                if (wPendingCmds[row]) {
                    DrawPhraseCmdLetter(wPendingCmds[row]);
                    if (wMessageStrPtr || wMessageTimer)
                        FarCallArg(1, FAR_FN(LoadCmdHelp), wPendingCmds[row]);
                    hBgDigitPal = 4;
                    hBgPal = 1;
                    DrawFieldVal(wPendingCmds[row], PhraseReadFxVal(wPhrase, row));
                }
                break;
            case 4:
                v = PhraseReadFxVal(wPhrase, row);
                field = PhraseReadFx(wPhrase, row);
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
                    if (wSongCol == 2 || v != 3)
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
                WritePhraseFxVal(wPhrase, row, v);
                DrawFieldVal(field, v);
                if (FxPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x14) {
        if (wCursorMode == 4) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            PhraseDrawCursorGlyph(row);
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            switch (wPhraseCol) {
            case 0:
                v = PhraseReadNote(row);
                field = v & 0xF0;
                if (field) {
                    field += 0x20;
                    if (field < 0x20)
                        field = 0xF0;
                    v &= 0x0F;
                    WritePhraseNote(row, v + field);
                    field >>= 4;
                    ReadKitSampleName(wPhraseRowFlags[row], field);
                    wPhraseColLast[wSongCol] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case 1:
                if (PhraseCheckColFD(row)) {
                    v = ReadPhraseInstr(wPhrase, row);
                    if (v == 0x40) {
                        v = PhraseReadNote(row) + 2;
                        if (v > 0x0E)
                            v = 0x0E;
                        FarCallArg(0x1F, FAR_FN(DrawWordRow), v);
                        WritePhraseNote(row, v);
                    } else
                        PhraseEditUp(row);
                } else {
                    v = PhraseReadNote(row);
                    field = v & 0x0F;
                    if (field) {
                        field += 2;
                        if (field > 0x0F)
                            field = 0x0F;
                        v &= 0xF0;
                        WritePhraseNote(row, v + field);
                        ReadKitSampleName(wPhraseRowFlagsB[row], field);
                        wPhraseColLast[wSongCol] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case 2:
                redraw |= PhraseCellFlagD(row);
                redraw |= wCursorMode == 4;
                break;
            case 3:
                do {
                    if (!wPendingCmds[row]) {
                        if (!(wPendingCmds[row] = PhraseReadFx(wPhrase, row)))
                            break;
                        wPendingCmdTarget = 0;
                    }
                    if (wPendingCmds[row] < 0x13)
                        wPendingCmds[row]++;
                    else
                        wPendingCmds[row] = 0x13;
                } while (!PhraseStepOnes[(UWORD)wSongCol * 20 + wPendingCmds[row]]);
                if (wPendingCmds[row]) {
                    DrawPhraseCmdLetter(wPendingCmds[row]);
                    if (wMessageStrPtr || wMessageTimer)
                        FarCallArg(1, FAR_FN(LoadCmdHelp), wPendingCmds[row]);
                    hBgDigitPal = 4;
                    hBgPal = 1;
                    DrawFieldVal(wPendingCmds[row], PhraseReadFxVal(wPhrase, row));
                }
                break;
            case 4:
                v = PhraseReadFxVal(wPhrase, row);
                field = PhraseReadFx(wPhrase, row);
                switch (field) {
                case 0:
                    break;
                case 7:
                    v += 0x10;
                    if (v > 0x1F)
                        v = 0x1F;
                    break;
                case 0x12:
                    if (wSongCol == 2)
                        v += 0x10;
                    else if (v != 3)
                        ++v;
                    break;
                case 0x0C:
                    v = 3;
                    break;
                case 0x0F:
                    v += wSongCol == 3 ? 4 : 0x10;
                    break;
                case 1:
                    if (v >= 0x10) {
                        v = 0x20;
                        break;
                    }
                default:
                    v += 0x10;
                }
                WritePhraseFxVal(wPhrase, row, v);
                DrawFieldVal(field, v);
                if (FxPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x12) {
        if (wCursorMode == 4) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            PhraseDrawCursorGlyph(row);
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            switch (wPhraseCol) {
            case 0:
                v = PhraseReadNote(row);
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
                    wPhraseColLast[wSongCol] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case 1:
                if (PhraseCheckColFD(row)) {
                    v = ReadPhraseInstr(wPhrase, row);
                    if (v == 0x40) {
                        v = PhraseReadNote(row) - 1;
                        if (v) {
                            FarCallArg(0x1F, FAR_FN(DrawWordRow), v);
                            WritePhraseNote(row, v);
                        }
                    } else
                        PhraseEditLeft(row);
                } else {
                    v = PhraseReadNote(row);
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
                        wPhraseColLast[wSongCol] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case 2:
                redraw |= PhraseCellFlagA(row);
                redraw |= wCursorMode == 4;
                break;
            case 3:
                do {
                    if (!wPendingCmds[row]) {
                        wPendingCmds[row] = PhraseReadFx(wPhrase, row);
                        wPendingCmdTarget = 0;
                    }
                    if (wPendingCmds[row] > 1)
                        wPendingCmds[row]--;
                } while (!PhraseStepOnes[(UWORD)wSongCol * 20 + wPendingCmds[row]]);
                DrawPhraseCmdLetter(wPendingCmds[row]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(LoadCmdHelp), wPendingCmds[row]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[row], PhraseReadFxVal(wPhrase, row));
                break;
            case 4:
                v = PhraseReadFxVal(wPhrase, row);
                field = PhraseReadFx(wPhrase, row);
                switch (field) {
                case 0:
                    break;
                case 7:
                    if (v)
                        --v;
                    break;
                case 0x12:
                    if (v || wSongCol == 2)
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
                WritePhraseFxVal(wPhrase, row, v);
                DrawFieldVal(field, v);
                if (FxPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x18) {
        if (wCursorMode == 4) {
            row = wSelectionTop;
            rowEnd = (char)wPhraseRow;
            SortBytePair(&row, &rowEnd);
        } else {
            rowEnd = (char)wPhraseRow;
            row = (char)wPhraseRow;
        }
        rowEnd++;
        for (; row < rowEnd; row++) {
            PhraseDrawCursorGlyph(row);
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            switch (wPhraseCol) {
            case 0:
                v = PhraseReadNote(row);
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
                    wPhraseColLast[wSongCol] = field;
                    DrawNoteCell();
                }
                PrelistenCursorRow();
                break;
            case 1:
                if (PhraseCheckColFD(row)) {
                    v = ReadPhraseInstr(wPhrase, row);
                    if (v == 0x40) {
                        v = PhraseReadNote(row) - 2;
                        if (!v || v >= 0x0E)
                            v = 1;
                        FarCallArg(0x1F, FAR_FN(DrawWordRow), v);
                        WritePhraseNote(row, v);
                    } else
                        PhraseEditDown(row);
                } else {
                    v = PhraseReadNote(row);
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
                        wPhraseColLast[wSongCol] = field;
                        DrawNoteCell();
                    }
                }
                PrelistenCursorRow();
                break;
            case 2:
                redraw |= PhraseCellFlagC(row);
                redraw |= wCursorMode == 4;
                break;
            case 3:
                do {
                    if (!wPendingCmds[row]) {
                        wPendingCmds[row] = PhraseReadFx(wPhrase, row);
                        wPendingCmdTarget = 0;
                    }
                    if (wPendingCmds[row] > 1)
                        wPendingCmds[row]--;
                } while (!PhraseStepOnes[(UWORD)wSongCol * 20 + wPendingCmds[row]]);
                DrawPhraseCmdLetter(wPendingCmds[row]);
                if (wMessageStrPtr || wMessageTimer)
                    FarCallArg(1, FAR_FN(LoadCmdHelp), wPendingCmds[row]);
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawFieldVal(wPendingCmds[row], PhraseReadFxVal(wPhrase, row));
                break;
            case 4:
                v = PhraseReadFxVal(wPhrase, row);
                field = PhraseReadFx(wPhrase, row);
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
                    if (wSongCol == 2)
                        v -= 0x10;
                    else if (v)
                        --v;
                    break;
                case 0x0F:
                    v -= wSongCol == 3 ? 4 : 0x10;
                    break;
                case 1:
                    if (v < 0x10 || v > 0x20) {
                        v = 0;
                        break;
                    }
                default:
                    v -= 0x10;
                }
                WritePhraseFxVal(wPhrase, row, v);
                DrawFieldVal(field, v);
                if (FxPrelistenFlags[field])
                    PrelistenCursorRow();
            }
        }
        if (redraw)
            DrawPhraseScreen();
        return 1;
    }
    if (wBHeld && wButtons == 0x21) {
        wBHeld = 0;
        FarCall(0x1F, FAR_FN(HasEnterableColRight));
        return 1;
    }
    if (wBHeld && wButtons == 0x22) {
        wBHeld = 0;
        FarCall(0x1F, FAR_FN(HasEnterableColLeft));
        return 1;
    }
    if (wBHeld && wButtons == 0x24) {
        wBHeld = 0;
        wMessageStrPtr = 0;
        if (wChainRow) {
            oldPhrase = wPhrase;
            wChainRow--;
            wPhrase = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, wChainRow);
            if (wPhrase == 0xFF) {
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
    if (wBHeld && wButtons == 0x28) {
        wBHeld = 0;
        wMessageStrPtr = 0;
        if ((char)wChainRow == 0x0F)
            return 1;
        v = FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, wChainRow + 1);
        if (v != 0xFF) {
            wPhrase = v;
            wChainRow = wChainRow + 1;
            DrawPhraseScreen();
        }
        return 1;
    }
    if (wButtons == 0x30) {
        if (!wAPressEnabled)
            return 0;
        wAPressEnabled = 0;
        wCursorMode = 2;
        wSelectionTop = wPhraseRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = 1;
        switch (wPhraseCol) {
        case 0:
            wSelectionLeft = 0;
            wSelectionRight = 3;
            v = PhraseReadNote((char)wPhraseRow);
            if (v & 0xF0)
                wPhraseColLast[wSongCol] = v >> 4;
            WriteClipboardCell(0, (char)wPhraseRow, v >> 4);
            WriteClipboardCell(1, (char)wPhraseRow, 0xFF);
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstr());
            WritePhraseNote((char)wPhraseRow, v & 0x0F);
            v = ReadCursorInstr();
            SetColInstr(v);
            break;
        case 1:
            SetColInstr(ReadCursorInstr());
            wSelectionLeft = 1;
            wSelectionRight = 3;
            v = PhraseReadNote((char)wPhraseRow);
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstr());
            if (PhraseCheckColFD((char)wPhraseRow)) {
                if (v)
                    wPhraseColLast[wSongCol] = v;
                WriteClipboardCell(1, (char)wPhraseRow, v);
                WritePhraseNote((char)wPhraseRow, 0);
                WritePhraseInstr(wPhrase, (char)wPhraseRow, 0xFF);
            } else {
                if (v & 0x0F)
                    wPhraseColLast[wSongCol] = v & 0x0F;
                WriteClipboardCell(1, (char)wPhraseRow, v & 0x0F);
                WritePhraseNote((char)wPhraseRow, v & 0xF0);
            }
            break;
        case 2:
            wSelectionLeft = 2;
            wSelectionRight = 3;
            WriteClipboardCell(2, (char)wPhraseRow, ReadCursorInstr());
            WritePhraseInstr(wPhrase, (char)wPhraseRow, 0xFF);
            SetColInstr(ReadClipboardCell(2, (char)wPhraseRow));
            break;
        case 3:
        case 4:
            wSelectionLeft = 3;
            wSelectionRight = 5;
            v = PhraseReadFx(wPhrase, (char)wPhraseRow);
            WriteClipboardCell(3, (char)wPhraseRow, v);
            if (v)
                wColLastCmd[wSongCol] = v;
            WriteClipboardCell(4, (char)wPhraseRow, PhraseReadFxVal(wPhrase, (char)wPhraseRow));
            WritePhraseFx(wPhrase, (char)wPhraseRow, 0);
            WritePhraseFxVal(wPhrase, (char)wPhraseRow, 0);
        }
        DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x50) {
        if (wCursorMode == 1 || !wSelectHeld)
            return 0;
        if (wClipboardType != 1 && wClipboardType != 5)
            return 0;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            if (PhraseCloneTableCmd())
                return 1;
            FarCallArg(0x1F, FAR_FN(CutCopyPhraseSelection), 1);
            return 0;
        }
        if (wClipboardType == 1)
            switch (wPhraseCol) {
            case 0:
            case 1:
                if (wSelectionLeft > 1)
                    return 0;
                break;
            case 2:
                if (wSelectionLeft != 2)
                    return 0;
                break;
            case 3:
            case 4:
                if (wSelectionLeft < 3)
                    return 0;
            }
        FarCall(0x1F, FAR_FN(ErasePhraseCursor));
        PastePhraseSelection();
        DrawPhraseScreen();
        return 1;
    }
    if (wButtons == 0x70 && wCursorMode == 4)
        return PhraseCloneTableCmd();
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
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    }
    if (wButtons == 0x42) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x48) {
        if (wButtonsSame)
            return 0;
        if (wPhraseCol > 2 && PhraseReadFx(wPhrase, (char)wPhraseRow) == 7)
            wGroove = PhraseReadFxVal(wPhrase, (char)wPhraseRow);
        FarCall(5, MapMoveDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x44) {
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveUp);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wButtons == 0x41) {
        if (wButtonsSame)
            return 0;
        instr = FindCursorInstr();
        if (instr <= 0x40) {
            SetColInstr(instr);
            wInstr = instr;
            if (wInstr == 0x40) {
                wSpeechCursorRow = PhraseReadNote((char)wPhraseRow) - 1;
                if (wSpeechCursorRow >= 0x0E)
                    wSpeechCursorRow = 0;
            }
        } else
            instr = wInstr;
        if (wPhraseCol > 2 && PhraseReadFx(wPhrase, (char)wPhraseRow) == 1) {
            table = PhraseReadFxVal(wPhrase, (char)wPhraseRow);
            if (table < 0x20) {
                wTable = table;
                wInstr = 0xFF;
                FarCall(5, MapMoveRight);
                FarCall(5, MapMoveRight);
                FarCall(7, FAR_FN(RedrawScreen));
                wInstr = instr;
                return 1;
            }
        }
        FarCall(5, MapMoveRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    }
    if (wCursorMode == 4 && wButtons == 0x20) {
        if (!wAPressEnabled)
            return 0;
        FarCallArg(0x1F, FAR_FN(CutCopyPhraseSelection), 0);
        return 1;
    }
    if (wButtons == 0x60) {
        if (!wSelectHeld)
            return 0;
        if (wCursorMode == 4 && wClipboardType == 1) {
            left = wSelectionLeft;
            right = wPhraseCol;
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
                wPhraseCol = 4;
                wSelectHeld = 0;
                DrawPhraseSelectionFar();
                return 1;
            }
            wSelectHeld = 0;
            FarCallArg(0x1F, FAR_FN(CutCopyPhraseSelection), 0);
            return 1;
        }
        FarCall(5, SaveSelectionState);
        wSelectionLeft = wPhraseCol;
        wSelectionTop = wPhraseRow;
        wSelectHeld = 0;
        ShowMessage("SELECT");
        return 1;
    }
    return 0;
}
