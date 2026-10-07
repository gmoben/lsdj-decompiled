/* Bank $02: the sound engine's C (the sequencer core around it is
 * hand-written asm).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundCh;         /* $FF8B */
extern volatile sfr UBYTE hFxValue;         /* $FF8F */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wPlayChanFirst;                /* $C8DD */
extern UBYTE wPlayChanLast;                 /* $C8DE */
extern UBYTE wTableCmd1Val;                 /* $C36F */
extern UBYTE wTableCmd2Val;                 /* $C370 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChanVibShape[];               /* $C35F, [4] */
extern UBYTE wNoiPSlideStart;                 /* $C8C5 */
/* the working pointers' low bytes: the per-channel arrays they point into
   sit in one page, so moving to the next channel is a byte increment */
extern UBYTE wPtrChainRow, wPtrPhraseRow, wPtrNoteCountdown, wPtrGroove;  /* $C8DF.. */
extern UBYTE wPtrGrooveRow, wPtrPhrase, wPtrNoteSrc;                       /* ..$C8ED */

void SndWritePanning(UBYTE v);              /* bank 0 */
void SetInterruptEnable(UBYTE ie);          /* bank 0 */
UBYTE Random16(void);                       /* bank 0 (this file reads the low byte) */
void SetSyncSendRate(void);                 /* bank 7 */
void SndCheckChanMode(void);
void SndCheckRetrigState(UBYTE ch);
void SndTableHop(UBYTE v);           /* asm */
void ApplyPitchBend(void);                  /* asm */
UBYTE SndAnyChannelActive(void);
void SndStartChannels(void);
void StopPlayback(void);

/* $507F: step the working pointers to the next channel */

void SndAdvanceStatePtrs(void)
{
    wPtrPhraseRow++;
    wPtrChainRow++;
    wPtrNoteCountdown++;
    wPtrGroove++;
    wPtrGrooveRow++;
    wPtrPhrase++;
    wPtrNoteSrc++;
}

/* $6479: a random number 0..n (the Z command's digits) */

UBYTE SndRandomUpTo(UBYTE n)
{
    if (!n)
        return 0;
    return Random16() % (n + 1);
}

/* $6646: P, the pitch bend; on the noise channel P applies S every tick */

void FxCmd_P(void)
{
    if (hSoundCh == 3) {
        wChanVibShape[3] = hFxValue;
        wNoiPSlideStart = 1;
    } else
        ApplyPitchBend();
}

/* $665F: O, the output (panning) */

void FxCmd_O(void)
{
    SndWritePanning(hFxValue);
}

/* $6669: T, the tempo */

void FxCmd_T(void)
{
    wTempo = hFxValue;
    wTempoMode = 0;
    FarCall(7, FAR_FN(SetSyncSendRate));
}

const UBYTE EnvReleaseSpeeds[8] = {0x00, 0x06, 0x0B, 0x11, 0x16, 0x1C, 0x22, 0x27};

/* $69C3: E's release digit (the value's low three bits) -> the fade speed */

UBYTE EnvReleaseSpeed(void)
{
    return EnvReleaseSpeeds[hFxValue & 7];
}

/* $6B9B: advance the retrigger of every playing channel */

void SndUpdateRetrig(void)
{
    UBYTE ch;

    SndCheckChanMode();
    for (ch = wPlayChanFirst; ch <= wPlayChanLast; ch++)
        SndCheckRetrigState(ch);
}

/* $6E0F */

void SndStopAllIfIdle(void)
{
    if (SndAnyChannelActive())
        SndUpdateRetrig();
    else
        SndStartChannels();
}

/* $70FB: enable the LCD-STAT interrupt (and the serial one if syncing) */

void SndEnableLcdInts(void)
{
    SetInterruptEnable(IE_REG | 0x04 | wSyncSerialIeMask);
}

/* $6F65: chain_phrases[chain][row] (SRAM bank 1 $A080) */

UBYTE SndGetChainPhrase(UBYTE chain, UBYTE row)
{
    UBYTE *p;

    if (chain & 0x80)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = ((UWORD)chain << 4) + SramChainPhrases;
    return ReadSongByte(1, (UWORD)(p + row));
}

/* $710E: play the current screen's scope from scratch */

void SndInitPlayState(void)
{
    wPlayMode = wCurrentScreen;
    wLiveMode = 0;
    StopPlayback();
}

/* $7362: B in a table, MayBe hop: with the probability in the high digit of
   the command value (of the right column if `right`), hop to the row in the
   low digit */

UBYTE SndTableMaybeHop(UBYTE right)
{
    UBYTE v;

    v = right ? wTableCmd2Val : wTableCmd1Val;
    if ((v & 0xF0) > Random16()) {
        v &= 0x0F;
        if (right)
            v |= 0x80;
        SndTableHop(v);
        return 1;
    }
    return 0;
}
