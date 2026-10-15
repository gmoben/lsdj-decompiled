/* Bank $02: the sound engine's C (the sequencer core around it is
 * hand-written asm).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChan;         /* $FF8B */
extern volatile sfr UBYTE hFxVal;         /* $FF8F */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE DIV_REG;          /* $FF04 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE hFxValMem;                   /* hFxVal read as a plain global (ld hl; or a,(hl)) */
extern volatile sfr BYTE hFxValS;         /* hFxVal as a signed byte (P and F's steps) */
extern UBYTE wFxChan;                    /* $C527: the FX slot (channel + 4 x column) */
extern UBYTE wFxSlotCmdTimer[];             /* $C230, [12] */
extern UBYTE wChanFxSlots[];                /* $C2F0, [12] */
extern UBYTE wWavLevel;                     /* $C8F3 */
extern BYTE wWavLevelS;                     /* wWavLevel as a signed byte */
extern UBYTE wChanNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChanStopQueued[];                /* $C304, [4] */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wSyncTxByte;                   /* $CBDC */
extern UBYTE wSpeechWord;                   /* $C4CE */
extern UBYTE wSpeechFrameTicks;             /* $C4CF */
extern UBYTE wWavPlayMode;                  /* $C500 */
extern UBYTE wKbdNoteInjected;              /* $C8F9 */
extern UBYTE wKbdNoteQueued;                /* $CB92 */
extern UWORD wChanPitchRate[];              /* $C2D0, [4] */
extern UBYTE wChanTable[];                  /* $C204, [4] */
extern UBYTE wChanDispNote[];               /* $C41F, [4] */
extern UBYTE wChanChord[];                  /* $C218, [4] */
extern UBYTE wChanLiveQueued[];             /* $C300, [4] */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSavedScreen;                  /* $C8FE */
extern UBYTE wChanRowQueue[];               /* $C100, [4] */
extern UBYTE wChain;                        /* $C418 */
extern char wChainRow;                      /* $C41A: a char here (copied through a register on use) */
extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncStartRow;                 /* $CB6A */
extern UBYTE wPlayStartChan;                /* $C94C */
extern UBYTE wPlayStartRow;                 /* $C94D */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wChanChain[];                  /* $C170, [4] */
extern UBYTE wChanChainRow[];               /* $C17C, [4] */
extern UBYTE wChanPhrase[];                 /* $C168, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChanPhraseRow[];              /* $C16C, [4] */
extern UBYTE wChanNoteCountdown[];          /* $C160, [8]: the phrase's 4, the table's 4 */
extern UBYTE wChanGroove[];                 /* $C150, [8] */
extern UBYTE wChanGrooveRow[];              /* $C158, [8] */
extern UBYTE wChanTableRow1[];              /* $C208, [8] */
extern UBYTE wChanHopCount[];               /* $C214, [4] */
extern UBYTE wChanSlideNoteSrc[];           /* $C330, [4] */
extern UWORD wChanPitchCur[];               /* $C337, [4] */
extern UBYTE wChanCmdStep[];                /* $C34E, [4] */
extern UBYTE wChanTableActive[];            /* $C2E8, [4] */
extern UBYTE wChanTableLatch[];             /* $C220, [4] */
extern UBYTE wChanTableFresh[];             /* $C2EC, [4] */
extern UBYTE wChanSweep[];                  /* $C2E4, [4]: [3] is the noise note */
extern UBYTE wNoiseNoteLatch;               /* $C4FD */
extern UBYTE wKitVoicesActive;              /* $C8F6 */
extern UBYTE wSndUpdateFlags;               /* $C8F5 */
extern UBYTE wEnvNode1Pu1, wEnvNode1Pu2, wEnvNode1Noi;    /* $CBBC, $CBC3, $CBCA */
extern UBYTE wNoiPitchFrac;                 /* $C8DB */
extern UBYTE wNoiVibPrevOfs;                /* $C8DA */
extern UBYTE wChanVibSpeed[];               /* $C314, [4] words, read here by byte */
extern UWORD wChanFreq[];                   /* $C0F4, [4] */
extern UBYTE wKit1OffsetHi, wKit1OffsetLo;  /* $C4EA, $C4EB */
extern UBYTE wKit2OffsetHi, wKit2OffsetLo;  /* $C4EC, $C4ED */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wPlayChanFirst;                /* $C8DD */
extern UBYTE wPlayChanLast;                 /* $C8DE */
extern UBYTE wTableCmd1Val;                 /* $C36F */
extern UBYTE wTableCmd2Val;                 /* $C370 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChanVibShape[];               /* $C35F, [4] */
extern UBYTE wNoiPSlideStart;                 /* $C8C5 */
extern UBYTE wChanCmdRate[];                /* $C8C6, [4] */
extern UBYTE wChanRetrigTimer[];            /* $C148, [4] */
extern UBYTE wChanRetrigRate[];             /* $C14C, [4] */
extern UBYTE wFxSlotRetrigRate[];           /* $C8CE, [12] */
extern UBYTE wChanEnvVolOffset[];           /* $C224, [12]: R's volume digit, by FX slot */
extern UBYTE wFxSlotEnvTimer[];             /* $C23C, [12] */
extern UBYTE wChanCmd[];                    /* $C365, [6]: 4 phrase channels, the table's CMD1 and CMD2 */
extern UBYTE wChanCmdVal[];                 /* $C36B, [6] */
extern UBYTE wLastCmd[];                    /* $C371, [68] */
extern UBYTE wLastCmdVal[];                 /* $C3B5, [68] */
extern UBYTE *wSpeechPtr;                   /* $C4D0: the word's allophone stream (SRAM bank 0) */
extern UBYTE wKit2Bank;                     /* $C4F5 */
extern UBYTE wKit2LoopEnabled;              /* $C4E5 */
extern UBYTE wChanNote[];                   /* $C0CC, [4] */
extern UBYTE *wWavFrameDeltaPtr;            /* $C521 */
extern UBYTE *wWavFrameDeltaLoop;           /* $C523 */
extern UBYTE wWavFrameTimer;                /* $C525 */
extern UBYTE wWavSynthFrame;                /* $C694 */
extern UBYTE wWavUploadPending;              /* $C69C */
extern UBYTE wFxWaveParam2;                 /* $C8C3 */
extern UBYTE wChanFreqHi[];                 /* $C2E0, [4] */
extern UBYTE wChanEnv[];                    /* $C2D8, [4] */
extern UBYTE wChanRestart[];                /* $C0E4, [4] */
extern UBYTE wChanChordPhase[];             /* $C21C, [4] */
extern UBYTE wNoiChordPrevOfs;              /* $C8DC */
extern UBYTE wEnvStagePu1, wEnvSpeedPu1, wEnvCounterPu1, wEnvTargetPu1;    /* $CBBF.. */
extern UBYTE wEnvStagePu2, wEnvSpeedPu2, wEnvCounterPu2, wEnvTargetPu2;    /* $CBC6.. */
extern UBYTE wEnvStageNoi, wEnvSpeedNoi, wEnvCounterNoi, wEnvTargetNoi;    /* $CBCD.. */
/* the working pointers' low bytes: the per-channel arrays they point into
   sit in one page, so moving to the next channel is a byte increment */
extern UBYTE wPtrChainRow, wPtrPhraseRow, wPtrNoteCountdown, wPtrGroove;  /* $C8DF.. */
extern UBYTE wPtrGrooveRow, wPtrPhrase, wPtrNoteSrc;                       /* ..$C8ED */

void SndWritePanning(UBYTE v);              /* bank 0 */
void SetInterruptEnable(UBYTE ie);          /* bank 0 */
UBYTE randw(void);                       /* bank 0 (this file reads the low byte) */
UBYTE MulU8(UBYTE a, UBYTE b);              /* bank 0 */
void SetSyncSendRate(void);                 /* bank 7 */
void SndSetPlayRange(void);
void SndLiveQueueStart(UBYTE ch);
UBYTE SndGetChainPhrase(UBYTE chain, UBYTE row);
UBYTE SndRandomUpTo(UBYTE n);
void SndTableHop(UBYTE v);           /* asm */
void ApplyPitchBend(void);                  /* asm */
UBYTE SndAnyChanActive(void);
void SndStartChans(void);
void TogglePlayback(void);
void SndEnableLcdInts(void);
void SndLiveStopRange(void);
void StartScreenPlayback(void);
void SyncStopTransport(void);               /* bank 0 */
void SyncSetSendMode(void);                 /* bank 0 */
void StartSongPlayback(void);
void SndKillNote(void);
void SndResetChanFx(void);                  /* asm */
void SndStartChanFromRow(void);
void SndSetChanEnable(void);
void SndResetFxState(void);
void SyncStartTransport(void);              /* bank 0 */
void initrand(UWORD seed);              /* bank 0 */
void SndStepAllChans(void);
void SndRunChanFxSlots(void);
void SndSetupWaveChan(void);
void SndSpeechStep(void);
void SndStepWavFrames(void);
void SndUpdateAllChans(void);
void SndRetrigNote(void);
void InitKit2Sample(UBYTE n);             /* bank 0 */
void TriggerWav(void);                     /* asm */
void SndClearChanFxSlots(void);             /* asm */
void SndResetWavFlat(void);                 /* asm */
void SndReadChainRow(void);                 /* asm */
void SndDeactivateChan(void);            /* asm */
void SndEnterSongRow(void);                 /* asm */
void SndClearVoiceHram(void);               /* asm */
void LoadWaveRam(void);                     /* bank 0, asm */
void TriggerPu1(void);                      /* asm */
void TriggerPu2(void);                      /* asm */
void TriggerNoi(void);                    /* asm */
void SndSetupLfo(void);                     /* asm */
UBYTE EnvReleaseSpeed(void);
UBYTE EnvReleaseTarget(void);
void SndClampNoteRange(UBYTE n);            /* asm */
void SndFxParsePitchVal(UBYTE *env);        /* asm */
void ReadTableEnv(void);               /* asm */
void ReadTableTranspose(void);              /* asm */
void TriggerNote(void);                     /* asm */
void SndSetKitParams(void);                 /* asm */
UBYTE SndStepPitchVibNoi(void);             /* bank 0, asm */
void SndWavLevelStep(UBYTE v);              /* defined below */

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
    return randw() % (n + 1);
}

/* $649D: R, retrig (R8x: resync). The first digit changes the volume on each
   retrig; the second sets the rate in command-rate steps (1 fastest, F
   slowest, 0 once: stored as $0F) */

void FxCmd_R(void)
{
    UBYTE vol;
    UBYTE n;
    UBYTE k;

    vol = hFxVal & 0xF0;
    if (vol == 0x80) {
        n = hFxVal & 0x0F;
        n = MulU8(n, wChanCmdRate[hSoundChan] + 1);
        k = hSoundChan;
        wChanRetrigTimer[k] = ++n;
        wChanRetrigRate[k] = n & 0x0F;
    } else {
        n = (hFxVal - 1) & 0x0F;
        if (n != 0x0F)
            n = MulU8(n, wChanCmdRate[hSoundChan] + 1);
        wFxSlotRetrigRate[wFxChan] = n;
        wChanFxSlots[wFxChan] |= 2;
        wChanEnvVolOffset[wFxChan] = vol;
        if (wFxChan < 4)
            wFxSlotEnvTimer[wFxChan] = 0;
        else
            SndRetrigNote();
    }
}

/* $6577: kill this channel's note: its FX and FX slots, pitch slide, table,
   chord and vibrato off */

void SndKillNote(void)
{
    UBYTE ch;

    SndResetChanFx();
    wChanFxSlots[hSoundChan] = 0;
    wChanFxSlots[hSoundChan + 4] = 0;
    wChanFxSlots[hSoundChan + 8] = 0;
    wChanPitchRate[hSoundChan] = 0;
    ch = hSoundChan;
    wChanTable[ch] = 0xFF;
    wChanDispNote[ch] = 0;
    wChanChord[ch] = 0;
    wChanVibShape[ch] = 0;
}

/* $65F3: K, kill the note: now (K00) or after the value's ticks, one fewer
   from a table column */

void FxCmd_K(void)
{
    if (hFxValMem) {
        wFxSlotCmdTimer[wFxChan] = hFxVal - (wFxChan > 3);
        wChanFxSlots[wFxChan] |= 1;
    } else
        SndKillNote();
}

/* $6646: P, the pitch bend; on the noise channel P applies S every tick */

void FxCmd_P(void)
{
    if (hSoundChan == 3) {
        wChanVibShape[3] = hFxVal;
        wNoiPSlideStart = 1;
    } else
        ApplyPitchBend();
}

/* $665F: O, the output (panning) */

void FxCmd_O(void)
{
    SndWritePanning(hFxVal);
}

/* $6669: T, the tempo */

void FxCmd_T(void)
{
    wTempo = hFxVal;
    wTempoMode = 0;
    FarCall(7, FAR_FN(SetSyncSendRate));
}

/* $66E7: R fires on FX slot wFxChan and the note retriggers: a slide
   pending on a pulse or the wave channel restarts and the envelope is parsed
   again (the wave's level steps by R's volume digit instead); a phrase slot
   restarts its channel's latched table too. Then the slot reloads its timer
   from its rate, or ends (rate $0F: once) */

void SndRetrigNote(void)
{
    UBYTE rate;

    hSoundChan = wFxChan & 3;
    if (!wSpeechWord || hSoundChan != 2) {
        switch (hSoundChan) {
        case 0:
            if (wChanSlideNoteSrc[0]) {
                wChanPitchCur[0] = 0x8000;
                wChanCmdStep[0] = 1;
            }
            SndFxParsePitchVal(&wEnvNode1Pu1);
            break;
        case 1:
            if (wChanSlideNoteSrc[1]) {
                wChanPitchCur[1] = 0x8000;
                wChanCmdStep[1] = 1;
            }
            SndFxParsePitchVal(&wEnvNode1Pu2);
            break;
        case 2:
            if (wChanSlideNoteSrc[2]) {
                wChanPitchCur[2] = 0x8000;
                wChanCmdStep[2] = 1;
            }
            SndWavLevelStep(wChanEnvVolOffset[wFxChan]);
            break;
        case 3:
            SndFxParsePitchVal(&wEnvNode1Noi);
            break;
        }
        if (wFxChan < 4 && wChanTableActive[hSoundChan]) {
            wChanTable[hSoundChan] = wChanTableLatch[hSoundChan];
            wChanTableRow1[hSoundChan + 4] = 0xFF;
            wChanTableRow1[hSoundChan] = 0xFF;
            wChanGrooveRow[hSoundChan + 4] = 0xFF;
            wChanNoteCountdown[hSoundChan + 4] = 0;
            ReadTableEnv();
            ReadTableTranspose();
            wChanTableFresh[hSoundChan] = 1;
        }
    }
    if (hSoundChan == 3)
        wChanSweep[hSoundChan] = wNoiseNoteLatch;
    TriggerNote();
    if (hSoundChan == 2) {
        wKitVoicesActive = wSndUpdateFlags;
        SndSetKitParams();
    }
    rate = wFxSlotRetrigRate[wFxChan];
    if (rate == 0x0F)
        wChanFxSlots[wFxChan] = 0;
    else
        wFxSlotEnvTimer[wFxChan] = rate;
}

/* $6270: P with the instrument's PITCH on STEP (ApplyPitchBend jumps
   here): the pitch changes at once -- a kit's playback rate by 3 a step,
   any other instrument's pitch by 8 a step, its slide restarted */

void FxCmd_P_Step(void)
{
    if (wSndUpdateFlags && hSoundChan == 2)
        wChanFreq[hSoundChan] += (WORD)hFxValS * 3;
    else {
        wChanPitchCur[hSoundChan] += (WORD)hFxValS << 3;
        wChanCmdStep[hSoundChan] = 1;
    }
}

/* $632B: F on the wave channel: a synth steps its wave frame by the value
   and reloads the wave RAM; a kit moves both voices' sample positions by
   the signed value x16 (beyond +-16 the steps double), stopping at the
   sample's start, and retriggers at the instrument's level */

void FxCmd_F_Wav(void)
{
    WORD step;
    UWORD off;

    if (!wSndUpdateFlags) {
        if (hFxValMem) {
            wWavSynthFrame = hFxVal + wWavSynthFrame;
            SndClearVoiceHram();
            _asm di _endasm;
            LoadWaveRam();
            _asm ei _endasm;
        }
    } else {
        step = hFxValS;
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
        wWavLevel = wChanEnv[2];
        TriggerWav();
        wWavLevel = step;
        SndClearChanFxSlots();
    }
}

/* $68A0: the noise channel's pitch each tick: P slides it by its signed
   speed in quarter semitones (the remainder carried in wNoiPitchFrac), then
   the vibrato's offset applies, both as note steps through SndClampNoteRange */

void SndNoiPitchStep(void)
{
    UBYTE v, neg, ofs;

    v = wChanVibShape[3];
    if (v) {
        if (wNoiPSlideStart) {
            wNoiPSlideStart = 0;
            wNoiPitchFrac = 0;
        } else {
            neg = v & 0x80;
            if (neg)
                v = -v;
            v = v + wNoiPitchFrac;
            wNoiPitchFrac = v & (UBYTE)3;
            v >>= 2;
            if (neg)
                v = -v;
            SndClampNoteRange(v);
        }
    }
    if (wChanVibSpeed[7]) {
        ofs = SndStepPitchVibNoi();
        SndClampNoteRange(ofs - wNoiVibPrevOfs);
        wNoiVibPrevOfs = ofs;
    }
}

/* $6904: step the noise channel's chord: the note, the first digit's
   offset, the second's (skipped when 0), the note again */

void SndNoiChordStep(void)
{
    UBYTE chord;
    UBYTE *q;

    chord = wChanChord[3];
    if (chord) {
        q = wChanChordPhase;
        switch (q[3]) {
        case 0:
            q[3] = 1;
            wNoiChordPrevOfs = 0;
            break;
        case 1:
            q[3] = 2;
            chord >>= 4;
            SndClampNoteRange(chord);
            wNoiChordPrevOfs = chord;
            break;
        case 2:
            if (chord & 0x0F) {
                q[3] = 3;
                chord &= 0x0F;
                SndClampNoteRange(chord - wNoiChordPrevOfs);
                wNoiChordPrevOfs = chord;
                break;
            }
        case 3:
            q[3] = 1;
            SndClampNoteRange(-wNoiChordPrevOfs);
            wNoiChordPrevOfs = 0;
            break;
        }
    }
}

const UBYTE EnvReleaseSpeeds[8] = {0x00, 0x06, 0x0B, 0x11, 0x16, 0x1C, 0x22, 0x27};

/* $69C3: E's release digit (the value's low three bits) -> the fade speed */

UBYTE EnvReleaseSpeed(void)
{
    return EnvReleaseSpeeds[hFxVal & 7];
}

/* $6685: a delayed K's slot ran out: kill that channel's note */

void SndRunFxSlot(void)
{
    hSoundChan = wFxChan & 3;
    SndKillNote();
}

/* $6694: step the WAV output level (NR32 bits 5-6: $20 100%, $40 50%,
   $60 25%, 0 off) by the signed high digit of v: negative fades towards
   off, positive towards 100% */

void SndWavLevelStep(UBYTE v)
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

UBYTE EnvReleaseTarget(void)
{
    return (hFxVal & 8) ? 0x0F : 0;
}

/* $69F2: E on PU1: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void FxCmd_E_Pu1(void)
{
    UBYTE v;

    if (wChanFreqHi[0] & 0x40)
        TriggerPu1();
    wEnvStagePu1 = 0;
    SndSetupLfo();
    v = EnvReleaseSpeed();
    wEnvSpeedPu1 = v;
    if (v) {
        wEnvCounterPu1 = wEnvSpeedPu1;
        wEnvTargetPu1 = EnvReleaseTarget();
        wEnvStagePu1 = 3;
    }
}

/* $6A28: E on PU2: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void FxCmd_E_Pu2(void)
{
    UBYTE v;

    if (wChanFreqHi[1] & 0x40)
        TriggerPu2();
    wEnvStagePu2 = 0;
    SndSetupLfo();
    v = EnvReleaseSpeed();
    wEnvSpeedPu2 = v;
    if (v) {
        wEnvCounterPu2 = wEnvSpeedPu2;
        wEnvTargetPu2 = EnvReleaseTarget();
        wEnvStagePu2 = 3;
    }
}

/* $6A5E: E on NOI: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void FxCmd_E_Noi(void)
{
    UBYTE v;

    if (wChanFreqHi[3] & 0x40)
        TriggerNoi();
    wEnvStageNoi = 0;
    SndSetupLfo();
    v = EnvReleaseSpeed();
    wEnvSpeedNoi = v;
    if (v) {
        wEnvCounterNoi = wEnvSpeedNoi;
        wEnvTargetNoi = EnvReleaseTarget();
        wEnvStageNoi = 3;
    }
}

/* $6A94: live mode, queue channel ch to start the song row's chain when its
   current one ends; pressed again on the same row (the chain or phrase
   screen), start it at the edited chain row, sooner (wChanLiveQueued); with
   a sync restart pending, start it now */

void SndLiveQueueStart(UBYTE ch)
{
    UBYTE *p;
    UBYTE k;

    p = wChanNextSongRow + ch;
    if (wSongRow == *p) {
        if (wCurScreen == 3 || wCurScreen == 1) {
            if (SndGetChainPhrase(wChain, wChainRow) == 0xFF)
                return;
            wChanRowQueue[ch] = wChainRow;
        } else
            wChanRowQueue[ch] = 0;
        wChanLiveQueued[ch] = 1;
    } else {
        *p = wSongRow;
        wChanLiveQueued[ch] = 0;
    }
    wChanStopQueued[ch] = 0;
    if (wPlayRestartPending) {
        hSoundChan = ch;
        SndStartChanFromRow();
        k = hSoundChan;
        wChanStopQueued[k] = 0;
        wChanNextSongRow[k] = 0xFF;
    }
}

/* $6B59: the channels to play: the song cursor's column, or on the song
   screen the selection's columns */

void SndSetPlayRange(void)
{
    wPlayChanFirst = wSongCol;
    wPlayChanLast = wSongCol;
    if (wCurScreen == 4 && wCursorMode == 4) {
        if (wSelectionLeft < wSongCol)
            wPlayChanFirst = wSelectionLeft;
        else
            wPlayChanLast = wSelectionLeft;
    }
}

/* $6B9B: advance the retrigger of every playing channel */

void SndLiveQueueRange(void)
{
    UBYTE ch;

    SndSetPlayRange();
    for (ch = wPlayChanFirst; ch <= wPlayChanLast; ch++)
        SndLiveQueueStart(ch);
}

/* $6D97: start the channels in the play range from the song row */

void SndStartChans(void)
{
    wPlayMode = 0;
    SndSetPlayRange();
    for (hSoundChan = wPlayChanFirst; hSoundChan < wPlayChanLast + 1; hSoundChan++) {
        wChanNextSongRow[hSoundChan] = wSongRow;
        SndStartChanFromRow();
    }
    if (SndAnyChanActive()) {
        MemSet(wChanStopQueued, 0, 4);
        MemSet(wChanNextSongRow, 0xFF, 4);
        SndSetChanEnable();
        SndResetFxState();
        SndEnableLcdInts();
        SyncStartTransport();
        wPlayMode = 4;
    }
}

/* $6E0F */

void SndStartRange(void)
{
    if (SndAnyChanActive())
        SndLiveQueueRange();
    else
        SndStartChans();
}

/* $6E24: live mode, stop the play range's channels: queued starts are
   cancelled; an active channel stops at the end of its chain, or of its
   phrase when pressed again (wChanLiveQueued) */

void SndLiveStopRange(void)
{
    UBYTE ch;
    UBYTE *p;

    SndSetPlayRange();
    for (ch = wPlayChanFirst; ch <= wPlayChanLast; ch++) {
        p = wChanNextSongRow + ch;
        if (*p != 0xFF)
            *p = 0xFF;
        if (wChanActive[ch]) {
            p = wChanStopQueued + ch;
            if (!*p) {
                *p = 1;
                wChanLiveQueued[ch] = 0;
            } else
                wChanLiveQueued[ch] = 1;
        }
    }
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
    wPlayMode = wCurScreen;
    wLiveMode = 0;
    TogglePlayback();
}

/* $7362: B in a table, MayBe hop: with the probability in the high digit of
   the command value (of the right column if `right`), hop to the row in the
   low digit */

UBYTE SndTableMaybeHop(UBYTE right)
{
    UBYTE v;

    v = right ? wTableCmd2Val : wTableCmd1Val;
    if ((v & 0xF0) > randw()) {
        v &= 0x0F;
        if (right)
            v |= 0x80;
        SndTableHop(v);
        return 1;
    }
    return 0;
}

/* $77F0: one engine tick */

void SndTick(void)
{
    if (wPlayRestartPending) {
        wSyncTxByte = wSongRow;
        initrand(DIV_REG);
    } else {
        SndStepAllChans();
        SndRunChanFxSlots();
        SndSetupWaveChan();
        if (wSpeechWord && wSpeechFrameTicks != 0xFF)
            SndSpeechStep();
        if (wWavPlayMode && wChanActive[2])
            SndStepWavFrames();
    }
    SndUpdateAllChans();
    if (wPlayRestartPending) {
        wPlayRestartPending = 0;
        if (wSyncMode == 3 || wSyncMode == 6)
            SndTick();
    }
    if (wKbdNoteInjected) {
        wKbdNoteInjected = 0;
        wKbdNoteQueued = 0;
    }
}

/* $78C4: stop playback in both contexts, the screen's and the song's */

void ToggleSongPlayback(void)
{
    if (wCurScreen == 4)
        DebugTrap();
    if (wLiveMode) {
        SndLiveStopRange();
        return;
    }
    if (wSyncMode == 1 || wSyncMode == 3)
        wSyncTick = 0xFF;
    if (wPlayMode && wPlayMode != 4)
        TogglePlayback();
    if (wSavedScreen)
        DebugTrap();
    wSavedScreen = wCurScreen;
    wCurScreen = 4;
    TogglePlayback();
    wCurScreen = wSavedScreen;
    wSavedScreen = 0;
}

/* $792B: start the song, all four channels, at the song cursor's row: each
   channel's chain there, walking back past empty ($80+) rows. Started from
   another screen's chain row (SELECT+START, sync OFF or KEYBD), each chain
   starts at that row instead, modulo the chain's length */

void StartSongPlayback(void)
{
    UBYTE resume;
    UBYTE *chain, *active, *phrase;
    UBYTE row, n, stop, end, k;

    resume = wChainRow && (wSavedScreen && wSavedScreen != 4) && (wSyncMode == 0 || wSyncMode == 5);
    for (hSoundChan = 0; hSoundChan < 4; hSoundChan++) {
        chain = &wChanChain[hSoundChan];
        active = &wChanActive[hSoundChan];
        phrase = &wChanPhrase[hSoundChan];
        *active = 1;
        wChanSongRow[hSoundChan] = wSongRow;
        *chain = ReadSeqByte(hSoundChan, wSongRow);
        if (*chain & 0x80) {
            row = wSongRow;
            while ((ReadSeqByte(hSoundChan, row) & 0x80) && row)
                row--;
            wChanSongRow[hSoundChan] = row;
            *chain = ReadSeqByte(hSoundChan, row);
            if (*chain & 0x80) {
                *active = 0;
                continue;
            }
        }
        wChanChainRow[hSoundChan] = 0xFF;
        *phrase = SndGetChainPhrase(*chain, 0);
        if (*phrase == 0xFF) {
            *active = 0;
            continue;
        }
        if (resume) {
            n = 1;
            while (!(stop = wChainRow < n) && !(end = SndGetChainPhrase(*chain, n) == 0xFF))
                n++;
            k = wChainRow % n;
            wChanChainRow[hSoundChan] = k - 1;
            *phrase = SndGetChainPhrase(*chain, k);
        }
    }
}

/* $7B31: start the current screen's playback: the song from the cursor row
   (StartSongPlayback), the chain from the cursor's chain row, or the phrase */

void StartScreenPlayback(void)
{
    if (wSyncMode == 1 && wSyncTick == 0xFF) {
        wSyncRun = wCurScreen == 4;
        SyncSetSendMode();
    }
    if (wButtons & 0x80)
        wSyncStartRow = wSongRow;
    else
        wSongRow = wSyncStartRow;
    SndSetChanEnable();
    SndResetFxState();
    switch (wCurScreen) {
    case 4:
        StartSongPlayback();
        break;
    case 3:
        wChanActive[wSongCol] = 1;
        wPlayStartChan = wSongCol;
        wPlayStartRow = wSongRow;
        hSoundChan = wSongCol;
        if (wChain & 0x80)
            DebugTrap();
        wChanChain[wSongCol] = wChain;
        wChanChainRow[wSongCol] = wChainRow - 1;
        if (SndGetChainPhrase(wChain, wChainRow) == 0xFF)
            wChanChainRow[wSongCol] = 0xFF;
        break;
    default:
        wChanActive[wSongCol] = 1;
        wPlayStartChan = wSongCol;
        wPlayStartRow = wSongRow;
        hSoundChan = wSongCol;
        wChanPhrase[wSongCol] = wPhrase;
        break;
    }
    wPlayMode = wCurScreen;
    SndEnableLcdInts();
}

/* $7C4C: stop the current screen's playback */

void TogglePlayback(void)
{
    UBYTE mode, row;

    mode = wPlayMode;
    row = wSongRow;
    if (wLiveMode) {
        SndStartRange();
        return;
    }
    NR52_REG = 0;
    wPlayMode = 0;
    for (hSoundChan = 0; hSoundChan < 4; hSoundChan++)
        if (wChanActive[hSoundChan])
            SndResetChanFx();
    MemSet(wChanActive, 0, 8);
    MemSet(wChanFxSlots, 0, 12);
    MemSet(wChanDispNote, 0, 4);
    MemSet(wChanNextSongRow, 0xFF, 4);
    ClearSprites012();
    if (mode != wCurScreen)
        StartScreenPlayback();
    else
        SyncStopTransport();
    wSongRow = row;
}

/* $6BBA: start channel hSoundChan at its next song row (wChanNextSongRow, $FF
   none): phrase and table cursors reset, the row walked back past empty
   ($80+) chains to the last real one, then that chain's first phrase -- or
   the channel stops when the song has none */

void SndStartChanFromRow(void)
{
    if (wChanNextSongRow[hSoundChan] == 0xFF)
        return;
    wChanNoteCountdown[hSoundChan] = 0;
    wChanGrooveRow[hSoundChan] = 0xFF;
    wChanTableRow1[hSoundChan] = 0xFF;
    wChanTable[hSoundChan] = 0xFF;
    wChanPhraseRow[hSoundChan] = 0xFF;
    wChanHopCount[hSoundChan] = 0;
    wChanNoteCountdown[hSoundChan + 4] = 0;
    wChanGroove[hSoundChan + 4] = 0;
    wChanGrooveRow[hSoundChan + 4] = 0xFF;
    wChanTableRow1[hSoundChan + 4] = 0xFF;
    wChanSongRow[hSoundChan] = wChanNextSongRow[hSoundChan];
    wChanChain[hSoundChan] = ReadSeqByte(hSoundChan, wChanNextSongRow[hSoundChan]);
    while ((wChanChain[hSoundChan] & 0x80) && wChanSongRow[hSoundChan]) {
        wChanSongRow[hSoundChan]--;
        wChanChain[hSoundChan] = ReadSeqByte(hSoundChan, wChanSongRow[hSoundChan]);
    }
    if (wChanChain[hSoundChan] & 0x80) {
        wChanActive[hSoundChan] = 0;
        return;
    }
    wChanActive[hSoundChan] = 1;
    wChanChainRow[hSoundChan] = 0xFF;
    wChanPhrase[hSoundChan] = SndGetChainPhrase(wChanChain[hSoundChan], 0);
}

/* $6EA7: live mode: a channel queued to start whose chain has ended starts
   now (wChanRestart) */

void SndLiveStartQueued(void)
{
    UBYTE *q;

    if (wLiveMode) {
        q = wChanLiveQueued;
        if (q[0] && !wChanActive[0]) {
            q[0] = 0;
            wChanRestart[0] = 1;
        }
        if (q[1] && !wChanActive[1]) {
            q[1] = 0;
            wChanRestart[1] = 1;
        }
        if (q[2] && !wChanActive[2]) {
            q[2] = 0;
            wChanRestart[2] = 1;
        }
        if (q[3] && !wChanActive[3]) {
            q[3] = 0;
            wChanRestart[3] = 1;
        }
    }
}

/* $6FB6: step the synth's frames: when the timer runs out, add the delta
   list's next step to the frame and reload the wave; at the list's end stop
   (play mode 1: a flat wave) or go back to the loop point */

void SndStepWavFrames(void)
{
    if (wWavFrameDeltaPtr) {
        if (!wWavFrameTimer) {
            if (!*wWavFrameDeltaPtr) {
                if (wWavPlayMode == 1) {
                    SndResetWavFlat();
                    return;
                }
                wWavFrameDeltaPtr = wWavFrameDeltaLoop;
            }
            if (*wWavFrameDeltaPtr) {
                wWavSynthFrame += *wWavFrameDeltaPtr;
                SndClearVoiceHram();
                if (wWavPlayMode == 4) {
                    _asm di _endasm;
                    LoadWaveRam();
                    _asm ei _endasm;
                } else
                    wWavUploadPending = 1;
                wWavFrameDeltaPtr++;
                wWavFrameTimer = wFxWaveParam2;
            }
        } else
            wWavFrameTimer--;
    }
}

/* $7034: speech: when the frame's ticks run out, play the word's next
   allophone (SRAM bank 0; 0 ends the word: a flat wave) on the kit voice and
   read the next frame's length */

void SndSpeechStep(void)
{
    UBYTE v, n;

    rRAMB = 0;
    if (!wSpeechFrameTicks) {
        hSoundChan = 2;
        _asm di _endasm;
        rRAMG = 0x0A;
        if (*wSpeechPtr) {
            v = *wSpeechPtr;
            rRAMG = 0;
            _asm ei _endasm;
            v--;
            wKit2Bank = v / 15 + 0x19;
            if (wKit2Bank > 0x1A)
                wKit2Bank += 5;
            v = v % 15;
            n = wChanNote[2] = v + 1;
            _asm di _endasm;
            wSpeechWord = 0;
            wKit2LoopEnabled = 0;
            InitKit2Sample(v);
            TriggerWav();
            wSpeechWord = 1;
            _asm ei _endasm;
            wChanDispNote[2] = n;
        } else {
            rRAMG = 0;
            _asm ei _endasm;
            SndResetWavFlat();
        }
        wSpeechPtr++;
        _asm di _endasm;
        rRAMG = 0x0A;
        wSpeechFrameTicks = *wSpeechPtr;
        rRAMG = 0;
        _asm ei _endasm;
        if (wSpeechFrameTicks)
            wSpeechPtr++;
    }
    wSpeechFrameTicks--;
}

/* $786F: live mode, at a chain's end: stop the channel if a stop is queued
   (and the sound once no channel plays), else start the queued song row;
   1 if neither, for the caller to carry on */

UBYTE SndLiveChainEnd(void)
{
    SndReadChainRow();
    if (wChanStopQueued[hSoundChan]) {
        SndDeactivateChan();
        SndResetChanFx();
        if (!SndAnyChanActive()) {
            NR52_REG = 0;
            wPlayMode = 0;
            SyncStopTransport();
        }
        return 0;
    }
    if (wChanNextSongRow[hSoundChan] != 0xFF) {
        SndEnterSongRow();
        return 0;
    }
    return 1;
}

/* $73B0: Z for command slot `slot`: repeat the last command that was not Z
   or H (wLastCmd[last]) with its value plus 0..each Z digit; returns the
   command */

UBYTE SndZRepeat(UBYTE slot, UBYTE last)
{
    wChanCmdVal[slot] = wLastCmdVal[last] + (SndRandomUpTo(wChanCmdVal[slot] >> 4) << 4)
                        + SndRandomUpTo(wChanCmdVal[slot] & 0x0F);
    wChanCmd[slot] = wLastCmd[last];
    return wChanCmd[slot];
}

/* $743E: Z in a phrase: this channel's last command */

UBYTE SndZPhrase(void)
{
    return SndZRepeat(hSoundChan, hSoundChan);
}

/* $744D: Z in a table's CMD1 column: that table's last CMD1 */

UBYTE SndZTableCmd1(void)
{
    return SndZRepeat(4, wChanTable[hSoundChan] + 4);
}

/* $746F: Z in a table's CMD2 column */

UBYTE SndZTableCmd2(void)
{
    return SndZRepeat(5, wChanTable[hSoundChan] + 0x24);
}

/* $7CFB: E at a note-on: on PU1, PU2 and NOI stop the fade and take the
   first digit as the envelope's volume (e = 5: the release runs as the note
   plays); on WAV set the output level from the value (e = 0) */

UBYTE FxCmd_E_NoteOn(void)
{
    UBYTE v;

    v = wChanCmdVal[hSoundChan];
    switch (hSoundChan) {
    case 0:
        wEnvStagePu1 = 0;
        wEnvSpeedPu1 = 0;
        break;
    case 1:
        wEnvStagePu2 = 0;
        wEnvSpeedPu2 = 0;
        break;
    case 2:
        v <<= 4;
        wWavLevel = ~(v << 1) + 1;
        return 0;
    case 3:
        wEnvStageNoi = 0;
        wEnvSpeedNoi = 0;
        break;
    }
    wChanEnv[hSoundChan] = v >> 4;
    return 5;
}
