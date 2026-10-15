/*
 * CLEAN SONG DATA and CLEAN INSTR DATA -- LSDj 9.4.0, ROM bank $1F.
 *
 * The project screen's two clean-ups: unused instruments, tables and synths
 * freed, duplicate phrases, chains and tables folded and their references
 * remapped.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void WriteChainPhrase(UBYTE chain, UBYTE row, UBYTE phrase);
extern UBYTE wInstrumentRemapTable[];       /* $C9B8, [16] */
extern UBYTE wInstrumentRemapDirty;         /* $C9B7 */
extern UBYTE wNameEditBuffer[];             /* $C431, [5] */
extern UBYTE wColumnInstrument[];           /* $C468, [4] */
UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row); /* $04:$4918 */
void WriteInstrumentName(UBYTE instrument); /* $01:$4A49 */
extern UBYTE wColumnPhrase[];               /* $C453, [4] */
extern UBYTE wColumnChain[];                /* $C44F, [4] */
extern UBYTE wDeduplicateTableIsDuplicate;  /* $C9CC */
extern UBYTE wDeduplicatePhraseUsed;        /* $C9C8 */
extern UBYTE wDeduplicatePhraseIsDuplicate; /* $C9C9 */
extern UBYTE wDeduplicateChainUsed;         /* $C9CA */
extern UBYTE wDeduplicateChainIsDuplicate;  /* $C9CB */
void FreePhrase(void);                      /* $01:$4DD3 */
void FreeChain(void);                       /* $01:$4E43 */
void MarkChainUsed(void);                   /* $01:$4D67 */
UBYTE FreeUnusedInstruments(void);
UBYTE CountUsedPhrases(void);
UBYTE CountUsedChains(void);
void RemapSequenceChains(void);
void RebuildPhraseAllocationMap(void);
UBYTE FindDuplicateTable(UBYTE a, UBYTE b);
void RemapCommandTableRefs(UBYTE from, UBYTE to);
UBYTE IsChainUsed(UBYTE chain);
UBYTE IsTableUsed(UBYTE t);
UBYTE IsPhraseUsedFar(UBYTE phrase);
UBYTE IsChainUsedFar(UBYTE chain);
UBYTE IsTableFree(UWORD t);
UBYTE CountModifiedSynths(void);
UBYTE AnyCommandFUsed(void);
void ResetUnusedSynths(void);
void MarkTablesOfUsedInstruments(void);
void MarkTablesOfUsedPhrases(void);
UBYTE MarkTablesOfUsedTables(void);
UBYTE ClearUnusedTables(void);
UBYTE ClaimInstrumentSlot(UBYTE instrument);
void ClearTable(UBYTE t);
void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v); /* $06:$5655 */

/* $4A26: fill wInstrumentRemapTable..+16 with identity 0..15 (remap table
   for clone/dedupe) */
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

    p = (UWORD)SramInstrumentAllocation;
    n = 0;
    _asm di _endasm;
    for (instrument = 0; instrument < INSTRUMENTS; instrument++) {
        if (ReadSongByte(1, p))
            n++;
        WriteSongByte(1, p, 0);
        p++;
    }
    for (phrase = 0; phrase < 0xFF; phrase++) {
        for (row = 0; row < 0x10; row++) {
            instrument = FarCallArgument2P(4, FAR_FN(ReadPhraseInstrument), phrase, row);
            if (instrument < INSTRUMENTS)
                WriteSongByte(1, (UWORD)SramInstrumentAllocation + instrument, 1);
        }
    }
    _asm ei _endasm;
    p = (UWORD)SramInstrumentAllocation;
    for (instrument = 0; instrument < INSTRUMENTS; instrument++) {
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

/* $5183: zero 16 bytes from `p` in SRAM bank `bank`: one table's column */
void ClearTableRow(UBYTE bank, UBYTE *p)
{
    UBYTE step;

    for (step = 0; step < 0x10; step++) {
        WriteSongByte(bank, p, 0);
        p = p + 1;
    }
}

/* $51B9: wipe table n (<$20) across all five column regions ($3480..$3C80) */
void ClearTable(UBYTE t)
{
    UWORD o;
    UBYTE *p1, *p2, *p3, *p4, *p5;

    o = (UWORD)t << 4;
    if (t >= 0x20)
        DebugTrap();
    p1 = SramTableCommands1 + o;
    p2 = SramTableCommandValues1 + o;
    p3 = SramTableCommands2 + o;
    p4 = SramTableCommandValues2 + o;
    p5 = SramTableTransposes + o;
    o += (UWORD)SRAM_TABLE_ENVELOPES;
    ClearTableRow(1, p1);
    ClearTableRow(1, p2);
    ClearTableRow(1, p3);
    ClearTableRow(1, p4);
    ClearTableRow(1, p5);
    ClearTableRow(0, (UBYTE *)o);
}

/* $526B: clear every unmarked table; -> how many of them were not empty */
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

/* $52B3: CLEAN INSTR: mark the tables instruments, phrases and tables use,
   clear the rest; -> how many were freed */
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

/* $52DA: walk the 4 sequence columns remapping chain refs (CleanSongData
   step) */
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
    t = offsetA + (UWORD)SRAM_PHRASE_NOTES;
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)SRAM_PHRASE_NOTES);
    rRAMB = 0;
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 3;
    p = (UBYTE *)(offsetA + (UWORD)SRAM_PHRASE_INSTRUMENTS);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_PHRASE_INSTRUMENTS);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 2;
    p = (UBYTE *)(offsetA | (UWORD)SRAM_PHRASE_COMMANDS);
    q = (UBYTE *)(offsetB | (UWORD)SRAM_PHRASE_COMMANDS);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_PHRASE_COMMAND_VALUES);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_PHRASE_COMMAND_VALUES);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

/* $5644: does chain b equal chain a? compares the 16 phrases and transposes;
   1 = duplicate */
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
    t = offsetA + (UWORD)SRAM_CHAIN_PHRASES;
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)SRAM_CHAIN_PHRASES);
    rRAMB = 1;
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_CHAIN_TRANSPOSES);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_CHAIN_TRANSPOSES);
    for (row = 0; row < 0x10; row++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

/* $573C: does table b equal table a? compares fx 1/values, fx 2/values,
   envelopes and transposes; 1 = duplicate */
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
    t = offsetA + (UWORD)SRAM_TABLE_COMMANDS1;
    p = (UBYTE *)t;
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_COMMANDS1);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_TABLE_COMMAND_VALUES1);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_COMMAND_VALUES1);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_TABLE_COMMANDS2);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_COMMANDS2);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_TABLE_COMMAND_VALUES2);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_COMMAND_VALUES2);
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_TABLE_ENVELOPES);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_ENVELOPES);
    rRAMB = 0;
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(offsetA + (UWORD)SRAM_TABLE_TRANSPOSES);
    q = (UBYTE *)(offsetB + (UWORD)SRAM_TABLE_TRANSPOSES);
    rRAMB = 1;
    for (step = 0; step < 0x10; step++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

/* $5996: walk all chains ($2080..) replacing dup phrase refs with the
   canonical one */
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

/* $5A12: walk the song sequence ($B290-$B68F) replacing references to a
   duplicate chain with the canonical one */
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

/* $5A8E: walk phrase/table cmd values replacing dup table refs (A command) */
void RemapCommandTableRefs(UBYTE from, UBYTE to)
{
    volatile UWORD command;
    volatile UWORD value;
    volatile UBYTE instrument;
    volatile UBYTE nt;
    UBYTE nf;

    command = (UWORD)SRAM_TABLE_COMMANDS1;
    value = (UWORD)SRAM_TABLE_COMMAND_VALUES1;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    while (command != (UWORD)0xB880) {
        if (*(UBYTE *)command == COMMAND_A && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    command = (UWORD)SRAM_TABLE_COMMANDS2;
    value = (UWORD)SRAM_TABLE_COMMAND_VALUES2;
    while (command != (UWORD)0xBC80) {
        if (*(UBYTE *)command == COMMAND_A && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    rRAMB = 2;
    command = (UWORD)SRAM_PHRASE_COMMANDS;
    value = (UWORD)SRAM_PHRASE_COMMAND_VALUES;
    while (command != (UWORD)0xAFF0) {
        if (*(UBYTE *)command == COMMAND_A && from == *(UBYTE *)value)
            *(UBYTE *)value = to;
        command++;
        value++;
    }
    rRAMG = 0;
    _asm ei _endasm;
    instrument = 0;
    nt = to | 0x20;
    nf = from | 0x20;
    while (instrument != INSTRUMENT_SPEECH) {
        if (ReadInstrumentByte(instrument, INSTRUMENT_BYTE_TABLE) == nf)
            WriteInstrumentByte(instrument, INSTRUMENT_BYTE_TABLE, nt);
        instrument++;
    }
}

/* $5BDE: fold duplicate phrases: for each used phrase i (hex counter at
   $98AE) every later used phrase FindDuplicatePhrase finds equal is remapped
   to i (RemapPhraseRefs) */
void DeduplicatePhrases(void)
{
    UBYTE phrase, candidate;

    for (phrase = 0; phrase < 0xFF; phrase = phrase + 1) {
        if (!IsPhraseUsedFar(phrase))
            continue;
        wVramCursor = (UBYTE *)BG_MAP(14, 5);
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
    wVramCursor = (UBYTE *)BG_MAP(14, 5);
    DrawStringTile20();
    DrawStringTile20();
}

/* $5C78: fold duplicate chains: for each used chain i (counter at $98AE)
   every later used chain FindDuplicateChain finds equal is remapped to i
   (RemapChainRefs); then OK */
void DeduplicateChains(void)
{
    UBYTE chain, candidate;

    for (chain = 0; chain < 0x80; chain = chain + 1) {
        if (!IsChainUsedFar(chain))
            continue;
        wVramCursor = (UBYTE *)BG_MAP(14, 5);
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
    wVramCursor = (UBYTE *)BG_MAP(14, 5);
    DrawString("OK");
}

/* $5D18: fold duplicate tables: for each used table i (hex counter at $98AE)
   every later used table FindDuplicateTable finds equal is remapped to i
   (RemapCommandTableRefs); then draws OK */
void DeduplicateTables(void)
{
    UBYTE table, candidate;

    for (table = 0; table < 0x20; table = table + 1) {
        if (IsTableFree(table))
            continue;
        wVramCursor = (UBYTE *)BG_MAP(14, 5);
        DrawHexByte(table);
        for (candidate = table + 1; candidate < 0x20; candidate = candidate + 1) {
            if (IsTableFree(candidate))
                continue;
            wDeduplicateTableIsDuplicate = FindDuplicateTable(table, candidate);
            if (wDeduplicateTableIsDuplicate)
                RemapCommandTableRefs(candidate, table);
        }
    }
    wVramCursor = (UBYTE *)BG_MAP(14, 5);
    DrawString("OK");
}

/* $5FEC: 1 if any phrase step holds command F (6) */
UBYTE AnyPhraseUsesCommandF(void)
{
    UBYTE *p;
    UBYTE phrase, row;

    p = SramPhraseCommands;
    for (phrase = 0; phrase != EMPTY; phrase++) {
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

/* $6074: 1 if either command column of any table holds command F (6) */
UBYTE AnyTableUsesCommandF(void)
{
    UBYTE *p, *q;
    UBYTE table, step, ok;

    p = SramTableCommands1;
    q = SramTableCommands2;
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

/* $62BD: CLEAN INSTR: unless a phrase or table uses F, reset the synths no
   wave instrument uses; -> how many were freed */
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

/* $62DD: project CLEAN: de-dupe chains+phrases, DE-DUPE/FREEING progress
   panel */
void CleanSongData(void)
{
    UBYTE phrases, chains;

    phrases = CountUsedPhrases();
    chains = CountUsedChains();
    wVramCursor = BG_MAP(4, 5);
    DrawString("DE-DUPE...");
    DeduplicatePhrases();
    DeduplicateChains();
    wVramCursor = BG_MAP(4, 6);
    DrawString("FREEING:");
    wVramCursor = BG_MAP(4, 7);
    printf("CHAINS....");
    RemapSequenceChains();
    printf("%d", chains - CountUsedChains());
    wVramCursor = BG_MAP(4, 8);
    printf("PHRASES...");
    RebuildPhraseAllocationMap();
    printf("%d", phrases - CountUsedPhrases());
    wVramCursor = BG_MAP(4, 10);
    printf("OK!");
}

/* $63B1: project CLEAN: de-dupe instruments/tables, DE-DUPE/FREEING progress
   panel */
void CleanInstrumentData(void)
{
    wVramCursor = BG_MAP(4, 5);
    DrawString("DE-DUPE...");
    DeduplicateTables();
    wVramCursor = BG_MAP(4, 6);
    DrawString("FREEING:");
    wVramCursor = BG_MAP(4, 7);
    printf("INSTRS....");
    printf("%d", FreeUnusedInstruments());
    wVramCursor = BG_MAP(4, 8);
    printf("TABLES....");
    printf("%d", FreeUnusedTables());
    wVramCursor = BG_MAP(4, 9);
    printf("SYNTHS....");
    printf("%d", FreeUnusedSynths());
    wVramCursor = BG_MAP(4, 11);
    printf("OK!");
}
