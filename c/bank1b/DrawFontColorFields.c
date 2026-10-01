/*
 * DrawFontColorFields -- LSDj 9.4.0, ROM bank $1B:$569C-$5706 (107 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank1b/DrawFontColorFields.c --target DrawFontColorFields
 *
 * The Project screen's FONT and COLOR SET names. The two name tables are
 * `const` 2-D char arrays: SDCC 2.2.1 cannot keep a NUL inside a string
 * literal, and it emits a unit's const data after its *first* function, so
 * the tables sitting right behind this function in the ROM ($5707 FontNames,
 * $5716 ColorSetNames) put it first in its own compilation unit. This file
 * pushes hGameboyType with `ldh`, so here it is an absolute sfr.
 */

#include "lsdj.h"

sfr hGameboyTypeIo = 0xFF90;
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wColorSet;                     /* $C52F */

const char FontNames[3][5] = {"Y2K ", "ZERO", "KIKO"};
const char ColorSetNames[2][7][5] = {
    {"AIRY", "DARK", "KIWI", "NORI", "", "", ""},
    {"AMI", "CRT ", "CUTE", "BORT", "COLR", "DEAT", "GOOD"},
};

void DrawFontColorFields(void)
{
    DrawString(FontNames[wFont]);
    DrawStringIndexed('/');
    DrawString(ColorSetNames[hGameboyTypeIo][wColorSet]);
}
