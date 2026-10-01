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
UBYTE IsPhraseUsed(UBYTE phrase);
UBYTE IsChainUsed(UBYTE chain);
void InstrHReadCursorCell(UBYTE col, UBYTE row);
void DrawPhraseCellValue(UBYTE col, UBYTE row);
UBYTE InstrHGetTableUsed(UBYTE t);
void InstrHMarkTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
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
UBYTE IsInstrAllocated(UBYTE instr);
void InstrHMarkInstrTable(UBYTE instr);
void DrawChanStatus(UBYTE chan);
void GetInstrFlags1F5(UBYTE a, UBYTE b);
UBYTE InstrHGetParamPlus4(void);
UBYTE ClaimInstrSlot(UBYTE instr);
UBYTE InstrHCountUsedTables(void);
void ReadTableRowCmds(UBYTE t);
void ClearTable(UBYTE t);
void MarkPhraseUsed(UBYTE phrase);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr);

void DrawInstrTableField(void)
{
    char v;

    v = ReadCurInstrByte(6);
    if (v & 0x20)
        DrawHexByte(v & 0x1F);
    else
        DrawString("--");
}

void DrawTableFlag(void)
{
    GetInstrFlags1F5(0x40, 2);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    GetInstrFlags1F5(0x20, 9);
}

UBYTE GetInstrEnv1F(void)
{
    return ReadCurInstrByte(1) & 0x0F;
}

UBYTE GetInstrByte1F9(void)
{
    UBYTE a, b;

    a = GetInstrEnv1F();
    b = ReadCurInstrByte(9) & 0x0F;
    return a && b;
}

void DrawKitNameCell(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
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

UBYTE FindFreeTable(void)
{
    UBYTE i;

    for (i = 0; i != 0x20; i++)
        if (IsTableFree((UWORD)i))
            return i;
    ShowMessage("OUT OF TABLES!");
    return 0xFF;
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

void PhraseScreenUpdate(void)
{
    if (wLiveStartCol != 0xFF)
        return;
    if (wPlayMode && wPhrase == wChanPhrase[wSongCol])
        SetSpriteY(0, wChanPhraseRow[wSongCol]);
    else
        ClearSpriteY(0);
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

void InstrHReadCursorCellAt(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseCol;
    InstrHReadCursorCell(col, row);
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
    DrawTextField(0, InstrHGetParamPlus4(), 3, !(wSpeechCursorRow & 1));
}

void DrawInstrParamField(void)
{
    DrawTextField(0, InstrHGetParamPlus4(), 3, hGameboyType ? 3 : 2);
}
