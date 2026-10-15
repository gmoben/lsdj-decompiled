/* Bank $02: the sound engine's C (the sequencer core around it is
 * hand-written asm).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern volatile sfr UBYTE hCommandValue;         /* $FF8F */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE DIV_REG;          /* $FF04 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE hCommandValueMemory;                   /* hCommandValue read as a plain global (ld hl; or a,(hl)) */
extern volatile sfr BYTE hCommandValueS;         /* hCommandValue as a signed byte (P and F's steps) */
extern UBYTE wCommandChannel;                    /* $C527: the FX slot (channel + 4 x column) */
extern UBYTE wCommandSlotCommandTimer[];             /* $C230, [12] */
extern UBYTE wChannelCommandSlots[];                /* $C2F0, [12] */
extern UBYTE wWavLevel;                     /* $C8F3 */
extern BYTE wWavLevelS;                     /* wWavLevel as a signed byte */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelStopQueued[];                /* $C304, [4] */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wSyncTransmitByte;                   /* $CBDC */
extern UBYTE wSpeechWord;                   /* $C4CE */
extern UBYTE wSpeechFrameTicks;             /* $C4CF */
extern UBYTE wWavPlayMode;                  /* $C500 */
extern UBYTE wKeyboardNoteInjected;              /* $C8F9 */
extern UBYTE wKeyboardNoteQueued;                /* $CB92 */
extern UWORD wChannelPitchRate[];              /* $C2D0, [4] */
extern UBYTE wChannelTable[];                  /* $C204, [4] */
extern UBYTE wChannelDisplayNote[];               /* $C41F, [4] */
extern UBYTE wChannelChord[];                  /* $C218, [4] */
extern UBYTE wChannelLiveQueued[];             /* $C300, [4] */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSavedScreen;                  /* $C8FE */
extern UBYTE wChannelRowQueue[];               /* $C100, [4] */
extern UBYTE wChain;                        /* $C418 */
extern char wChainRow;                      /* $C41A: a char here (copied through a register on use) */
extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncStartRow;                 /* $CB6A */
extern UBYTE wPlayStartChannel;                /* $C94C */
extern UBYTE wPlayStartRow;                 /* $C94D */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wChannelChain[];                  /* $C170, [4] */
extern UBYTE wChannelChainRow[];               /* $C17C, [4] */
extern UBYTE wChannelPhrase[];                 /* $C168, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChannelPhraseRow[];              /* $C16C, [4] */
extern UBYTE wChannelNoteCountdown[];          /* $C160, [8]: the phrase's 4, the table's 4 */
extern UBYTE wChannelGroove[];                 /* $C150, [8] */
extern UBYTE wChannelGrooveRow[];              /* $C158, [8] */
extern UBYTE wChannelTableRow1[];              /* $C208, [8] */
extern UBYTE wChannelHopCount[];               /* $C214, [4] */
extern UBYTE wChannelSlideNoteSource[];           /* $C330, [4] */
extern UWORD wChannelPitchCurrent[];               /* $C337, [4] */
extern UBYTE wChannelCommandStep[];                /* $C34E, [4] */
extern UBYTE wChannelTableActive[];            /* $C2E8, [4] */
extern UBYTE wChannelTableLatch[];             /* $C220, [4] */
extern UBYTE wChannelTableFresh[];             /* $C2EC, [4] */
extern UBYTE wChannelSweep[];                  /* $C2E4, [4]: [3] is the noise note */
extern UBYTE wNoiseNoteLatch;               /* $C4FD */
extern UBYTE wKitVoicesActive;              /* $C8F6 */
extern UBYTE wSoundUpdateFlags;               /* $C8F5 */
extern UBYTE wEnvelopeNode1Pu1, wEnvelopeNode1Pu2, wEnvelopeNode1Noi;    /* $CBBC, $CBC3, $CBCA */
extern UBYTE wNoiPitchFraction;                 /* $C8DB */
extern UBYTE wNoiVibratoPreviousOffset;                /* $C8DA */
extern UBYTE wChannelVibratoSpeed[];               /* $C314, [4] words, read here by byte */
extern UWORD wChannelFrequency[];                   /* $C0F4, [4] */
extern UBYTE wKit1OffsetHi, wKit1OffsetLo;  /* $C4EA, $C4EB */
extern UBYTE wKit2OffsetHi, wKit2OffsetLo;  /* $C4EC, $C4ED */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wPlayChannelFirst;                /* $C8DD */
extern UBYTE wPlayChannelLast;                 /* $C8DE */
extern UBYTE wTableCommand1Value;                 /* $C36F */
extern UBYTE wTableCommand2Value;                 /* $C370 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChannelVibratoShape[];               /* $C35F, [4] */
extern UBYTE wNoiPSlideStart;                 /* $C8C5 */
extern UBYTE wChannelCommandRate[];                /* $C8C6, [4] */
extern UBYTE wChannelRetrigTimer[];            /* $C148, [4] */
extern UBYTE wChannelRetrigRate[];             /* $C14C, [4] */
extern UBYTE wCommandSlotRetrigRate[];           /* $C8CE, [12] */
extern UBYTE wChannelEnvelopeVolumeOffset[];           /* $C224, [12]: R's volume digit, by FX slot */
extern UBYTE wCommandSlotEnvelopeTimer[];             /* $C23C, [12] */
extern UBYTE wChannelCommand[];                    /* $C365, [6]: 4 phrase channels, the table's CMD1 and CMD2 */
extern UBYTE wChannelCommandValue[];                 /* $C36B, [6] */
extern UBYTE wLastCommand[];                    /* $C371, [68] */
extern UBYTE wLastCommandValue[];                 /* $C3B5, [68] */
extern UBYTE *wSpeechPointer;                   /* $C4D0: the word's allophone stream (SRAM bank 0) */
extern UBYTE wKit2Bank;                     /* $C4F5 */
extern UBYTE wKit2LoopEnabled;              /* $C4E5 */
extern UBYTE wChannelNote[];                   /* $C0CC, [4] */
extern UBYTE *wWavFrameDeltaPointer;            /* $C521 */
extern UBYTE *wWavFrameDeltaLoop;           /* $C523 */
extern UBYTE wWavFrameTimer;                /* $C525 */
extern UBYTE wWavSynthFrame;                /* $C694 */
extern UBYTE wWavUploadPending;              /* $C69C */
extern UBYTE wCommandWaveParameter2;                 /* $C8C3 */
extern UBYTE wChannelFrequencyHi[];                 /* $C2E0, [4] */
extern UBYTE wChannelEnvelope[];                    /* $C2D8, [4] */
extern UBYTE wChannelRestart[];                /* $C0E4, [4] */
extern UBYTE wChannelChordPhase[];             /* $C21C, [4] */
extern UBYTE wNoiChordPreviousOffset;              /* $C8DC */
extern UBYTE wEnvelopeStagePu1, wEnvelopeSpeedPu1, wEnvelopeCounterPu1, wEnvelopeTargetPu1;    /* $CBBF.. */
extern UBYTE wEnvelopeStagePu2, wEnvelopeSpeedPu2, wEnvelopeCounterPu2, wEnvelopeTargetPu2;    /* $CBC6.. */
extern UBYTE wEnvelopeStageNoi, wEnvelopeSpeedNoi, wEnvelopeCounterNoi, wEnvelopeTargetNoi;    /* $CBCD.. */
/* the working pointers' low bytes: the per-channel arrays they point into
   sit in one page, so moving to the next channel is a byte increment */
extern UBYTE wPointerChainRow, wPointerPhraseRow, wPointerNoteCountdown, wPointerGroove;  /* $C8DF.. */
extern UBYTE wPointerGrooveRow, wPointerPhrase, wPointerNoteSource;                       /* ..$C8ED */

void SoundWritePanning(UBYTE v);              /* bank 0 */
void SetInterruptEnable(UBYTE ie);          /* bank 0 */
UBYTE randw(void);                       /* bank 0 (this file reads the low byte) */
UBYTE MulU8(UBYTE a, UBYTE b);              /* bank 0 */
void SetSyncSendRate(void);                 /* bank 7 */
void SoundSetPlayRange(void);
void SoundLiveQueueStart(UBYTE chan);
UBYTE SoundGetChainPhrase(UBYTE chain, UBYTE row);
UBYTE SoundRandomUpTo(UBYTE n);
void SoundTableHop(UBYTE v);           /* asm */
void SoundApplyPitchBend(void);                  /* asm */
UBYTE SoundAnyChannelActive(void);
void SoundStartChannels(void);
void SoundTogglePlayback(void);
void SoundEnableLcdInterrupts(void);
void SoundLiveStopRange(void);
void SoundStartScreenPlayback(void);
void SyncStopTransport(void);               /* bank 0 */
void SyncSetSendMode(void);                 /* bank 0 */
void SoundStartSongPlayback(void);
void SoundKillNote(void);
void SoundResetChannelCommand(void);                  /* asm */
void SoundStartChannelFromRow(void);
void SoundSetChannelEnable(void);
void SoundResetCommandState(void);
void SyncStartTransport(void);              /* bank 0 */
void initrand(UWORD seed);              /* bank 0 */
void SoundStepAllChannels(void);
void SoundRunChannelCommandSlots(void);
void SoundSetupWaveChannel(void);
void SoundSpeechStep(void);
void SoundStepWavFrames(void);
void SoundUpdateAllChannels(void);
void SoundRetrigNote(void);
void SoundInitKit2Sample(UBYTE n);             /* bank 0 */
void SoundTriggerWav(void);                     /* asm */
void SoundClearChannelCommandSlots(void);             /* asm */
void SoundResetWavFlat(void);                 /* asm */
void SoundReadChainRow(void);                 /* asm */
void SoundDeactivateChannel(void);            /* asm */
void SoundEnterSongRow(void);                 /* asm */
void SoundClearVoiceHram(void);               /* asm */
void SoundLoadWaveRam(void);                     /* bank 0, asm */
void SoundTriggerPu1(void);                      /* asm */
void SoundTriggerPu2(void);                      /* asm */
void SoundTriggerNoi(void);                    /* asm */
void SoundSetupLfo(void);                     /* asm */
UBYTE SoundEnvelopeReleaseSpeed(void);
UBYTE SoundEnvelopeReleaseTarget(void);
void SoundClampNoteRange(UBYTE n);            /* asm */
void SoundCommandParsePitchValue(UBYTE *env);        /* asm */
void SoundReadTableEnvelope(void);               /* asm */
void SoundReadTableTranspose(void);              /* asm */
void SoundTriggerNote(void);                     /* asm */
void SoundSetKitParameters(void);                 /* asm */
UBYTE SoundStepPitchVibratoNoi(void);             /* bank 0, asm */
void SoundWavLevelStep(UBYTE v);              /* defined below */

/* $507F: step the working pointers to the next channel */

void SoundAdvanceStatePointers(void)
{
    wPointerPhraseRow++;
    wPointerChainRow++;
    wPointerNoteCountdown++;
    wPointerGroove++;
    wPointerGrooveRow++;
    wPointerPhrase++;
    wPointerNoteSource++;
}

/* $6479: a random number 0..n (the Z command's digits) */

UBYTE SoundRandomUpTo(UBYTE n)
{
    if (!n)
        return 0;
    return randw() % (n + 1);
}

/* $649D: R, retrig (R8x: resync). The first digit changes the volume on each
   retrig; the second sets the rate in command-rate steps (1 fastest, F
   slowest, 0 once: stored as $0F) */

void Command_R(void)
{
    UBYTE vol;
    UBYTE n;
    UBYTE k;

    vol = hCommandValue & 0xF0;
    if (vol == 0x80) {
        n = hCommandValue & 0x0F;
        n = MulU8(n, wChannelCommandRate[hSoundChannel] + 1);
        k = hSoundChannel;
        wChannelRetrigTimer[k] = ++n;
        wChannelRetrigRate[k] = n & 0x0F;
    } else {
        n = (hCommandValue - 1) & 0x0F;
        if (n != 0x0F)
            n = MulU8(n, wChannelCommandRate[hSoundChannel] + 1);
        wCommandSlotRetrigRate[wCommandChannel] = n;
        wChannelCommandSlots[wCommandChannel] |= 2;
        wChannelEnvelopeVolumeOffset[wCommandChannel] = vol;
        if (wCommandChannel < 4)
            wCommandSlotEnvelopeTimer[wCommandChannel] = 0;
        else
            SoundRetrigNote();
    }
}

/* $6577: kill this channel's note: its FX and FX slots, pitch slide, table,
   chord and vibrato off */

void SoundKillNote(void)
{
    UBYTE chan;

    SoundResetChannelCommand();
    wChannelCommandSlots[hSoundChannel] = 0;
    wChannelCommandSlots[hSoundChannel + 4] = 0;
    wChannelCommandSlots[hSoundChannel + 8] = 0;
    wChannelPitchRate[hSoundChannel] = 0;
    chan = hSoundChannel;
    wChannelTable[chan] = 0xFF;
    wChannelDisplayNote[chan] = 0;
    wChannelChord[chan] = 0;
    wChannelVibratoShape[chan] = 0;
}

/* $65F3: K, kill the note: now (K00) or after the value's ticks, one fewer
   from a table column */

void Command_K(void)
{
    if (hCommandValueMemory) {
        wCommandSlotCommandTimer[wCommandChannel] = hCommandValue - (wCommandChannel > 3);
        wChannelCommandSlots[wCommandChannel] |= 1;
    } else
        SoundKillNote();
}

/* $6646: P, the pitch bend; on the noise channel P applies S every tick */

void Command_P(void)
{
    if (hSoundChannel == 3) {
        wChannelVibratoShape[3] = hCommandValue;
        wNoiPSlideStart = 1;
    } else
        SoundApplyPitchBend();
}

/* $665F: O, the output (panning) */

void Command_O(void)
{
    SoundWritePanning(hCommandValue);
}

/* $6669: T, the tempo */

void Command_T(void)
{
    wTempo = hCommandValue;
    wTempoMode = 0;
    FarCall(7, FAR_FN(SetSyncSendRate));
}

/* $66E7: R fires on FX slot wCommandChannel and the note retriggers: a slide
   pending on a pulse or the wave channel restarts and the envelope is parsed
   again (the wave's level steps by R's volume digit instead); a phrase slot
   restarts its channel's latched table too. Then the slot reloads its timer
   from its rate, or ends (rate $0F: once) */

void SoundRetrigNote(void)
{
    UBYTE rate;

    hSoundChannel = wCommandChannel & 3;
    if (!wSpeechWord || hSoundChannel != 2) {
        switch (hSoundChannel) {
        case 0:
            if (wChannelSlideNoteSource[0]) {
                wChannelPitchCurrent[0] = 0x8000;
                wChannelCommandStep[0] = 1;
            }
            SoundCommandParsePitchValue(&wEnvelopeNode1Pu1);
            break;
        case 1:
            if (wChannelSlideNoteSource[1]) {
                wChannelPitchCurrent[1] = 0x8000;
                wChannelCommandStep[1] = 1;
            }
            SoundCommandParsePitchValue(&wEnvelopeNode1Pu2);
            break;
        case 2:
            if (wChannelSlideNoteSource[2]) {
                wChannelPitchCurrent[2] = 0x8000;
                wChannelCommandStep[2] = 1;
            }
            SoundWavLevelStep(wChannelEnvelopeVolumeOffset[wCommandChannel]);
            break;
        case 3:
            SoundCommandParsePitchValue(&wEnvelopeNode1Noi);
            break;
        }
        if (wCommandChannel < 4 && wChannelTableActive[hSoundChannel]) {
            wChannelTable[hSoundChannel] = wChannelTableLatch[hSoundChannel];
            wChannelTableRow1[hSoundChannel + 4] = 0xFF;
            wChannelTableRow1[hSoundChannel] = 0xFF;
            wChannelGrooveRow[hSoundChannel + 4] = 0xFF;
            wChannelNoteCountdown[hSoundChannel + 4] = 0;
            SoundReadTableEnvelope();
            SoundReadTableTranspose();
            wChannelTableFresh[hSoundChannel] = 1;
        }
    }
    if (hSoundChannel == 3)
        wChannelSweep[hSoundChannel] = wNoiseNoteLatch;
    SoundTriggerNote();
    if (hSoundChannel == 2) {
        wKitVoicesActive = wSoundUpdateFlags;
        SoundSetKitParameters();
    }
    rate = wCommandSlotRetrigRate[wCommandChannel];
    if (rate == 0x0F)
        wChannelCommandSlots[wCommandChannel] = 0;
    else
        wCommandSlotEnvelopeTimer[wCommandChannel] = rate;
}

/* $6270: P with the instrument's PITCH on STEP (SoundApplyPitchBend jumps
   here): the pitch changes at once -- a kit's playback rate by 3 a step,
   any other instrument's pitch by 8 a step, its slide restarted */

void Command_P_Step(void)
{
    if (wSoundUpdateFlags && hSoundChannel == 2)
        wChannelFrequency[hSoundChannel] += (WORD)hCommandValueS * 3;
    else {
        wChannelPitchCurrent[hSoundChannel] += (WORD)hCommandValueS << 3;
        wChannelCommandStep[hSoundChannel] = 1;
    }
}

/* $632B: F on the wave channel: a synth steps its wave frame by the value
   and reloads the wave RAM; a kit moves both voices' sample positions by
   the signed value x16 (beyond +-16 the steps double), stopping at the
   sample's start, and retriggers at the instrument's level */

void Command_F_Wav(void)
{
    WORD step;
    UWORD off;

    if (!wSoundUpdateFlags) {
        if (hCommandValueMemory) {
            wWavSynthFrame = hCommandValue + wWavSynthFrame;
            SoundClearVoiceHram();
            _asm di _endasm;
            SoundLoadWaveRam();
            _asm ei _endasm;
        }
    } else {
        step = hCommandValueS;
        off = wKit1OffsetHi;
        if (step >= 16) {
            step <<= 1;
            step -= 16;
        } else if (step < -16) {
            step <<= 1;
            step += 16;
        }
        step <<= 4;
        off <<= 8;
        off += wKit1OffsetLo;
        off += step;
        if (off & 0x8000)
            off = 0;
        wKit1OffsetLo = off & 0xFF;
        wKit1OffsetHi = off >> 8;
        off = wKit2OffsetHi;
        off <<= 8;
        off += wKit2OffsetLo;
        off += step;
        if (off & 0x8000)
            off = 0;
        wKit2OffsetLo = off & 0xFF;
        wKit2OffsetHi = off >> 8;
        step = wWavLevelS;
        wWavLevel = wChannelEnvelope[2];
        SoundTriggerWav();
        wWavLevel = step;
        SoundClearChannelCommandSlots();
    }
}

/* $68A0: the noise channel's pitch each tick: P slides it by its signed
   speed in quarter semitones (the remainder carried in wNoiPitchFraction), then
   the vibrato's offset applies, both as note steps through SoundClampNoteRange */

void SoundNoiPitchStep(void)
{
    UBYTE v, neg, ofs;

    v = wChannelVibratoShape[3];
    if (v) {
        if (wNoiPSlideStart) {
            wNoiPSlideStart = 0;
            wNoiPitchFraction = 0;
        } else {
            neg = v & 0x80;
            if (neg)
                v = -v;
            v = v + wNoiPitchFraction;
            wNoiPitchFraction = v & (UBYTE)3;
            v >>= 2;
            if (neg)
                v = -v;
            SoundClampNoteRange(v);
        }
    }
    if (wChannelVibratoSpeed[7]) {
        ofs = SoundStepPitchVibratoNoi();
        SoundClampNoteRange(ofs - wNoiVibratoPreviousOffset);
        wNoiVibratoPreviousOffset = ofs;
    }
}

/* $6904: step the noise channel's chord: the note, the first digit's
   offset, the second's (skipped when 0), the note again */

void SoundNoiChordStep(void)
{
    UBYTE chord;
    UBYTE *q;

    chord = wChannelChord[3];
    if (chord) {
        q = wChannelChordPhase;
        switch (q[3]) {
        case 0:
            q[3] = 1;
            wNoiChordPreviousOffset = 0;
            break;
        case 1:
            q[3] = 2;
            chord >>= 4;
            SoundClampNoteRange(chord);
            wNoiChordPreviousOffset = chord;
            break;
        case 2:
            if (chord & 0x0F) {
                q[3] = 3;
                chord &= 0x0F;
                SoundClampNoteRange(chord - wNoiChordPreviousOffset);
                wNoiChordPreviousOffset = chord;
                break;
            }
        case 3:
            q[3] = 1;
            SoundClampNoteRange(-wNoiChordPreviousOffset);
            wNoiChordPreviousOffset = 0;
            break;
        }
    }
}

const UBYTE EnvelopeReleaseSpeeds[8] = {0x00, 0x06, 0x0B, 0x11, 0x16, 0x1C, 0x22, 0x27};

/* $69C3: E's release digit (the value's low three bits) -> the fade speed */

UBYTE SoundEnvelopeReleaseSpeed(void)
{
    return EnvelopeReleaseSpeeds[hCommandValue & 7];
}

/* $6685: a delayed K's slot ran out: kill that channel's note */

void SoundRunCommandSlot(void)
{
    hSoundChannel = wCommandChannel & 3;
    SoundKillNote();
}

/* $6694: step the WAV output level (NR32 bits 5-6: $20 100%, $40 50%,
   $60 25%, 0 off) by the signed high digit of v: negative fades towards
   off, positive towards 100% */

void SoundWavLevelStep(UBYTE v)
{
    if (!v)
        return;
    wWavLevel &= 0x60;
    if (v & 0x80) {
        while (v && wWavLevel) {
            wWavLevel += 0x20;
            wWavLevel &= 0x60;
            v += 0x10;
        }
    } else {
        while (v && wWavLevel != 0x20) {
            wWavLevel -= 0x20;
            wWavLevel &= 0x60;
            v -= 0x10;
        }
    }
}

/* $69DF: E's release direction (the second digit's bit 3: 9-F fade up): the
   volume the envelope fades toward */

UBYTE SoundEnvelopeReleaseTarget(void)
{
    return (hCommandValue & 8) ? 0x0F : 0;
}

/* $69F2: E on PU1: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Pu1(void)
{
    UBYTE v;

    if (wChannelFrequencyHi[0] & 0x40)
        SoundTriggerPu1();
    wEnvelopeStagePu1 = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedPu1 = v;
    if (v) {
        wEnvelopeCounterPu1 = wEnvelopeSpeedPu1;
        wEnvelopeTargetPu1 = SoundEnvelopeReleaseTarget();
        wEnvelopeStagePu1 = 3;
    }
}

/* $6A28: E on PU2: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Pu2(void)
{
    UBYTE v;

    if (wChannelFrequencyHi[1] & 0x40)
        SoundTriggerPu2();
    wEnvelopeStagePu2 = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedPu2 = v;
    if (v) {
        wEnvelopeCounterPu2 = wEnvelopeSpeedPu2;
        wEnvelopeTargetPu2 = SoundEnvelopeReleaseTarget();
        wEnvelopeStagePu2 = 3;
    }
}

/* $6A5E: E on NOI: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void Command_E_Noi(void)
{
    UBYTE v;

    if (wChannelFrequencyHi[3] & 0x40)
        SoundTriggerNoi();
    wEnvelopeStageNoi = 0;
    SoundSetupLfo();
    v = SoundEnvelopeReleaseSpeed();
    wEnvelopeSpeedNoi = v;
    if (v) {
        wEnvelopeCounterNoi = wEnvelopeSpeedNoi;
        wEnvelopeTargetNoi = SoundEnvelopeReleaseTarget();
        wEnvelopeStageNoi = 3;
    }
}

/* $6A94: live mode, queue channel ch to start the song row's chain when its
   current one ends; pressed again on the same row (the chain or phrase
   screen), start it at the edited chain row, sooner (wChannelLiveQueued); with
   a sync restart pending, start it now */

void SoundLiveQueueStart(UBYTE chan)
{
    UBYTE *p;
    UBYTE k;

    p = wChannelNextSongRow + chan;
    if (wSongRow == *p) {
        if (wCurrentScreen == 3 || wCurrentScreen == 1) {
            if (SoundGetChainPhrase(wChain, wChainRow) == 0xFF)
                return;
            wChannelRowQueue[chan] = wChainRow;
        } else
            wChannelRowQueue[chan] = 0;
        wChannelLiveQueued[chan] = 1;
    } else {
        *p = wSongRow;
        wChannelLiveQueued[chan] = 0;
    }
    wChannelStopQueued[chan] = 0;
    if (wPlayRestartPending) {
        hSoundChannel = chan;
        SoundStartChannelFromRow();
        k = hSoundChannel;
        wChannelStopQueued[k] = 0;
        wChannelNextSongRow[k] = 0xFF;
    }
}

/* $6B59: the channels to play: the song cursor's column, or on the song
   screen the selection's columns */

void SoundSetPlayRange(void)
{
    wPlayChannelFirst = wSongColumn;
    wPlayChannelLast = wSongColumn;
    if (wCurrentScreen == 4 && wCursorMode == 4) {
        if (wSelectionLeft < wSongColumn)
            wPlayChannelFirst = wSelectionLeft;
        else
            wPlayChannelLast = wSelectionLeft;
    }
}

/* $6B9B: advance the retrigger of every playing channel */

void SoundLiveQueueRange(void)
{
    UBYTE chan;

    SoundSetPlayRange();
    for (chan = wPlayChannelFirst; chan <= wPlayChannelLast; chan++)
        SoundLiveQueueStart(chan);
}

/* $6D97: start the channels in the play range from the song row */

void SoundStartChannels(void)
{
    wPlayMode = 0;
    SoundSetPlayRange();
    for (hSoundChannel = wPlayChannelFirst; hSoundChannel < wPlayChannelLast + 1; hSoundChannel++) {
        wChannelNextSongRow[hSoundChannel] = wSongRow;
        SoundStartChannelFromRow();
    }
    if (SoundAnyChannelActive()) {
        FillMemory(wChannelStopQueued, 0, 4);
        FillMemory(wChannelNextSongRow, 0xFF, 4);
        SoundSetChannelEnable();
        SoundResetCommandState();
        SoundEnableLcdInterrupts();
        SyncStartTransport();
        wPlayMode = 4;
    }
}

/* $6E0F */

void SoundStartRange(void)
{
    if (SoundAnyChannelActive())
        SoundLiveQueueRange();
    else
        SoundStartChannels();
}

/* $6E24: live mode, stop the play range's channels: queued starts are
   cancelled; an active channel stops at the end of its chain, or of its
   phrase when pressed again (wChannelLiveQueued) */

void SoundLiveStopRange(void)
{
    UBYTE chan;
    UBYTE *p;

    SoundSetPlayRange();
    for (chan = wPlayChannelFirst; chan <= wPlayChannelLast; chan++) {
        p = wChannelNextSongRow + chan;
        if (*p != 0xFF)
            *p = 0xFF;
        if (wChannelActive[chan]) {
            p = wChannelStopQueued + chan;
            if (!*p) {
                *p = 1;
                wChannelLiveQueued[chan] = 0;
            } else
                wChannelLiveQueued[chan] = 1;
        }
    }
}

/* $70FB: enable the LCD-STAT interrupt (and the serial one if syncing) */

void SoundEnableLcdInterrupts(void)
{
    SetInterruptEnable(IE_REG | 0x04 | wSyncSerialIeMask);
}

/* $6F65: chain_phrases[chain][row] (SRAM bank 1 $A080) */

UBYTE SoundGetChainPhrase(UBYTE chain, UBYTE row)
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

void SoundInitPlayState(void)
{
    wPlayMode = wCurrentScreen;
    wLiveMode = 0;
    SoundTogglePlayback();
}

/* $7362: B in a table, MayBe hop: with the probability in the high digit of
   the command value (of the right column if `right`), hop to the row in the
   low digit */

UBYTE SoundTableMaybeHop(UBYTE right)
{
    UBYTE v;

    v = right ? wTableCommand2Value : wTableCommand1Value;
    if ((v & 0xF0) > randw()) {
        v &= 0x0F;
        if (right)
            v |= 0x80;
        SoundTableHop(v);
        return 1;
    }
    return 0;
}

/* $77F0: one engine tick */

void SoundTick(void)
{
    if (wPlayRestartPending) {
        wSyncTransmitByte = wSongRow;
        initrand(DIV_REG);
    } else {
        SoundStepAllChannels();
        SoundRunChannelCommandSlots();
        SoundSetupWaveChannel();
        if (wSpeechWord && wSpeechFrameTicks != 0xFF)
            SoundSpeechStep();
        if (wWavPlayMode && wChannelActive[2])
            SoundStepWavFrames();
    }
    SoundUpdateAllChannels();
    if (wPlayRestartPending) {
        wPlayRestartPending = 0;
        if (wSyncMode == 3 || wSyncMode == 6)
            SoundTick();
    }
    if (wKeyboardNoteInjected) {
        wKeyboardNoteInjected = 0;
        wKeyboardNoteQueued = 0;
    }
}

/* $78C4: stop playback in both contexts, the screen's and the song's */

void SoundToggleSongPlayback(void)
{
    if (wCurrentScreen == 4)
        DebugTrap();
    if (wLiveMode) {
        SoundLiveStopRange();
        return;
    }
    if (wSyncMode == 1 || wSyncMode == 3)
        wSyncTick = 0xFF;
    if (wPlayMode && wPlayMode != 4)
        SoundTogglePlayback();
    if (wSavedScreen)
        DebugTrap();
    wSavedScreen = wCurrentScreen;
    wCurrentScreen = 4;
    SoundTogglePlayback();
    wCurrentScreen = wSavedScreen;
    wSavedScreen = 0;
}

/* $792B: start the song, all four channels, at the song cursor's row: each
   channel's chain there, walking back past empty ($80+) rows. Started from
   another screen's chain row (SELECT+START, sync OFF or KEYBD), each chain
   starts at that row instead, modulo the chain's length */

void SoundStartSongPlayback(void)
{
    UBYTE resume;
    UBYTE *chain, *active, *phrase;
    UBYTE row, n, stop, end, k;

    resume = wChainRow && (wSavedScreen && wSavedScreen != 4) && (wSyncMode == 0 || wSyncMode == 5);
    for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++) {
        chain = &wChannelChain[hSoundChannel];
        active = &wChannelActive[hSoundChannel];
        phrase = &wChannelPhrase[hSoundChannel];
        *active = 1;
        wChannelSongRow[hSoundChannel] = wSongRow;
        *chain = ReadSequenceByte(hSoundChannel, wSongRow);
        if (*chain & 0x80) {
            row = wSongRow;
            while ((ReadSequenceByte(hSoundChannel, row) & 0x80) && row)
                row--;
            wChannelSongRow[hSoundChannel] = row;
            *chain = ReadSequenceByte(hSoundChannel, row);
            if (*chain & 0x80) {
                *active = 0;
                continue;
            }
        }
        wChannelChainRow[hSoundChannel] = 0xFF;
        *phrase = SoundGetChainPhrase(*chain, 0);
        if (*phrase == 0xFF) {
            *active = 0;
            continue;
        }
        if (resume) {
            n = 1;
            while (!(stop = wChainRow < n) && !(end = SoundGetChainPhrase(*chain, n) == 0xFF))
                n++;
            k = wChainRow % n;
            wChannelChainRow[hSoundChannel] = k - 1;
            *phrase = SoundGetChainPhrase(*chain, k);
        }
    }
}

/* $7B31: start the current screen's playback: the song from the cursor row
   (SoundStartSongPlayback), the chain from the cursor's chain row, or the phrase */

void SoundStartScreenPlayback(void)
{
    if (wSyncMode == 1 && wSyncTick == 0xFF) {
        wSyncRun = wCurrentScreen == 4;
        SyncSetSendMode();
    }
    if (wButtons & 0x80)
        wSyncStartRow = wSongRow;
    else
        wSongRow = wSyncStartRow;
    SoundSetChannelEnable();
    SoundResetCommandState();
    switch (wCurrentScreen) {
    case 4:
        SoundStartSongPlayback();
        break;
    case 3:
        wChannelActive[wSongColumn] = 1;
        wPlayStartChannel = wSongColumn;
        wPlayStartRow = wSongRow;
        hSoundChannel = wSongColumn;
        if (wChain & 0x80)
            DebugTrap();
        wChannelChain[wSongColumn] = wChain;
        wChannelChainRow[wSongColumn] = wChainRow - 1;
        if (SoundGetChainPhrase(wChain, wChainRow) == 0xFF)
            wChannelChainRow[wSongColumn] = 0xFF;
        break;
    default:
        wChannelActive[wSongColumn] = 1;
        wPlayStartChannel = wSongColumn;
        wPlayStartRow = wSongRow;
        hSoundChannel = wSongColumn;
        wChannelPhrase[wSongColumn] = wPhrase;
        break;
    }
    wPlayMode = wCurrentScreen;
    SoundEnableLcdInterrupts();
}

/* $7C4C: stop the current screen's playback */

void SoundTogglePlayback(void)
{
    UBYTE mode, row;

    mode = wPlayMode;
    row = wSongRow;
    if (wLiveMode) {
        SoundStartRange();
        return;
    }
    NR52_REG = 0;
    wPlayMode = 0;
    for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++)
        if (wChannelActive[hSoundChannel])
            SoundResetChannelCommand();
    FillMemory(wChannelActive, 0, 8);
    FillMemory(wChannelCommandSlots, 0, 12);
    FillMemory(wChannelDisplayNote, 0, 4);
    FillMemory(wChannelNextSongRow, 0xFF, 4);
    ClearSprites012();
    if (mode != wCurrentScreen)
        SoundStartScreenPlayback();
    else
        SyncStopTransport();
    wSongRow = row;
}

/* $6BBA: start channel hSoundChannel at its next song row (wChannelNextSongRow, $FF
   none): phrase and table cursors reset, the row walked back past empty
   ($80+) chains to the last real one, then that chain's first phrase -- or
   the channel stops when the song has none */

void SoundStartChannelFromRow(void)
{
    if (wChannelNextSongRow[hSoundChannel] == 0xFF)
        return;
    wChannelNoteCountdown[hSoundChannel] = 0;
    wChannelGrooveRow[hSoundChannel] = 0xFF;
    wChannelTableRow1[hSoundChannel] = 0xFF;
    wChannelTable[hSoundChannel] = 0xFF;
    wChannelPhraseRow[hSoundChannel] = 0xFF;
    wChannelHopCount[hSoundChannel] = 0;
    wChannelNoteCountdown[hSoundChannel + 4] = 0;
    wChannelGroove[hSoundChannel + 4] = 0;
    wChannelGrooveRow[hSoundChannel + 4] = 0xFF;
    wChannelTableRow1[hSoundChannel + 4] = 0xFF;
    wChannelSongRow[hSoundChannel] = wChannelNextSongRow[hSoundChannel];
    wChannelChain[hSoundChannel] = ReadSequenceByte(hSoundChannel, wChannelNextSongRow[hSoundChannel]);
    while ((wChannelChain[hSoundChannel] & 0x80) && wChannelSongRow[hSoundChannel]) {
        wChannelSongRow[hSoundChannel]--;
        wChannelChain[hSoundChannel] = ReadSequenceByte(hSoundChannel, wChannelSongRow[hSoundChannel]);
    }
    if (wChannelChain[hSoundChannel] & 0x80) {
        wChannelActive[hSoundChannel] = 0;
        return;
    }
    wChannelActive[hSoundChannel] = 1;
    wChannelChainRow[hSoundChannel] = 0xFF;
    wChannelPhrase[hSoundChannel] = SoundGetChainPhrase(wChannelChain[hSoundChannel], 0);
}

/* $6EA7: live mode: a channel queued to start whose chain has ended starts
   now (wChannelRestart) */

void SoundLiveStartQueued(void)
{
    UBYTE *q;

    if (wLiveMode) {
        q = wChannelLiveQueued;
        if (q[0] && !wChannelActive[0]) {
            q[0] = 0;
            wChannelRestart[0] = 1;
        }
        if (q[1] && !wChannelActive[1]) {
            q[1] = 0;
            wChannelRestart[1] = 1;
        }
        if (q[2] && !wChannelActive[2]) {
            q[2] = 0;
            wChannelRestart[2] = 1;
        }
        if (q[3] && !wChannelActive[3]) {
            q[3] = 0;
            wChannelRestart[3] = 1;
        }
    }
}

/* $6FB6: step the synth's frames: when the timer runs out, add the delta
   list's next step to the frame and reload the wave; at the list's end stop
   (play mode 1: a flat wave) or go back to the loop point */

void SoundStepWavFrames(void)
{
    if (wWavFrameDeltaPointer) {
        if (!wWavFrameTimer) {
            if (!*wWavFrameDeltaPointer) {
                if (wWavPlayMode == 1) {
                    SoundResetWavFlat();
                    return;
                }
                wWavFrameDeltaPointer = wWavFrameDeltaLoop;
            }
            if (*wWavFrameDeltaPointer) {
                wWavSynthFrame += *wWavFrameDeltaPointer;
                SoundClearVoiceHram();
                if (wWavPlayMode == 4) {
                    _asm di _endasm;
                    SoundLoadWaveRam();
                    _asm ei _endasm;
                } else
                    wWavUploadPending = 1;
                wWavFrameDeltaPointer++;
                wWavFrameTimer = wCommandWaveParameter2;
            }
        } else
            wWavFrameTimer--;
    }
}

/* $7034: speech: when the frame's ticks run out, play the word's next
   allophone (SRAM bank 0; 0 ends the word: a flat wave) on the kit voice and
   read the next frame's length */

void SoundSpeechStep(void)
{
    UBYTE v, n;

    rRAMB = 0;
    if (!wSpeechFrameTicks) {
        hSoundChannel = 2;
        _asm di _endasm;
        rRAMG = 0x0A;
        if (*wSpeechPointer) {
            v = *wSpeechPointer;
            rRAMG = 0;
            _asm ei _endasm;
            v--;
            wKit2Bank = v / 15 + 0x19;
            if (wKit2Bank > 0x1A)
                wKit2Bank += 5;
            v = v % 15;
            n = wChannelNote[2] = v + 1;
            _asm di _endasm;
            wSpeechWord = 0;
            wKit2LoopEnabled = 0;
            SoundInitKit2Sample(v);
            SoundTriggerWav();
            wSpeechWord = 1;
            _asm ei _endasm;
            wChannelDisplayNote[2] = n;
        } else {
            rRAMG = 0;
            _asm ei _endasm;
            SoundResetWavFlat();
        }
        wSpeechPointer++;
        _asm di _endasm;
        rRAMG = 0x0A;
        wSpeechFrameTicks = *wSpeechPointer;
        rRAMG = 0;
        _asm ei _endasm;
        if (wSpeechFrameTicks)
            wSpeechPointer++;
    }
    wSpeechFrameTicks--;
}

/* $786F: live mode, at a chain's end: stop the channel if a stop is queued
   (and the sound once no channel plays), else start the queued song row;
   1 if neither, for the caller to carry on */

UBYTE SoundLiveChainEnd(void)
{
    SoundReadChainRow();
    if (wChannelStopQueued[hSoundChannel]) {
        SoundDeactivateChannel();
        SoundResetChannelCommand();
        if (!SoundAnyChannelActive()) {
            NR52_REG = 0;
            wPlayMode = 0;
            SyncStopTransport();
        }
        return 0;
    }
    if (wChannelNextSongRow[hSoundChannel] != 0xFF) {
        SoundEnterSongRow();
        return 0;
    }
    return 1;
}

/* $73B0: Z for command slot `slot`: repeat the last command that was not Z
   or H (wLastCommand[last]) with its value plus 0..each Z digit; returns the
   command */

UBYTE SoundZRepeat(UBYTE slot, UBYTE last)
{
    wChannelCommandValue[slot] = wLastCommandValue[last] + (SoundRandomUpTo(wChannelCommandValue[slot] >> 4) << 4)
                        + SoundRandomUpTo(wChannelCommandValue[slot] & 0x0F);
    wChannelCommand[slot] = wLastCommand[last];
    return wChannelCommand[slot];
}

/* $743E: Z in a phrase: this channel's last command */

UBYTE SoundZPhrase(void)
{
    return SoundZRepeat(hSoundChannel, hSoundChannel);
}

/* $744D: Z in a table's CMD1 column: that table's last CMD1 */

UBYTE SoundZTableCommand1(void)
{
    return SoundZRepeat(4, wChannelTable[hSoundChannel] + 4);
}

/* $746F: Z in a table's CMD2 column */

UBYTE SoundZTableCommand2(void)
{
    return SoundZRepeat(5, wChannelTable[hSoundChannel] + 0x24);
}

/* $7CFB: E at a note-on: on PU1, PU2 and NOI stop the fade and take the
   first digit as the envelope's volume (e = 5: the release runs as the note
   plays); on WAV set the output level from the value (e = 0) */

UBYTE Command_E_NoteOn(void)
{
    UBYTE v;

    v = wChannelCommandValue[hSoundChannel];
    switch (hSoundChannel) {
    case 0:
        wEnvelopeStagePu1 = 0;
        wEnvelopeSpeedPu1 = 0;
        break;
    case 1:
        wEnvelopeStagePu2 = 0;
        wEnvelopeSpeedPu2 = 0;
        break;
    case 2:
        v <<= 4;
        wWavLevel = ~(v << 1) + 1;
        return 0;
    case 3:
        wEnvelopeStageNoi = 0;
        wEnvelopeSpeedNoi = 0;
        break;
    }
    wChannelEnvelope[hSoundChannel] = v >> 4;
    return 5;
}
