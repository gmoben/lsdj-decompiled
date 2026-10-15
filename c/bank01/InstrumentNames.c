/*
 * Instrument names -- LSDj 9.4.0, ROM bank $01.
 *
 * An instrument's five-letter name copied to the name editor and written
 * back.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wNameEditBuffer[];             /* $C431, [5] + NUL */

/* $493B: copy instrument `instrument`'s 5-letter name into wNameEditBuffer,
   "SPCH" for the speech instrument ($40) */
void CopyInstrumentName(UBYTE instrument)
{
    UBYTE *p;

    if (instrument > INSTRUMENT_SPEECH)
        DebugTrap();
    if (instrument == INSTRUMENT_SPEECH) {
        strcpy(wNameEditBuffer, "SPCH");
        return;
    }
    p = SramInstrumentNames + (UWORD)instrument * 5;
    wNameEditBuffer[0] = ReadSongByte(0, p++);
    wNameEditBuffer[1] = ReadSongByte(0, p++);
    wNameEditBuffer[2] = ReadSongByte(0, p++);
    wNameEditBuffer[3] = ReadSongByte(0, p++);
    wNameEditBuffer[4] = ReadSongByte(0, p);
    wNameEditBuffer[5] = 0;
}

/* $4A49: wNameEditBuffer -> instrument name storage ($be7a + instr*5) */
void WriteInstrumentName(UBYTE instrument)
{
    UBYTE *p;

    if (instrument >= INSTRUMENTS)
        DebugTrap();
    p = SramInstrumentNames + (UWORD)instrument * 5;
    WriteSongByte(0, p++, wNameEditBuffer[0]);
    WriteSongByte(0, p++, wNameEditBuffer[1]);
    WriteSongByte(0, p++, wNameEditBuffer[2]);
    WriteSongByte(0, p++, wNameEditBuffer[3]);
    WriteSongByte(0, p, wNameEditBuffer[4]);
}
