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
void WriteInstrumentName(UBYTE instr);           /* $01:$4A49 */
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
UBYTE IsInstrumentAllocated(UBYTE instr);
void MarkInstrumentTableUsed(UBYTE instr);
UBYTE ClaimInstrumentSlot(UBYTE instr);
UBYTE CountUsedTables(void);
void MarkTableTablesUsed(UBYTE t);
void ClearTable(UBYTE t);
void MarkPhraseUsed(UBYTE phrase);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr);
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

UBYTE ClaimInstrumentSlot(UBYTE instr)
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
    switch (wSongColumn) {
    case 2:
        src = DefaultInstrumentKit;
        n = 14;
        break;
    case 3:
        src = DefaultInstrumentWave;
        n = 11;
        break;
    default:
        src = DefaultInstrumentPulse;
        n = 11;
    }
    for (i = 0; i != n; i++)
        WriteInstrumentByte(instr, i, src[i]);
    return 1;
}

void InitRemapTable(void)
{
    UBYTE i, v;
    UBYTE *p;

    for (i = 0; i != 0x10; i++) {
        p = &wInstrumentRemapTable[i];
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
    UBYTE i;

    for (i = 0; i < 0x40; i++) {
        if (ClaimInstrumentSlot(i)) {
            FarCallArgument3(4, (UWORD)WritePhraseInstrument, wPhrase, wPhraseRow, i);
            return i;
        }
    }
    strcpy(wStringBuffer, "OUT OF MEMORY. CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

/* 1 if the 16 bytes at bank:addr are all zero. (The loop reuses `bank`.) */
UBYTE IsSongBlockEmpty(UBYTE bank, UBYTE *addr)
{
    ReadSongBytes(bank, addr, wSongReadBuffer);
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

    start = wColumnChain[wSongColumn];
    if (start & 0x80)
        start = 0;
    i = start;
    do {
        p = SramChainAlloc + (i >> 3);
        if (!(ReadSongByte(1, (UWORD)p) & (1 << (i & 7)))) {
            FarCallArgument(1, FAR_FN(MarkChainUsed), i);
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
UBYTE FreeUnusedInstruments(void)
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
            i = FarCallArgument2P(4, FAR_FN(ReadPhraseInstrument), ph, row);
            if (i < 0x40)
                WriteSongByte(1, (UWORD)SramInstrAlloc + i, 1);
        }
    }
    _asm ei _endasm;
    p = (UWORD)SramInstrAlloc;
    for (i = 0; i < 0x40; i++) {
        _asm di _endasm;
        if (!ReadSongByte(1, p)) {
            wNameEditBuffer[0] = 0;
            wNameEditBuffer[1] = 0;
            wNameEditBuffer[2] = 0;
            wNameEditBuffer[3] = 0;
            wNameEditBuffer[4] = 0;
            FarCallArgument(1, FAR_FN(WriteInstrumentName), i);
            addr = i;
            addr <<= 4;
            addr |= 0xB000;
            addr += 0x80;
            rRAMB = 1;
            _asm di _endasm;
            rRAMG = 0x0A;
            FillMemory((UBYTE *)addr, 0, 0x10);
            rRAMG = 0;
            _asm ei _endasm;
            k = 4;
            do {
                /* SDCC fetches i into a over the element it just read
                   (`cp a,a`): the test is always true, so freeing any
                   instrument clears all four columns' */
                if (wColumnInstrument[k - 1] == i)
                    wColumnInstrument[k - 1] = 0xFF;
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

void MarkTableUsed(UBYTE t)
{
    if (t < 0x20)
        wUsedTableMap[t] = 1;
}

UBYTE IsTableUsed(UBYTE t)
{
    return wUsedTableMap[t];
}

UBYTE IsInstrumentAllocated(UBYTE instr)
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
            MarkTableUsed(ReadSongByte(2, v));
        c = c + 1;
        v = v + 1;
    }
}

void MarkTableTablesUsed(UBYTE t)
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
    UBYTE i;

    for (i = 0; i < 0xFF; i++)
        if (IsPhraseUsedFar(i))
            MarkPhraseTablesUsed(i);
}

UBYTE CountUsedTables(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x20; i++)
        if (IsTableUsed(i))
            n++;
    return n;
}

UBYTE MarkTablesOfUsedTables(void)
{
    UBYTE before, i;

    before = CountUsedTables();
    for (i = 0; i < 0x20; i++)
        if (IsTableUsed(i))
            MarkTableTablesUsed(i);
    return !(before == CountUsedTables());
}

void MarkInstrumentTableUsed(UBYTE instr)
{
    UBYTE v;

    v = ReadInstrumentByte(instr, 6);
    if (v & 0x20)
        MarkTableUsed(v & 0x1F);
}

void MarkTablesOfUsedInstruments(void)
{
    UBYTE i;

    for (i = 0; i < 0x40; i++)
        if (IsInstrumentAllocated(i))
            MarkInstrumentTableUsed(i);
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

UBYTE ClearUnusedTables(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x20; i++) {
        if (!IsTableUsed(i)) {
            if (!IsTableFree((UWORD)i))
                n++;
            ClearTable(i);
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
    UBYTE j, i, col;

    col = wSongColumn;
    _asm
        di
    _endasm;
    for (i = 0; i < 0x80; i++)
        FarCallArgument(1, FAR_FN(FreeChain), i);
    _asm
        ei
        di
    _endasm;
    for (wSongColumn = 0; wSongColumn < 4; wSongColumn++) {
        for (j = 0; j != 0xC0; j++) {
            i = ReadSequenceByte(wSongColumn, j);
            if (!(i & 0x80))
                FarCallArgument(1, FAR_FN(MarkChainUsed), i);
        }
    }
    wSongColumn = col;
    _asm
        ei
        di
    _endasm;
    for (i = 0; i < 0x80; i++) {
        if (!FarCallArgumentByte(1, FAR_FN(IsChainUsed), i)) {
            for (j = 0; j < 0x10; j++) {
                WriteChainPhrase(i, j, 0xFF);
                FarCallArgument3P(6, FAR_FN(SetChainTranspose), i, j, 0);
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
        p = FarCallArgument2(2, (UWORD)SoundGetChainPhrase, chain, i);
        if (p != 0xFF)
            FarCallArgument(1, FAR_FN(MarkPhraseUsed), p);
    }
}

UBYTE IsChainUsedFar(UBYTE chain)
{
    return FarCallArgumentByte(1, (UWORD)IsChainUsed, chain);
}

void MarkPhrasesOfUsedChains(void)
{
    UBYTE i;

    for (i = 0; i < 0x80; i++)
        if (IsChainUsedFar(i))
            MarkChainPhrasesUsed(i);
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
    UWORD oa, ob;
    UBYTE i;
    UWORD t;

    oa = a;
    ob = b;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    oa <<= 4;
    ob <<= 4;
    t = oa + (UWORD)0xA000;                 /* bank 0: phrase notes */
    p = (UBYTE *)t;
    q = (UBYTE *)(ob + (UWORD)0xA000);
    rRAMB = 0;
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 3;
    p = (UBYTE *)(oa + (UWORD)0xB000);                      /* bank 3: phrase instruments */
    q = (UBYTE *)(ob + (UWORD)0xB000);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    rRAMB = 2;
    p = (UBYTE *)(oa | (UWORD)0xA000);      /* bank 2: phrase commands */
    q = (UBYTE *)(ob | (UWORD)0xA000);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xAFF0);                      /* bank 2: command values */
    q = (UBYTE *)(ob + (UWORD)0xAFF0);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

UBYTE FindDuplicateChain(UBYTE a, UBYTE b)
{
    UBYTE *p, *q;
    UWORD oa, ob;
    UBYTE i;
    UWORD t;

    oa = a;
    ob = b;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    oa <<= 4;
    ob <<= 4;
    t = oa + (UWORD)0xA080;                 /* bank 1: chain phrases */
    p = (UBYTE *)t;
    q = (UBYTE *)(ob + (UWORD)0xA080);
    rRAMB = 1;
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xA880);                      /* bank 1: chain transposes */
    q = (UBYTE *)(ob + (UWORD)0xA880);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
}

UBYTE FindDuplicateTable(UBYTE a, UBYTE b)
{
    UBYTE *p, *q;
    UWORD oa, ob;
    UBYTE i;
    UWORD t;

    oa = a;
    ob = b;
    oa <<= 4;
    ob <<= 4;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    t = oa + (UWORD)0xB680;                 /* bank 1: table fx 1 */
    p = (UBYTE *)t;
    q = (UBYTE *)(ob + (UWORD)0xB680);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xB880);                      /* bank 1: table fx 1 values */
    q = (UBYTE *)(ob + (UWORD)0xB880);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xBA80);                      /* bank 1: table fx 2 */
    q = (UBYTE *)(ob + (UWORD)0xBA80);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xBC80);                      /* bank 1: table fx 2 values */
    q = (UBYTE *)(ob + (UWORD)0xBC80);
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xB690);                      /* bank 0: table envelopes */
    q = (UBYTE *)(ob + (UWORD)0xB690);
    rRAMB = 0;
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    p = (UBYTE *)(oa + (UWORD)0xB480);                      /* bank 1: table transposes */
    q = (UBYTE *)(ob + (UWORD)0xB480);
    rRAMB = 1;
    for (i = 0; i < 0x10; i++) {
        if (*p++ != *q++)
            return 0;
    }
    return 1;
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
    FarCallArgument(1, FAR_FN(FreePhrase), from);
    i = 4;
    do {
        if (from == wColumnPhrase[i - 1])
            wColumnPhrase[i - 1] = to;
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
    FarCallArgument(1, FAR_FN(FreeChain), from);
    i = 4;
    do {
        if (from == wColumnChain[i - 1])
            wColumnChain[i - 1] = to;
        i--;
    } while (i);
}

void RemapCommandTableRefs(UBYTE from, UBYTE to)
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
        if (ReadInstrumentByte(i, 6) == nf)
            WriteInstrumentByte(i, 6, nt);
        i++;
    }
}

void DeduplicatePhrases(void)
{
    UBYTE i, j;

    for (i = 0; i < 0xFF; i = i + 1) {
        if (!IsPhraseUsedFar(i))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(i);
        for (j = i + 1; j < 0xFF; j = j + 1) {
            wDeduplicatePhraseUsed = IsPhraseUsedFar(j);
            if (wDeduplicatePhraseUsed) {
                wDeduplicatePhraseIsDuplicate = FindDuplicatePhrase(i, j);
                if (wDeduplicatePhraseIsDuplicate)
                    RemapPhraseRefs(j, i);
            }
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawStringTile20();
    DrawStringTile20();
}

void DeduplicateChains(void)
{
    UBYTE i, j;

    for (i = 0; i < 0x80; i = i + 1) {
        if (!IsChainUsedFar(i))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(i);
        for (j = i + 1; j < 0x80; j = j + 1) {
            wDeduplicateChainUsed = IsChainUsedFar(j);
            if (wDeduplicateChainUsed) {
                wDeduplicateChainIsDuplicate = FindDuplicateChain(i, j);
                if (wDeduplicateChainIsDuplicate)
                    RemapChainRefs(j, i);
            }
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawString("OK");
}

void DeduplicateTables(void)
{
    UBYTE i, j;

    for (i = 0; i < 0x20; i = i + 1) {
        if (IsTableFree(i))
            continue;
        wVramCursor = (UBYTE *)0x98AE;
        DrawHexByte(i);
        for (j = i + 1; j < 0x20; j = j + 1) {
            if (IsTableFree(j))
                continue;
            wDeduplicateTableIsDuplicate = FindDuplicateTable(i, j);
            if (wDeduplicateTableIsDuplicate)
                RemapCommandTableRefs(j, i);
        }
    }
    wVramCursor = (UBYTE *)0x98AE;
    DrawString("OK");
}

UBYTE CountUsedPhrases(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0xFF; i++)
        if (IsPhraseUsedFar(i))
            n++;
    return n;
}

UBYTE CountUsedChains(void)
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
    FillMemory(SramPhraseAlloc, 0, 0x20);
    rRAMG = 0;
    _asm ei _endasm;
    MarkPhrasesOfUsedChains();
    for (i = 0; i < 0xFF; i++) {
        if (IsPhraseUsedFar(i))
            continue;
        o = i;
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
    UBYTE i;

    p = SramWaveFrames + ((UWORD)w << 4);
    for (i = 0; i < 0x10; i++) {
        if (ReadSongByte(3, p) != DefaultWaveFrameB1F[i])
            return 1;
        p = p + 1;
    }
    return 0;
}

UBYTE SynthFramesModified(UBYTE chain)
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

UBYTE CountModifiedSynths(void)
{
    UBYTE n, i;

    n = 0;
    for (i = 0; i < 0x10; i++)
        if (SynthFramesModified(i))
            n++;
    return n;
}

UBYTE AnyPhraseUsesCommandF(void)
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

UBYTE AnyTableUsesCommandF(void)
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

void ResetSynthWaves(UBYTE chain)
{
    UBYTE p, i;

    p = chain << 4;
    for (i = 0; i < 16; i++) {
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
    UBYTE i;
    UBYTE used[16];
    UBYTE n;

    i = 16;
    do {
        used[i - 1] = 0;
        i--;
    } while (i);
    for (n = 0; n < 0x40; n++)
        if ((ReadInstrumentByte(n, 0) & 3) == 1)
            used[ReadInstrumentByte(n, 3) >> 4] = 1;
    for (i = 0; i < 0x10; i++)
        if (!used[i]) {
            ResetSynthParameters(i);
            ResetSynthWaves(i);
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
