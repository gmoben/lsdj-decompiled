/*
 * Small UI helpers -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE SpeedGlyphTiles[];             /* $01:$51EE */

void ApplyColorSet(void);
void ClearOamBuffer(void);
void UiLoadHwTables(void);

void UiInitScreen(void)
{
    ApplyColorSet();
    ClearOamBuffer();
    UiLoadHwTables();
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
