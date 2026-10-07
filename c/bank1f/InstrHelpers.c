/*
 * Bank $1F helpers -- LSDj 9.4.0: instrument-screen field readers, the
 * used-table map, far-call wrappers into bank 1, and the phrase cursor.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wInstrRemapTable[];            /* $C9B8, [16] */
extern UBYTE wInstrRemapDirty;              /* $C9B7 */
extern UBYTE wLiveStartCol;                 /* $C571 */
extern UBYTE wChanPhrase[];                 /* $C168, [4] */
extern UBYTE wChanPhraseRow[];              /* $C16C, [4] */
extern UBYTE DefaultWaveFrameB1F[];         /* $1F:$491A, [16] */
extern UBYTE DefaultSynthParams[];          /* $1F:$61FF, [16] */
extern UBYTE wPhraseCol;                    /* $C415 */
extern UBYTE wNameEditBuf[];                /* $C431, [5] */
extern UBYTE wNameEditIsInstr;              /* $C430 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wColInstrument[];              /* $C468, [4] */
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
void CopySelection(void);                   /* $1C:$71CD */
void PhraseReadNote(void);                  /* $04:$4968, (row) */
void WritePhraseNote(void);                 /* $04:$418F, (row, v) */
void PhraseReadFx(void);                    /* $04:$4AEA, (phrase, row) */
void WritePhraseFx(void);                   /* $04:$49C5, (phrase, row, cmd) */
void PhraseReadFxVal(void);                 /* $04:$4A8F, (phrase, row) */
void WritePhraseFxVal(void);                /* $04:$4A30, (phrase, row, v) */
UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row);  /* $04:$4918 */
void WriteInstrName(UBYTE instr);           /* $01:$4A49 */
UBYTE IsPhraseUsed(UBYTE phrase);
extern UBYTE wColPhrase[];                  /* $C453, [4] */
extern UBYTE wColChain[];                   /* $C44F, [4] */
extern UBYTE wPhraseDedupScratch5;          /* $C9CC */
void FreePhrase(void);                      /* $01:$4DD3 */
void FreeChain(void);                       /* $01:$4E43 */
void MarkChainUsed(void);                   /* $01:$4D67 */
void ShowOutOfMemory(void);                 /* $01:$75BD */
void DrawFreedTables(void);
UBYTE FreeUnusedInstrs(void);
UBYTE InstrHCountUsedPhrases(void);
UBYTE InstrHCountUsedChains(void);
void DedupPhrases(void);
void DedupChains(void);
void RemapSeqChains(void);
void RebuildPhraseAllocMap(void);
UBYTE FindDupTable(UBYTE a, UBYTE b);
void RemapCmdTableRefs(UBYTE from, UBYTE to);
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE PhraseCellX[];                 /* $1F:$64E1, [5]: the six phrase-cell layout rows */
extern UBYTE PhraseCellXAlt[];              /* $64E6 */
extern UBYTE PhraseCellWidthAlt[];          /* $64EB */
extern UBYTE PhraseCellWidth[];             /* $64F0 */
extern UBYTE PhraseCellPalAlt[];            /* $64F5 */
extern UBYTE PhraseCellPal[];               /* $64FA */
extern UBYTE DefaultInstrPulse[];           /* $1F:$4A02, [11] */
extern UBYTE DefaultInstrWave[];            /* $4A0D, [11] */
extern UBYTE DefaultInstrKit[];             /* $4A18, [14] */
UBYTE IsChainUsed(UBYTE chain);
void DrawPhraseCellField(UBYTE col, UBYTE row);
void DrawPhraseCellValue(UBYTE col, UBYTE row);
UBYTE InstrHGetTableUsed(UBYTE t);
void InstrHMarkTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
void InstrHScanUsedChains(void);
UBYTE IsChainUsedFar(UBYTE chain);
UBYTE InstrHScanChainPhrases(UBYTE chain);
void ResetWaveFrame(UBYTE p);
void MarkPhraseTablesUsed(UBYTE phrase);
UBYTE IsTableFree(UWORD t);
UBYTE InstrHScanAllChainPhr(void);
UBYTE InstrHAnyCmdRef(void);
void InstrHScanTableRows(void);
void InstrHScanInstrTables(void);
void InstrHScanUsedPhrases(void);
UBYTE InstrHProcessUsedTables(void);
UBYTE InstrHCollectUsedTables(void);
void MarkChainPhrasesUsed(UBYTE chain);
UBYTE WaveFrameModified(UBYTE phrase);
UBYTE CanEnterSongCol(UBYTE col);
void InstrHReadCursorCellAt(void);
UBYTE IsInstrAllocated(UBYTE instr);
void InstrHMarkInstrTable(UBYTE instr);
void DrawChanStatus(UBYTE chan);
UBYTE InstrHGetParamPlus4(void);
void OpenNameEditor(void);                  /* bank $1B */
void MapMoveLeft(void);                     /* bank 5 */
void MapMoveRight(void);                    /* bank 5 */
void MapMoveUp(void);                       /* bank 5 */
void MapMoveDown(void);                     /* bank 5 */
void InstrSelectPrevPage(void);             /* bank 3 */
void ToggleSongPlayback(void);                 /* bank 2 */
void TogglePlayback(void);                    /* bank 2 */
UBYTE ClaimInstrSlot(UBYTE instr);
UBYTE InstrHCountUsedTables(void);
void ReadTableRowCmds(UBYTE t);
void ClearTable(UBYTE t);
void MarkPhraseUsed(UBYTE phrase);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr);

void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */
extern char PlayModeNames[];                /* $00:$0839, five 9-byte names "  MANUAL".."  RESYNC" */
void DrawPulseDuty(UBYTE duty);             /* bank 0 */
void DrawInstrByte12(void);                 /* $03:$4CD9 */
void DrawWaveSynthField(void);              /* $03:$4D15 */
void DrawInstrByte10(void);                 /* $03:$498B */
void DrawInstrRow14(void);                  /* $03:$49FD */
void DrawInstrByte11(void);                 /* $03:$49B7 */

void InstrScreenFrame(void);                /* $03:$45A7 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wChanChain[];                  /* $C170, [4] */
extern UBYTE wChanChainRow[];               /* $C17C, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
void ScreenPreDraw(void);                   /* $1E:$4000 */

void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v);    /* $06:$5655 */

void DrawInstrTableMode(void)
{
    char *s;

    switch (ReadCurInstrByte(0)) {
    case 0:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 1:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 2:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 3:
        wVramCursor = (UBYTE *)0x99C6;
        break;
    }
    s = (ReadCurInstrByte(6) & 0x20) ? ((ReadCurInstrByte(5) & 8) ? "STEP" : "TICK") : " OFF";
    DrawString(s);
}

void DrawInstrTableField(void)
{
    char v;

    v = ReadCurInstrByte(6);
    if (v & 0x20)
        DrawHexByte(v & 0x1F);
    else
        DrawString("--");
}

/*
 * One kit's LOOP setting: OFF unless `loopBit` of byte 5 is set, then ATK
 * (loop from the start) when bit 7 of byte `atkByte` is set, else ON (loop
 * from OFFSET). The literal first in `0x80 & ...` keeps the masked byte in
 * a register for the test (`and; ld c,a; or a,c`): SDCC fuses `x & lit`
 * into its `if` only with the literal on the right.
 */
void DrawKitLoopMode(UBYTE loopBit, UBYTE atkByte)
{
    if (ReadCurInstrByte(5) & loopBit) {
        if (0x80 & ReadCurInstrByte(atkByte))
            DrawString("ATK");
        else
            DrawString(" ON");
    } else
        DrawString("OFF");
}

void DrawKitLoopField(void)
{
    DrawKitLoopMode(0x40, 2);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawKitLoopMode(0x20, 9);
}

UBYTE InstrEnvHasSecond(void)
{
    return ReadCurInstrByte(1) & 0x0F;
}

UBYTE InstrEnvHasThird(void)
{
    UBYTE a, b;

    a = InstrEnvHasSecond();
    b = ReadCurInstrByte(9) & 0x0F;
    return a && b;
}

void DrawInstrEnvFields(void)
{
    UBYTE v;

    wVramCursor = 0x98A7;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawString("/--/--");
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    wVramCursor = 0x98A5;
    DrawHexByte(ReadCurInstrByte(1));
    if (InstrEnvHasSecond()) {
        wVramCursor = 0x98A8;
        DrawHexByte(ReadCurInstrByte(9));
        if (InstrEnvHasThird()) {
            v = ReadCurInstrByte(10);
            wVramCursor = 0x98AB;
            if (v & 0xF0)
                DrawHexByte(v);
            else {
                DrawByte(v >> 4);
                DrawStringIndexed('-');
            }
        }
    }
}

void DrawPulseInstr(void)
{
    UBYTE v;

    DrawInstrEnvFields();
    wVramCursor = 0x98EB;
    v = ReadCurInstrByte(7);
    DrawPulseDuty(v >> 6);
    wVramCursor = 0x990B;
    DrawFieldValue(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x9928;
    v = ReadCurInstrByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = 0x994B;
    DrawHexByte(ReadCurInstrByte(4) & 0x7F);
    wVramCursor = 0x9967;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = 0x998A;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x99EC;
    DrawByte(ReadCurInstrByte(8));
    wVramCursor = 0x99AB;
    DrawHexByte(ReadCurInstrByte(2));
    wVramCursor = 0x99CB;
    DrawHexByte(ReadCurInstrByte(0x0B));
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawNoiseInstr(void)
{
    UBYTE v;

    wVramCursor = 0x98A5;
    DrawInstrEnvFields();
    wVramCursor = 0x98EB;
    DrawFieldValue(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x9908;
    v = ReadCurInstrByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = 0x9929;
    DrawString(ReadCurInstrByte(2) ? "SAFE" : "FREE");
    wVramCursor = 0x994B;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedGlyph), v);
    wVramCursor = 0x996A;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x998C;
    DrawByte(ReadCurInstrByte(8));
    wVramCursor = 0x99C6;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawKitNameCell(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
}

void DrawKitDistName(UBYTE v);              /* bank 3 */

/*
 * The KIT instrument's fields: both kits' names, VOLUME (byte 1 bits 6-5,
 * shown 3..0 for $20/$40/$60/0), OUTPUT, LOOP, SPEED (byte 2 bit 6: 0.5X or
 * 1X), the DIST mode, FINETUNE, both OFFSETs and LENs (0 = ALL), the pitch
 * mode and the table.
 */
void DrawKitInstr(void)
{
    UBYTE v, flags;

    wVramCursor = 0x98A4;
    v = ReadCurInstrByte(2) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = 0x98C4;
    v = ReadCurInstrByte(9) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = 0x9909;
    v = ReadCurInstrByte(1);
    switch (v & 0x60) {
    case 0x20:
        v = '3';
        break;
    case 0x40:
        v = '2';
        break;
    case 0x60:
        v = '1';
        break;
    default:
        v = '0';
    }
    DrawStringIndexed((char)v);             /* via a copy: ld b,(hl); ld a,b */
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawFieldValue(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x99A6;
    DrawKitLoopField();
    wVramCursor = 0x99C9;
    if (0x40 & ReadCurInstrByte(2))         /* literal first: tested in b */
        DrawString("0.5");
    else
        DrawString("  1");
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('X');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    wVramCursor = 0x99E8;
    v = ReadCurInstrByte(0x0A);
    FarCallArg(3, FAR_FN(DrawKitDistName), v);
    wVramCursor = 0x994B;
    DrawHexByte(ReadCurInstrByte(8));
    wVramCursor = 0x9968;
    v = ReadCurInstrByte(0x0C);
    DrawHexByte(v);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    v = ReadCurInstrByte(0x0D);
    DrawHexByte(v);
    wVramCursor = 0x9986;
    v = ReadCurInstrByte(3);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    v = ReadCurInstrByte(0x0B);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    wVramCursor = 0x9927;
    flags = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), flags);
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawWaveInstr(void)
{
    UBYTE v;

    wVramCursor = 0x98A9;
    v = ReadCurInstrByte(1);
    switch (v & 0x60) {
    case 0x20:
        v = '3';
        break;
    case 0x40:
        v = '2';
        break;
    case 0x60:
        v = '1';
        break;
    default:
        v = '0';
        break;
    }
    DrawStringIndexed((char)v);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawFieldValue(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x98C7;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = 0x98EA;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x992C;
    DrawByte(ReadCurInstrByte(8));
    FarCall(3, DrawInstrByte12);
    FarCall(3, DrawWaveSynthField);
    FarCall(3, DrawInstrByte10);
    FarCall(3, DrawInstrRow14);
    FarCall(3, DrawInstrByte11);
    wVramCursor = 0x9965;
    v = ReadCurInstrByte(9);
    DrawString(PlayModeNames + (v < 5 ? v : 5) * 9);
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void WriteChainPhrase(UBYTE chain, UBYTE row, UBYTE phrase)
{
    UBYTE *p;

    if (chain & 0x80)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramChainPhrases + ((UWORD)chain << 4);
    WriteSongByte(1, p + row, phrase);
}

UBYTE ClaimInstrSlot(UBYTE instr)
{
    UBYTE *src;
    UWORD n;
    UWORD i;
    UWORD p;

    if (instr >= 0x40)
        DebugTrap();
    p = (UWORD)0xA040 + instr;
    if (ReadSongByte(1, p))
        return 0;
    WriteSongByte(1, p, 1);
    switch (wSongCol) {
    case 2:
        src = DefaultInstrKit;
        n = 14;
        break;
    case 3:
        src = DefaultInstrWave;
        n = 11;
        break;
    default:
        src = DefaultInstrPulse;
        n = 11;
    }
    for (i = 0; i != n; i++)
        WriteInstrByte(instr, i, src[i]);
    return 1;
}

void InitRemapTable(void)
{
    UBYTE i, v;
    UBYTE *p;

    for (i = 0; i != 0x10; i++) {
        p = &wInstrRemapTable[i];
        v = *p;
        if (v < 0x40) {
            *p = 0xFF;
            ClaimInstrSlot(v);
        }
    }
    wInstrRemapDirty = 0;
}

UBYTE AllocNextFreeInstr(void)
{
    UBYTE i;

    for (i = 0; i < 0x40; i++) {
        if (ClaimInstrSlot(i)) {
            FarCallArg3(4, (UWORD)WritePhraseInstrument, wPhrase, wPhraseRow, i);
            return i;
        }
    }
    strcpy(wStrBuffer, "OUT OF MEMORY. CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

/* 1 if the 16 bytes at bank:addr are all zero. (The loop reuses `bank`.) */
UBYTE InstrHReadTableRow(UBYTE bank, UBYTE *addr)
{
    ReadSongBytes(bank, addr, wSongReadBuf);
    for (bank = 0; bank != 16; bank++)
        if (wSongReadBuf[bank])
            return 0;
    return 1;
}

UBYTE IsTableFree(UWORD t)
{
    t <<= 4;
    if (!InstrHReadTableRow(1, (UBYTE *)0xB480 + t))
        return 0;
    if (!InstrHReadTableRow(1, (UBYTE *)0xB680 + t))
        return 0;
    if (!InstrHReadTableRow(1, (UBYTE *)0xBA80 + t))
        return 0;
    return InstrHReadTableRow(0, (UBYTE *)0xB690 + t);
}

UBYTE FindFreeTable(void)
{
    UBYTE i;

    for (i = 0; i != 0x20; i++)
        if (IsTableFree((UWORD)i))
            return i;
    ShowMessage("OUT OF TABLES!");
    return 0xFF;
}

UBYTE IsSongChainUsed(void)
{
    UBYTE i;
    UBYTE *p;
    UBYTE start;

    start = wColChain[wSongCol];
    if (start & 0x80)
        start = 0;
    i = start;
    do {
        p = SramChainAlloc + (i >> 3);
        if (!(ReadSongByte(1, (UWORD)p) & (1 << (i & 7)))) {
            FarCallArg(1, FAR_FN(MarkChainUsed), i);
            if (i >= 0x80)
                DebugTrap();
            return i;
        }
        i++;
        if (i == 0x80)
            i = 0;
    } while (i != start);
    FarCall(1, FAR_FN(ShowOutOfMemory));
    return 0xFF;
}

/* CLEAN INSTR DATA's instrument pass: clear the instr-alloc table, mark
   every instrument a phrase row uses, then reset each unmarked one (its
   name, its $B080 parameter block, the columns' current instrument).
   Returns how many of the instruments allocated before were freed. */
UBYTE FreeUnusedInstrs(void)
{
    UWORD p;
    UBYTE i;
    UBYTE ph;
    UBYTE row;
    UBYTE n;
    UBYTE k;
    UWORD addr;

    p = (UWORD)SramInstrAlloc;
    n = 0;
    _asm di _endasm;
    for (i = 0; i < 0x40; i++) {
        if (ReadSongByte(1, p))
            n++;
        WriteSongByte(1, p, 0);
        p++;
    }
    for (ph = 0; ph < 0xFF; ph++) {
        for (row = 0; row < 0x10; row++) {
            i = FarCallArg2P(4, FAR_FN(ReadPhraseInstr), ph, row);
            if (i < 0x40)
                WriteSongByte(1, (UWORD)SramInstrAlloc + i, 1);
        }
    }
    _asm ei _endasm;
    p = (UWORD)SramInstrAlloc;
    for (i = 0; i < 0x40; i++) {
        _asm di _endasm;
        if (!ReadSongByte(1, p)) {
            wNameEditBuf[0] = 0;
            wNameEditBuf[1] = 0;
            wNameEditBuf[2] = 0;
            wNameEditBuf[3] = 0;
            wNameEditBuf[4] = 0;
            FarCallArg(1, FAR_FN(WriteInstrName), i);
            addr = i;
            addr <<= 4;
            addr |= 0xB000;
            addr += 0x80;
            rRAMB = 1;
            _asm di _endasm;
            rRAMG = 0x0A;
            MemSet((UBYTE *)addr, 0, 0x10);
            rRAMG = 0;
            _asm ei _endasm;
            k = 4;
            do {
                /* SDCC fetches i into a over the element it just read
                   (`cp a,a`): the test is always true, so freeing any
                   instrument clears all four columns' */
                if (wColInstrument[k - 1] == i)
                    wColInstrument[k - 1] = 0xFF;
            } while (--k);
        } else {
            n--;
        }
        _asm ei _endasm;
        p++;
    }
    if (n & 0x80)
        n = 0;
    return n;
}

void InstrHMarkTableUsed(UBYTE t)
{
    if (t < 0x20)
        wUsedTableMap[t] = 1;
}

UBYTE InstrHGetTableUsed(UBYTE t)
{
    return wUsedTableMap[t];
}

UBYTE IsInstrAllocated(UBYTE instr)
{
    return ReadSongByte(1, SramInstrAlloc + (UWORD)instr);
}

void MarkPhraseTablesUsed(UBYTE phrase)
{
    UBYTE *c;
    UBYTE *v;
    UBYTE i;

    c = SramPhraseFx + ((unsigned int)phrase << 4);
    v = SramPhraseFxVal + ((unsigned int)phrase << 4);
    for (i = 0; i < 16; i++) {
        if (ReadSongByte(2, c) == 1)
            InstrHMarkTableUsed(ReadSongByte(2, v));
        c = c + 1;
        v = v + 1;
    }
}

void ReadTableRowCmds(UBYTE t)
{
    UWORD o;
    UBYTE *p1, *p2, *p3, *p4;
    UBYTE i;

    o = (UWORD)t << 4;
    p1 = (UBYTE *)0xB680 + o;
    p2 = (UBYTE *)0xB880 + o;
    p3 = (UBYTE *)0xBA80 + o;
    p4 = (UBYTE *)0xBC80 + o;
    for (i = 0; i < 0x10; i++) {
        if (ReadSongByte(1, (UWORD)p1) == 1)
            InstrHMarkTableUsed(ReadSongByte(1, (UWORD)p2));
        if (ReadSongByte(1, (UWORD)p3) == 1)
            InstrHMarkTableUsed(ReadSongByte(1, (UWORD)p4));
        p1++;
        p2++;
        p3++;
        p4++;
    }
}

UBYTE IsPhraseUsedFar(UBYTE phrase)
{
    return FarCallArgByte(1, (UWORD)IsPhraseUsed, phrase);
}

void InstrHScanUsedPhrases(void)
{
    UBYTE i;

    for (i = 0; i < 0xFF; i++)
        if (IsPhraseUsedFar(i))
            MarkPhraseTablesUsed(i);
}

UBYTE InstrHCountUsedTables(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x20; i++)
        if (InstrHGetTableUsed(i))
            n++;
    return n;
}

UBYTE InstrHProcessUsedTables(void)
{
    UBYTE before, i;

    before = InstrHCountUsedTables();
    for (i = 0; i < 0x20; i++)
        if (InstrHGetTableUsed(i))
            ReadTableRowCmds(i);
    return !(before == InstrHCountUsedTables());
}

void InstrHMarkInstrTable(UBYTE instr)
{
    UBYTE v;

    v = ReadInstrByte(instr, 6);
    if (v & 0x20)
        InstrHMarkTableUsed(v & 0x1F);
}

void InstrHScanInstrTables(void)
{
    UBYTE i;

    for (i = 0; i < 0x40; i++)
        if (IsInstrAllocated(i))
            InstrHMarkInstrTable(i);
}

void ClearTableRow(UBYTE bank, UBYTE *p)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++) {
        WriteSongByte(bank, p, 0);
        p = p + 1;
    }
}

void ClearTable(UBYTE t)
{
    UWORD o;
    UBYTE *p1, *p2, *p3, *p4, *p5;

    o = (UWORD)t << 4;
    if (t >= 0x20)
        DebugTrap();
    p1 = (UBYTE *)0xB680 + o;
    p2 = (UBYTE *)0xB880 + o;
    p3 = (UBYTE *)0xBA80 + o;
    p4 = (UBYTE *)0xBC80 + o;
    p5 = (UBYTE *)0xB480 + o;
    o += (UWORD)0xB690;
    ClearTableRow(1, p1);
    ClearTableRow(1, p2);
    ClearTableRow(1, p3);
    ClearTableRow(1, p4);
    ClearTableRow(1, p5);
    ClearTableRow(0, (UBYTE *)o);
}

UBYTE InstrHCollectUsedTables(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x20; i++) {
        if (!InstrHGetTableUsed(i)) {
            if (!IsTableFree((UWORD)i))
                n++;
            ClearTable(i);
        }
    }
    return n;
}

UBYTE BuildUsedTablesMap(void)
{
    MemSet(wUsedTableMap, 0, 0x20);
    InstrHScanInstrTables();
    InstrHScanUsedPhrases();
    do
        ;
    while (InstrHProcessUsedTables());
    return InstrHCollectUsedTables();
}

void RemapSeqChains(void)
{
    UBYTE j, i, col;

    col = wSongCol;
    _asm
        di
    _endasm;
    for (i = 0; i < 0x80; i++)
        FarCallArg(1, FAR_FN(FreeChain), i);
    _asm
        ei
        di
    _endasm;
    for (wSongCol = 0; wSongCol < 4; wSongCol++) {
        for (j = 0; j != 0xC0; j++) {
            i = ReadSeqByte(wSongCol, j);
            if (!(i & 0x80))
                FarCallArg(1, FAR_FN(MarkChainUsed), i);
        }
    }
    wSongCol = col;
    _asm
        ei
        di
    _endasm;
    for (i = 0; i < 0x80; i++) {
        if (!FarCallArgByte(1, FAR_FN(IsChainUsed), i)) {
            for (j = 0; j < 0x10; j++) {
                WriteChainPhrase(i, j, 0xFF);
                FarCallArg3P(6, FAR_FN(SetChainTranspose), i, j, 0);
            }
        }
    }
    _asm
        ei
    _endasm;
}

void MarkChainPhrasesUsed(UBYTE chain)
{
    UBYTE i, p;

    for (i = 0; i < 16; i++) {
        p = FarCallArg2(2, (UWORD)SndGetChainPhrase, chain, i);
        if (p != 0xFF)
            FarCallArg(1, FAR_FN(MarkPhraseUsed), p);
    }
}

UBYTE IsChainUsedFar(UBYTE chain)
{
    return FarCallArgByte(1, (UWORD)IsChainUsed, chain);
}

void InstrHScanUsedChains(void)
{
    UBYTE i;

    for (i = 0; i < 0x80; i++)
        if (IsChainUsedFar(i))
            MarkChainPhrasesUsed(i);
}

void RemapPhraseRefs(UBYTE from, UBYTE to)
{
    UBYTE *p;
    UBYTE i;

    p = SramChainPhrases;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (; p != SramChainPhrases + 0x800; p++)
        if (from == *p)
            *p = to;
    rRAMG = 0;
    _asm ei _endasm;
    FarCallArg(1, FAR_FN(FreePhrase), from);
    i = 4;
    do {
        if (from == wColPhrase[i - 1])
            wColPhrase[i - 1] = to;
        i--;
    } while (i);
}

void RemapChainRefs(UBYTE from, UBYTE to)
{
    UBYTE *p;
    UBYTE i;

    p = SramSequence;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    for (; p != SramSequence + 0x400; p++)
        if (from == *p)
            *p = to;
    rRAMG = 0;
    _asm ei _endasm;
    FarCallArg(1, FAR_FN(FreeChain), from);
    i = 4;
    do {
        if (from == wColChain[i - 1])
            wColChain[i - 1] = to;
        i--;
    } while (i);
}

void RemapCmdTableRefs(UBYTE from, UBYTE to)
{
    volatile UWORD p;
    volatile UWORD q;
    volatile UBYTE i;
    volatile UBYTE nt;
    UBYTE nf;

    p = (UWORD)0xB680;
    q = (UWORD)0xB880;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    while (p != (UWORD)0xB880) {
        if (*(UBYTE *)p == 1 && from == *(UBYTE *)q)
            *(UBYTE *)q = to;
        p++;
        q++;
    }
    p = (UWORD)0xBA80;
    q = (UWORD)0xBC80;
    while (p != (UWORD)0xBC80) {
        if (*(UBYTE *)p == 1 && from == *(UBYTE *)q)
            *(UBYTE *)q = to;
        p++;
        q++;
    }
    rRAMB = 2;
    p = (UWORD)0xA000;
    q = (UWORD)0xAFF0;
    while (p != (UWORD)0xAFF0) {
        if (*(UBYTE *)p == 1 && from == *(UBYTE *)q)
            *(UBYTE *)q = to;
        p++;
        q++;
    }
    rRAMG = 0;
    _asm ei _endasm;
    i = 0;
    nt = to | 0x20;
    nf = from | 0x20;
    while (i != 0x40) {
        if (ReadInstrByte(i, 6) == nf)
            WriteInstrByte(i, 6, nt);
        i++;
    }
}

UBYTE InstrHCountUsedPhrases(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0xFF; i++)
        if (IsPhraseUsedFar(i))
            n++;
    return n;
}

UBYTE InstrHCountUsedChains(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x80; i++)
        if (IsChainUsedFar(i))
            n++;
    return n;
}

void RebuildPhraseAllocMap(void)
{
    UBYTE i;
    UWORD o;

    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    MemSet(SramPhraseAlloc, 0, 0x20);
    rRAMG = 0;
    _asm ei _endasm;
    InstrHScanUsedChains();
    for (i = 0; i < 0xFF; i++) {
        if (IsPhraseUsedFar(i))
            continue;
        o = i;
        o <<= 4;
        o |= 0xA000;
        _asm di _endasm;
        rRAMG = 0x0A;
        rRAMB = 0;
        MemSet((UBYTE *)o, 0, 0x10);
        rRAMB = 2;
        MemSet((UBYTE *)o, 0, 0x10);
        o += 0xFF0;
        MemSet((UBYTE *)o, 0, 0x10);
        o += 0x10;
        rRAMB = 3;
        MemSet((UBYTE *)o, 0xFF, 0x10);
        rRAMG = 0;
        _asm ei _endasm;
    }
}

UBYTE WaveFrameModified(UBYTE w)
{
    UBYTE *p;
    UBYTE i;

    p = SramWaveFrames + ((UWORD)w << 4);
    for (i = 0; i < 0x10; i++) {
        if (ReadSongByte(3, p) != DefaultWaveFrameB1F[i])
            return 1;
        p = p + 1;
    }
    return 0;
}

UBYTE InstrHScanChainPhrases(UBYTE chain)
{
    UBYTE p, i;

    p = chain << 4;
    for (i = 0; i != 16; i++) {
        if (WaveFrameModified(p))
            return 1;
        p++;
    }
    return 0;
}

UBYTE InstrHScanAllChainPhr(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x10; i++)
        if (InstrHScanChainPhrases(i))
            n++;
    return n;
}

UBYTE InstrHScanPhraseCmds(void)
{
    UBYTE *p;
    UBYTE ph, i;

    p = SramPhraseFx;
    for (ph = 0; ph != 0xFF; ph++) {
        i = 0;
        while (1) {
            if (!(i < 0x10))
                break;
            if (ReadSongByte(2, p) == 6)
                return 1;
            p = p + 1;
            i = i + 1;
        }
    }
    return 0;
}

UBYTE InstrHScanTableCmds(void)
{
    UBYTE *p, *q;
    UBYTE i, j, ok;

    p = (UBYTE *)0xB680;
    q = (UBYTE *)0xBA80;
    for (i = 0; i < 0x20; i++) {
        for (j = 0; j < 0x10; j = j + 1) {
            if (ReadSongByte(1, (UWORD)p) == 6)
                return 1;
            if (ReadSongByte(1, (UWORD)q) == 6)
                return 1;
            p = p + 1;
            q = q + 1;
        }
    }
    return 0;
}

void ResetWaveFrame(UBYTE p)
{
    UBYTE *d;

    d = SramWaveFrames + ((UWORD)p << 4);
    rRAMB = 3;
    _asm di _endasm;
    rRAMG = 0x0A;
    memcpy(d, DefaultWaveFrameB1F, 16);
    rRAMG = 0;
    _asm ei _endasm;
}

void InstrHScanChainRows(UBYTE chain)
{
    UBYTE p, i;

    p = chain << 4;
    for (i = 0; i < 16; i++) {
        ResetWaveFrame(p);
        p++;
    }
}

void ResetSynthParams(UBYTE s)
{
    UBYTE *d;

    d = SramSynthParams + ((UWORD)s << 4);
    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    memcpy(d, DefaultSynthParams, 16);
    rRAMG = 0;
    _asm ei _endasm;
}

void InstrHScanTableRows(void)
{
    UBYTE i;
    UBYTE used[16];
    UBYTE n;

    i = 16;
    do {
        used[i - 1] = 0;
        i--;
    } while (i);
    for (n = 0; n < 0x40; n++)
        if ((ReadInstrByte(n, 0) & 3) == 1)
            used[ReadInstrByte(n, 3) >> 4] = 1;
    for (i = 0; i < 0x10; i++)
        if (!used[i]) {
            ResetSynthParams(i);
            InstrHScanChainRows(i);
        }
}

UBYTE TableIsReferenced(void)
{
    UBYTE a, b;

    a = InstrHScanAllChainPhr();
    if (InstrHAnyCmdRef())
        return 0;
    InstrHScanTableRows();
    b = InstrHScanAllChainPhr();
    return a - b;
}

void CleanSongData(void)
{
    UBYTE phrases, chains;

    phrases = InstrHCountUsedPhrases();
    chains = InstrHCountUsedChains();
    wVramCursor = 0x98A4;
    DrawString("DE-DUPE...");
    DedupPhrases();
    DedupChains();
    wVramCursor = 0x98C4;
    DrawString("FREEING:");
    wVramCursor = 0x98E4;
    DrawFormat("CHAINS....");
    RemapSeqChains();
    DrawFormat("%d", chains - InstrHCountUsedChains());
    wVramCursor = 0x9904;
    DrawFormat("PHRASES...");
    RebuildPhraseAllocMap();
    DrawFormat("%d", phrases - InstrHCountUsedPhrases());
    wVramCursor = 0x9944;
    DrawFormat("OK!");
}

void CleanInstrData(void)
{
    wVramCursor = 0x98A4;
    DrawString("DE-DUPE...");
    DrawFreedTables();
    wVramCursor = 0x98C4;
    DrawString("FREEING:");
    wVramCursor = 0x98E4;
    DrawFormat("INSTRS....");
    DrawFormat("%d", FreeUnusedInstrs());
    wVramCursor = 0x9904;
    DrawFormat("TABLES....");
    DrawFormat("%d", BuildUsedTablesMap());
    wVramCursor = 0x9924;
    DrawFormat("SYNTHS....");
    DrawFormat("%d", TableIsReferenced());
    wVramCursor = 0x9964;
    DrawFormat("OK!");
}

void PhraseScreenUpdate(void)
{
    if (wLiveStartCol != 0xFF)
        return;
    if (wPlayMode && wPhrase == wChanPhrase[wSongCol])
        SetSpriteY(0, wChanPhraseRow[wSongCol]);
    else
        ClearSpriteY(0);
}

UBYTE CanEnterSongCol(UBYTE col)
{
    UBYTE chain, row, phrase;
    void (*fn)(void);

    if (col >= 4)
        DebugTrap();
    wMsgStrPtr = 0;
    if (wLiveMode && wChanActive[col]) {
        chain = wChanChain[col];
        if (chain & 0x80)
            return 0;
        phrase = wChanPhrase[col];
        if (phrase & 0x80)
            return 0;
        row = wChanChainRow[col];
        if (row & 0xF0)
            return 0;
        wSongRow = wChanSongRow[col];
    } else {
        chain = ReadSeqByte(col, wSongRow);
        if (chain & 0x80)
            return 0;
        row = wChainRow;
        fn = FAR_FN(SndGetChainPhrase);
        phrase = FarCallArg2(2, (UWORD)fn, chain, wChainRow);
        while (phrase == 0xFF && row)
            phrase = FarCallArg2(2, (UWORD)fn, chain, --row);
        if (phrase == 0xFF)
            return 0;
    }
    InstrHReadCursorCellAt();
    wSongCol = col;
    wChain = chain;
    wChainRow = row;
    wPhrase = phrase;
    FarCall(4, FAR_FN(DrawPhraseScreen));
    FarCall(0x1E, FAR_FN(ScreenPreDraw));
    return 1;
}

UBYTE HasEnterableColRight(void)
{
    UBYTE c;

    for (c = wSongCol + 1; c < 4; c++)
        if (CanEnterSongCol(c))
            break;
}

UBYTE HasEnterableColLeft(void)
{
    UBYTE c;

    for (c = wSongCol - 1; c < 4; c--)
        if (CanEnterSongCol(c))
            break;
}

/* The phrase screen's cut/copy: every selected cell into the clipboard
   (the note byte split over columns 0 and 1 unless the row flag is $FF),
   cleared on a cut. A selection of the note column alone on channel 2
   takes the instrument column too, copied but never cut (cutInstr). The
   note accessors take only the row: FarCallArg2/3 get one byte fewer. */
void CutCopyPhraseSelection(UBYTE cut)
{
    UBYTE x, y, t;
    UBYTE cutInstr = cut;

    if (wClipboardType != 1)
        return;
    ShowMessage(cutInstr ? "CUT" : "COPY");
    wCursorMode = 2;
    wSelRight = wPhraseCol;
    wSelBottom = wPhraseRow;
    SortBytePair(&wSelLeft, &wSelRight);
    SortBytePair(&wSelTop, &wSelBottom);
    if (wSelLeft == 4)
        wSelLeft = 3;
    if (wSelRight == 3)
        wSelRight = 4;
    if (wSelRight == 1 && wSongCol == 2) {
        cutInstr = 0;
        wSelRight = 2;
    }
    wSelRight++;
    wSelBottom++;
    for (y = wSelTop; y < wSelBottom; y++) {
        for (x = wSelLeft; x < wSelRight; x = x + 1) {
            wPhraseCol = x;
            wPhraseRow = y;
            InstrHReadCursorCellAt();
            switch (x) {
            case 0:
                if (wPhraseRowFlags[y] != 0xFF) {
                    t = FarCallArg2P(4, FAR_FN(PhraseReadNote), y);
                    WriteClipboardCell(0, y, t >> 4);
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, t & 0x0F);
                }
                if (wSelRight == 1 && wSongCol == 2) {
                    WriteClipboardCell(1, y, 0xFF);
                    cutInstr = 0;
                    goto instr;
                }
                break;
            case 1:
                if (wPhraseRowFlags[y] == 0xFF) {
                    WriteClipboardCell(0, y, 0);
                    WriteClipboardCell(1, y, FarCallArg2P(4, FAR_FN(PhraseReadNote), y));
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, 0);
                } else {
                    t = FarCallArg2P(4, FAR_FN(PhraseReadNote), y);
                    WriteClipboardCell(1, y, t & 0x0F);
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, t & 0xF0);
                }
                break;
            case 2:
            instr:
                WriteClipboardCell(2, y, FarCallArg2P(4, FAR_FN(ReadPhraseInstr), wPhrase, y));
                if (cutInstr)
                    FarCallArg3P(4, FAR_FN(WritePhraseInstrument), wPhrase, y, 0xFF);
                break;
            case 3:
                WriteClipboardCell(3, y, FarCallArg2P(4, FAR_FN(PhraseReadFx), wPhrase, y));
                if (cut)
                    FarCallArg3P(4, FAR_FN(WritePhraseFx), wPhrase, y, 0);
                break;
            case 4:
                WriteClipboardCell(4, y, FarCallArg2P(4, FAR_FN(PhraseReadFxVal), wPhrase, y));
                if (cut)
                    FarCallArg3P(4, FAR_FN(WritePhraseFxVal), wPhrase, y, 0);
                break;
            }
        }
    }
    if (wSelRight == 1)
        wSelRight = 3;
    FarCall(0x1C, FAR_FN(CopySelection));
    wPhraseCol = wSelLeft;
    wPhraseRow = wSelTop;
    FarCall(4, FAR_FN(DrawPhraseScreen));
}

void DrawPhraseCursor(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseCol;
    DrawPhraseCellValue(col, row);
}

void DrawWordRow(UBYTE n)
{
    UBYTE *p;
    UBYTE i, c, w;

    w = n;
    w--;
    if (w < 14) {
        p = SramWordNames + (unsigned int)w * 3;
        for (i = 0; i < 3; i++) {
            c = ReadSongByte(0, p++);
            if (!c)
                c = ' ';
            DrawStringIndexed(c);
        }
    } else
        DrawString("---");
}

void DrawPhraseCellField(UBYTE col, UBYTE row)
{
    char x;
    UBYTE y;
    UBYTE pal;
    UBYTE w;

    x = PhraseCellX[col];
    y = row + 2;
    if (wPhraseDrawMode)
        x += 3;
    if (x < 0)
        return;
    if (wPhraseRow & 0xF0)
        DebugTrap();
    if (wPhraseRowFlags[row] == 0xFE || wPhraseRowFlags[row] == 0xFF) {
        w = PhraseCellWidth[col];
        pal = PhraseCellPal[col];
    } else {
        x = PhraseCellXAlt[col];
        w = PhraseCellWidthAlt[col];
        pal = PhraseCellPalAlt[col];
    }
    ColorField(x, y, w, pal);
}

void DrawPhraseSelection(void)
{
    UBYTE x, y, left, top, right, bottom;
    UBYTE col;

    if (wClipboardType != 1)
        return;
    col = wPhraseCol;               /* the ROM reads the column into c for the test only */
    if (wSelLeft < col) {
        left = wSelLeft;
        right = wPhraseCol + 1;
    } else {
        right = wSelLeft + 1;
        left = wPhraseCol;
    }
    if (wSelTop < wPhraseRow) {
        top = wSelTop;
        bottom = wPhraseRow + 1;
    } else {
        bottom = wSelTop + 1;
        top = wPhraseRow;
    }
    /* the one-cell ring around the rectangle back to its column palettes... */
    for (x = left ? left - 1 : 0; x < (right == 5 ? 5 : right + 1); x++) {
        for (y = top ? top - 1 : 0; y < (bottom == 0x10 ? 0x10 : bottom + 1); y = y + 1) {
            if (x < left || x >= right || y < top || y >= bottom)
                DrawPhraseCellField(x, y);
        }
    }
    /* ...and the rectangle itself drawn as the cursor cell */
    for (x = left; x < right; x++)
        for (y = top; y < bottom; y = y + 1)
            DrawPhraseCellValue(x, y);
}

void InstrHReadCursorCellAt(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseCol;
    DrawPhraseCellField(col, row);
}

void DrawAllChanStatus(void)
{
    DrawChanStatus(0);
    DrawChanStatus(1);
    DrawChanStatus(2);
    DrawChanStatus(3);
}

void RedrawScreenFar1F(void)
{
    FarCall(4, FAR_FN(RedrawScreen));
}

UBYTE InstrHGetParamPlus4(void)
{
    return (UWORD)wSpeechCursorRow + 4;
}

void InstrHDrawParamField(void)
{
    ColorField(0, InstrHGetParamPlus4(), 3, !(wSpeechCursorRow & 1));
}

void DrawInstrParamField(void)
{
    ColorField(0, InstrHGetParamPlus4(), 3, hGameboyType ? 3 : 2);
}

void DrawSpeechInstrScreen(void)
{
    UBYTE *p;
    UBYTE row, i, c;

    wCurrentScreen = 0x0B;
    FarCall(1, FAR_FN(UiDrawTextHw));
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    wCurrentScreen = 6;
    FarCall(3, FAR_FN(InstrScreenFrame));
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = 0x9840;
    DrawString("TYPE   SPEECH");
    p = (UBYTE *)0xBDD0;
    row = 4;
    while (row < 4 + 14) {
        GetTilemapAddr(0, row);
        if (row & 1) {
            hBgDigitPalette = wShadowDigitPal;
            hBgPalette = wShadowPal;
        } else {
            hBgDigitPalette = 4;
            hBgPalette = 1;
        }
        i = 0;
        while (i < 3) {
            c = ReadSongByte(0, p);
            if (!c)
                break;
            DrawStringIndexed(c);
            p = p + 1;
            i = i + 1;
        }
        while (i < 3) {
            DrawStringIndexed(' ');
            p = p + 1;
            i = i + 1;
        }
        row++;
    }
    DrawInstrParamField();
}

/*
 * The word (speech) screen's input: A opens the name editor on the word
 * under the cursor, up/down move the cursor, select+direction leaves through
 * the screen map, A+B / select+B cut or copy the word (its 16 allophone
 * pairs and 3-letter name; a cut leaves the name "W-n"), select+A pastes it,
 * B+left / B+down step the instrument back. 1 when the key was taken.
 */
UBYTE WordScreenInput(void)
{
    UWORD p;
    UBYTE i;
    UBYTE v;

    switch (wButtons) {
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        p = (UWORD)0xBDD0 + wSpeechCursorRow * 3;
        wNameEditBuf[0] = ReadSongByte(0, p++);
        wNameEditBuf[1] = ReadSongByte(0, p++);
        wNameEditBuf[2] = ReadSongByte(0, p);
        wNameEditBuf[3] = 0;
        wNameEditBuf[4] = 0;
        wNameEditIsInstr = 0;
        FarCall(0x1B, FAR_FN(OpenNameEditor));
        return 1;
    case 0x04:
        if (!wSpeechCursorRow)
            return 0;
        InstrHDrawParamField();
        wSpeechCursorRow--;
        DrawInstrParamField();
        return 1;
    case 0x08:
        if (wSpeechCursorRow == 0x0D)
            return 0;
        InstrHDrawParamField();
        wSpeechCursorRow++;
        DrawInstrParamField();
        return 1;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveDown);
        RedrawScreenFar1F();
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveUp);
        RedrawScreenFar1F();
        return 1;
    case 0x41:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveRight);
        RedrawScreenFar1F();
        return 1;
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveLeft);
        RedrawScreenFar1F();
        return 1;
    case 0x30:
    case 0x60:
        if (!wAPressEnabled)
            return 0;
        if (wButtons == 0x60)
            ShowMessage("COPIED WORD");
        else
            ShowMessage("CUT WORD");
        wClipboardType = 0x0B;
        wCursorMode = 2;
        p = ((UWORD)wSpeechCursorRow << 5) + (UWORD)0xB890;
        for (i = 0; i < 0x10; i++) {
            WriteClipboardCell(0, i, ReadSongByte(0, p));
            if (wButtons == 0x30)
                WriteSongByte(0, p, 0);
            p++;
            WriteClipboardCell(1, i, ReadSongByte(0, p));
            if (wButtons == 0x30)
                WriteSongByte(0, p, 0);
            p++;
        }
        p = (UWORD)wSpeechCursorRow * 3 + (UWORD)0xBDD0;
        for (i = 0; i < 3; i++) {
            WriteClipboardCell(2, i, ReadSongByte(0, p));
            p++;
        }
        if (wButtons == 0x30) {
            GetTilemapAddr(0, InstrHGetParamPlus4());
            hBgDigitPalette = wSelectDigitPal;
            hBgPalette = wSelectPal;
            p -= 3;
            WriteSongByte(0, p++, 'W');
            DrawStringIndexed('W');
            WriteSongByte(0, p++, '-');
            DrawStringIndexed('-');
            i = wSpeechCursorRow;
            i += wSpeechCursorRow < 10 ? '0' : '7';
            WriteSongByte(0, p, i);
            DrawStringIndexed((char)i);
        }
        return 1;
    case 0x50:
        if (!wAPressEnabled)
            return 0;
        if (wClipboardType != 0x0B)
            return 0;
        ShowMessage("PASTED WORD");
        p = ((UWORD)wSpeechCursorRow << 5) + (UWORD)0xB890;
        for (i = 0; i < 0x10; i++) {
            if (p >= (UWORD)0xBDD0)
                DebugTrap();
            WriteSongByte(0, p++, ReadClipboardCell(0, i));
            if (p >= (UWORD)0xBDD0)
                DebugTrap();
            WriteSongByte(0, p++, ReadClipboardCell(1, i));
        }
        p = (UWORD)wSpeechCursorRow * 3 + (UWORD)0xBDD0;
        hBgDigitPalette = wSelectDigitPal;
        hBgPalette = wSelectPal;
        GetTilemapAddr(0, InstrHGetParamPlus4());
        for (i = 0; i < 3; i++) {
            if (p >= (UWORD)0xBE78)
                DebugTrap();
            v = ReadClipboardCell(2, i);
            WriteSongByte(0, p++, v);
            if (v)
                DrawStringIndexed((char)v);
            else
                DrawStringTile20();
        }
        return 1;
    case 0x22:
        FarCall(3, InstrSelectPrevUsed);
        return 1;
    case 0x28:
        FarCall(3, InstrSelectPrevPage);
        return 1;
    case 0xC0:
        if (!wButtonsSame)
            FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (!wButtonsSame && !wBHeld)
            FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    }
    return 0;
}
