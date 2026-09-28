/*
 * ReadSeqByte -- LSDj 9.4.0, ROM bank $00:$0AF6-$0B4A (85 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank00/ReadSeqByte.c --target ReadSeqByte
 *
 * song[step][channel] of the working song's sequence (song offset $1290 =
 * SRAM bank 0, $B290, SramSequence; 4 channels per row), with the empty
 * marker normalised: any value with bit 7 set reads as $FF.
 */

typedef unsigned char UBYTE;

extern UBYTE SramSequence[];                /* SRAM bank 0, $B290: 192 x 4 bytes */

void DebugTrap(void);
UBYTE ReadSongByte(UBYTE bank, UBYTE *addr);

UBYTE ReadSeqByte(UBYTE channel, UBYTE step)
{
    UBYTE *p;
    UBYTE v;

    if (channel >= 4)
        DebugTrap();
    if (step >= 0xC0)
        DebugTrap();
    p = SramSequence + ((unsigned int)step << 2);
    v = ReadSongByte(0, p + channel);
    return (v & 0x80) ? 0xFF : v;
}
