/*
 * Small field drawers -- LSDj 9.4.0, ROM bank $01.
 *
 * The speed mode and on/off names, the speed glyph.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

/*
 * The speed icon's tiles are file-scope const data, defined here: SDCC puts
 * const data in the constant segment and flushes that after the next
 * function it compiles, ahead of the function's own string literals, which
 * is where the ROM has the table ($01:$51EE, between DrawSpeedModeName's
 * code and its strings).
 */
const UBYTE SpeedGlyphTiles[] = { 0x6B, 0x6C, 0x69, 0x6A, 0x6D, 0x6E };

void DrawSpeedModeName(UBYTE v)
{
    if (v & 0x80)
        DrawString("STEP");
    else if (v & 0x10)
        DrawString("TICK");
    else if ((v & 0x40) && ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) != INSTRUMENT_KIT)
        DrawString("DRUM");
    else
        DrawString("FAST");
    DrawSpeedGlyph(v);
}

void DrawSpeedGlyph(UBYTE v)
{
    char g;

    g = SpeedGlyphTiles[v & 7];
    DrawStringIndexed(g);
    DrawStringIndexed(g);
}

void DrawOnOffName(UBYTE v)
{
    DrawString((v & 0x20) ? "OFF" : " ON");
}
