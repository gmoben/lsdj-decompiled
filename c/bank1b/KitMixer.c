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
extern UBYTE KitMixTable[];               /* $1B:$6C38, the 1152-byte nibble-sum LUT */
void MixerKernel(void);                     /* $1B:$4000, the 27-byte asm mixer step (code, hence a function); DrawHelpMenu follows it */

void InitializeKitMixer(void)
{
    UBYTE *dst;
    UBYTE *src;
    UBYTE i;
    UBYTE b;

    dst = (UBYTE *)0xD480;
    memcpy((UBYTE *)0xD000, KitMixTable, 0x480);
    for (i = 0; i != 0x10; i++) {
        src = (UBYTE *)(UWORD)MixerKernel;
        do {
            b = *src;
            *dst = b;
            dst = dst + 1;
            src = src + 1;
        } while (src != (UBYTE *)(UWORD)DrawHelpMenu);
        if (i != 0x0F) {
            *dst++ = 0x0C;
            *dst++ = 0x1C;
        }
    }
    *dst = 0xC9;
}
