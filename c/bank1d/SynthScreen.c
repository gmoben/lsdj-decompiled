/*
 * The synth screen -- LSDj 9.4.0, ROM bank $1D.
 *
 * Its fields and input, the per-wave parameter curves and their loader, the
 * LOWP/HIGHP filter, the overwrite prompt.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
extern UBYTE wSynthWaveBuffer[];               /* $CA34, [32] */
extern UBYTE wSynthBufferA[];                  /* $CAB8, [64]: filter accumulator, sample * weight per slot */
extern UBYTE wSynthBufferB[];                  /* $CAF8, [64]: filter accumulator, weight per slot */
extern UBYTE *wSynthFilterPointer;              /* $CB38 */
extern UWORD wSynthFilterPointerW;              /* $CB38, the same word (aliased by match_function.py) */
extern UWORD wSynthFilterValue;               /* $CB3A */
extern UBYTE wSynthWaveScratch[];               /* $CB40, [32] */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthCursor;                  /* $C442 */
extern UBYTE SynthFieldLayout[];            /* $1D:$60E5, rows of the 16 fields */
extern UBYTE SynthFieldX[];                 /* $1D:$60F5 */
extern UBYTE SynthFieldWidth[];             /* $1D:$6105 */
extern UBYTE wSynthScreen;                  /* $C441, the synth being edited */
extern UBYTE wOverwriteStart;               /* $CB61 */
extern UBYTE wOverwriteEnd;                 /* $CB62 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSynthNavigationSide;                 /* $CB63 */
UBYTE ReadSynthParameter(UBYTE field);          /* the current synth's param byte */
void WriteSynthParameter(UBYTE field, UBYTE v);
void SynthFilterBandPass(void);
void SynthFilterPass(void);
void SendLinkClipboard(void);                   /* $1C:$71CD */
void DrawAllChannelStatus(void);
void DrawSynthNumber(void);
void BuildOverwritePrompt(void);
void RedrawScreenFar(void);
void ClearSynthFlag(UBYTE x);
void MoveMapCursorLeft(void);                     /* bank 5 */
void MoveMapCursorRight(void);                    /* bank 5 */
void MoveMapCursorDown(void);                     /* bank 5 */
void ShowSynthFieldHelp(void);               /* bank 5 */
void SoundToggleSongPlayback(void);                 /* bank 2 */
void SoundTogglePlayback(void);                    /* bank 2 */
extern UBYTE wSynthRenderIndex;               /* $C9F0 */
extern UBYTE wSynthSegmentLength1;                 /* $C9E8 */
extern UBYTE wSynthSegmentLength2;                 /* $C9EA */
extern UBYTE wSynthSegmentLength3;                 /* $C9EF */
extern UBYTE wSynthSignalType;              /* $C9ED */
extern UBYTE wSynthCurveSegment2[];             /* $CA54, [16] */
extern UBYTE wSynthCurveCutoff[];           /* $CA64, [16] */
extern UBYTE wSynthCurveSegment1[];             /* $CA74, [16] */
extern UBYTE wSynthCurveSegment3[];             /* $CA84, [16] */
extern UBYTE wSynthCurveAmplitude[];              /* $CA94, [16] */
extern UBYTE wSynthCurveLimit[];            /* $CAA4, [16], biased by $0F */
extern UBYTE wSynthShapeBuffer[];              /* $C9F4, byte view of 32 signed words */
extern BYTE SynthShapeTableA[];             /* $1D:$4EF0, signed correction samples */
extern BYTE SynthShapeTableB[];             /* $1D:$50F0 */
extern char SynthSeedTableA[];              /* $1E:$71EC, shape 0 / signal 0 */
extern char SynthSeedTableB[];              /* $1E:$73EC, shape 0 / signal 1 */
extern char SynthSeedTableC[];              /* $1E:$74EC, shape 1 / signal 0 */
extern char SynthSeedTableD[];              /* $1E:$76EC, shape 1 / signal 1 */
extern char SynthSeedTableE[];              /* $1E:$77EC, shape 0 / signal 2 */
extern char SynthSeedTableF[];              /* $1E:$78EC, shape 1 / signal 2 */
UWORD SynthLockByteAddressB1D(UBYTE x);
void ApplySynthFilter(void);
void LoadSynthSignalWave(void);
void RenderSynthWaveType(void);
void ComposeSynthWave(void);
void WriteSynthWaveColumn(void);
void RenderSynthWaveA(UBYTE amplitude);
void RenderSynthWaveB(UBYTE amplitude);
extern UBYTE wSynthWaveShape;               /* $C9E9 */
extern UBYTE wSynthFilterParameter;             /* $C9EE, distortion: CLIP/FOLD/WRAP */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE *wSynthRenderDestination;              /* $C9F2 */
extern UWORD *wSynthBandDestination;                /* $CB3C, band-pass filter: output pointer / the slot value */
extern UWORD wSynthBandDestinationW;                /* $CB3C, the same word (aliased by match_function.py) */
extern UBYTE *wSynthBandSlot;               /* $CB3E, band-pass filter: the slot pointer / the BufB value */
extern UWORD wSynthBandSlotW;               /* $CB3E, the same word (aliased by match_function.py) */
extern UBYTE wWaveEditX;                    /* $CB60 */
void FillSynthWaveRange(UBYTE a, UBYTE b, UBYTE *buffer);

/* Add the current table cycle, weighted by the fractional segment parameter.
 * Keep the two signal-type comparisons separate: SDCC shares their boolean
 * temporary, but naming it changes the register spills. The signed byte is
 * widened through WORD before storage in samp; the final shift is explicitly
 * sign-extended because the period compiler shifts words logically.
 */
void RenderSynthWaveA(UBYTE amplitude)
{
    UBYTE volume;
    UBYTE i;
    BYTE *tbl;
    UWORD samp;

    volume = wSynthSegmentLength1 >> 4;
    if (wSynthSignalType == 2)
        tbl = &SynthShapeTableB[0];
    else
        tbl = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        volume &= 0x0E;
        if (wSynthSignalType == 2) {
            volume >>= 1;
            if (volume > 4)
                volume = 4;
        }
    }
    for (i = 0; i < volume; i++)
        tbl += 0x20;
    if (wSynthSignalType) {
        volume = wSynthSegmentLength1 & 0x1F;
        volume >>= 1;
    } else {
        volume = wSynthSegmentLength1 & 0x0F;
    }
    if (!volume)
        return;
    volume = volume * amplitude;
    for (i = 0; i < 0x20; i++) {
        samp = (WORD)*tbl;
        samp = samp * volume;
        if ((WORD)samp < 0) {
            samp >>= 4;
            samp |= 0xF000;
        } else {
            samp >>= 4;
        }
        *(UWORD *)(wSynthShapeBuffer + (i << 1)) += samp;
        tbl++;
    }
}

/* The complementary weight comes from the preceding cycle. There is no
 * preceding cycle at segment index zero, so that contribution is skipped.
 */
void RenderSynthWaveB(UBYTE amplitude)
{
    UBYTE i;
    UBYTE volume;
    BYTE *tbl;
    UWORD samp;

    if (wSynthSignalType == 2)
        tbl = &SynthShapeTableB[0];
    else
        tbl = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        volume = wSynthSegmentLength1 >> 5;
        if (!volume)
            return;
        volume--;
        volume <<= 1;
        if (wSynthSignalType == 2) {
            volume >>= 1;
            if (volume > 4)
                volume = 4;
        }
    } else {
        volume = wSynthSegmentLength1 >> 4;
        if (!volume)
            return;
        volume--;
    }
    for (i = 0; i < volume; i++)
        tbl += 0x20;
    if (wSynthSignalType) {
        volume = wSynthSegmentLength1 & 0x1F;
        volume >>= 1;
    } else {
        volume = wSynthSegmentLength1 & 0x0F;
    }
    volume ^= 0x0F;
    if (!volume)
        return;
    volume = volume * amplitude;
    for (i = 0; i < 0x20; i++) {
        samp = (WORD)*tbl;
        samp = samp * volume;
        if ((WORD)samp < 0) {
            samp >>= 4;
            samp |= 0xF000;
        } else {
            samp >>= 4;
        }
        *(UWORD *)(wSynthShapeBuffer + (i << 1)) += samp;
        tbl++;
    }
}

/* Scale the signed accumulator, apply the selected distortion within a
 * per-frame window, and quantize to the wave buffer. Keep the shifts and
 * sign extension explicit for SDCC 2.2.1; FOLD's >= hi + 1 also preserves
 * the loop-invariant temporary in the ROM's stack frame.
 */
void ComposeSynthWave(void)
{
    UBYTE *destination;
    UBYTE i;
    WORD span;
    WORD low;
    WORD high;
    WORD v;

    destination = &wSynthWaveBuffer[0];
    span = 0x10 - (UWORD)wSynthCurveLimit[wSynthRenderIndex];
    low = (span >> 1) << 4;
    high = 0x100 - (((span + 1) >> 1) << 4);
    for (i = 0; i < 0x20; i++) {
        v = *(WORD *)(wSynthShapeBuffer + (i << 1));
        v = v * wSynthSegmentLength3;
        v >>= 4;
        if (v & 0x0800)
            v |= 0xF000;
        v += 0x80;
        v += wSynthSegmentLength2;
        switch (wSynthFilterParameter) {
        case 0:
            if (v < low)
                v = low;
            else if (v > high)
                v = high;
            break;
        case 1:
            v &= 0xFFF0;
            if (high == low) {
                v = high;
                break;
            }
            for (;;) {
                if (v >= high + 1)
                    v = (high << 1) - v;
                else if (v < low)
                    v = (low << 1) - v;
                else
                    break;
            }
            break;
        case 2:
            v &= 0xFFF0;
            if (high == low) {
                v = high;
                break;
            }
            while (v > high) {
                v = v - (high + 0x10);
                v = v + low;
            }
            while (v < low) {
                v = v + (high + 0x10);
                v = v - low;
            }
            break;
        }
        *destination = v >> 4;
        destination++;
    }
}

/* Seed the signed accumulator from bank $1E, or zero it for shape 2, then
 * add the weighted bank-$1D cycles. p holds both the cycle offset and the
 * resulting banked address; the cast at LoadSynthShape preserves its ABI.
 */
void RenderSynthWaveType(void)
{
    UBYTE amplitude;
    UBYTE i;
    UWORD p;

    amplitude = wSynthCurveAmplitude[wSynthRenderIndex];
    switch (wSynthWaveShape) {
    case 0:
        switch (wSynthSignalType) {
        default:
            p = wSynthSegmentLength1 & 0xF0;
            p = (UWORD)(SynthSeedTableA + (p << 1));
            break;
        case 1:
            p = wSynthSegmentLength1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableB + (p << 1));
            break;
        case 2:
            p = wSynthSegmentLength1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableE + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        RenderSynthWaveA(amplitude + 1);
        if (amplitude)
            RenderSynthWaveB(amplitude);
        break;
    case 1:
        switch (wSynthSignalType) {
        case 0:
            p = wSynthSegmentLength1 & 0xF0;
            p = (UWORD)(SynthSeedTableC + (p << 1));
            break;
        case 1:
            p = wSynthSegmentLength1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableD + (p << 1));
            break;
        case 2:
            p = wSynthSegmentLength1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableF + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        RenderSynthWaveB(amplitude + 1);
        if (amplitude)
            RenderSynthWaveA(amplitude);
        break;
    case 2:
        i = 0x20;
        do {
            *(UWORD *)(wSynthShapeBuffer + ((i - 1) << 1)) = 0;
        } while (--i);
        RenderSynthWaveA(amplitude ? amplitude : 1);
        RenderSynthWaveB(amplitude ? amplitude : 1);
        break;
    case 3:
        switch (wSynthSignalType) {
        case 0:
            p = (UWORD)(SynthSeedTableA + 0xF0);
            break;
        case 1:
            p = (UWORD)(SynthSeedTableB + 0x70);
            break;
        case 2:
            p = (UWORD)(SynthSeedTableE + 0x70);
            break;
        }
        LoadSynthShape((char *)p);
        RenderSynthWaveA(amplitude);
        RenderSynthWaveB(amplitude);
        break;
    }
}

UBYTE WrapSynthIndex32(UBYTE v)
{
    return (v + 1) & 0x1F;
}

/* $58E6: the LOWP/HIGHP filter (every type but BANDP and ALLP). Each of
   the 32 samples is scattered over two accumulator slots at an 8.8 position
   that advances by a cutoff-driven step (the slower the step, the more
   samples a slot averages: a low cutoff smears the wave); HIGHP reverses
   direction after 15 samples. A second pass divides each slot's sum by its
   weight; LOWP fills a slot's missing weight with the mid level 8.
   The slot pointer and value live in two scratch words ($CB38/$CB3A); the
   second half reuses the value's low byte for the doubled index and the
   pointer word for the value -- the index is a block-scope extern, which
   this compiler keeps in a register and spills into the global. */
void SynthFilterLowHigh(void)
{
    UWORD acc;
    UBYTE step;
    UBYTE i;
    UBYTE position;
    UWORD fraction;
    UWORD sample;
    UWORD ua;
    UWORD ub;
    UBYTE next2;
    extern UBYTE wSynthFilterIndex;           /* $CB3A, wSynthFilterValue's low byte (aliased by match_function.py) */

    acc = 0;
    step = -(wSynthFilterCutoff + (wSynthFilterCutoff >> 5));
    FillMemory(wSynthBufferA, 0, 0x40);
    FillMemory(wSynthBufferB, 0, 0x40);
    for (i = 0; i < 0x20; i++) {
        position = acc >> 8;
        fraction = (UBYTE)acc;
        sample = wSynthWaveBuffer[WrapSynthIndex32(i)];
        next2 = ((position + 1) & 0x1F) << 1;
        wSynthFilterPointer = wSynthBufferA + next2;
        wSynthFilterValue = *(UWORD *)wSynthFilterPointer;
        *(UWORD *)wSynthFilterPointer = wSynthFilterValue + sample * fraction;
        wSynthFilterValue = *(UWORD *)(wSynthBufferB + next2);
        wSynthFilterValue += fraction;
        *(UWORD *)(wSynthBufferB + next2) = wSynthFilterValue;
        fraction = 0x100 - fraction;
        wSynthFilterIndex = position << 1;
        wSynthFilterPointerW = *(UWORD *)(wSynthBufferA + wSynthFilterIndex);
        *(UWORD *)(wSynthBufferA + wSynthFilterIndex) = wSynthFilterPointerW + sample * fraction;
        *(UWORD *)(wSynthBufferB + wSynthFilterIndex) += fraction;
        if (wSynthFilterType == 0 || i < 0x0F)
            acc += step;
        else
            acc += 0x200 - step;
    }
    for (i = 0; i < 0x20; i++) {
        ua = *(UWORD *)(wSynthBufferA + (i << 1));
        ub = *(UWORD *)(wSynthBufferB + (i << 1));
        if (wSynthFilterType == 0) {
            if ((ub - 0x100) & 0x8000) {
                ua += (0x100 - ub) << 3;
                ub = 0x100;
            }
        }
        wSynthWaveBuffer[WrapSynthIndex32(i)] = ua / ub;
    }
}

/* The BANDP filter: halve the wave an octave per cutoff doubling over a
 * shortened n (the two copies through a volatile k), then the LOWP/HIGHP
 * scatter core in a `for (;;)` whose head computes pos/frac/sample and, at
 * pos == 0x20, runs the normalise loop and returns.
 *
 * lsdj-alloc(SynthFilterBandPass): BUSYC=82-82
 * -- register c held when `next2 = i << 1` is allocated, so next2 takes b
 * and the BufB word comes out as the pair bc (docs/toolchain.md, "The
 * phantom register"). */
void SynthFilterBandPass(void)
{
    UWORD acc;
    UBYTE step;
    UBYTE i;
    UBYTE n;
    volatile UBYTE k;
    UBYTE position;
    UWORD fraction;
    UWORD sample;
    UBYTE next2;
    extern UBYTE wSynthBandIndex;             /* $CB3E */

    acc = 0;
    step = -wSynthFilterCutoff;
    i = 0;
    n = 0x20;
    FillMemory(wSynthBufferA, 0, 0x40);
    FillMemory(wSynthBufferB, 0, 0x40);
    while (step < 0x80) {
        k = 0;
        step <<= 1;
        n >>= 1;
        for (i = 0; i < n; i++) {
            wSynthBandDestination = (UWORD *)(wSynthWaveBuffer + i);
            *(UBYTE *)wSynthBandDestination = (wSynthWaveBuffer[k++] + wSynthWaveBuffer[k++]) >> 1;
        }
    }
    n--;
    for (;;) {
        position = acc >> 8;
        fraction = (UBYTE)acc;
        sample = wSynthWaveBuffer[WrapSynthIndex32(i)];
        if (position == 0x20) {
            for (i = 0; i < 0x20; i++) {
                wSynthBandDestination = (UWORD *)(wSynthWaveBuffer + WrapSynthIndex32(i));
                next2 = i << 1;
                wSynthBandSlotW = *(UWORD *)(wSynthBufferA + next2);
                *(UBYTE *)wSynthBandDestination = wSynthBandSlotW / *(UWORD *)(wSynthBufferB + next2);
            }
            return;
        }
        next2 = ((position + 1) & 0x1F) << 1;
        wSynthBandSlot = wSynthBufferA + next2;
        wSynthBandDestinationW = *(UWORD *)wSynthBandSlot;
        *(UWORD *)wSynthBandSlot = wSynthBandDestinationW + sample * fraction;
        wSynthBandSlotW = *(UWORD *)(wSynthBufferB + next2);
        wSynthBandSlotW += fraction;
        *(UWORD *)(wSynthBufferB + next2) = wSynthBandSlotW;
        fraction = 0x100 - fraction;
        wSynthBandIndex = position << 1;
        wSynthBandDestinationW = *(UWORD *)(wSynthBufferA + wSynthBandIndex);
        *(UWORD *)(wSynthBufferA + wSynthBandIndex) = wSynthBandDestinationW + sample * fraction;
        *(UWORD *)(wSynthBufferB + wSynthBandIndex) += fraction;
        acc += step;
        i++;
        i &= n;
    }
}

void SynthFilterPass(void)
{
    UBYTE i;
    UBYTE n;
    UBYTE k;

    n = 0x20 - (wSynthFilterCutoff >> 3);
    for (i = 0; i < 0x20; i++) {
        k = (((UWORD)i << 5) + 0x10) / n % 0x20;
        wSynthWaveScratch[WrapSynthIndex32(i)] = wSynthWaveBuffer[WrapSynthIndex32(k)];
    }
    memcpy(wSynthWaveBuffer, wSynthWaveScratch, 0x20);
}

void ApplySynthFilter(void)
{
    if (wSynthFilterCutoff) {
        switch (wSynthFilterType) {
        case 2:
            SynthFilterBandPass();
            break;
        case 3:
            SynthFilterPass();
            break;
        default:
            SynthFilterLowHigh();
        }
    }
}

/* lsdj-alloc(WriteSynthWaveColumn): BUSYC=1-17
 * -- register c held from the first instruction to the loop body's last
 * byte temporary, so i takes b and is evicted by the body, and the pointer
 * pair comes out as bc (docs/toolchain.md, "The phantom register"). */
void WriteSynthWaveColumn(void)
{
    UBYTE i;

    for (i = 0; i < 0x20; i += 2) {
        wWaveEditX = wSynthWaveBuffer[i] << 4;
        wWaveEditX = (wSynthWaveBuffer[i + 1] + wWaveEditX) ^ 0xFF;
        WriteSongByte(3, (UWORD)wSynthRenderDestination++, wWaveEditX);
    }
}

/* $5F90: load the custom-wave SIGNAL (W.FX: wave $F0 + synth, 16 packed
   bytes at SRAM $AF00 + 16 * synth) into wSynthShapeBuffer as 32 signed words,
   each nibble n becoming 120 - 16 * n. */
void LoadSynthSignalWave(void)
{
    UBYTE i;
    UWORD p;
    char v;
    int w;

    i = 0;
    p = wSynthScreen;
    p <<= 4;
    p += (UWORD)0xAF00;
    while (i != 0x20) {
        v = 0xFF ^ ReadSongByte(3, p);
        w = (char)((v & 0xF0) + 0x88);
        *(int *)(wSynthShapeBuffer + (i++ << 1)) = w;
        w = (char)(((v & 0x0F) << 4) + 0x88);
        *(int *)(wSynthShapeBuffer + (i++ << 1)) = w;
        p++;
    }
}

void RenderSynthWave(void)
{
    if (wSynthRenderIndex >= 0x10)
        DebugTrap();
    wSynthSegmentLength3 = wSynthCurveSegment3[wSynthRenderIndex];
    wSynthSegmentLength1 = wSynthCurveSegment1[wSynthRenderIndex];
    wSynthSegmentLength2 = wSynthCurveSegment2[wSynthRenderIndex];
    wSynthFilterCutoff = wSynthCurveCutoff[wSynthRenderIndex];
    if (wSynthSignalType == 3)
        LoadSynthSignalWave();
    else
        RenderSynthWaveType();
    ComposeSynthWave();
    ApplySynthFilter();
    WriteSynthWaveColumn();
}

UWORD SynthLockByteAddressB1D(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

UBYTE IsSynthLocked(UBYTE x)
{
    UBYTE v;

    v = ReadSongByte(1, SynthLockByteAddressB1D(x));
    return v & (1 << (x & 7));
}

void ClearSynthFlag(UBYTE x)
{
    UWORD p;
    UBYTE v;

    p = SynthLockByteAddressB1D(x);
    v = ReadSongByte(1, p);
    x &= 7;
    v &= ~(1 << x);
    WriteSongByte(1, p, v);
}

void FillSynthWaveRange(UBYTE a, UBYTE b, UBYTE *buffer)
{
    UBYTE i;

    if (b >= a) {
        b -= a;
        if (b < 0xFF)
            b++;
        for (i = 0; i < 16; i++)
            *buffer++ = ((UWORD)i * b >> 4) + a;
    } else {
        a -= b;
        if (a < 0xFF)
            a++;
        for (i = 15; i != 0xFF; i--)
            *buffer++ = b + ((UWORD)i * a >> 4);
    }
}

void DrawSynthField(UBYTE v)
{
    switch (v) {
    case 0:
        DrawString(" PINCH");
        break;
    case 1:
        DrawString("  WARP");
        break;
    case 2:
        DrawString("RESYNC");
        break;
    case 3:
        DrawString("RESYN2");
        break;
    }
}

UBYTE ReadSynthParameter(UBYTE field)
{
    UWORD p;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    return ReadSongByte(1, p);
}

UBYTE SynthSignalIsFrame(void)
{
    return ReadSynthParameter(0) == 3;
}

void DrawSynthFilterField(void)
{
    wVramCursor = (UBYTE *)0x9868;
    if (SynthSignalIsFrame()) {
        DrawString("-----");
    } else {
        switch (ReadSynthParameter(1)) {
        case 0:
            DrawString(" LOWP");
            break;
        case 1:
            DrawString("HIGHP");
            break;
        case 2:
            DrawString("BANDP");
            break;
        case 3:
            DrawString(" ALLP");
            break;
        case 4:
            DrawString("NOTCH");
            break;
        }
    }
}

void DrawSynthWaveField(void)
{
    wVramCursor = (UBYTE *)0x9849;
    switch (ReadSynthParameter(0)) {
    case 0:
        DrawString("iiii");
        break;
    case 1:
        DrawString("mmmm");
        break;
    case 2:
        DrawString("kkkk");
        break;
    case 3:
        DrawString("W.F");
        DrawByte(wSynthScreen);
        break;
    default:
        DrawString("??");
        break;
    }
}

void EraseSynthCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        ColorField(x, SynthFieldLayout[wSynthCursor], 1, 1);
}

void DrawSynthCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        ColorField(x, SynthFieldLayout[wSynthCursor], 1, hGameboyType ? 3 : 2);
}

void DrawSynthDistortionField(UBYTE v)
{
    switch (v) {
    case 0:
        DrawString("CLIP");
        break;
    case 1:
        DrawString("FOLD");
        break;
    case 2:
        DrawString("WRAP");
        break;
    }
}

/* Redraw both endpoints of the synth parameters. The integer SRAM cursor
 * is incremented between fields even when a saved-frame source hides one.
 */
void DrawSynthNumber(void)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x98E8;
    DrawByte(wSynthScreen);
    wVramCursor = (UBYTE *)0x98EB;
    DrawByte(wSynthScreen);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawSynthWaveField();
    p++;
    DrawSynthFilterField();
    p++;
    v = ReadSongByte(1, (UWORD)p++);
    wVramCursor = (UBYTE *)0x9969;
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v >> 4);
    wVramCursor = (UBYTE *)0x996C;
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v & 0x0F);
    wVramCursor = (UBYTE *)0x9889;
    DrawSynthDistortionField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x98A7;
    DrawSynthField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9928;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9948;
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)0x99C8;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9988;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x992B;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x994B;
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)0x99CB;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x998B;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x99A8;
    DrawHexByte(ReadSongByte(1, (UWORD)p++) + 0x0F);
    wVramCursor = (UBYTE *)0x99AB;
    DrawHexByte(ReadSongByte(1, (UWORD)p) + 0x0F);
    DrawSynthCursor();
}

void DrawSynthScreen(void)
{
    wSynth &= 0x0F;
    wSynth = wSynth + (wSynthScreen << 4);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    DrawSynthNumber();
    FarCall(5, DrawMapTopRow);
}

void LoadSynthParameters(void)
{
    volatile UWORD p;
    volatile UWORD o;
    volatile UWORD s;
    UBYTE v;
    UBYTE x, y;

    s = wSynthScreen;
    o = s << 4;
    p = (UWORD)SramSynthParams + o;
    wSynthSignalType = ReadSongByte(1, p++);
    wSynthWaveShape = ReadSongByte(1, p++);
    v = ReadSongByte(1, p++);
    FillSynthWaveRange(v >> 4, v & 0x0F, wSynthCurveAmplitude);
    wSynthFilterParameter = ReadSongByte(1, p++);
    wSynthFilterType = ReadSongByte(1, p++);
    wSynthSegmentLength3 = ReadSongByte(1, p++);
    wSynthSegmentLength1 = ReadSongByte(1, p++);
    wSynthFilterCutoff = ReadSongByte(1, p++) << 3;
    wSynthSegmentLength2 = ReadSongByte(1, p++);
    FillSynthWaveRange(wSynthSegmentLength3, ReadSongByte(1, p++), wSynthCurveSegment3);
    FillSynthWaveRange(wSynthSegmentLength1, ReadSongByte(1, p++), wSynthCurveSegment1);
    FillSynthWaveRange(wSynthFilterCutoff, ReadSongByte(1, p++) << 3, wSynthCurveCutoff);
    FillSynthWaveRange(wSynthSegmentLength2, ReadSongByte(1, p++), wSynthCurveSegment2);
    x = ReadSongByte(1, p++);
    y = ReadSongByte(1, p);
    FillSynthWaveRange(x + 0x0F, y + 0x0F, wSynthCurveLimit);
    wSynthRenderDestination = (UBYTE *)((s << 8) + (UWORD)0xA000);
    wSynthRenderIndex = 0;
    wSynthRenderActive = 1;
}

/* The confirmation dialog's frame: border tiles through the identity map,
 * the interior cleared with tile 2 in palette 1. The in-loop restore of the
 * ASCII map sits at the end of the middle-column branch: there SDCC 2.2.1
 * hoists it out of the inner loop only (its definition does not reach the
 * inner preheader in the pre-loop dataflow), which is where the ROM has it.
 * At the end of the whole body it is hoisted out of both loops.
 */
void BuildOverwritePrompt(void)
{
    UBYTE x;
    UBYTE y;

    for (x = 3; (UWORD)x < 15; x++) {
        for (y = 4; (UWORD)y < 11; y++) {
            wStringSource = IdentityCharacterMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            ColorField(x, y, 1, 0);
            SetVramCursor(x, y);
            if (x == 3) {
                if (y == 4)
                    DrawStringIndexed(0);
                else if ((UWORD)y == 10)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if ((UWORD)x == 14) {
                if (y == 4)
                    DrawStringIndexed(1);
                else if ((UWORD)y == 10)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (y == 4)
                    DrawStringIndexed(4);
                else if ((UWORD)y == 10)
                    DrawStringIndexed(6);
                else {
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawStringIndexed(2);
                }
                wStringSource = AsciiCharacterMap - 0x20;
            }
        }
    }
    wStringSource = AsciiCharacterMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
}

void DrawOverwriteChoice(void)
{
    UBYTE x1, w2, x2, w1;

    if (wOverwriteEnd) {
        x2 = 0x0B;
        x1 = 4;
        w2 = 2;
        w1 = 3;
    } else {
        x2 = 4;
        x1 = 0x0B;
        w2 = 3;
        w1 = 2;
    }
    ColorField(x1, 9, w1, 0);
    ColorField(x2, 9, w2, hGameboyType ? 3 : 2);
}

/* Open the confirmation dialog; OverwritePromptInput handles the choice. */
void OverwriteSynthWaves(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    BuildOverwritePrompt();
    wOverwriteStart = 1;
    wOverwriteEnd = 1;
    wVramCursor = (UBYTE *)0x98A4;
    DrawString("OVERWRITE");
    wVramCursor = (UBYTE *)0x98C4;
    DrawString("HAND-DRAWN");
    wVramCursor = (UBYTE *)0x98E4;
    DrawString("WAVES?");
    wVramCursor = (UBYTE *)0x9924;
    DrawString("YES");
    wVramCursor = (UBYTE *)0x992B;
    DrawString("NO");
    DrawOverwriteChoice();
}

void RedrawScreenFar(void)
{
    FarCall(1, FAR_FN(RedrawScreen));
}

void OverwritePromptInput(void)
{
    if (wAPressEnabled) {
        switch (wButtons) {
        case 0x20:
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        case 2:
            wOverwriteEnd = 0;
            DrawOverwriteChoice();
            break;
        case 1:
            wOverwriteEnd = 1;
            DrawOverwriteChoice();
            break;
        case 0x10:
            if (!wOverwriteEnd)
                ClearSynthFlag(wSynthScreen);
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        }
    }
}

void WriteSynthParameter(UBYTE field, UBYTE v)
{
    UWORD p;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    WriteSongByte(1, p, v);
}

void AddSynthParameterClamped(UBYTE field, UBYTE maximum, UBYTE increment)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v += (UWORD)increment;
    if (v > maximum)
        v = maximum;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void SubSynthParameterClamped(UBYTE field, UBYTE decrement)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v -= decrement;
    if (v & 0x8000)
        v = 0;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void RaiseSynthParameterTo(UBYTE field, UBYTE maximum)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v != maximum) {
        v++;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

void LowerSynthParameterTo0(UBYTE field)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v) {
        v--;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

UBYTE RaiseSynthFieldHigh(void)
{
    UBYTE v, t;

    v = ReadSynthParameter(2);
    t = v & 0xF0;
    if (t != 0xF0) {
        v += 0x10;
        WriteSynthParameter(2, v);
    }
    return v >> 4;
}

UBYTE RaiseSynthFieldLow(void)
{
    UBYTE v, t;

    v = ReadSynthParameter(2);
    t = v & 0x0F;
    if (t != 0x0F) {
        v++;
        WriteSynthParameter(2, v);
    }
    return v & 0x0F;
}

UBYTE LowerSynthFieldHigh(void)
{
    UBYTE v;

    v = ReadSynthParameter(2);
    if (v & 0xF0) {
        v -= 0x10;
        WriteSynthParameter(2, v);
    }
    return v >> 4;
}

UBYTE LowerSynthFieldLow(void)
{
    UBYTE v;

    v = ReadSynthParameter(2);
    if (v & 0x0F) {
        v--;
        WriteSynthParameter(2, v);
    }
    return v & 0x0F;
}

/*
 * The synth screen's input. A on a field that would overwrite the synth's
 * waves asks first (OverwriteSynthWaves / the prompt's own input); A alone
 * shows the field's help after two presses; the directions walk the
 * two-column field layout (skipping the rows a framed signal hides);
 * A+right/left step the field under the cursor, A+up/down by the coarse
 * step; B+direction picks another synth; select+B / select+A copy and paste
 * its 16 parameter bytes; start and select+start stop playback. 1 when the
 * key was taken.
 */
UBYTE SynthScreenInput(void)
{
    UBYTE v;
    UWORD p;
    UWORD q;

    if ((wButtons & 0x10) && IsSynthLocked(wSynthScreen) && !wOverwriteStart) {
        if (wAPressEnabled)
            OverwriteSynthWaves();
        return 1;
    }
    if (wOverwriteStart) {
        OverwritePromptInput();
        return 1;
    }
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wButtons != 0x10)
        wDrawStep = 0;
    switch (wButtons) {
    case 0x10:
        if (wAPressEnabled && ++wDrawStep == 2)
            FarCall(5, ShowSynthFieldHelp);
        return 1;
    case 0x04:
        if (!wSynthCursor)
            break;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x0A:
            wSynthCursor = 3;
            break;
        case 0x07:
        case 0x0D:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        case 0x02:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        default:
            wSynthCursor--;
        }
        DrawSynthCursor();
        return 1;
    case 0x08:
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x03:
            wSynthCursor = wSynthNavigationSide ? 4 : 10;
            break;
        case 0x04:
        case 0x0A:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        case 0x00:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        default:
            wSynthCursor++;
        case 0x09:
        case 0x0F:
            break;
        }
        DrawSynthCursor();
        return 1;
    case 0x02:
        wSynthNavigationSide = 1;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F:
            wSynthCursor -= 6;
        }
        DrawSynthCursor();
        return 1;
    case 0x01:
        wSynthNavigationSide = 0;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
            wSynthCursor += 6;
        }
        DrawSynthCursor();
        return 1;
    case 0x11:
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 0:
            if (wButtonsSame)
                break;
            switch (ReadSynthParameter(0)) {
            case 0:
                WriteSynthParameter(0, 1);
                break;
            case 1:
                WriteSynthParameter(0, 2);
                break;
            default:
                WriteSynthParameter(0, 3);
                DrawSynthScreen();
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawSynthWaveField();
            }
            DrawSynthWaveField();
            break;
        case 1:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(1);
            if (v == 3)
                break;
            v++;
            WriteSynthParameter(1, v);
            DrawSynthFilterField();
            break;
        case 2:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(3);
            if (v >= 2)
                break;
            v++;
            WriteSynthParameter(3, v);
            DrawSynthDistortionField(v);
            break;
        case 8:
            v = ReadSynthParameter(0x0D) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParameter(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParameter(0x0E) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParameter(0x0E, v - 0x0F);
            break;
        case 3:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(4);
            if (v >= 3)
                break;
            v++;
            DrawSynthField(v);
            WriteSynthParameter(4, v);
            break;
        case 6:
            DrawByte(RaiseSynthFieldHigh());
            break;
        case 12:
            DrawByte(RaiseSynthFieldLow());
            break;
        case 4:
            RaiseSynthParameterTo(5, 0xFF);
            break;
        case 10:
            RaiseSynthParameterTo(9, 0xFF);
            break;
        case 5:
            RaiseSynthParameterTo(6, 0xFF);
            break;
        case 11:
            RaiseSynthParameterTo(0x0A, 0xFF);
            break;
        case 7:
            RaiseSynthParameterTo(8, 0xFF);
            break;
        case 13:
            RaiseSynthParameterTo(0x0C, 0xFF);
            break;
        case 9:
            RaiseSynthParameterTo(7, 0x1F);
            break;
        case 15:
            RaiseSynthParameterTo(0x0B, 0x1F);
            break;
        }
        LoadSynthParameters();
        return 1;
    case 0x12:
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 0:
            if (wButtonsSame)
                break;
            switch (ReadSynthParameter(0)) {
            default:
                WriteSynthParameter(0, 0);
                break;
            case 2:
                WriteSynthParameter(0, 1);
                break;
            case 3:
                WriteSynthParameter(0, 2);
                DrawSynthScreen();
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawSynthWaveField();
            }
            DrawSynthWaveField();
            break;
        case 1:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(1);
            if (!v)
                break;
            v--;
            WriteSynthParameter(1, v);
            DrawSynthFilterField();
            break;
        case 2:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(3);
            if (!v)
                break;
            v--;
            WriteSynthParameter(3, v);
            DrawSynthDistortionField(v);
            break;
        case 8:
            v = ReadSynthParameter(0x0D) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParameter(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParameter(0x0E) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParameter(0x0E, v - 0x0F);
            break;
        case 3:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(4);
            if (!v)
                break;
            v--;
            DrawSynthField(v);
            WriteSynthParameter(4, v);
            break;
        case 6:
            DrawByte(LowerSynthFieldHigh());
            break;
        case 12:
            DrawByte(LowerSynthFieldLow());
            break;
        case 4:
            LowerSynthParameterTo0(5);
            break;
        case 10:
            LowerSynthParameterTo0(9);
            break;
        case 5:
            LowerSynthParameterTo0(6);
            break;
        case 11:
            LowerSynthParameterTo0(0x0A);
            break;
        case 7:
            LowerSynthParameterTo0(8);
            break;
        case 13:
            LowerSynthParameterTo0(0x0C);
            break;
        case 9:
            LowerSynthParameterTo0(7);
            break;
        case 15:
            LowerSynthParameterTo0(0x0B);
            break;
        }
        LoadSynthParameters();
        return 1;
    case 0x14:
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 6:
            RaiseSynthFieldHigh();
            DrawByte(RaiseSynthFieldHigh());
            break;
        case 12:
            RaiseSynthFieldLow();
            DrawByte(RaiseSynthFieldLow());
            break;
        case 9:
            AddSynthParameterClamped(7, 0x1F, 8);
            break;
        case 15:
            AddSynthParameterClamped(0x0B, 0x1F, 8);
            break;
        case 4:
            AddSynthParameterClamped(5, 0xFF, 0x10);
            break;
        case 10:
            AddSynthParameterClamped(9, 0xFF, 0x10);
            break;
        case 8:
            v = ReadSynthParameter(0x0D) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParameter(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParameter(0x0E) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParameter(0x0E, v - 0x0F);
            break;
        case 5:
            AddSynthParameterClamped(6, 0xFF, 0x10);
            break;
        case 11:
            AddSynthParameterClamped(0x0A, 0xFF, 0x10);
            break;
        case 7:
            AddSynthParameterClamped(8, 0xFF, 0x10);
            break;
        case 13:
            AddSynthParameterClamped(0x0C, 0xFF, 0x10);
            break;
        }
        LoadSynthParameters();
        return 1;
    case 0x18:
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 6:
            LowerSynthFieldHigh();
            DrawByte(LowerSynthFieldHigh());
            break;
        case 12:
            LowerSynthFieldLow();
            DrawByte(LowerSynthFieldLow());
            break;
        case 9:
            SubSynthParameterClamped(7, 8);
            break;
        case 15:
            SubSynthParameterClamped(0x0B, 8);
            break;
        case 4:
            SubSynthParameterClamped(5, 0x10);
            break;
        case 10:
            SubSynthParameterClamped(9, 0x10);
            break;
        case 8:
            v = ReadSynthParameter(0x0D) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParameter(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParameter(0x0E) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParameter(0x0E, v - 0x0F);
            break;
        case 5:
            SubSynthParameterClamped(6, 0x10);
            break;
        case 11:
            SubSynthParameterClamped(0x0A, 0x10);
            break;
        case 7:
            SubSynthParameterClamped(8, 0x10);
            break;
        case 13:
            SubSynthParameterClamped(0x0C, 0x10);
            break;
        }
        LoadSynthParameters();
        return 1;
    case 0x21:
    case 0x24:
        wMessageStringPointer = 0;
        if (wSynthScreen != 0x0F) {
            wSynthScreen++;
            DrawSynthScreen();
        }
        return 1;
    case 0x22:
    case 0x28:
        wMessageStringPointer = 0;
        if (wSynthScreen) {
            wSynthScreen--;
            DrawSynthScreen();
        }
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case 0x48:
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        RedrawScreenFar();
        return 1;
    case 0x42:
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorLeft);
        RedrawScreenFar();
        return 1;
    case 0x41:
        if (wButtonsSame)
            return 0;
        if (wSynthCursor == 0 && SynthSignalIsFrame()) {
            wSynth = wSynthScreen + 0xF0;
        } else {
            wSynth &= 0x0F;
            wSynth += wSynthScreen << 4;
        }
        FarCall(5, MoveMapCursorRight);
        FarCall(7, RedrawScreen);
        return 1;
    case 0x60:
        ShowMessage("COPIED SYNTH");
        wCursorMode = 2;
        wClipboardType = 0x0A;
        p = ((UWORD)wSynthScreen << 4) + (UWORD)0xBEB2;
        for (v = 0; v < 0x10; v++) {
            WriteClipboardCell(0, v, ReadSongByte(1, p));
            p++;
        }
        wSelectionLeft = 0;
        wSelectionRight = 1;
        wSelectionTop = 0;
        wSelectionBottom = 0x10;
        FarCall(0x1C, FAR_FN(SendLinkClipboard));
        return 1;
    case 0x50:
        if (wCursorMode == 1 || wClipboardType != 0x0A)
            return 0;
        ShowMessage("PASTED SYNTH");
        q = ((UWORD)wSynthScreen << 4) + (UWORD)0xBEB2;
        for (v = 0; v < 0x10; v++) {
            WriteSongByte(1, q, ReadClipboardCell(0, v));
            q++;
        }
        LoadSynthParameters();
        DrawSynthNumber();
        return 1;
    }
    return 0;
}
