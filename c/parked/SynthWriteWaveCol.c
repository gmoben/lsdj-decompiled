#include "lsdj.h"

extern UBYTE wSynthWaveBuf[];               /* $CA34, [32] */
extern UBYTE *wSynthRenderDst;              /* $C9F2 */
extern UBYTE wWaveEditX;

void SynthWriteWaveCol(void)
{
    UBYTE i;

    for (i = 0; i < 0x20; i += 2) {
        wWaveEditX = wSynthWaveBuf[i] << 4;
        wWaveEditX = (wSynthWaveBuf[i + 1] + wWaveEditX) ^ 0xFF;
        WriteSongByte(3, (UWORD)wSynthRenderDst++, wWaveEditX);
    }
}
