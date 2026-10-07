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
void SndSyncStart(void);
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
void SndResetPitchState(void);
void SelectKit2Sample(UBYTE n);             /* bank 0 */
void TriggerWave(void);                     /* asm */
void SndResetWavFlat(void);                 /* asm */
void SndReadChainRow(void);                 /* asm */
void SndDeactivateChannel(void);            /* asm */
void SndEnterSongRow(void);                 /* asm */
void SndClearVoiceHram(void);               /* asm */
void LoadWaveRam(void);                     /* bank 0, asm */

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
            SndResetPitchState();
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

/* $7B31: start the current screen's playback: the song from the cursor row
   (SndSyncStart), the chain from the cursor's chain row, or the phrase */

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
        SndSyncStart();
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
