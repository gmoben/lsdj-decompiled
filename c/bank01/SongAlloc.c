/*
 * Song data helpers (allocation bitmaps, copies) -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void WriteSongInitFlag(void)
{
    WriteSongByte(0, SramInitFlag1, 'r');
    WriteSongByte(0, SramInitFlag1 + 1, 'b');
    WriteSongByte(1, SramInitFlag2, 'r');
    WriteSongByte(1, SramInitFlag2 + 1, 'b');
    WriteSongByte(3, SramInitFlag3, 'r');
    WriteSongByte(3, SramInitFlag3 + 1, 'b');
}

UBYTE IsChainUsed(UBYTE chain)
{
    UBYTE v, m;

    if (chain >= 0x80)
        DebugTrap();
    v = ReadSongByte(1, SramChainAlloc + (chain >> 3));
    m = 1 << (chain & 7);
    return v & m;
}

void CopySong16(UBYTE bank, UBYTE *src, UBYTE *dst)
{
    UBYTE i, v;

    for (i = 0; i < 0x10; i++) {
        v = ReadSongByte(bank, src);
        WriteSongByte(bank, dst, v);
        src = src + 1;
        dst = dst + 1;
    }
}
