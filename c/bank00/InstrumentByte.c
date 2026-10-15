/*
 * The instrument-record accessors -- LSDj 9.4.0, ROM bank $00:
 *   WriteInstrumentByte    $0953-$09B8 (102 bytes)
 *   ReadInstrumentByte     $0A91-$0AE1  (81 bytes)
 *   ReadCurrentInstrumentByte  $0AE2-$0AF5  (20 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank00/InstrumentByte.c --target ReadInstrumentByte
 *     (or WriteInstrumentByte / ReadCurrentInstrumentByte; `make match` runs all three)
 *
 * instruments[instr][field] of the working song: the 16-byte instrument
 * records live at song offset $3080 = SRAM bank 1, $B080 (SramInstruments),
 * accessed through ReadSongByte(bank, addr) / WriteSongByte(bank, addr, val).
 * Indices are range-checked with DebugTrap (the `ld b,b` + `rst $38` assert);
 * field 0 (the instrument type) may only be written with 0..3.
 *
 * Exercises: byte arguments, a pointer local, a 16-bit shift (SDCC's
 * `rl c / rl b` loop for `<< 4` -- the cast matters: a plain `instr << 4`
 * is shifted in 8 bits), pointer + byte arithmetic in both of SDCC's forms
 * (`p + field` adds through hl/de; `p + (unsigned int)field` extends the
 * byte into bc first -- the ROM has one of each), a compound assert
 * (`field == 0 && ...`: `== 0` on a stack byte is `ld a,(hl); or a,a`, while
 * `!field` would be `xor a; or a,(hl)`), and calls with 16-bit arguments
 * pushed right to left. The `return` of a value that is already in e is
 * where LSDj's compiler drops the `ld c,e ... ld e,c` round trip.
 */

#include "lsdj.h"

/* $0953: write `value` to byte `field` of instrument `instrument` in SRAM
   (SramInstruments, 16 bytes an instrument); asserts the instrument and
   field are in range and a type below 4 */
void WriteInstrumentByte(UBYTE instrument, UBYTE field, UBYTE value)
{
    UBYTE *p;

    if (instrument >= INSTRUMENTS)
        DebugTrap();
    if (field >= 0x10)
        DebugTrap();
    if (field == 0 && value >= 4)
        DebugTrap();
    p = SramInstruments + ((unsigned int)instrument << 4);
    WriteSongByte(1, p + (unsigned int)field, value);   /* the cast: field into bc, p into hl */
}

/* $0A91: read instruments[instr][field]: SRAM bank1 $B080 + instr*$10 +
   field (asserts instr<$40, field<$10) */
UBYTE ReadInstrumentByte(UBYTE instrument, UBYTE field)
{
    UBYTE *p;

    if (instrument >= INSTRUMENTS)
        DebugTrap();
    if (field >= 0x10)
        DebugTrap();
    p = SramInstruments + ((unsigned int)instrument << 4);
    return ReadSongByte(1, p + field);
}

/* $0AE2: ReadInstrumentByte([wInstrument] current instrument, field) */
UBYTE ReadCurrentInstrumentByte(UBYTE field)
{
    return ReadInstrumentByte(wInstrument, field);
}
