/*
 * Bank $1F helpers -- LSDj 9.4.0: instrument-screen field readers, the
 * used-table map, far-call wrappers into bank 1, and the phrase cursor.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wUsedTableMap[];               /* $C47D, [32] tables referenced by instruments */
extern UBYTE wSpeechCursorRow;              /* $C429 */
extern UBYTE wSongCol;                      /* $C41D */
extern UBYTE DefaultWaveFrameB1F[];         /* $1F:$491A, [16] */
extern UBYTE DefaultSynthParams[];          /* $1F:$61FF, [16] */
extern UBYTE wPhraseCol;                    /* $C415 */
extern UBYTE wPhraseRow;                    /* $C416 */
UBYTE FarCallArgByte(UBYTE bank, UWORD fn, UBYTE arg);
UBYTE IsPhraseUsed(UBYTE phrase);
UBYTE IsChainUsed(UBYTE chain);
void RedrawScreen(void);
void InstrHReadCursorCell(UBYTE col, UBYTE row);
void DrawPhraseCellValue(UBYTE col, UBYTE row);
UBYTE InstrHGetTableUsed(UBYTE t);
void InstrHMarkTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
UBYTE IsChainUsedFar(UBYTE chain);
UBYTE InstrHScanChainPhrases(UBYTE chain);
void ResetWaveFrame(UBYTE p);
void ComparePhraseToWork(UBYTE phrase);
UBYTE IsTableFree(UWORD t);
UBYTE InstrHScanAllChainPhr(void);
UBYTE InstrHAnyCmdRef(void);
void InstrHScanTableRows(void);
void InstrHScanInstrTables(void);
void InstrHScanUsedPhrases(void);
UBYTE InstrHProcessUsedTables(void);
UBYTE InstrHCollectUsedTables(void);
void MarkChainPhrasesUsed(UBYTE chain);
UBYTE MarkPhraseDataUsed(UBYTE phrase);
UBYTE CanEnterSongCol(UBYTE col);
UBYTE IsInstrAllocated(UBYTE instr);
void InstrHMarkInstrTable(UBYTE instr);

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

UBYTE IsPhraseUsedFar(UBYTE phrase)
{
    return FarCallArgByte(1, (UWORD)IsPhraseUsed, phrase);
}

void InstrHScanUsedPhrases(void)
{
    UBYTE i;

    for (i = 0; i < 0xFF; i++)
        if (IsPhraseUsedFar(i))
            ComparePhraseToWork(i);
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

UBYTE InstrHScanChainPhrases(UBYTE chain)
{
    UBYTE p, i;

    p = chain << 4;
    for (i = 0; i != 16; i++) {
        if (MarkPhraseDataUsed(p))
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

void InstrHReadCursorCellAt(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseCol;
    InstrHReadCursorCell(col, row);
}

void RedrawScreenFar1F(void)
{
    FarCall(4, FAR_FN(RedrawScreen));
}

UBYTE InstrHGetParamPlus4(void)
{
    return (UWORD)wSpeechCursorRow + 4;
}
