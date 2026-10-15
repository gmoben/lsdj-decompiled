/*
 * The synth's wave renderer -- LSDj 9.4.0, ROM bank $1D.
 *
 * A synth's 16 wave frames rendered from its parameters: the curves' fill,
 * the signal shaped by the filter, the volume, VSHIFT and LIMIT
 * composition, the PHASE stage, each frame written back.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSynthPhaseType;              /* $C9EB */
extern UBYTE wSynthPhase;            /* $C9EC */
extern UBYTE wSynthWaveBuffer[];               /* $CA34, [32] */
extern UBYTE wSynthBufferA[];                  /* $CAB8, [64]: phase accumulator, sample * weight per slot */
extern UBYTE wSynthBufferB[];                  /* $CAF8, [64]: phase accumulator, weight per slot */
extern UBYTE *wSynthPhasePointer;              /* $CB38 */
extern UWORD wSynthPhasePointerW;              /* $CB38, the same word (aliased by match_function.py) */
extern UWORD wSynthPhaseValue;               /* $CB3A */
extern UBYTE wSynthWaveScratch[];               /* $CB40, [32] */
extern UBYTE wSynthScreen;                  /* $C441, the synth being edited */
void ResyncSynthWave(void);
void Resync2SynthWave(void);
extern UBYTE wSynthRenderIndex;               /* $C9F0 */
extern UBYTE wSynthCutoff;                 /* $C9E8 */
extern UBYTE wSynthVerticalShift;                 /* $C9EA */
extern UBYTE wSynthVolume;                 /* $C9EF */
extern UBYTE wSynthSignalType;              /* $C9ED */
extern UBYTE wSynthCurveVerticalShift[];             /* $CA54, [16] */
extern UBYTE wSynthCurvePhase[];           /* $CA64, [16] */
extern UBYTE wSynthCurveCutoff[];             /* $CA74, [16] */
extern UBYTE wSynthCurveVolume[];             /* $CA84, [16] */
extern UBYTE wSynthCurveResonance[];              /* $CA94, [16] */
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
void ApplySynthPhase(void);
void LoadSynthSignalWave(void);
void RenderSynthWaveType(void);
void ComposeSynthWave(void);
void WriteSynthWaveColumn(void);
void RenderSynthWaveA(UBYTE amplitude);
void RenderSynthWaveB(UBYTE amplitude);
extern UBYTE wSynthFilterType;               /* $C9E9 */
extern UBYTE wSynthDistortionType;             /* $C9EE, distortion: CLIP/FOLD/WRAP */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE *wSynthRenderDestination;              /* $C9F2 */
extern UWORD *wSynthResyncDestination;                /* $CB3C, RESYNC phase: output pointer / the slot value */
extern UWORD wSynthResyncDestinationW;                /* $CB3C, the same word (aliased by match_function.py) */
extern UBYTE *wSynthResyncSlot;               /* $CB3E, RESYNC phase: the slot pointer / the BufB value */
extern UWORD wSynthResyncSlotW;               /* $CB3E, the same word (aliased by match_function.py) */
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
    UBYTE position;
    BYTE *table;
    UWORD sample;

    volume = wSynthCutoff >> 4;
    if (wSynthSignalType == SYNTH_SIGNAL_TRIANGLE)
        table = &SynthShapeTableB[0];
    else
        table = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        volume &= 0x0E;
        if (wSynthSignalType == SYNTH_SIGNAL_TRIANGLE) {
            volume >>= 1;
            if (volume > 4)
                volume = 4;
        }
    }
    for (position = 0; position < volume; position++)
        table += 0x20;
    if (wSynthSignalType) {
        volume = wSynthCutoff & 0x1F;
        volume >>= 1;
    } else {
        volume = wSynthCutoff & 0x0F;
    }
    if (!volume)
        return;
    volume = volume * amplitude;
    for (position = 0; position < 0x20; position++) {
        sample = (WORD)*table;
        sample = sample * volume;
        if ((WORD)sample < 0) {
            sample >>= 4;
            sample |= 0xF000;
        } else {
            sample >>= 4;
        }
        *(UWORD *)(wSynthShapeBuffer + (position << 1)) += sample;
        table++;
    }
}

/* The complementary weight comes from the preceding cycle. There is no
 * preceding cycle at segment index zero, so that contribution is skipped.
 */
void RenderSynthWaveB(UBYTE amplitude)
{
    UBYTE position;
    UBYTE volume;
    BYTE *table;
    UWORD sample;

    if (wSynthSignalType == SYNTH_SIGNAL_TRIANGLE)
        table = &SynthShapeTableB[0];
    else
        table = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        volume = wSynthCutoff >> 5;
        if (!volume)
            return;
        volume--;
        volume <<= 1;
        if (wSynthSignalType == SYNTH_SIGNAL_TRIANGLE) {
            volume >>= 1;
            if (volume > 4)
                volume = 4;
        }
    } else {
        volume = wSynthCutoff >> 4;
        if (!volume)
            return;
        volume--;
    }
    for (position = 0; position < volume; position++)
        table += 0x20;
    if (wSynthSignalType) {
        volume = wSynthCutoff & 0x1F;
        volume >>= 1;
    } else {
        volume = wSynthCutoff & 0x0F;
    }
    volume ^= 0x0F;
    if (!volume)
        return;
    volume = volume * amplitude;
    for (position = 0; position < 0x20; position++) {
        sample = (WORD)*table;
        sample = sample * volume;
        if ((WORD)sample < 0) {
            sample >>= 4;
            sample |= 0xF000;
        } else {
            sample >>= 4;
        }
        *(UWORD *)(wSynthShapeBuffer + (position << 1)) += sample;
        table++;
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
    UBYTE position;
    WORD span;
    WORD low;
    WORD high;
    WORD v;

    destination = &wSynthWaveBuffer[0];
    span = 0x10 - (UWORD)wSynthCurveLimit[wSynthRenderIndex];
    low = (span >> 1) << 4;
    high = 0x100 - (((span + 1) >> 1) << 4);
    for (position = 0; position < 0x20; position++) {
        v = *(WORD *)(wSynthShapeBuffer + (position << 1));
        v = v * wSynthVolume;
        v >>= 4;
        if (v & 0x0800)
            v |= 0xF000;
        v += 0x80;
        v += wSynthVerticalShift;
        switch (wSynthDistortionType) {
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
    UBYTE position;
    UWORD p;

    amplitude = wSynthCurveResonance[wSynthRenderIndex];
    switch (wSynthFilterType) {
    case SYNTH_FILTER_LOWPASS:
        switch (wSynthSignalType) {
        default:
            p = wSynthCutoff & 0xF0;
            p = (UWORD)(SynthSeedTableA + (p << 1));
            break;
        case SYNTH_SIGNAL_SAW:
            p = wSynthCutoff & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableB + (p << 1));
            break;
        case SYNTH_SIGNAL_TRIANGLE:
            p = wSynthCutoff & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableE + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        RenderSynthWaveA(amplitude + 1);
        if (amplitude)
            RenderSynthWaveB(amplitude);
        break;
    case SYNTH_FILTER_HIGHPASS:
        switch (wSynthSignalType) {
        case SYNTH_SIGNAL_SQUARE:
            p = wSynthCutoff & 0xF0;
            p = (UWORD)(SynthSeedTableC + (p << 1));
            break;
        case SYNTH_SIGNAL_SAW:
            p = wSynthCutoff & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableD + (p << 1));
            break;
        case SYNTH_SIGNAL_TRIANGLE:
            p = wSynthCutoff & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableF + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        RenderSynthWaveB(amplitude + 1);
        if (amplitude)
            RenderSynthWaveA(amplitude);
        break;
    case SYNTH_FILTER_BANDPASS:
        position = 0x20;
        do {
            *(UWORD *)(wSynthShapeBuffer + ((position - 1) << 1)) = 0;
        } while (--position);
        RenderSynthWaveA(amplitude ? amplitude : 1);
        RenderSynthWaveB(amplitude ? amplitude : 1);
        break;
    case SYNTH_FILTER_ALLPASS:
        switch (wSynthSignalType) {
        case SYNTH_SIGNAL_SQUARE:
            p = (UWORD)(SynthSeedTableA + 0xF0);
            break;
        case SYNTH_SIGNAL_SAW:
            p = (UWORD)(SynthSeedTableB + 0x70);
            break;
        case SYNTH_SIGNAL_TRIANGLE:
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

/* $58E6: the PINCH and WARP phase (types 0 and 1; RESYNC and RESYN2 have
   their own). Each of the 32 samples is scattered over two accumulator
   slots at an 8.8 position that advances by a phase-driven step (the
   slower the step, the more samples a slot averages: the wave is squeezed
   horizontally); WARP reverses direction after 15 samples. A second pass
   divides each slot's sum by its weight; PINCH fills a slot's missing
   weight with the mid level 8.
   The slot pointer and value live in two scratch words ($CB38/$CB3A); the
   second half reuses the value's low byte for the doubled index and the
   pointer word for the value -- the index is a block-scope extern, which
   this compiler keeps in a register and spills into the global. */
void PinchOrWarpSynthWave(void)
{
    UWORD accumulator;
    UBYTE step;
    UBYTE output;
    UBYTE position;
    UWORD fraction;
    UWORD sample;
    UWORD ua;
    UWORD ub;
    UBYTE next2;
    extern UBYTE wSynthPhaseIndex;           /* $CB3A, wSynthPhaseValue's low byte (aliased by match_function.py) */

    accumulator = 0;
    step = -(wSynthPhase + (wSynthPhase >> 5));
    FillMemory(wSynthBufferA, 0, 0x40);
    FillMemory(wSynthBufferB, 0, 0x40);
    for (output = 0; output < 0x20; output++) {
        position = accumulator >> 8;
        fraction = (UBYTE)accumulator;
        sample = wSynthWaveBuffer[WrapSynthIndex32(output)];
        next2 = ((position + 1) & 0x1F) << 1;
        wSynthPhasePointer = wSynthBufferA + next2;
        wSynthPhaseValue = *(UWORD *)wSynthPhasePointer;
        *(UWORD *)wSynthPhasePointer = wSynthPhaseValue + sample * fraction;
        wSynthPhaseValue = *(UWORD *)(wSynthBufferB + next2);
        wSynthPhaseValue += fraction;
        *(UWORD *)(wSynthBufferB + next2) = wSynthPhaseValue;
        fraction = 0x100 - fraction;
        wSynthPhaseIndex = position << 1;
        wSynthPhasePointerW = *(UWORD *)(wSynthBufferA + wSynthPhaseIndex);
        *(UWORD *)(wSynthBufferA + wSynthPhaseIndex) = wSynthPhasePointerW + sample * fraction;
        *(UWORD *)(wSynthBufferB + wSynthPhaseIndex) += fraction;
        if (wSynthPhaseType == SYNTH_PHASE_PINCH || output < 0x0F)
            accumulator += step;
        else
            accumulator += 0x200 - step;
    }
    for (output = 0; output < 0x20; output++) {
        ua = *(UWORD *)(wSynthBufferA + (output << 1));
        ub = *(UWORD *)(wSynthBufferB + (output << 1));
        if (wSynthPhaseType == SYNTH_PHASE_PINCH) {
            if ((ub - 0x100) & 0x8000) {
                ua += (0x100 - ub) << 3;
                ub = 0x100;
            }
        }
        wSynthWaveBuffer[WrapSynthIndex32(output)] = ua / ub;
    }
}

/* The RESYNC phase: halve the wave an octave per phase doubling over a
 * shortened n (the two copies through a volatile k), then the PINCH/WARP
 * scatter core in a `for (;;)` whose head computes pos/frac/sample and, at
 * pos == 0x20, runs the normalise loop and returns.
 *
 * lsdj-alloc(ResyncSynthWave): BUSYC=82-82
 * -- register c held when `next2 = i << 1` is allocated, so next2 takes b
 * and the BufB word comes out as the pair bc (docs/toolchain.md, "The
 * phantom register"). */
void ResyncSynthWave(void)
{
    UWORD accumulator;
    UBYTE step;
    UBYTE output;
    UBYTE n;
    volatile UBYTE k;
    UBYTE position;
    UWORD fraction;
    UWORD sample;
    UBYTE next2;
    extern UBYTE wSynthResyncIndex;             /* $CB3E */

    accumulator = 0;
    step = -wSynthPhase;
    output = 0;
    n = 0x20;
    FillMemory(wSynthBufferA, 0, 0x40);
    FillMemory(wSynthBufferB, 0, 0x40);
    while (step < 0x80) {
        k = 0;
        step <<= 1;
        n >>= 1;
        for (output = 0; output < n; output++) {
            wSynthResyncDestination = (UWORD *)(wSynthWaveBuffer + output);
            *(UBYTE *)wSynthResyncDestination = (wSynthWaveBuffer[k++] + wSynthWaveBuffer[k++]) >> 1;
        }
    }
    n--;
    for (;;) {
        position = accumulator >> 8;
        fraction = (UBYTE)accumulator;
        sample = wSynthWaveBuffer[WrapSynthIndex32(output)];
        if (position == 0x20) {
            for (output = 0; output < 0x20; output++) {
                wSynthResyncDestination = (UWORD *)(wSynthWaveBuffer + WrapSynthIndex32(output));
                next2 = output << 1;
                wSynthResyncSlotW = *(UWORD *)(wSynthBufferA + next2);
                *(UBYTE *)wSynthResyncDestination = wSynthResyncSlotW / *(UWORD *)(wSynthBufferB + next2);
            }
            return;
        }
        next2 = ((position + 1) & 0x1F) << 1;
        wSynthResyncSlot = wSynthBufferA + next2;
        wSynthResyncDestinationW = *(UWORD *)wSynthResyncSlot;
        *(UWORD *)wSynthResyncSlot = wSynthResyncDestinationW + sample * fraction;
        wSynthResyncSlotW = *(UWORD *)(wSynthBufferB + next2);
        wSynthResyncSlotW += fraction;
        *(UWORD *)(wSynthBufferB + next2) = wSynthResyncSlotW;
        fraction = 0x100 - fraction;
        wSynthResyncIndex = position << 1;
        wSynthResyncDestinationW = *(UWORD *)(wSynthBufferA + wSynthResyncIndex);
        *(UWORD *)(wSynthBufferA + wSynthResyncIndex) = wSynthResyncDestinationW + sample * fraction;
        *(UWORD *)(wSynthBufferB + wSynthResyncIndex) += fraction;
        accumulator += step;
        output++;
        output &= n;
    }
}

void Resync2SynthWave(void)
{
    UBYTE position;
    UBYTE n;
    UBYTE k;

    n = 0x20 - (wSynthPhase >> 3);
    for (position = 0; position < 0x20; position++) {
        k = (((UWORD)position << 5) + 0x10) / n % 0x20;
        wSynthWaveScratch[WrapSynthIndex32(position)] = wSynthWaveBuffer[WrapSynthIndex32(k)];
    }
    memcpy(wSynthWaveBuffer, wSynthWaveScratch, 0x20);
}

void ApplySynthPhase(void)
{
    if (wSynthPhase) {
        switch (wSynthPhaseType) {
        case SYNTH_PHASE_RESYNC:
            ResyncSynthWave();
            break;
        case SYNTH_PHASE_RESYNC2:
            Resync2SynthWave();
            break;
        default:
            PinchOrWarpSynthWave();
        }
    }
}

/* lsdj-alloc(WriteSynthWaveColumn): BUSYC=1-17
 * -- register c held from the first instruction to the loop body's last
 * byte temporary, so i takes b and is evicted by the body, and the pointer
 * pair comes out as bc (docs/toolchain.md, "The phantom register"). */
void WriteSynthWaveColumn(void)
{
    UBYTE position;

    for (position = 0; position < 0x20; position += 2) {
        wWaveEditX = wSynthWaveBuffer[position] << 4;
        wWaveEditX = (wSynthWaveBuffer[position + 1] + wWaveEditX) ^ 0xFF;
        WriteSongByte(3, (UWORD)wSynthRenderDestination++, wWaveEditX);
    }
}

/* $5F90: load the custom-wave SIGNAL (W.FX: wave $F0 + synth, 16 packed
   bytes at SRAM $AF00 + 16 * synth) into wSynthShapeBuffer as 32 signed words,
   each nibble n becoming 120 - 16 * n. */
void LoadSynthSignalWave(void)
{
    UBYTE position;
    UWORD p;
    char v;
    int w;

    position = 0;
    p = wSynthScreen;
    p <<= 4;
    p += (UWORD)0xAF00;
    while (position != 0x20) {
        v = 0xFF ^ ReadSongByte(3, p);
        w = (char)((v & 0xF0) + 0x88);
        *(int *)(wSynthShapeBuffer + (position++ << 1)) = w;
        w = (char)(((v & 0x0F) << 4) + 0x88);
        *(int *)(wSynthShapeBuffer + (position++ << 1)) = w;
        p++;
    }
}

void RenderSynthWave(void)
{
    if (wSynthRenderIndex >= 0x10)
        DebugTrap();
    wSynthVolume = wSynthCurveVolume[wSynthRenderIndex];
    wSynthCutoff = wSynthCurveCutoff[wSynthRenderIndex];
    wSynthVerticalShift = wSynthCurveVerticalShift[wSynthRenderIndex];
    wSynthPhase = wSynthCurvePhase[wSynthRenderIndex];
    if (wSynthSignalType == SYNTH_SIGNAL_WAVE_FRAMES)
        LoadSynthSignalWave();
    else
        RenderSynthWaveType();
    ComposeSynthWave();
    ApplySynthPhase();
    WriteSynthWaveColumn();
}

void FillSynthWaveRange(UBYTE a, UBYTE b, UBYTE *buffer)
{
    UBYTE step;

    if (b >= a) {
        b -= a;
        if (b < 0xFF)
            b++;
        for (step = 0; step < 16; step++)
            *buffer++ = ((UWORD)step * b >> 4) + a;
    } else {
        a -= b;
        if (a < 0xFF)
            a++;
        for (step = 15; step != 0xFF; step--)
            *buffer++ = b + ((UWORD)step * a >> 4);
    }
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
    p = (UWORD)SramSynthParameters + o;
    wSynthSignalType = ReadSongByte(1, p++);
    wSynthFilterType = ReadSongByte(1, p++);
    v = ReadSongByte(1, p++);
    FillSynthWaveRange(v >> 4, v & 0x0F, wSynthCurveResonance);
    wSynthDistortionType = ReadSongByte(1, p++);
    wSynthPhaseType = ReadSongByte(1, p++);
    wSynthVolume = ReadSongByte(1, p++);
    wSynthCutoff = ReadSongByte(1, p++);
    wSynthPhase = ReadSongByte(1, p++) << 3;
    wSynthVerticalShift = ReadSongByte(1, p++);
    FillSynthWaveRange(wSynthVolume, ReadSongByte(1, p++), wSynthCurveVolume);
    FillSynthWaveRange(wSynthCutoff, ReadSongByte(1, p++), wSynthCurveCutoff);
    FillSynthWaveRange(wSynthPhase, ReadSongByte(1, p++) << 3, wSynthCurvePhase);
    FillSynthWaveRange(wSynthVerticalShift, ReadSongByte(1, p++), wSynthCurveVerticalShift);
    x = ReadSongByte(1, p++);
    y = ReadSongByte(1, p);
    FillSynthWaveRange(x + 0x0F, y + 0x0F, wSynthCurveLimit);
    wSynthRenderDestination = (UBYTE *)((s << 8) + (UWORD)0xA000);
    wSynthRenderIndex = 0;
    wSynthRenderActive = 1;
}
