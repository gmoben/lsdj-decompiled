/*
 * Song data housekeeping -- LSDj 9.4.0, ROM bank $1F.
 *
 * Instrument/table/phrase allocation, the usage scans and counters, the
 * de-duplication and remapping behind the PROJECT screen's CLEAN SONG DATA
 * / CLEAN INSTR DATA.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wInstrumentRemapTable[];            /* $C9B8, [16] */
extern UBYTE wInstrumentRemapDirty;              /* $C9B7 */
extern UBYTE DefaultWaveFrameB1F[];         /* $1F:$491A, [16] */
extern UBYTE DefaultSynthParameters[];          /* $1F:$61FF, [16] */
extern UBYTE wNameEditBuffer[];                /* $C431, [5] */
extern UBYTE wColumnInstrument[];              /* $C468, [4] */
UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row);  /* $04:$4918 */
void WriteInstrumentName(UBYTE instrument);           /* $01:$4A49 */
UBYTE IsPhraseUsed(UBYTE phrase);
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
extern UBYTE wColumnChain[];                   /* $C44F, [4] */
extern UBYTE wDeduplicateTableIsDuplicate;              /* $C9CC */
extern UBYTE wDeduplicatePhraseUsed;              /* $C9C8 */
extern UBYTE wDeduplicatePhraseIsDuplicate;             /* $C9C9 */
extern UBYTE wDeduplicateChainUsed;               /* $C9CA */
extern UBYTE wDeduplicateChainIsDuplicate;              /* $C9CB */
void FreePhrase(void);                      /* $01:$4DD3 */
void FreeChain(void);                       /* $01:$4E43 */
void MarkChainUsed(void);                   /* $01:$4D67 */
void ShowOutOfMemory(void);                 /* $01:$75BD */
UBYTE FreeUnusedInstruments(void);
UBYTE CountUsedPhrases(void);
UBYTE CountUsedChains(void);
void RemapSequenceChains(void);
void RebuildPhraseAllocMap(void);
UBYTE FindDuplicateTable(UBYTE a, UBYTE b);
void RemapCommandTableRefs(UBYTE from, UBYTE to);
extern UBYTE DefaultInstrumentPulse[];           /* $1F:$4A02, [11] */
extern UBYTE DefaultInstrumentWave[];            /* $4A0D, [11] */
extern UBYTE DefaultInstrumentKit[];             /* $4A18, [14] */
UBYTE IsChainUsed(UBYTE chain);
UBYTE IsTableUsed(UBYTE t);
void MarkTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
void MarkPhrasesOfUsedChains(void);
UBYTE IsChainUsedFar(UBYTE chain);
UBYTE SynthFramesModified(UBYTE chain);
void ResetWaveFrame(UBYTE p);
void MarkPhraseTablesUsed(UBYTE phrase);
UBYTE IsTableFree(UWORD t);
UBYTE CountModifiedSynths(void);
UBYTE AnyCommandFUsed(void);
void ResetUnusedSynths(void);
void MarkTablesOfUsedInstruments(void);
void MarkTablesOfUsedPhrases(void);
UBYTE MarkTablesOfUsedTables(void);
UBYTE ClearUnusedTables(void);
void MarkChainPhrasesUsed(UBYTE chain);
UBYTE WaveFrameModified(UBYTE phrase);
UBYTE IsInstrumentAllocated(UBYTE instrument);
void MarkInstrumentTableUsed(UBYTE instrument);
UBYTE ClaimInstrumentSlot(UBYTE instrument);
UBYTE CountUsedTables(void);
void MarkTableTablesUsed(UBYTE t);
void ClearTable(UBYTE t);
void MarkPhraseUsed(UBYTE phrase);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instrument);
void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v);    /* $06:$5655 */

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

UBYTE ClaimInstrumentSlot(UBYTE instrument)
{
    UBYTE *source;
    UWORD n;
    UWORD field;
    UWORD p;

    if (instrument >= 0x40)
        DebugTrap();
    p = (UWORD)0xA040 + instrument;
    if (ReadSongByte(1, p))
        return 0;
    WriteSongByte(1, p, 1);
    switch (wSongColumn) {
    case 2:
        source = DefaultInstrumentKit;
        n = 14;
        break;
    case 3:
        source = DefaultInstrumentWave;
        n = 11;
        break;
    default:
        source = DefaultInstrumentPulse;
        n = 11;
    }
    for (field = 0; field != n; field++)
        WriteInstrumentByte(instrument, field, source[field]);
    return 1;
}

void InitializeRemapTable(void)
{
    UBYTE entry, v;
    UBYTE *p;

    for (entry = 0; entry != 0x10; entry++) {
        p = &wInstrumentRemapTable[entry];
        v = *p;
        if (v < 0x40) {
            *p = 0xFF;
            ClaimInstrumentSlot(v);
        }
    }
    wInstrumentRemapDirty = 0;
}

UBYTE AllocNextFreeInstrument(void)
{
    UBYTE instrument;

    for (instrument = 0; instrument < 0x40; instrument++) {
        if (ClaimInstrumentSlot(instrument)) {
            FarCallArgument3(4, (UWORD)WritePhraseInstrument, wPhrase, wPhraseRow, instrument);
            return instrument;
        }
    }
    strcpy(wStringBuffer, "OUT OF MEMORY. CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

/* 1 if the 16 bytes at bank:addr are all zero. (The loop reuses `bank`.) */
UBYTE IsSongBlockEmpty(UBYTE bank, UBYTE *address)
{
    ReadSongBytes(bank, address, wSongReadBuffer);
    for (bank = 0; bank != 16; bank++)
        if (wSongReadBuffer[bank])
            return 0;
    return 1;
}

UBYTE IsTableFree(UWORD t)
{
    t <<= 4;
    if (!IsSongBlockEmpty(1, (UBYTE *)0xB480 + t))
        return 0;
    if (!IsSongBlockEmpty(1, (UBYTE *)0xB680 + t))
        return 0;
    if (!IsSongBlockEmpty(1, (UBYTE *)0xBA80 + t))
        return 0;
    return IsSongBlockEmpty(0, (UBYTE *)0xB690 + t);
}

UBYTE FindFreeTable(void)
{
    UBYTE table;

    for (table = 0; table != 0x20; table++)
        if (IsTableFree((UWORD)table))
            return table;
    ShowMessage("OUT OF TABLES!");
    return 0xFF;
}

UBYTE IsSongChainUsed(void)
{
    UBYTE chain;
    UBYTE *p;
    UBYTE start;

    start = wColumnChain[wSongColumn];
    if (start & 0x80)
        start = 0;
    chain = start;
    do {
        p = SramChainAlloc + (chain >> 3);
        if (!(ReadSongByte(1, (UWORD)p) & (1 << (chain & 7)))) {
            FarCallArgument(1, FAR_FN(MarkChainUsed), chain);
            if (chain >= 0x80)
                DebugTrap();
            return chain;
        }
        chain++;
        if (chain == 0x80)
            chain = 0;
    } while (chain != start);
    FarCall(1, FAR_FN(ShowOutOfMemory));
    return 0xFF;
}

/* CLEAN INSTR DATA's instrument pass: clear the instr-alloc table, mark
   every instrument a phrase row uses, then reset each unmarked one (its
   name, its $B080 parameter block, the columns' current instrument).
   Returns how many of the instruments allocated before were freed. */
UBYTE FreeUnusedInstruments(void)
{
    UWORD p;
    UBYTE instrument;
    UBYTE phrase;
    UBYTE row;
    UBYTE n;
    UBYTE column;
    UWORD address;

    p = (UWORD)SramInstrAlloc;
    n = 0;
    _asm di _endasm;
    for (instrument = 0; instrument < 0x40; instrument++) {
        if (ReadSongByte(1, p))
            n++;
        WriteSongByte(1, p, 0);
        p++;
    }
    for (phrase = 0; phrase < 0xFF; phrase++) {
        for (row = 0; row < 0x10; row++) {
            instrument = FarCallArgument2P(4, FAR_FN(ReadPhraseInstrument), phrase, row);
            if (instrument < 0x40)
                WriteSongByte(1, (UWORD)SramInstrAlloc + instrument, 1);
        }
    }
    _asm ei _endasm;
    p = (UWORD)SramInstrAlloc;
    for (instrument = 0; instrument < 0x40; instrument++) {
        _asm di _endasm;
        if (!ReadSongByte(1, p)) {
            wNameEditBuffer[0] = 0;
            wNameEditBuffer[1] = 0;
            wNameEditBuffer[2] = 0;
            wNameEditBuffer[3] = 0;
            wNameEditBuffer[4] = 0;
            FarCallArgument(1, FAR_FN(WriteInstrumentName), instrument);
            address = instrument;
            address <<= 4;
            address |= 0xB000;
            address += 0x80;
            rRAMB = 1;
            _asm di _endasm;
            rRAMG = 0x0A;
            FillMemory((UBYTE *)address, 0, 0x10);
            rRAMG = 0;
            _asm ei _endasm;
            column = 4;
            do {
                /* SDCC fetches i into a over the element it just read
                   (`cp a,a`): the test is always true, so freeing any
                   instrument clears all four columns' */
                if (wColumnInstrument[column - 1] == instrument)
                    wColumnInstrument[column - 1] = 0xFF;
            } while (--column);
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

void MarkTableUsed(UBYTE t)
{
    if (t < 0x20)
        wUsedTableMap[t] = 1;
}

UBYTE IsTableUsed(UBYTE t)
{
    return wUsedTableMap[t];
}

UBYTE IsInstrumentAllocated(UBYTE instrument)
{
    return ReadSongByte(1, SramInstrAlloc + (UWORD)instrument);
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

void ClearTableRow(UBYTE bank, UBYTE *p)
{
    UBYTE step;

    for (step = 0; step < 0x10; step++) {
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

UBYTE ClearUnusedTables(void)
{
    UBYTE n, table;

    n = 0;
    for (table = 0; table < 0x20; table++) {
        if (!IsTableUsed(table)) {
            if (!IsTableFree((UWORD)table))
                n++;
            ClearTable(table);
        }
    }
    return n;
}

UBYTE FreeUnusedTables(void)
{
    FillMemory(wUsedTableMap, 0, 0x20);
    MarkTablesOfUsedInstruments();
    MarkTablesOfUsedPhrases();
    do
        ;
    while (MarkTablesOfUsedTables());
    return ClearUnusedTables();
}

void RemapSequenceChains(void)
{
    UBYTE row, chain, column;

    column = wSongColumn;
    _asm
        di
    _endasm;
    for (chain = 0; chain < 0x80; chain++)
        FarCallArgument(1, FAR_FN(FreeChain), chain);
    _asm
        ei
        di
    _endasm;
    for (wSongColumn = 0; wSongColumn < 4; wSongColumn++) {
        for (row = 0; row != 0xC0; row++) {
            chain = ReadSequenceByte(wSongColumn, row);
            if (!(chain & 0x80))
                FarCallArgument(1, FAR_FN(MarkChainUsed), chain);
        }
    }
    wSongColumn = column;
    _asm
        ei
        di
    _endasm;
    for (chain = 0; chain < 0x80; chain++) {
        if (!FarCallArgumentByte(1, FAR_FN(IsChainUsed), chain)) {
            for (row = 0; row < 0x10; row++) {
                WriteChainPhrase(chain, row, 0xFF);
                FarCallArgument3P(6, FAR_FN(SetChainTranspose), chain, row, 0);
            }
        }
    }
    _asm
        ei
    _endasm;
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

/* The three duplicate scanners share one register story. The row offsets
   oa/ob and the row pointers p/q all live in their slots; the first sum
   goes through t, a UWORD declared last and used once, which the
   allocator spills by the use-count rule so the sum is written to its
   slot and cast into p byte-wise, while every other sum is a temporary
   in bc cast into its pointer. `*p++ != *q++` keeps the two bytes as
   location-less temporaries: i holds c into the first loop, so the
   first byte lands in b there and in c in the later loops. */
UBYTE FindDuplicatePhrase(UBYTE a, UBYTE b)
{
    UBYTE *p, *q;
    UWORD offsetA, offsetB;
    UBYTE row;
    UWORD t;

    offsetA = a;
    offsetB = b;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    offsetA <<= 4;
    offsetB <<= 4;
    t = offsetA + (UWORD)0xA000;                 /* bank 0: phrase notes */
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)0xA000);
    rRAMB = 0;
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 3;
    p = (UBYTE *)(offsetA + (UWORD)0xB000);                      /* bank 3: phrase instruments */
    q = (UBYTE *)(offsetB + (UWORD)0xB000);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 2;
    p = (UBYTE *)(offsetA | (UWORD)0xA000);      /* bank 2: phrase commands */
    q = (UBYTE *)(offsetB | (UWORD)0xA000);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xAFF0);                      /* bank 2: command values */
    q = (UBYTE *)(offsetB + (UWORD)0xAFF0);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

UBYTE FindDuplicateChain(UBYTE a, UBYTE b)
{
    UBYTE *p, *q;
    UWORD offsetA, offsetB;
    UBYTE row;
    UWORD t;

    offsetA = a;
    offsetB = b;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    offsetA <<= 4;
    offsetB <<= 4;
    t = offsetA + (UWORD)0xA080;                 /* bank 1: chain phrases */
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)0xA080);
    rRAMB = 1;
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xA880);                      /* bank 1: chain transposes */
    q = (UBYTE *)(offsetB + (UWORD)0xA880);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

UBYTE FindDuplicateTable(UBYTE a, UBYTE b)
{
    UBYTE *p, *q;
    UWORD offsetA, offsetB;
    UBYTE step;
    UWORD t;

    offsetA = a;
    offsetB = b;
    offsetA <<= 4;
    offsetB <<= 4;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    t = offsetA + (UWORD)0xB680;                 /* bank 1: table fx 1 */
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)0xB680);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xB880);                      /* bank 1: table fx 1 values */
    q = (UBYTE *)(offsetB + (UWORD)0xB880);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xBA80);                      /* bank 1: table fx 2 */
    q = (UBYTE *)(offsetB + (UWORD)0xBA80);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xBC80);                      /* bank 1: table fx 2 values */
    q = (UBYTE *)(offsetB + (UWORD)0xBC80);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xB690);                      /* bank 0: table envelopes */
    q = (UBYTE *)(offsetB + (UWORD)0xB690);
    rRAMB = 0;
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)0xB480);                      /* bank 1: table transposes */
    q = (UBYTE *)(offsetB + (UWORD)0xB480);
    rRAMB = 1;
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

void RemapPhraseRefs(UBYTE from, UBYTE to)
{
    UBYTE *p;
    UBYTE column;

    p = SramChainPhrases;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (; p != SramChainPhrases + 0x800; p++)
        if (from == *p)
            *p = to;
    rRAMG = 0;
    _asm ei _endasm;
    FarCallArgument(1, FAR_FN(FreePhrase), from);
    column = 4;
    do {
        if (from == wColumnPhrase[column - 1])
            wColumnPhrase[column - 1] = to;
        column--;
    } while (column);
}

void RemapChainRefs(UBYTE from, UBYTE to)
{
    UBYTE *p;
    UBYTE column;

    p = SramSequence;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    for (; p != SramSequence + 0x400; p++)
        if (from == *p)
            *p = to;
    rRAMG = 0;
    _asm ei _endasm;
    FarCallArgument(1, FAR_FN(FreeChain), from);
    column = 4;
    do {
        if (from == wColumnChain[column - 1])
            wColumnChain[column - 1] = to;
        column--;
    } while (column);
}

void RemapCommandTableRefs(UBYTE from, UBYTE to)
{
    volatile UWORD command;
    volatile UWORD value;
    volatile UBYTE instrument;
    volatile UBYTE nt;
    UBYTE nf;

    command = (UWORD)0xB680;
    value = (UWORD)0xB880;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    while (command != (UWORD)0xB880) {
        if (*(UBYTE *)command == 1 && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    command = (UWORD)0xBA80;
    value = (UWORD)0xBC80;
    while (command != (UWORD)0xBC80) {
        if (*(UBYTE *)command == 1 && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    rRAMB = 2;
    command = (UWORD)0xA000;
    value = (UWORD)0xAFF0;
    while (command != (UWORD)0xAFF0) {
        if (*(UBYTE *)command == 1 && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    rRAMG = 0;
    _asm ei _endasm;
    instrument = 0;
    nt = to | 0x20;
    nf = from | 0x20;
    while (instrument != 0x40) {
        if (ReadInstrumentByte(instrument, 6) == nf)
            WriteInstrumentByte(instrument, 6, nt);
        instrument++;
    }
}

void DeduplicatePhrases(void)
{
    UBYTE phrase, candidate;

    for (phrase = 0; phrase < 0xFF; phrase = phrase + 1) {
        if (!IsPhraseUsedFar(phrase))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(phrase);
        for (candidate = phrase + 1; candidate < 0xFF; candidate = candidate + 1) {
            wDeduplicatePhraseUsed = IsPhraseUsedFar(candidate);
            if (wDeduplicatePhraseUsed) {
                wDeduplicatePhraseIsDuplicate = FindDuplicatePhrase(phrase, candidate);
                if (wDeduplicatePhraseIsDuplicate)
                    RemapPhraseRefs(candidate, phrase);
            }
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawStringTile20();
    DrawStringTile20();
}

void DeduplicateChains(void)
{
    UBYTE chain, candidate;

    for (chain = 0; chain < 0x80; chain = chain + 1) {
        if (!IsChainUsedFar(chain))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(chain);
        for (candidate = chain + 1; candidate < 0x80; candidate = candidate + 1) {
            wDeduplicateChainUsed = IsChainUsedFar(candidate);
            if (wDeduplicateChainUsed) {
                wDeduplicateChainIsDuplicate = FindDuplicateChain(chain, candidate);
                if (wDeduplicateChainIsDuplicate)
                    RemapChainRefs(candidate, chain);
            }
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawString("OK");
}

void DeduplicateTables(void)
{
    UBYTE table, candidate;

    for (table = 0; table < 0x20; table = table + 1) {
        if (IsTableFree(table))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(table);
        for (candidate = table + 1; candidate < 0x20; candidate = candidate + 1) {
            if (IsTableFree(candidate))
                continue;
            wDeduplicateTableIsDuplicate = FindDuplicateTable(table, candidate);
            if (wDeduplicateTableIsDuplicate)
                RemapCommandTableRefs(candidate, table);
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawString("OK");
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

UBYTE WaveFrameModified(UBYTE w)
{
    UBYTE *p;
    UBYTE byte;

    p = SramWaveFrames + ((UWORD)w << 4);
    for (byte = 0; byte < 0x10; byte++) {
        if (ReadSongByte(3, p) != DefaultWaveFrameB1F[byte])
            return 1;
        p = p + 1;
    }
    return 0;
}

UBYTE SynthFramesModified(UBYTE chain)
{
    UBYTE p, frame;

    p = chain << 4;
    for (frame = 0; frame != 16; frame++) {
        if (WaveFrameModified(p))
            return 1;
        p++;
    }
    return 0;
}

UBYTE CountModifiedSynths(void)
{
    UBYTE n, synth;

    n = 0;
    for (synth = 0; synth < 0x10; synth++)
        if (SynthFramesModified(synth))
            n++;
    return n;
}

UBYTE AnyPhraseUsesCommandF(void)
{
    UBYTE *p;
    UBYTE phrase, row;

    p = SramPhraseFx;
    for (phrase = 0; phrase != 0xFF; phrase++) {
        row = 0;
        while (1) {
            if (!(row < 0x10))
                break;
            if (ReadSongByte(2, p) == 6)
                return 1;
            p = p + 1;
            row = row + 1;
        }
    }
    return 0;
}

UBYTE AnyTableUsesCommandF(void)
{
    UBYTE *p, *q;
    UBYTE table, step, ok;

    p = (UBYTE *)0xB680;
    q = (UBYTE *)0xBA80;
    for (table = 0; table < 0x20; table++) {
        for (step = 0; step < 0x10; step = step + 1) {
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

void ResetSynthWaves(UBYTE chain)
{
    UBYTE p, frame;

    p = chain << 4;
    for (frame = 0; frame < 16; frame++) {
        ResetWaveFrame(p);
        p++;
    }
}

void ResetSynthParameters(UBYTE s)
{
    UBYTE *d;

    d = SramSynthParams + ((UWORD)s << 4);
    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    memcpy(d, DefaultSynthParameters, 16);
    rRAMG = 0;
    _asm ei _endasm;
}

void ResetUnusedSynths(void)
{
    UBYTE synth;
    UBYTE used[16];
    UBYTE instrument;

    synth = 16;
    do {
        used[synth - 1] = 0;
        synth--;
    } while (synth);
    for (instrument = 0; instrument < 0x40; instrument++)
        if ((ReadInstrumentByte(instrument, 0) & 3) == 1)
            used[ReadInstrumentByte(instrument, 3) >> 4] = 1;
    for (synth = 0; synth < 0x10; synth++)
        if (!used[synth]) {
            ResetSynthParameters(synth);
            ResetSynthWaves(synth);
        }
}

UBYTE FreeUnusedSynths(void)
{
    UBYTE a, b;

    a = CountModifiedSynths();
    if (AnyCommandFUsed())
        return 0;
    ResetUnusedSynths();
    b = CountModifiedSynths();
    return a - b;
}

void CleanSongData(void)
{
    UBYTE phrases, chains;

    phrases = CountUsedPhrases();
    chains = CountUsedChains();
    wVramCursor = 0x98A4;
    DrawString("DE-DUPE...");
    DeduplicatePhrases();
    DeduplicateChains();
    wVramCursor = 0x98C4;
    DrawString("FREEING:");
    wVramCursor = 0x98E4;
    printf("CHAINS....");
    RemapSequenceChains();
    printf("%d", chains - CountUsedChains());
    wVramCursor = 0x9904;
    printf("PHRASES...");
    RebuildPhraseAllocMap();
    printf("%d", phrases - CountUsedPhrases());
    wVramCursor = 0x9944;
    printf("OK!");
}

void CleanInstrumentData(void)
{
    wVramCursor = 0x98A4;
    DrawString("DE-DUPE...");
    DeduplicateTables();
    wVramCursor = 0x98C4;
    DrawString("FREEING:");
    wVramCursor = 0x98E4;
    printf("INSTRS....");
    printf("%d", FreeUnusedInstruments());
    wVramCursor = 0x9904;
    printf("TABLES....");
    printf("%d", FreeUnusedTables());
    wVramCursor = 0x9924;
    printf("SYNTHS....");
    printf("%d", FreeUnusedSynths());
    wVramCursor = 0x9964;
    printf("OK!");
}
