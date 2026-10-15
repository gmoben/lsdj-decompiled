/*
 * The envelope graph -- LSDj 9.4.0, ROM bank $1B.
 *
 * The instrument screen's envelope graph: the envelope bytes, the plotted
 * curve and its sprites.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE ExponentialCurve16[];                 /* $00:$300F, envelope speed -> frame count */
extern UBYTE wDecompressBuffer[];                  /* $DA00 */
void PlotEnvelopeCurve(UBYTE x, UBYTE y);
void OrBitsIntoDaBuffer(void);              /* $00:$3261 */
void UploadTilesToVram(void);               /* $00:$3298 */
int abs(int v);                           /* $00:$343F */

UWORD GetInstrumentEnvelopeBytes(void)
{
    UWORD total;
    UBYTE v, v2, v3;
    char volume;
    UBYTE length;
    char volume2;
    UBYTE length2;
    char volume3;
    UBYTE length3;
    int d;

    total = 0;
    v = ReadCurrentInstrumentByte(1);
    volume = v >> 4;
    length = ExponentialCurve16[v & 0x0F];
    if (length) {
        v2 = ReadCurrentInstrumentByte(9);
        volume2 = v2 >> 4;
        length2 = ExponentialCurve16[v2 & 0x0F];
        total = abs(volume2 - volume);
        if (total == 0)
            total = 1;
        total = total * length;
        if (length2) {
            v3 = ReadCurrentInstrumentByte(10);
            volume3 = v3 >> 4;
            length3 = ExponentialCurve16[v3 & 0x0F];
            d = volume3 - volume2;
            if (abs(d))
                d = abs(d);
            else
                d = 1;
            total += d * length2;
            d = volume3;
            total += d * length3;
        }
    }
    return total;
}

extern UBYTE wEnvelopePlotX;                     /* $CBD8, the last plotted column */

/* $7200: plot envelope column x (clamped to $3F) at height y into the
   graph buffer ($DA00, wDecompressBuffer), filling the columns since wEnvelopePlotX
   with y and keeping the higher of the two at x. The buffer is addressed
   as a `char *`: the store's (char)y is the loop invariant the ROM holds in
   c, and the compare at x reuses that signed copy. */
void PlotEnvelopeCurve(UBYTE x, UBYTE y)
{
    if (x > 0x3F)
        x = 0x3F;
    while (wEnvelopePlotX + 1 < x) {
        wEnvelopePlotX = wEnvelopePlotX + 1;
        if (wEnvelopePlotX >= 0x40)
            DebugTrap();
        ((char *)0xDA00)[wEnvelopePlotX] = y;
    }
    if (((char *)0xDA00)[x] < y)
        ((char *)0xDA00)[x] = y;
    wEnvelopePlotX = x;
}

/* The instrument screen's envelope graph: the attack/decay/release
   segments of instrument bytes 1, 9 and 10 plotted over 64 columns, the
   time axis scaled by GetInstrumentEnvelopeBytes' total (500 at least) */
void ComputeEnvelope(void)
{
    UWORD position;
    UWORD total;
    UBYTE v;
    UBYTE volume;
    UBYTE length;
    UBYTE v2;
    UBYTE volume2;
    UBYTE length2;
    UBYTE volume3;
    UBYTE length3;
    UBYTE v3;

    position = 0;
    total = GetInstrumentEnvelopeBytes();
    v = ReadCurrentInstrumentByte(1);
    volume = v >> 4;
    length = ExponentialCurve16[v & 0x0F];
    if (total < 500)
        total = 500;
    total >>= 3;
    FillMemory(wDecompressBuffer, 0, 0xC0);
    PlotEnvelopeCurve(0, volume);
    if (length) {
        v2 = ReadCurrentInstrumentByte(9);
        volume2 = v2 >> 4;
        length2 = ExponentialCurve16[v2 & 0x0F];
        do {
            position += length;
            PlotEnvelopeCurve((position << 3) / total, volume);
            if (volume2 < volume)
                volume--;
            else if (volume < volume2)
                volume++;
        } while (volume != volume2);
        if (length2) {
            v3 = ReadCurrentInstrumentByte(10);
            volume3 = v3 >> 4;
            length3 = ExponentialCurve16[v3 & 0x0F];
            do {
                position += length2;
                PlotEnvelopeCurve((position << 3) / total, volume);
                if (volume3 < volume)
                    volume--;
                else if (volume < volume3)
                    volume++;
            } while (volume != volume3);
            if (length3) {
                while (volume) {
                    position += length3;
                    PlotEnvelopeCurve((position << 3) / total, volume);
                    volume--;
                }
            }
        }
    }
    PlotEnvelopeCurve((position << 3) / total, volume);
    PlotEnvelopeCurve(0x3F, volume);
    OrBitsIntoDaBuffer();
    UploadTilesToVram();
}

void UpdateEnvelopeSprites(void)
{
    UBYTE i, t, u;

    for (i = 0; i != 8; i++) {
        t = i << 2;
        SetOamByte(t + 0x0C, 0x40);
        u = (i << 3) + 0x30;
        SetOamByte(t + 0x0D, u);
        SetOamByte(t + 0x0E, u = i + 6);
    }
}

/* redraw the envelope graph and its sprites unless the instrument is a
   wave (1) or kit (2) */
void UpdateInstrumentEnvelope(void)
{
    UBYTE type;

    type = ReadCurrentInstrumentByte(0);
    if (type == 1 || type == 2)
        return;
    ComputeEnvelope();
    UpdateEnvelopeSprites();
}
