/* Bank $02: the sequencer's per-channel step, in C (the sound engine's other
 * C is in Sound.c). The working pointers (wPtr*) are pointers here; Sound.c
 * declares their low bytes, which SoundAdvanceStatePtrs steps.
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundChan;         /* $FF8B */
extern UBYTE *wPtrNoteSrc;                  /* $C8ED: the channel's note cursor */
extern UBYTE *wPtrNoteCountdown;            /* $C8E3: the channel's ticks left in the step */
extern UBYTE *wPtrPhraseRow;                /* $C8E1 */
extern UBYTE *wPtrPhrase;                   /* $C8EB */
extern UBYTE wPlayRestartPending;           /* $C4CD */
extern UBYTE wLiveStartCol;                 /* $C571 */
extern UWORD wChanInstrPtr[];               /* $C140, [4]: SRAM cursors into the phrase's columns */
extern UWORD wPhraseCmdPtr[];               /* $C110, [4] */
extern UWORD wPhraseCmdValPtr[];            /* $C128, [4] */
extern UBYTE wTableHopDone;                 /* $C8F8 */
extern UBYTE wChanCmdVal[];                 /* $C36B, [6] */
extern UBYTE wChanNoteTrigger[];            /* $C0F0, [4] */
extern UBYTE wChanGrooveTicks[];            /* $C0FC, [4] */
extern UBYTE *wPtrChainRow;                 /* $C8DF */
extern UBYTE wChanHopCount[];               /* $C214, [4] */
extern UBYTE wSeqNoteLatch;                 /* $C8F7 */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChanChain[];                  /* $C170, [4] */
extern UBYTE wChanLiveQueued[];             /* $C300, [4] */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wKbdNoteQueued;                /* $CB92 */
extern UBYTE wKbdChanState[];               /* $CB96, [4] */
extern UBYTE wKbdNoteInjected;              /* $C8F9 */
extern UBYTE wKbdInstr;                /* $CB93 */
extern UBYTE wChanDelayTicks[];             /* $C178, [4] */
extern UBYTE wChanInstr[];                  /* $C210, [4] */
extern UBYTE wSoundUpdateFlags;               /* $C8F5 */
extern UBYTE wKitNoteNibbleMask;            /* $C8FA */

void SoundReadCursorByte(void);               /* asm */
void SoundAdvanceGroove(void);                /* asm */
void SoundLoadInstr(UBYTE n);            /* asm (takes it in a) */
void SoundLoadChanInstr(void);             /* asm */
void SoundTransposeNote(void);                /* asm */
UBYTE SoundCountdownStep(void);               /* asm */
UBYTE SoundReadChainEnd(void);                /* asm */
void SoundAdvanceTableCursor(void);           /* asm */
void SoundAdvanceChainRow(void);              /* asm */
void SoundLiveStartQueued(void);
void SoundSplitPhraseNibbles(void);           /* asm */
UBYTE SoundReadPhraseCmd(void);               /* asm */
UBYTE SoundPhraseHop(void);
UBYTE SoundRandom15(void);                    /* asm */
void SoundMaskLow5(UBYTE v);                  /* asm (takes it in a) */
void SoundInitPlayState(void);
void AdvanceCurChanCursors(UBYTE n);        /* asm (takes it in a) */
UBYTE SoundReadChainPhrase(void);                /* asm */
UBYTE SoundLiveChainEnd(void);
UBYTE SoundReadSongChain(void);                  /* asm */
void SoundAdvanceSongRow(void);                  /* asm */
void SoundQueueSyncByte(void);                /* asm */
void SoundResetChanFx(void);                  /* asm */
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
    v = wChanCmdVal[hSoundChan];
    if (v == 0xFF) {
        SoundInitPlayState();
        return 2;
    }
    if (wTableHopDone++)
        return 0;
    if (0xF0 & v) {                 /* literal first: the AND stays in c */
        p = &wChanHopCount[hSoundChan];
        if (!*p)
            *p = v >> 4;
        else if (!(*p = *p - 1))
            return 0;
        mode = 1;
    } else if (wPlayMode == 1 && wSongCol == hSoundChan)
        *wPtrPhrase = wPhrase;
    *wPtrPhraseRow = v & 0x0F;
    SoundSplitPhraseNibbles();
    wSeqNoteLatch = *wPtrPhraseRow;
    if (wSeqNoteLatch)
        AdvanceCurChanCursors(wSeqNoteLatch);
    switch (mode) {
    case 4:
        (*wPtrChainRow)++;
        if (wChanLiveQueued[hSoundChan] || SoundReadChainPhrase() == 0xFF || *wPtrChainRow == 0x10) {
            wChanLiveQueued[hSoundChan] = 0;
            *wPtrChainRow = 0;
            if (SoundLiveChainEnd()) {
                wChanSongRow[hSoundChan]++;
                wChanChain[hSoundChan] = SoundReadSongChain();
                SoundAdvanceSongRow();
            }
            SoundQueueSyncByte();
        }
        SoundLiveStartQueued();
        *wPtrPhrase = SoundReadChainPhrase();
        if (*wPtrPhrase == 0xFF) {
            wChanActive[hSoundChan] = 0;
            SoundResetChanFx();
        }
        SoundSplitPhraseNibbles();
        *wPtrPhraseRow = wSeqNoteLatch;
        if (wSeqNoteLatch)
            AdvanceCurChanCursors(wSeqNoteLatch);
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
            AdvanceCurChanCursors(wSeqNoteLatch);
        SoundReadChainTranspose();
        break;
    }
    return 1;
}

/* $748E: read the channel's next step: its note (or the PS/2 keyboard's), the
   groove's tick count less any D delay still owed, its instrument (or the
   keyboard's), then the transpose (a kit note on WAV is masked instead) */

void SoundReadChanStep(void)
{
    UBYTE t;

    rRAMB = 0;
    if (wKbdNoteQueued && wKbdChanState[hSoundChan]) {
        *wPtrNoteSrc = wKbdNoteQueued;
        wKbdNoteInjected = 1;
    } else
        SoundReadCursorByte();
    SoundAdvanceGroove();
    if (wChanDelayTicks[hSoundChan]) {
        if (*wPtrNoteCountdown > wChanDelayTicks[hSoundChan])
            t = wChanDelayTicks[hSoundChan];
        else
            t = *wPtrNoteCountdown - 1;
        wChanDelayTicks[hSoundChan] -= t;
        *wPtrNoteCountdown -= t;
    }
    if (wKbdNoteQueued && wKbdChanState[hSoundChan]) {
        SoundLoadInstr(wKbdInstr);
        wChanInstr[hSoundChan] = wKbdInstr;
    } else
        SoundLoadChanInstr();
    if (hSoundChan == 2 && wSoundUpdateFlags)
        *wPtrNoteSrc &= wKitNoteNibbleMask;
    else
        SoundTransposeNote();
}

/* $75C6: one tick of channel hSoundChan's sequencer: when its step's ticks
   are up and its chain goes on, move to the next phrase row (the next
   chain row after the phrase's last), then read the row's command and the
   step: B may mute the note, D delays it, G sets the groove, H hops */

void SoundUpdateChan(void)
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
            if (wSongCol == hSoundChan)
                *wPtrPhrase = wPhrase;
            SoundSplitPhraseNibbles();
            if (wLiveStartCol == hSoundChan) {
                wChanInstrPtr[hSoundChan] += wPhraseRow;
                wPhraseCmdPtr[hSoundChan] += wPhraseRow;
                wPhraseCmdValPtr[hSoundChan] += wPhraseRow;
            }
        }
    }
    wTableHopDone = 0;
    for (;;) {
        switch (SoundReadPhraseCmd()) {
        case 8:
            r = SoundPhraseHop();
            if (r == 1)
                continue;
            if (r == 2)
                return;
            break;
        case 2:
            v = wChanCmdVal[hSoundChan];
            if ((v >> 4) <= SoundRandom15())
                wKitNoteNibbleMask = 0x0F;
            if ((v & 0x0F) <= SoundRandom15())
                wKitNoteNibbleMask &= 0xF0;
            if (!wKitNoteNibbleMask) {
                SoundAdvanceGroove();
                wChanNoteTrigger[hSoundChan] = 0;
                return;
            }
            break;
        case 4:
            d = wChanCmdVal[hSoundChan];
            if (d) {
                wChanDelayTicks[hSoundChan] += d;
                wChanGrooveTicks[hSoundChan] = d;
                wChanNoteTrigger[hSoundChan] = 0;
                return;
            }
            break;
        case 7:
            SoundMaskLow5(wChanCmdVal[hSoundChan]);
        case 0:
            break;
        }
        break;
    }
    SoundReadChanStep();
}
