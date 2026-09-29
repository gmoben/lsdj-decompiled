/*
 * ScreenDrawDispatch -- LSDj 9.4.0, ROM bank $00:$254F-$264A (252 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank00/ScreenDrawDispatch.c --target ScreenDrawDispatch
 *
 * The editor index: page in the screen's ROM bank (HRAM shadow hCurrentBank +
 * the MBC5 register) and call its draw routine. SDCC compiles the switch to
 * its bounds check (`xor a; ld a,#14; sbc a,(hl); jp c,end`) and a `jp hl`
 * into ScreenDrawTable, 15 `jp`s at 3 bytes each; the empty cases 0, 7 and
 * 11 each become a `jp end` trampoline emitted in source order, which is how
 * the ROM has them (so the original spelled those cases out).
 *
 * The callees live in other banks and are called directly (nonbanked, as
 * everything in LSDj); the link step resolves them from src/bankNN.sym.
 */

#include "lsdj.h"

void ScreenDrawDispatch(void)
{
    switch (wCurrentScreen) {
    case 0:
        break;
    case 1:
        SWITCH_ROM(0x04); DrawPhraseScreen();
        break;
    case 2:
        SWITCH_ROM(0x1D); DrawGrooveScreen();
        break;
    case 3:
        SWITCH_ROM(0x06); DrawChainScreen();
        break;
    case 4:
        SWITCH_ROM(0x07); DrawSongScreen();
        break;
    case 5:
        SWITCH_ROM(0x05); DrawTableScreen();
        break;
    case 6:
        SWITCH_ROM(0x03); DrawInstrumentScreen();
        break;
    case 7:
        break;
    case 8:
        SWITCH_ROM(0x06); DrawWaveScreen();
        break;
    case 9:
        SWITCH_ROM(0x1B); DrawProjectScreen();
        break;
    case 10:
        SWITCH_ROM(0x1D); DrawSynthScreen();
        break;
    case 11:
        break;
    case 12:
        SWITCH_ROM(0x06); DrawWordScreen();
        break;
    case 13:
        SWITCH_ROM(0x1C); DrawFileScreen();
        break;
    case 14:
        SWITCH_ROM(0x1B); DrawHelpScreen();
        break;
    }
}
