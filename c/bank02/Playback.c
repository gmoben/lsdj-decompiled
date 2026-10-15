/*
 * Playback and live mode -- LSDj 9.4.0, ROM bank $02.
 *
 * Starting and stopping the song or a screen's playback, the play range,
 * live mode's queued starts and stops and its chain end.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE wChannelCommandSlots[];                /* $C2F0, [12] */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelStopQueued[];                /* $C304, [4] */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wChannelTable[];                  /* $C204, [4] */
extern UBYTE wChannelDisplayNote[];               /* $C41F, [4] */
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
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wPlayChannelFirst;                /* $C8DD */
extern UBYTE wPlayChannelLast;                 /* $C8DE */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChannelRestart[];                /* $C0E4, [4] */
void SetInterruptEnable(UBYTE interruptMask);          /* bank 0 */
void SoundSetPlayRange(void);
void SoundLiveQueueStart(UBYTE channel);
UBYTE SoundGetChainPhrase(UBYTE chain, UBYTE row);
UBYTE SoundAnyChannelActive(void);
void SoundStartChannels(void);
void SoundTogglePlayback(void);
void SoundEnableLcdInterrupts(void);
void SoundLiveStopRange(void);
void SoundStartScreenPlayback(void);
void SyncStopTransport(void);               /* bank 0 */
void SyncSetSendMode(void);                 /* bank 0 */
void SoundStartSongPlayback(void);
void SoundResetChannelCommand(void);                  /* asm */
void SoundStartChannelFromRow(void);
void SoundSetChannelEnable(void);
void SoundResetCommandState(void);
void SyncStartTransport(void);              /* bank 0 */
void SoundReadChainRow(void);                 /* asm */
void SoundDeactivateChannel(void);            /* asm */
void SoundEnterSongRow(void);                 /* asm */

/* $6A94: live mode, queue channel ch to start the song row's chain when its
   current one ends; pressed again on the same row (the chain or phrase
   screen), start it at the edited chain row, sooner (wChannelLiveQueued); with
   a sync restart pending, start it now */

void SoundLiveQueueStart(UBYTE channel)
{
    UBYTE *p;
    UBYTE k;

    p = wChannelNextSongRow + channel;
    if (wSongRow == *p) {
        if (wCurrentScreen == SCREEN_CHAIN || wCurrentScreen == SCREEN_PHRASE) {
            if (SoundGetChainPhrase(wChain, wChainRow) == 0xFF)
                return;
            wChannelRowQueue[channel] = wChainRow;
        } else
            wChannelRowQueue[channel] = 0;
        wChannelLiveQueued[channel] = 1;
    } else {
        *p = wSongRow;
        wChannelLiveQueued[channel] = 0;
    }
    wChannelStopQueued[channel] = 0;
    if (wPlayRestartPending) {
        hSoundChannel = channel;
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
    if (wCurrentScreen == SCREEN_SONG && wCursorMode == CURSOR_SELECTING) {
        if (wSelectionLeft < wSongColumn)
            wPlayChannelFirst = wSelectionLeft;
        else
            wPlayChannelLast = wSelectionLeft;
    }
}

/* $6B9B: advance the retrigger of every playing channel */

void SoundLiveQueueRange(void)
{
    UBYTE channel;

    SoundSetPlayRange();
    for (channel = wPlayChannelFirst; channel <= wPlayChannelLast; channel++)
        SoundLiveQueueStart(channel);
}

/* $6D97: start the channels in the play range from the song row */

void SoundStartChannels(void)
{
    wPlayMode = PLAY_STOPPED;
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
        wPlayMode = PLAY_SONG;
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
    UBYTE channel;
    UBYTE *p;

    SoundSetPlayRange();
    for (channel = wPlayChannelFirst; channel <= wPlayChannelLast; channel++) {
        p = wChannelNextSongRow + channel;
        if (*p != 0xFF)
            *p = 0xFF;
        if (wChannelActive[channel]) {
            p = wChannelStopQueued + channel;
            if (!*p) {
                *p = 1;
                wChannelLiveQueued[channel] = 0;
            } else
                wChannelLiveQueued[channel] = 1;
        }
    }
}

/* $70FB: enable the LCD-STAT interrupt (and the serial one if syncing) */

void SoundEnableLcdInterrupts(void)
{
    SetInterruptEnable(IE_REG | 0x04 | wSyncSerialIeMask);
}

/* $710E: play the current screen's scope from scratch */

void SoundInitializePlayState(void)
{
    wPlayMode = wCurrentScreen;
    wLiveMode = 0;
    SoundTogglePlayback();
}

/* $78C4: stop playback in both contexts, the screen's and the song's */

void SoundToggleSongPlayback(void)
{
    if (wCurrentScreen == SCREEN_SONG)
        DebugTrap();
    if (wLiveMode) {
        SoundLiveStopRange();
        return;
    }
    if (wSyncMode == SYNC_LSDJ || wSyncMode == SYNC_MIDI)
        wSyncTick = 0xFF;
    if (wPlayMode && wPlayMode != PLAY_SONG)
        SoundTogglePlayback();
    if (wSavedScreen)
        DebugTrap();
    wSavedScreen = wCurrentScreen;
    wCurrentScreen = SCREEN_SONG;
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

    resume = wChainRow && (wSavedScreen && wSavedScreen != SCREEN_SONG)
            && (wSyncMode == SYNC_OFF || wSyncMode == SYNC_KEYBOARD);
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
        if (*phrase == EMPTY) {
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
    if (wSyncMode == SYNC_LSDJ && wSyncTick == 0xFF) {
        wSyncRun = wCurrentScreen == SCREEN_SONG;
        SyncSetSendMode();
    }
    if (wButtons & J_START)
        wSyncStartRow = wSongRow;
    else
        wSongRow = wSyncStartRow;
    SoundSetChannelEnable();
    SoundResetCommandState();
    switch (wCurrentScreen) {
    case SCREEN_SONG:
        SoundStartSongPlayback();
        break;
    case SCREEN_CHAIN:
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
    wPlayMode = PLAY_STOPPED;
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
            wPlayMode = PLAY_STOPPED;
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
