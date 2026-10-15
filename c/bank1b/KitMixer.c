/*
 * The kit mixer's builder -- LSDj 9.4.0, ROM bank $1B.
 *
 * InitializeKitMixer copies KitMixTable to $D000 and MixerKernel, unrolled
 * sixteen times, to $D480: the WRAM code that mixes kit samples.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void DrawHelpMenu(void);
extern UBYTE KitMixTable[];                 /* $1B:$6C38, the 1152-byte nibble-sum LUT */
void MixerKernel(void);                     /* $1B:$4000, the 27-byte asm mixer step (code, hence a function); DrawHelpMenu follows it */

/* $6B7A: build the kit sample mixer in WRAM: KitMixTable -> $d000 (1152 B),
   then MixerKernel unrolled 16x at $d480 with inc c / inc e between copies
   and a ret after the last */
void InitializeKitMixer(void)
{
    UBYTE *destination;
    UBYTE *source;
    UBYTE copy;
    UBYTE b;

    destination = (UBYTE *)KIT_MIXER_CODE;
    memcpy((UBYTE *)KIT_MIX_TABLE, KitMixTable, 0x480);
    for (copy = 0; copy != 0x10; copy++) {
        source = (UBYTE *)(UWORD)MixerKernel;
        do {
            b = *source;
            *destination = b;
            destination = destination + 1;
            source = source + 1;
        } while (source != (UBYTE *)(UWORD)DrawHelpMenu);
        if (copy != 0x0F) {
            *destination++ = 0x0C;
            *destination++ = 0x1C;
        }
    }
    *destination = 0xC9;
}
