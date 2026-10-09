/* Parked attempt for SynthFilterBandPass ($1D:$5BBD): 10 rows from the ROM, see c/parked.txt.
   Link with --define wSynthBandDstW=0xCB3C --define wSynthBandSlotW=0xCB3E --define wSynthBandIdx=0xCB3E. */
#include "lsdj.h"

extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
extern UBYTE wSynthWaveBuf[];               /* $CA34 */
extern UBYTE wSynthBufA[];                  /* $CAB8 */
extern UBYTE wSynthBufB[];                  /* $CAF8 */
extern UWORD *wSynthBandDst;                /* $CB3C */
extern UWORD wSynthBandDstW;                /* $CB3C */
extern UBYTE *wSynthBandSlot;               /* $CB3E */
extern UWORD wSynthBandSlotW;               /* $CB3E */

UBYTE SynthWrapIndex32(UBYTE v);

void SynthFilterBandPass(void)
{
    UWORD acc;
    UBYTE step;
    UBYTE i;
    UBYTE n;
    volatile UBYTE k;
    UBYTE pos;
    UWORD frac;
    UWORD sample;
    UBYTE next2;
    extern UBYTE wSynthBandIdx;             /* $CB3E */

    acc = 0;
    step = -wSynthFilterCutoff;
    i = 0;
    n = 0x20;
    MemSet(wSynthBufA, 0, 0x40);
    MemSet(wSynthBufB, 0, 0x40);
    while (step < 0x80) {
        k = 0;
        step <<= 1;
        n >>= 1;
        for (i = 0; i < n; i++) {
            wSynthBandDst = (UWORD *)(wSynthWaveBuf + i);
            *(UBYTE *)wSynthBandDst = (wSynthWaveBuf[k++] + wSynthWaveBuf[k++]) >> 1;
        }
    }
    n--;
    for (;;) {
        pos = acc >> 8;
        frac = (UBYTE)acc;
        sample = wSynthWaveBuf[SynthWrapIndex32(i)];
        if (pos == 0x20) {
            for (i = 0; i < 0x20; i++) {
                wSynthBandDst = (UWORD *)(wSynthWaveBuf + SynthWrapIndex32(i));
                next2 = i << 1;
                wSynthBandSlotW = *(UWORD *)(wSynthBufA + next2);
                *(UBYTE *)wSynthBandDst = wSynthBandSlotW / *(UWORD *)(wSynthBufB + next2);
            }
            return;
        }
        next2 = ((pos + 1) & 0x1F) << 1;
        wSynthBandSlot = wSynthBufA + next2;
        wSynthBandDstW = *(UWORD *)wSynthBandSlot;
        *(UWORD *)wSynthBandSlot = wSynthBandDstW + sample * frac;
        wSynthBandSlotW = *(UWORD *)(wSynthBufB + next2);
        wSynthBandSlotW += frac;
        *(UWORD *)(wSynthBufB + next2) = wSynthBandSlotW;
        frac = 0x100 - frac;
        wSynthBandIdx = pos << 1;
        wSynthBandDstW = *(UWORD *)(wSynthBufA + wSynthBandIdx);
        *(UWORD *)(wSynthBufA + wSynthBandIdx) = wSynthBandDstW + sample * frac;
        *(UWORD *)(wSynthBufB + wSynthBandIdx) += frac;
        acc += step;
        i++;
        i &= n;
    }
}
