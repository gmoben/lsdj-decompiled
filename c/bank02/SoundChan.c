/* Bank $02: the sequencer's per-channel step, in C (the sound engine's other
 * C is in Sound.c). The working pointers (wPtr*) are pointers here; Sound.c
 * declares their low bytes, which SoundAdvanceStatePtrs steps.
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern UBYTE *wPtrNoteSrc;                  /* $C8ED: the channel's note cursor */
extern UBYTE *wPtrNoteCountdown;            /* $C8E3: the channel's ticks left in the step */
extern UBYTE *wPtrPhraseRow;                /* $C8E1 */
extern UBYTE *wPtrPhrase;                   /* $C8EB */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UWORD wChannelInstrumentPtr[];               /* $C140, [4]: SRAM cursors into the phrase's columns */
extern UWORD wPhraseCommandPtr[];               /* $C110, [4] */
extern UWORD wPhraseCommandValPtr[];            /* $C128, [4] */
extern UBYTE wTableHopDone;                 /* $C8F8 */
extern UBYTE wChannelCommandVal[];                 /* $C36B, [6] */
extern UBYTE wChannelNoteTrigger[];            /* $C0F0, [4] */
extern UBYTE wChannelGrooveTicks[];            /* $C0FC, [4] */
extern UBYTE *wPtrChainRow;                 /* $C8DF */
extern UBYTE wChannelHopCount[];               /* $C214, [4] */
extern UBYTE wSeqNoteLatch;                 /* $C8F7 */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChannelChain[];                  /* $C170, [4] */
extern UBYTE wChannelLiveQueued[];             /* $C300, [4] */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wKeyboardNoteQueued;                /* $CB92 */
extern UBYTE wKeyboardChannelState[];               /* $CB96, [4] */
extern UBYTE wKeyboardNoteInjected;              /* $C8F9 */
extern UBYTE wKeyboardInstrument;                /* $CB93 */
extern UBYTE wChannelDelayTicks[];             /* $C178, [4] */
extern UBYTE wChannelInstrument[];                  /* $C210, [4] */
extern UBYTE wSoundUpdateFlags;               /* $C8F5 */
extern UBYTE wKitNoteNibbleMask;            /* $C8FA */

void SoundReadCursorByte(void);               /* asm */
void SoundAdvanceGroove(void);                /* asm */
void SoundLoadInstrument(UBYTE n);            /* asm (takes it in a) */
void SoundLoadChannelInstrument(void);             /* asm */
void SoundTransposeNote(void);                /* asm */
UBYTE SoundCountdownStep(void);               /* asm */
UBYTE SoundReadChainEnd(void);                /* asm */
void SoundAdvanceTableCursor(void);           /* asm */
void SoundAdvanceChainRow(void);              /* asm */
void SoundLiveStartQueued(void);
void SoundSplitPhraseNibbles(void);           /* asm */
UBYTE SoundReadPhraseCommand(void);               /* asm */
UBYTE SoundPhraseHop(void);
UBYTE SoundRandom15(void);                    /* asm */
void SoundMaskLow5(UBYTE v);                  /* asm (takes it in a) */
void SoundInitPlayState(void);
void AdvanceCurrentChannelCursors(UBYTE n);        /* asm (takes it in a) */
UBYTE SoundReadChainPhrase(void);                /* asm */
UBYTE SoundLiveChainEnd(void);
UBYTE SoundReadSongChain(void);                  /* asm */
void SoundAdvanceSongRow(void);                  /* asm */
void SoundQueueSyncByte(void);                /* asm */
void SoundResetChannelCommand(void);                  /* asm */
void SoundReadChainTranspose(void);           /* asm */

/* $711F: H in a phrase, the hop: HFF stops the song; otherwise, once a tick,
   go to the low digit's row of this phrase (after the high digit's count of
   hops) or, playing a chain or the song, of the chain's next phrase; e = 0
   when no hop is taken, 2 when the song stopped, else 1 */

UBYTE SoundPhraseHop(void)
{
    UBYTE mode, v;
    UBYTE *p;

    mode = wPlayMode;
    v = wChannelCommandVal[hSoundChannel];
    if (v == 0xFF) {
        SoundInitPlayState();
        return 2;
    }
    if (wTableHopDone++)
        return 0;
    if (0xF0 & v) {                 /* literal first: the AND stays in c */
        p = &wChannelHopCount[hSoundChannel];
        if (!*p)
            *p = v >> 4;
        else if (!(*p = *p - 1))
            return 0;
        mode = 1;
    } else if (wPlayMode == 1 && wSongColumn == hSoundChannel)
        *wPtrPhrase = wPhrase;
    *wPtrPhraseRow = v & 0x0F;
    SoundSplitPhraseNibbles();
    wSeqNoteLatch = *wPtrPhraseRow;
    if (wSeqNoteLatch)
        AdvanceCurrentChannelCursors(wSeqNoteLatch);
    switch (mode) {
    case 4:
        (*wPtrChainRow)++;
        if (wChannelLiveQueued[hSoundChannel] || SoundReadChainPhrase() == 0xFF || *wPtrChainRow == 0x10) {
            wChannelLiveQueued[hSoundChannel] = 0;
            *wPtrChainRow = 0;
            if (SoundLiveChainEnd()) {
                wChannelSongRow[hSoundChannel]++;
                wChannelChain[hSoundChannel] = SoundReadSongChain();
                SoundAdvanceSongRow();
            }
            SoundQueueSyncByte();
        }
        SoundLiveStartQueued();
        *wPtrPhrase = SoundReadChainPhrase();
        if (*wPtrPhrase == 0xFF) {
            wChannelActive[hSoundChannel] = 0;
            SoundResetChannelCommand();
        }
        SoundSplitPhraseNibbles();
        *wPtrPhraseRow = wSeqNoteLatch;
        if (wSeqNoteLatch)
            AdvanceCurrentChannelCursors(wSeqNoteLatch);
        SoundReadChainTranspose();
        break;
    case 3:
        (*wPtrChainRow)++;
        if (SoundReadChainPhrase() == 0xFF || *wPtrChainRow == 0x10)
            *wPtrChainRow = 0;
        *wPtrPhrase = SoundReadChainPhrase();
        SoundSplitPhraseNibbles();
        *wPtrPhraseRow = wSeqNoteLatch;
        if (wSeqNoteLatch)
            AdvanceCurrentChannelCursors(wSeqNoteLatch);
        SoundReadChainTranspose();
        break;
    }
    return 1;
}

/* $748E: read the channel's next step: its note (or the PS/2 keyboard's), the
   groove's tick count less any D delay still owed, its instrument (or the
   keyboard's), then the transpose (a kit note on WAV is masked instead) */

void SoundReadChannelStep(void)
{
    UBYTE t;

    rRAMB = 0;
    if (wKeyboardNoteQueued && wKeyboardChannelState[hSoundChannel]) {
        *wPtrNoteSrc = wKeyboardNoteQueued;
        wKeyboardNoteInjected = 1;
    } else
        SoundReadCursorByte();
    SoundAdvanceGroove();
    if (wChannelDelayTicks[hSoundChannel]) {
        if (*wPtrNoteCountdown > wChannelDelayTicks[hSoundChannel])
            t = wChannelDelayTicks[hSoundChannel];
        else
            t = *wPtrNoteCountdown - 1;
        wChannelDelayTicks[hSoundChannel] -= t;
        *wPtrNoteCountdown -= t;
    }
    if (wKeyboardNoteQueued && wKeyboardChannelState[hSoundChannel]) {
        SoundLoadInstrument(wKeyboardInstrument);
        wChannelInstrument[hSoundChannel] = wKeyboardInstrument;
    } else
        SoundLoadChannelInstrument();
    if (hSoundChannel == 2 && wSoundUpdateFlags)
        *wPtrNoteSrc &= wKitNoteNibbleMask;
    else
        SoundTransposeNote();
}

/* $75C6: one tick of channel hSoundChannel's sequencer: when its step's ticks
   are up and its chain goes on, move to the next phrase row (the next
   chain row after the phrase's last), then read the row's command and the
   step: B may mute the note, D delays it, G sets the groove, H hops */

void SoundUpdateChannel(void)
{
    UBYTE v, d, r;

    if (SoundCountdownStep())
        return;
    if (!SoundReadChainEnd())
        return;
    wKitNoteNibbleMask = 0xFF;
    SoundAdvanceTableCursor();
    if (*wPtrPhraseRow == 0x10 || wPlayRestartPending) {
        *wPtrPhraseRow = 0;
        if (wPlayMode == 3 || wPlayMode == 4) {
            SoundAdvanceChainRow();
            SoundLiveStartQueued();
        } else {
            if (wSongColumn == hSoundChannel)
                *wPtrPhrase = wPhrase;
            SoundSplitPhraseNibbles();
            if (wLiveStartColumn == hSoundChannel) {
                wChannelInstrumentPtr[hSoundChannel] += wPhraseRow;
                wPhraseCommandPtr[hSoundChannel] += wPhraseRow;
                wPhraseCommandValPtr[hSoundChannel] += wPhraseRow;
            }
        }
    }
    wTableHopDone = 0;
    for (;;) {
        switch (SoundReadPhraseCommand()) {
        case 8:
            r = SoundPhraseHop();
            if (r == 1)
                continue;
            if (r == 2)
                return;
            break;
        case 2:
            v = wChannelCommandVal[hSoundChannel];
            if ((v >> 4) <= SoundRandom15())
                wKitNoteNibbleMask = 0x0F;
            if ((v & 0x0F) <= SoundRandom15())
                wKitNoteNibbleMask &= 0xF0;
            if (!wKitNoteNibbleMask) {
                SoundAdvanceGroove();
                wChannelNoteTrigger[hSoundChannel] = 0;
                return;
            }
            break;
        case 4:
            d = wChannelCommandVal[hSoundChannel];
            if (d) {
                wChannelDelayTicks[hSoundChannel] += d;
                wChannelGrooveTicks[hSoundChannel] = d;
                wChannelNoteTrigger[hSoundChannel] = 0;
                return;
            }
            break;
        case 7:
            SoundMaskLow5(wChannelCommandVal[hSoundChannel]);
        case 0:
            break;
        }
        break;
    }
    SoundReadChannelStep();
}
