/*
 * ReadInstrByte -- LSDj 9.4.0, ROM bank $00:$0A91-$0AE1 (81 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank00/ReadInstrByte.c --target ReadInstrByte
 *
 * instruments[instr][field] from the working song: the 16-byte instrument
 * records live at song offset $3080 = SRAM bank 1, $B080 (SramInstruments),
 * read through ReadSongByte(bank, addr). Both indices are range-checked with
 * DebugTrap (the `ld b,b` + `rst $38` assert).
 *
 * Exercises: two byte arguments, a pointer local, a 16-bit shift (SDCC's
 * `rl c / rl b` loop for `<< 4` -- the cast matters: a plain `instr << 4`
 * is shifted in 8 bits), pointer + byte arithmetic, and a call with a 16-bit
 * argument. ReadSongByte's arguments are pushed right to left, so the address
 * goes first and the bank byte last. The `return` of a value that is already
 * in e is where LSDj's compiler drops the `ld c,e ... ld e,c` round trip.
 */

typedef unsigned char UBYTE;

extern UBYTE SramInstruments[];             /* SRAM bank 1, $B080: 64 x 16 bytes */

void DebugTrap(void);
UBYTE ReadSongByte(UBYTE bank, UBYTE *addr);

UBYTE ReadInstrByte(UBYTE instr, UBYTE field)
{
    UBYTE *p;

    if (instr >= 0x40)
        DebugTrap();
    if (field >= 0x10)
        DebugTrap();
    p = SramInstruments + ((unsigned int)instr << 4);
    return ReadSongByte(1, p + field);
}
