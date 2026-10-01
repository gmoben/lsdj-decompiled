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
extern UBYTE wColPhrase[];                  /* $C453, [4] */
extern UBYTE wColChain[];                   /* $C44F, [4] */
extern UBYTE wPhraseDedupScratch5;          /* $C9CC */
void FreePhrase(void);                      /* $01:$4DD3 */
void FreeChain(void);                       /* $01:$4E43 */
void MarkChainUsed(void);                   /* $01:$4D67 */
void ShowOutOfMemory(void);                 /* $01:$75BD */
void DrawFreedTables(void);
UBYTE MarkInstrUsed(void);
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
    if (GetInstrEnv1F()) {
        wVramCursor = 0x98A8;
        DrawHexByte(ReadCurInstrByte(9));
        if (GetInstrByte1F9()) {
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
    DrawFormat("%d", MarkInstrUsed());
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
    DrawTextField(x, y, w, pal);
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
    DrawTextField(0, InstrHGetParamPlus4(), 3, !(wSpeechCursorRow & 1));
}

void DrawInstrParamField(void)
{
    DrawTextField(0, InstrHGetParamPlus4(), 3, hGameboyType ? 3 : 2);
}
