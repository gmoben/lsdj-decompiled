/* Bank $1E VRAM helpers (ROM order). Byte-identical to the ROM under the
 * period compiler plus tools/match/lsdj.peep. */
#include "lsdj.h"

extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
void WriteVramByteSafe(UWORD addr, UBYTE v);    /* $00:$17EC */

/* $79FA: blank the right third of the tile rows (two VRAM ranges) */
void GfxFillVramRegions(void)
{
    UWORD p;

    for (p = 0x9480; p != (UWORD)0x9800; p += 2)
        WriteVramByteSafe(p, 0xFF);
    for (p = 0x8800; p != (UWORD)0x88D0; p += 2)
        WriteVramByteSafe(p, 0xFF);
}

/* $7A38: wait until the LCD lets the CPU at VRAM, then read a byte */
UBYTE GfxReadVramByte(UBYTE *p)
{
    while (STAT_REG & 2)
        ;
    return *p;
}
