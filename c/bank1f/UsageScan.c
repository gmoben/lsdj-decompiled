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
void RebuildPhraseAllocMap(void);
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

void MarkTableUsed(UBYTE t)
{
    if (t < 0x20)
        wUsedTableMap[t] = 1;
}

UBYTE IsTableUsed(UBYTE t)
{
    return wUsedTableMap[t];
}

void MarkPhraseTablesUsed(UBYTE phrase)
{
    UBYTE *c;
    UBYTE *v;
    UBYTE row;

    c = SramPhraseFx + ((unsigned int)phrase << 4);
    v = SramPhraseFxVal + ((unsigned int)phrase << 4);
    for (row = 0; row < 16; row++) {
        if (ReadSongByte(2, c) == 1)
            MarkTableUsed(ReadSongByte(2, v));
        c = c + 1;
        v = v + 1;
    }
}

void MarkTableTablesUsed(UBYTE t)
{
    UWORD o;
    UBYTE *p1, *p2, *p3, *p4;
    UBYTE step;

    o = (UWORD)t << 4;
    p1 = (UBYTE *)0xB680 + o;
    p2 = (UBYTE *)0xB880 + o;
    p3 = (UBYTE *)0xBA80 + o;
    p4 = (UBYTE *)0xBC80 + o;
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

UBYTE IsPhraseUsedFar(UBYTE phrase)
{
    return FarCallArgumentByte(1, (UWORD)IsPhraseUsed, phrase);
}

void MarkTablesOfUsedPhrases(void)
{
    UBYTE phrase;

    for (phrase = 0; phrase < 0xFF; phrase++)
        if (IsPhraseUsedFar(phrase))
            MarkPhraseTablesUsed(phrase);
}

UBYTE CountUsedTables(void)
{
    UBYTE n, table;

    n = 0;
    for (table = 0; table < 0x20; table++)
        if (IsTableUsed(table))
            n++;
    return n;
}

UBYTE MarkTablesOfUsedTables(void)
{
    UBYTE before, table;

    before = CountUsedTables();
    for (table = 0; table < 0x20; table++)
        if (IsTableUsed(table))
            MarkTableTablesUsed(table);
    return !(before == CountUsedTables());
}

void MarkInstrumentTableUsed(UBYTE instrument)
{
    UBYTE v;

    v = ReadInstrumentByte(instrument, 6);
    if (v & 0x20)
        MarkTableUsed(v & 0x1F);
}

void MarkTablesOfUsedInstruments(void)
{
    UBYTE instrument;

    for (instrument = 0; instrument < 0x40; instrument++)
        if (IsInstrumentAllocated(instrument))
            MarkInstrumentTableUsed(instrument);
}

void MarkChainPhrasesUsed(UBYTE chain)
{
    UBYTE row, phrase;

    for (row = 0; row < 16; row++) {
        phrase = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, chain, row);
        if (phrase != 0xFF)
            FarCallArgument(1, FAR_FN(MarkPhraseUsed), phrase);
    }
}

UBYTE IsChainUsedFar(UBYTE chain)
{
    return FarCallArgumentByte(1, (UWORD)IsChainUsed, chain);
}

void MarkPhrasesOfUsedChains(void)
{
    UBYTE chain;

    for (chain = 0; chain < 0x80; chain++)
        if (IsChainUsedFar(chain))
            MarkChainPhrasesUsed(chain);
}

UBYTE CountUsedPhrases(void)
{
    UBYTE n, phrase;

    n = 0;
    for (phrase = 0; phrase < 0xFF; phrase++)
        if (IsPhraseUsedFar(phrase))
            n++;
    return n;
}

UBYTE CountUsedChains(void)
{
    UBYTE n, chain;

    n = 0;
    for (chain = 0; chain < 0x80; chain++)
        if (IsChainUsedFar(chain))
            n++;
    return n;
}

void RebuildPhraseAllocMap(void)
{
    UBYTE phrase;
    UWORD o;

    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    FillMemory(SramPhraseAlloc, 0, 0x20);
    rRAMG = 0;
    _asm ei _endasm;
    MarkPhrasesOfUsedChains();
    for (phrase = 0; phrase < 0xFF; phrase++) {
        if (IsPhraseUsedFar(phrase))
            continue;
        o = phrase;
        o <<= 4;
        o |= 0xA000;
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
