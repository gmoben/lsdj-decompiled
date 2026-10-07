/* Bank $02: the sound engine's C (the sequencer core around it is
 * hand-written asm).
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundCh;         /* $FF8B */
extern volatile sfr UBYTE hFxValue;         /* $FF8F */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE DIV_REG;          /* $FF04 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE hFxValueMem;                   /* hFxValue read as a plain global (ld hl; or a,(hl)) */
extern UBYTE wFxChannel;                    /* $C527: the FX slot (channel + 4 x column) */
extern UBYTE wFxSlotCmdTimer[];             /* $C230, [12] */
extern UBYTE wChanFxSlots[];                /* $C2F0, [12] */
extern UBYTE wWavLevel;                     /* $C8F3 */
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
UBYTE Random16(void);                       /* bank 0 (this file reads the low byte) */
UBYTE MulU8(UBYTE a, UBYTE b);              /* bank 0 */
void SetSyncSendRate(void);                 /* bank 7 */
void SndSetPlayRange(void);
void SndLiveQueueStart(UBYTE ch);
UBYTE SndGetChainPhrase(UBYTE chain, UBYTE row);
UBYTE SndRandomUpTo(UBYTE n);
void SndTableHop(UBYTE v);           /* asm */
void ApplyPitchBend(void);                  /* asm */
UBYTE SndAnyChannelActive(void);
void SndStartChannels(void);
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
void SndSetChannelEnable(void);
void SndResetFxState(void);
void SyncStartTransport(void);              /* bank 0 */
void SeedRandom16(UWORD seed);              /* bank 0 */
void SndStepAllChannels(void);
void SndRunChannelFxSlots(void);
void SndSetupWaveChannel(void);
void SndSpeechStep(void);
void SndStepWavFrames(void);
void SndUpdateAllChannels(void);
void SndRetrigNote(void);
void SelectKit2Sample(UBYTE n);             /* bank 0 */
void TriggerWave(void);                     /* asm */
void SndResetWavFlat(void);                 /* asm */
void SndReadChainRow(void);                 /* asm */
void SndDeactivateChannel(void);            /* asm */
void SndEnterSongRow(void);                 /* asm */
void SndClearVoiceHram(void);               /* asm */
void LoadWaveRam(void);                     /* bank 0, asm */
void TriggerPU1(void);                      /* asm */
void TriggerPU2(void);                      /* asm */
void TriggerNoise(void);                    /* asm */
void SndSetupLfo(void);                     /* asm */
UBYTE EnvReleaseSpeed(void);
UBYTE EnvReleaseTarget(void);
void SndClampNoteRange(UBYTE n);            /* asm */
void SndFxParsePitchVal(UBYTE *env);        /* asm */
void ReadTableEnvelope(void);               /* asm */
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
    return Random16() % (n + 1);
}

/* $649D: R, retrig (R8x: resync). The first digit changes the volume on each
   retrig; the second sets the rate in command-rate steps (1 fastest, F
   slowest, 0 once: stored as $0F) */

void FxCmd_R(void)
{
    UBYTE vol;
    UBYTE n;
    UBYTE k;

    vol = hFxValue & 0xF0;
    if (vol == 0x80) {
        n = hFxValue & 0x0F;
        n = MulU8(n, wChanCmdRate[hSoundCh] + 1);
        k = hSoundCh;
        wChanRetrigTimer[k] = ++n;
        wChanRetrigRate[k] = n & 0x0F;
    } else {
        n = (hFxValue - 1) & 0x0F;
        if (n != 0x0F)
            n = MulU8(n, wChanCmdRate[hSoundCh] + 1);
        wFxSlotRetrigRate[wFxChannel] = n;
        wChanFxSlots[wFxChannel] |= 2;
        wChanEnvVolOffset[wFxChannel] = vol;
        if (wFxChannel < 4)
            wFxSlotEnvTimer[wFxChannel] = 0;
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
    wChanFxSlots[hSoundCh] = 0;
    wChanFxSlots[hSoundCh + 4] = 0;
    wChanFxSlots[hSoundCh + 8] = 0;
    wChanPitchRate[hSoundCh] = 0;
    ch = hSoundCh;
    wChanTable[ch] = 0xFF;
    wChanDispNote[ch] = 0;
    wChanChord[ch] = 0;
    wChanVibShape[ch] = 0;
}

/* $65F3: K, kill the note: now (K00) or after the value's ticks, one fewer
   from a table column */

void FxCmd_K(void)
{
    if (hFxValueMem) {
        wFxSlotCmdTimer[wFxChannel] = hFxValue - (wFxChannel > 3);
        wChanFxSlots[wFxChannel] |= 1;
    } else
        SndKillNote();
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

/* $66E7: R fires on FX slot wFxChannel and the note retriggers: a slide
   pending on a pulse or the wave channel restarts and the envelope is parsed
   again (the wave's level steps by R's volume digit instead); a phrase slot
   restarts its channel's latched table too. Then the slot reloads its timer
   from its rate, or ends (rate $0F: once) */

void SndRetrigNote(void)
{
    UBYTE rate;

    hSoundCh = wFxChannel & 3;
    if (!wSpeechWord || hSoundCh != 2) {
        switch (hSoundCh) {
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
            SndWavLevelStep(wChanEnvVolOffset[wFxChannel]);
            break;
        case 3:
            SndFxParsePitchVal(&wEnvNode1Noi);
            break;
        }
        if (wFxChannel < 4 && wChanTableActive[hSoundCh]) {
            wChanTable[hSoundCh] = wChanTableLatch[hSoundCh];
            wChanTableRow1[hSoundCh + 4] = 0xFF;
            wChanTableRow1[hSoundCh] = 0xFF;
            wChanGrooveRow[hSoundCh + 4] = 0xFF;
            wChanNoteCountdown[hSoundCh + 4] = 0;
            ReadTableEnvelope();
            ReadTableTranspose();
            wChanTableFresh[hSoundCh] = 1;
        }
    }
    if (hSoundCh == 3)
        wChanSweep[hSoundCh] = wNoiseNoteLatch;
    TriggerNote();
    if (hSoundCh == 2) {
        wKitVoicesActive = wSndUpdateFlags;
        SndSetKitParams();
    }
    rate = wFxSlotRetrigRate[wFxChannel];
    if (rate == 0x0F)
        wChanFxSlots[wFxChannel] = 0;
    else
        wFxSlotEnvTimer[wFxChannel] = rate;
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
    return EnvReleaseSpeeds[hFxValue & 7];
}

/* $6685: a delayed K's slot ran out: kill that channel's note */

void SndRunFxSlot(void)
{
    hSoundCh = wFxChannel & 3;
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
    return (hFxValue & 8) ? 0x0F : 0;
}

/* $69F2: E on PU1: restart the envelope (retriggering the note when its
   length bit is set), then fade at the release digit's speed */

void FxCmd_E_Pu1(void)
{
    UBYTE v;

    if (wChanFreqHi[0] & 0x40)
        TriggerPU1();
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
        TriggerPU2();
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
        TriggerNoise();
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
        if (wCurrentScreen == 3 || wCurrentScreen == 1) {
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
        hSoundCh = ch;
        SndStartChanFromRow();
        k = hSoundCh;
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
    if (wCurrentScreen == 4 && wCursorMode == 4) {
        if (wSelLeft < wSongCol)
            wPlayChanFirst = wSelLeft;
        else
            wPlayChanLast = wSelLeft;
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

void SndStartChannels(void)
{
    wPlayMode = 0;
    SndSetPlayRange();
    for (hSoundCh = wPlayChanFirst; hSoundCh < wPlayChanLast + 1; hSoundCh++) {
        wChanNextSongRow[hSoundCh] = wSongRow;
        SndStartChanFromRow();
    }
    if (SndAnyChannelActive()) {
        MemSet(wChanStopQueued, 0, 4);
        MemSet(wChanNextSongRow, 0xFF, 4);
        SndSetChannelEnable();
        SndResetFxState();
        SndEnableLcdInts();
        SyncStartTransport();
        wPlayMode = 4;
    }
}

/* $6E0F */

void SndStartRange(void)
{
    if (SndAnyChannelActive())
        SndLiveQueueRange();
    else
        SndStartChannels();
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
    wPlayMode = wCurrentScreen;
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
    if ((v & 0xF0) > Random16()) {
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
        SeedRandom16(DIV_REG);
    } else {
        SndStepAllChannels();
        SndRunChannelFxSlots();
        SndSetupWaveChannel();
        if (wSpeechWord && wSpeechFrameTicks != 0xFF)
            SndSpeechStep();
        if (wWavPlayMode && wChanActive[2])
            SndStepWavFrames();
    }
    SndUpdateAllChannels();
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
    if (wCurrentScreen == 4)
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
    wSavedScreen = wCurrentScreen;
    wCurrentScreen = 4;
    TogglePlayback();
    wCurrentScreen = wSavedScreen;
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
    for (hSoundCh = 0; hSoundCh < 4; hSoundCh++) {
        chain = &wChanChain[hSoundCh];
        active = &wChanActive[hSoundCh];
        phrase = &wChanPhrase[hSoundCh];
        *active = 1;
        wChanSongRow[hSoundCh] = wSongRow;
        *chain = ReadSeqByte(hSoundCh, wSongRow);
        if (*chain & 0x80) {
            row = wSongRow;
            while ((ReadSeqByte(hSoundCh, row) & 0x80) && row)
                row--;
            wChanSongRow[hSoundCh] = row;
            *chain = ReadSeqByte(hSoundCh, row);
            if (*chain & 0x80) {
                *active = 0;
                continue;
            }
        }
        wChanChainRow[hSoundCh] = 0xFF;
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
            wChanChainRow[hSoundCh] = k - 1;
            *phrase = SndGetChainPhrase(*chain, k);
        }
    }
}

/* $7B31: start the current screen's playback: the song from the cursor row
   (StartSongPlayback), the chain from the cursor's chain row, or the phrase */

void StartScreenPlayback(void)
{
    if (wSyncMode == 1 && wSyncTick == 0xFF) {
        wSyncRun = wCurrentScreen == 4;
        SyncSetSendMode();
    }
    if (wButtons & 0x80)
        wSyncStartRow = wSongRow;
    else
        wSongRow = wSyncStartRow;
    SndSetChannelEnable();
    SndResetFxState();
    switch (wCurrentScreen) {
    case 4:
        StartSongPlayback();
        break;
    case 3:
        wChanActive[wSongCol] = 1;
        wPlayStartChan = wSongCol;
        wPlayStartRow = wSongRow;
        hSoundCh = wSongCol;
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
        hSoundCh = wSongCol;
        wChanPhrase[wSongCol] = wPhrase;
        break;
    }
    wPlayMode = wCurrentScreen;
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
    for (hSoundCh = 0; hSoundCh < 4; hSoundCh++)
        if (wChanActive[hSoundCh])
            SndResetChanFx();
    MemSet(wChanActive, 0, 8);
    MemSet(wChanFxSlots, 0, 12);
    MemSet(wChanDispNote, 0, 4);
    MemSet(wChanNextSongRow, 0xFF, 4);
    ClearSprites012();
    if (mode != wCurrentScreen)
        StartScreenPlayback();
    else
        SyncStopTransport();
    wSongRow = row;
}

/* $6BBA: start channel hSoundCh at its next song row (wChanNextSongRow, $FF
   none): phrase and table cursors reset, the row walked back past empty
   ($80+) chains to the last real one, then that chain's first phrase -- or
   the channel stops when the song has none */

void SndStartChanFromRow(void)
{
    if (wChanNextSongRow[hSoundCh] == 0xFF)
        return;
    wChanNoteCountdown[hSoundCh] = 0;
    wChanGrooveRow[hSoundCh] = 0xFF;
    wChanTableRow1[hSoundCh] = 0xFF;
    wChanTable[hSoundCh] = 0xFF;
    wChanPhraseRow[hSoundCh] = 0xFF;
    wChanHopCount[hSoundCh] = 0;
    wChanNoteCountdown[hSoundCh + 4] = 0;
    wChanGroove[hSoundCh + 4] = 0;
    wChanGrooveRow[hSoundCh + 4] = 0xFF;
    wChanTableRow1[hSoundCh + 4] = 0xFF;
    wChanSongRow[hSoundCh] = wChanNextSongRow[hSoundCh];
    wChanChain[hSoundCh] = ReadSeqByte(hSoundCh, wChanNextSongRow[hSoundCh]);
    while ((wChanChain[hSoundCh] & 0x80) && wChanSongRow[hSoundCh]) {
        wChanSongRow[hSoundCh]--;
        wChanChain[hSoundCh] = ReadSeqByte(hSoundCh, wChanSongRow[hSoundCh]);
    }
    if (wChanChain[hSoundCh] & 0x80) {
        wChanActive[hSoundCh] = 0;
        return;
    }
    wChanActive[hSoundCh] = 1;
    wChanChainRow[hSoundCh] = 0xFF;
    wChanPhrase[hSoundCh] = SndGetChainPhrase(wChanChain[hSoundCh], 0);
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
        hSoundCh = 2;
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
            SelectKit2Sample(v);
            TriggerWave();
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
    if (wChanStopQueued[hSoundCh]) {
        SndDeactivateChannel();
        SndResetChanFx();
        if (!SndAnyChannelActive()) {
            NR52_REG = 0;
            wPlayMode = 0;
            SyncStopTransport();
        }
        return 0;
    }
    if (wChanNextSongRow[hSoundCh] != 0xFF) {
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
    return SndZRepeat(hSoundCh, hSoundCh);
}

/* $744D: Z in a table's CMD1 column: that table's last CMD1 */

UBYTE SndZTableCmd1(void)
{
    return SndZRepeat(4, wChanTable[hSoundCh] + 4);
}

/* $746F: Z in a table's CMD2 column */

UBYTE SndZTableCmd2(void)
{
    return SndZRepeat(5, wChanTable[hSoundCh] + 0x24);
}

/* $7CFB: E at a note-on: on PU1, PU2 and NOI stop the fade and take the
   first digit as the envelope's volume (e = 5: the release runs as the note
   plays); on WAV set the output level from the value (e = 0) */

UBYTE FxCmd_E_NoteOn(void)
{
    UBYTE v;

    v = wChanCmdVal[hSoundCh];
    switch (hSoundCh) {
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
    wChanEnv[hSoundCh] = v >> 4;
    return 5;
}
