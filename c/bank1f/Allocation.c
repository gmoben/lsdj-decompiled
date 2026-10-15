/*
 * Song allocation -- LSDj 9.4.0, ROM bank $1F.
 *
 * Chains' phrases written, instruments claimed and allocated, free tables
 * found, song blocks tested for data.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wColumnChain[];                   /* $C44F, [4] */
void MarkChainUsed(void);                   /* $01:$4D67 */
void ShowOutOfMemory(void);                 /* $01:$75BD */
extern UBYTE DefaultInstrumentPulse[];           /* $1F:$4A02, [11] */
extern UBYTE DefaultInstrumentWave[];            /* $4A0D, [11] */
extern UBYTE DefaultInstrumentKit[];             /* $4A18, [14] */
UBYTE IsTableFree(UWORD t);
UBYTE IsInstrumentAllocated(UBYTE instrument);
UBYTE ClaimInstrumentSlot(UBYTE instrument);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instrument);

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
    case CHANNEL_WAV:
        source = DefaultInstrumentKit;
        n = 14;
        break;
    case CHANNEL_NOI:
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

UBYTE AllocateNextFreeInstrument(void)
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
        p = SramChainAllocation + (chain >> 3);
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

UBYTE IsInstrumentAllocated(UBYTE instrument)
{
    return ReadSongByte(1, SramInstrumentAllocation + (UWORD)instrument);
}
