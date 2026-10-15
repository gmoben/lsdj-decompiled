/*
 * The usage scans -- LSDj 9.4.0, ROM bank $1F.
 *
 * Which tables, phrases and chains the song uses: the marks, the walks that
 * set them, the counts.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

UBYTE IsPhraseUsed(UBYTE phrase);
UBYTE CountUsedPhrases(void);
UBYTE CountUsedChains(void);
void RebuildPhraseAllocationMap(void);
UBYTE IsChainUsed(UBYTE chain);
UBYTE IsTableUsed(UBYTE t);
void MarkTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
void MarkPhrasesOfUsedChains(void);
UBYTE IsChainUsedFar(UBYTE chain);
void MarkPhraseTablesUsed(UBYTE phrase);
void MarkTablesOfUsedInstruments(void);
void MarkTablesOfUsedPhrases(void);
UBYTE MarkTablesOfUsedTables(void);
void MarkChainPhrasesUsed(UBYTE chain);
UBYTE IsInstrumentAllocated(UBYTE instrument);
void MarkInstrumentTableUsed(UBYTE instrument);
UBYTE CountUsedTables(void);
void MarkTableTablesUsed(UBYTE t);
void MarkPhraseUsed(UBYTE phrase);

/* $4F04: set wUsedTableMap[t] (t < $20) */
void MarkTableUsed(UBYTE t)
{
    if (t < 0x20)
        wUsedTableMap[t] = 1;
}

/* $4F1C: -> wUsedTableMap[t], the table's mark */
UBYTE IsTableUsed(UBYTE t)
{
    return wUsedTableMap[t];
}

/* $4F49: mark the tables that `phrase`'s A commands run (in the first
   command column) used */
void MarkPhraseTablesUsed(UBYTE phrase)
{
    UBYTE *c;
    UBYTE *v;
    UBYTE row;

    c = SramPhraseCommands + ((unsigned int)phrase << 4);
    v = SramPhraseCommandValues + ((unsigned int)phrase << 4);
    for (row = 0; row < 16; row++) {
        if (ReadSongByte(2, c) == 1)
            MarkTableUsed(ReadSongByte(2, v));
        c = c + 1;
        v = v + 1;
    }
}

/* $4FC5: mark the tables table t's two command columns name with A
   (MarkTableUsed) */
void MarkTableTablesUsed(UBYTE t)
{
    UWORD o;
    UBYTE *p1, *p2, *p3, *p4;
    UBYTE step;

    o = (UWORD)t << 4;
    p1 = SramTableCommands1 + o;
    p2 = SramTableCommandValues1 + o;
    p3 = SramTableCommands2 + o;
    p4 = SramTableCommandValues2 + o;
    for (step = 0; step < 0x10; step++) {
        if (ReadSongByte(1, (UWORD)p1) == 1)
            MarkTableUsed(ReadSongByte(1, (UWORD)p2));
        if (ReadSongByte(1, (UWORD)p3) == 1)
            MarkTableUsed(ReadSongByte(1, (UWORD)p4));
        p1++;
        p2++;
        p3++;
        p4++;
    }
}

/* $5091: IsPhraseUsed(`phrase`), in bank 1 */
UBYTE IsPhraseUsedFar(UBYTE phrase)
{
    return FarCallArgumentByte(1, (UWORD)IsPhraseUsed, phrase);
}

/* $50A8: mark the tables each used phrase's A commands name
   (MarkPhraseTablesUsed) */
void MarkTablesOfUsedPhrases(void)
{
    UBYTE phrase;

    for (phrase = 0; phrase < 0xFF; phrase++)
        if (IsPhraseUsedFar(phrase))
            MarkPhraseTablesUsed(phrase);
}

/* $50CC: count the marked tables in wUsedTableMap */
UBYTE CountUsedTables(void)
{
    UBYTE n, table;

    n = 0;
    for (table = 0; table < 0x20; table++)
        if (IsTableUsed(table))
            n++;
    return n;
}

/* $50F6: mark the tables each marked table's A commands name; 1 if that
   marked more */
UBYTE MarkTablesOfUsedTables(void)
{
    UBYTE before, table;

    before = CountUsedTables();
    for (table = 0; table < 0x20; table++)
        if (IsTableUsed(table))
            MarkTableTablesUsed(table);
    return !(before == CountUsedTables());
}

/* $513D: mark instrument i's table used if its TABLE is on (byte 6 bit 5) */
void MarkInstrumentTableUsed(UBYTE instrument)
{
    UBYTE v;

    v = ReadInstrumentByte(instrument, INSTRUMENT_BYTE_TABLE);
    if (v & 0x20)
        MarkTableUsed(v & 0x1F);
}

/* $515F: mark the table of every allocated instrument
   (MarkInstrumentTableUsed) */
void MarkTablesOfUsedInstruments(void)
{
    UBYTE instrument;

    for (instrument = 0; instrument < INSTRUMENTS; instrument++)
        if (IsInstrumentAllocated(instrument))
            MarkInstrumentTableUsed(instrument);
}

/* $5413: mark the phrases of chain `chain` used */
void MarkChainPhrasesUsed(UBYTE chain)
{
    UBYTE row, phrase;

    for (row = 0; row < 16; row++) {
        phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, chain, row);
        if (phrase != EMPTY)
            FarCallArgument(1, FAR_FN(MarkPhraseUsed), phrase);
    }
}

/* $5461: IsChainUsed(`chain`), in bank 1 */
UBYTE IsChainUsedFar(UBYTE chain)
{
    return FarCallArgumentByte(1, (UWORD)IsChainUsed, chain);
}

/* $5478: mark the phrases every used chain plays (MarkChainPhrasesUsed) */
void MarkPhrasesOfUsedChains(void)
{
    UBYTE chain;

    for (chain = 0; chain < 0x80; chain++)
        if (IsChainUsedFar(chain))
            MarkChainPhrasesUsed(chain);
}

/* $5DC9: count the phrases in use (IsPhraseUsedFar) */
UBYTE CountUsedPhrases(void)
{
    UBYTE n, phrase;

    n = 0;
    for (phrase = 0; phrase < 0xFF; phrase++)
        if (IsPhraseUsedFar(phrase))
            n++;
    return n;
}

/* $5DF3: count the chains in use (IsChainUsedFar) */
UBYTE CountUsedChains(void)
{
    UBYTE n, chain;

    n = 0;
    for (chain = 0; chain < 0x80; chain++)
        if (IsChainUsedFar(chain))
            n++;
    return n;
}

/* $5E1D: clear the phrase-alloc bitmap ($3E82, 32B) + rescan via used chains */
void RebuildPhraseAllocationMap(void)
{
    UBYTE phrase;
    UWORD o;

    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    FillMemory(SramPhraseAllocation, 0, 0x20);
    rRAMG = 0;
    _asm ei _endasm;
    MarkPhrasesOfUsedChains();
    for (phrase = 0; phrase < 0xFF; phrase++) {
        if (IsPhraseUsedFar(phrase))
            continue;
        o = phrase;
        o <<= 4;
        o |= SRAM_PHRASE_NOTES;
        _asm di _endasm;
        rRAMG = 0x0A;
        rRAMB = 0;
        FillMemory((UBYTE *)o, 0, 0x10);
        rRAMB = 2;
        FillMemory((UBYTE *)o, 0, 0x10);
        o += 0xFF0;
        FillMemory((UBYTE *)o, 0, 0x10);
        o += 0x10;
        rRAMB = 3;
        FillMemory((UBYTE *)o, 0xFF, 0x10);
        rRAMG = 0;
        _asm ei _endasm;
    }
}
