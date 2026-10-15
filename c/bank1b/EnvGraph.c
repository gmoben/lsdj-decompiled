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

extern UBYTE ExpoCurve16[];                 /* $00:$300F, envelope speed -> frame count */
extern UBYTE wDecompressBuf[];                  /* $DA00 */
void PlotEnvCurve(UBYTE x, UBYTE y);
void OrBitsIntoDaBuf(void);              /* $00:$3261 */
void UploadTilesToVram(void);               /* $00:$3298 */
int abs(int v);                           /* $00:$343F */

UWORD GetInstrEnvBytes(void)
{
    UWORD total;
    UBYTE v, v2, v3;
    char vol;
    UBYTE len;
    char vol2;
    UBYTE len2;
    char vol3;
    UBYTE len3;
    int d;

    total = 0;
    v = ReadCurInstrByte(1);
    vol = v >> 4;
    len = ExpoCurve16[v & 0x0F];
    if (len) {
        v2 = ReadCurInstrByte(9);
        vol2 = v2 >> 4;
        len2 = ExpoCurve16[v2 & 0x0F];
        total = abs(vol2 - vol);
        if (total == 0)
            total = 1;
        total = total * len;
        if (len2) {
            v3 = ReadCurInstrByte(10);
            vol3 = v3 >> 4;
            len3 = ExpoCurve16[v3 & 0x0F];
            d = vol3 - vol2;
            if (abs(d))
                d = abs(d);
            else
                d = 1;
            total += d * len2;
            d = vol3;
            total += d * len3;
        }
    }
    return total;
}

extern UBYTE wEnvPlotX;                     /* $CBD8, the last plotted column */

/* $7200: plot envelope column x (clamped to $3F) at height y into the
   graph buffer ($DA00, wDecompressBuf), filling the columns since wEnvPlotX
   with y and keeping the higher of the two at x. The buffer is addressed
   as a `char *`: the store's (char)y is the loop invariant the ROM holds in
   c, and the compare at x reuses that signed copy. */
void PlotEnvCurve(UBYTE x, UBYTE y)
{
    if (x > 0x3F)
        x = 0x3F;
    while (wEnvPlotX + 1 < x) {
        wEnvPlotX = wEnvPlotX + 1;
        if (wEnvPlotX >= 0x40)
            DebugTrap();
        ((char *)0xDA00)[wEnvPlotX] = y;
    }
    if (((char *)0xDA00)[x] < y)
        ((char *)0xDA00)[x] = y;
    wEnvPlotX = x;
}

/* The instrument screen's envelope graph: the attack/decay/release
   segments of instrument bytes 1, 9 and 10 plotted over 64 columns, the
   time axis scaled by GetInstrEnvBytes' total (500 at least) */
void ComputeEnv(void)
{
    UWORD pos;
    UWORD total;
    UBYTE v;
    UBYTE vol;
    UBYTE len;
    UBYTE v2;
    UBYTE vol2;
    UBYTE len2;
    UBYTE vol3;
    UBYTE len3;
    UBYTE v3;

    pos = 0;
    total = GetInstrEnvBytes();
    v = ReadCurInstrByte(1);
    vol = v >> 4;
    len = ExpoCurve16[v & 0x0F];
    if (total < 500)
        total = 500;
    total >>= 3;
    MemSet(wDecompressBuf, 0, 0xC0);
    PlotEnvCurve(0, vol);
    if (len) {
        v2 = ReadCurInstrByte(9);
        vol2 = v2 >> 4;
        len2 = ExpoCurve16[v2 & 0x0F];
        do {
            pos += len;
            PlotEnvCurve((pos << 3) / total, vol);
            if (vol2 < vol)
                vol--;
            else if (vol < vol2)
                vol++;
        } while (vol != vol2);
        if (len2) {
            v3 = ReadCurInstrByte(10);
            vol3 = v3 >> 4;
            len3 = ExpoCurve16[v3 & 0x0F];
            do {
                pos += len2;
                PlotEnvCurve((pos << 3) / total, vol);
                if (vol3 < vol)
                    vol--;
                else if (vol < vol3)
                    vol++;
            } while (vol != vol3);
            if (len3) {
                while (vol) {
                    pos += len3;
                    PlotEnvCurve((pos << 3) / total, vol);
                    vol--;
                }
            }
        }
    }
    PlotEnvCurve((pos << 3) / total, vol);
    PlotEnvCurve(0x3F, vol);
    OrBitsIntoDaBuf();
    UploadTilesToVram();
}

void UpdateEnvSprites(void)
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
void UpdateInstrEnv(void)
{
    UBYTE type;

    type = ReadCurInstrByte(0);
    if (type == 1 || type == 2)
        return;
    ComputeEnv();
    UpdateEnvSprites();
}
