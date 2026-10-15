/*
 * The synths' reset -- LSDj 9.4.0, ROM bank $1F.
 *
 * Which synths' wave frames differ from the default, and the frames and
 * parameters reset.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE DefaultWaveFrameB1F[];         /* $1F:$491A, [16] */
extern UBYTE DefaultSynthParameters[];          /* $1F:$61FF, [16] */
UBYTE SynthFramesModified(UBYTE chain);
void ResetWaveFrame(UBYTE p);
UBYTE CountModifiedSynths(void);
void ResetUnusedSynths(void);
UBYTE WaveFrameModified(UBYTE phrase);

UBYTE WaveFrameModified(UBYTE w)
{
    UBYTE *p;
    UBYTE byte;

    p = SramWaveFrames + ((UWORD)w << 4);
    for (byte = 0; byte < 0x10; byte++) {
        if (ReadSongByte(3, p) != DefaultWaveFrameB1F[byte])
            return 1;
        p = p + 1;
    }
    return 0;
}

UBYTE SynthFramesModified(UBYTE chain)
{
    UBYTE p, frame;

    p = chain << 4;
    for (frame = 0; frame != 16; frame++) {
        if (WaveFrameModified(p))
            return 1;
        p++;
    }
    return 0;
}

UBYTE CountModifiedSynths(void)
{
    UBYTE n, synth;

    n = 0;
    for (synth = 0; synth < 0x10; synth++)
        if (SynthFramesModified(synth))
            n++;
    return n;
}

void ResetWaveFrame(UBYTE p)
{
    UBYTE *d;

    d = SramWaveFrames + ((UWORD)p << 4);
    rRAMB = 3;
    _asm di _endasm;
    rRAMG = 0x0A;
    memcpy(d, DefaultWaveFrameB1F, 16);
    rRAMG = 0;
    _asm ei _endasm;
}

void ResetSynthWaves(UBYTE chain)
{
    UBYTE p, frame;

    p = chain << 4;
    for (frame = 0; frame < 16; frame++) {
        ResetWaveFrame(p);
        p++;
    }
}

void ResetSynthParameters(UBYTE s)
{
    UBYTE *d;

    d = SramSynthParams + ((UWORD)s << 4);
    rRAMB = 1;
    _asm di _endasm;
    rRAMG = 0x0A;
    memcpy(d, DefaultSynthParameters, 16);
    rRAMG = 0;
    _asm ei _endasm;
}

void ResetUnusedSynths(void)
{
    UBYTE synth;
    UBYTE used[16];
    UBYTE instrument;

    synth = 16;
    do {
        used[synth - 1] = 0;
        synth--;
    } while (synth);
    for (instrument = 0; instrument < 0x40; instrument++)
        if ((ReadInstrumentByte(instrument, 0) & 3) == 1)
            used[ReadInstrumentByte(instrument, 3) >> 4] = 1;
    for (synth = 0; synth < 0x10; synth++)
        if (!used[synth]) {
            ResetSynthParameters(synth);
            ResetSynthWaves(synth);
        }
}
