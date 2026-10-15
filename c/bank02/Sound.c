/*
 * The sound engine's tick -- LSDj 9.4.0, ROM bank $02.
 *
 * The engine tick, the state pointers' advance, a chain's phrase lookup,
 * the synth's frame stepping and the speech player's step.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern volatile sfr UBYTE DIV_REG;          /* $FF04 */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wSyncTransmitByte;                   /* $CBDC */
extern UBYTE wSpeechWord;                   /* $C4CE */
extern UBYTE wSpeechFrameTicks;             /* $C4CF */
extern UBYTE wWavPlayMode;                  /* $C500 */
extern UBYTE wKeyboardNoteInjected;              /* $C8F9 */
extern UBYTE wKeyboardNoteQueued;                /* $CB92 */
extern UBYTE wChannelDisplayNote[];               /* $C41F, [4] */
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
extern UBYTE wPointerChainRow, wPointerPhraseRow, wPointerNoteCountdown, wPointerGroove;  /* $C8DF.. */
extern UBYTE wPointerGrooveRow, wPointerPhrase, wPointerNoteSource;                       /* ..$C8ED */
UBYTE SoundGetChainPhrase(UBYTE chain, UBYTE row);
void initrand(UWORD seed);              /* bank 0 */
void SoundStepAllChannels(void);
void SoundRunChannelCommandSlots(void);
void SoundSetupWaveChannel(void);
void SoundSpeechStep(void);
void SoundStepWavFrames(void);
void SoundUpdateAllChannels(void);
void SoundInitializeKit2Sample(UBYTE n);             /* bank 0 */
void SoundTriggerWav(void);                     /* asm */
void SoundResetWavFlat(void);                 /* asm */
void SoundClearVoiceHram(void);               /* asm */
void SoundLoadWaveRam(void);                     /* bank 0, asm */

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
            SoundInitializeKit2Sample(v);
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
