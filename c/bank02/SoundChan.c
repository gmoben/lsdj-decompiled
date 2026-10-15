/* Bank $02: the sequencer's per-channel step, in C (the sound engine's other
 * C is in Sound.c). The working pointers (wPtr*) are pointers here; Sound.c
 * declares their low bytes, which SndAdvanceStatePtrs steps.
 * Byte-identical to the ROM under the period compiler plus
 * tools/match/lsdj.peep. */
#define SOUND_BANK                          /* lsdj.h: our own far-call targets */
#include "lsdj.h"

extern volatile sfr UBYTE hSoundCh;         /* $FF8B */
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
extern UBYTE wSndUpdateFlags;               /* $C8F5 */
extern UBYTE wKitNoteNibbleMask;            /* $C8FA */

void SndReadCursorByte(void);               /* asm */
void SndAdvanceGroove(void);                /* asm */
void SndLoadInstr(UBYTE n);            /* asm (takes it in a) */
void SndLoadChanInstr(void);             /* asm */
void SndTransposeNote(void);                /* asm */
UBYTE SndCountdownStep(void);               /* asm */
UBYTE SndReadChainEnd(void);                /* asm */
void SndAdvanceTableCursor(void);           /* asm */
void SndAdvanceChainRow(void);              /* asm */
void SndLiveStartQueued(void);
void SndSplitPhraseNibbles(void);           /* asm */
UBYTE SndReadPhraseCmd(void);               /* asm */
UBYTE SndPhraseHop(void);
UBYTE SndRandom15(void);                    /* asm */
void SndMaskLow5(UBYTE v);                  /* asm (takes it in a) */
void SndInitPlayState(void);
void AdvanceCurChanCursors(UBYTE n);        /* asm (takes it in a) */
UBYTE ReadChainPhrase(void);                /* asm */
UBYTE SndLiveChainEnd(void);
UBYTE ReadSongChain(void);                  /* asm */
void AdvanceSongRow(void);                  /* asm */
void SndQueueSyncByte(void);                /* asm */
void SndResetChanFx(void);                  /* asm */
void SndReadChainTranspose(void);           /* asm */

/* $711F: H in a phrase, the hop: HFF stops the song; otherwise, once a tick,
   go to the low digit's row of this phrase (after the high digit's count of
   hops) or, playing a chain or the song, of the chain's next phrase; e = 0
   when no hop is taken, 2 when the song stopped, else 1 */

UBYTE SndPhraseHop(void)
{
    UBYTE mode, v;
    UBYTE *p;

    mode = wPlayMode;
    v = wChanCmdVal[hSoundCh];
    if (v == 0xFF) {
        SndInitPlayState();
        return 2;
    }
    if (wTableHopDone++)
        return 0;
    if (0xF0 & v) {                 /* literal first: the AND stays in c */
        p = &wChanHopCount[hSoundCh];
        if (!*p)
            *p = v >> 4;
        else if (!(*p = *p - 1))
            return 0;
        mode = 1;
    } else if (wPlayMode == 1 && wSongCol == hSoundCh)
        *wPtrPhrase = wPhrase;
    *wPtrPhraseRow = v & 0x0F;
    SndSplitPhraseNibbles();
    wSeqNoteLatch = *wPtrPhraseRow;
    if (wSeqNoteLatch)
        AdvanceCurChanCursors(wSeqNoteLatch);
    switch (mode) {
    case 4:
        (*wPtrChainRow)++;
        if (wChanLiveQueued[hSoundCh] || ReadChainPhrase() == 0xFF || *wPtrChainRow == 0x10) {
            wChanLiveQueued[hSoundCh] = 0;
            *wPtrChainRow = 0;
            if (SndLiveChainEnd()) {
                wChanSongRow[hSoundCh]++;
                wChanChain[hSoundCh] = ReadSongChain();
                AdvanceSongRow();
            }
            SndQueueSyncByte();
        }
        SndLiveStartQueued();
        *wPtrPhrase = ReadChainPhrase();
        if (*wPtrPhrase == 0xFF) {
            wChanActive[hSoundCh] = 0;
            SndResetChanFx();
        }
        SndSplitPhraseNibbles();
        *wPtrPhraseRow = wSeqNoteLatch;
        if (wSeqNoteLatch)
            AdvanceCurChanCursors(wSeqNoteLatch);
        SndReadChainTranspose();
        break;
    case 3:
        (*wPtrChainRow)++;
        if (ReadChainPhrase() == 0xFF || *wPtrChainRow == 0x10)
            *wPtrChainRow = 0;
        *wPtrPhrase = ReadChainPhrase();
        SndSplitPhraseNibbles();
        *wPtrPhraseRow = wSeqNoteLatch;
        if (wSeqNoteLatch)
            AdvanceCurChanCursors(wSeqNoteLatch);
        SndReadChainTranspose();
        break;
    }
    return 1;
}

/* $748E: read the channel's next step: its note (or the PS/2 keyboard's), the
   groove's tick count less any D delay still owed, its instrument (or the
   keyboard's), then the transpose (a kit note on WAV is masked instead) */

void SndReadChanStep(void)
{
    UBYTE t;

    rRAMB = 0;
    if (wKbdNoteQueued && wKbdChanState[hSoundCh]) {
        *wPtrNoteSrc = wKbdNoteQueued;
        wKbdNoteInjected = 1;
    } else
        SndReadCursorByte();
    SndAdvanceGroove();
    if (wChanDelayTicks[hSoundCh]) {
        if (*wPtrNoteCountdown > wChanDelayTicks[hSoundCh])
            t = wChanDelayTicks[hSoundCh];
        else
            t = *wPtrNoteCountdown - 1;
        wChanDelayTicks[hSoundCh] -= t;
        *wPtrNoteCountdown -= t;
    }
    if (wKbdNoteQueued && wKbdChanState[hSoundCh]) {
        SndLoadInstr(wKbdInstr);
        wChanInstr[hSoundCh] = wKbdInstr;
    } else
        SndLoadChanInstr();
    if (hSoundCh == 2 && wSndUpdateFlags)
        *wPtrNoteSrc &= wKitNoteNibbleMask;
    else
        SndTransposeNote();
}

/* $75C6: one tick of channel hSoundCh's sequencer: when its step's ticks
   are up and its chain goes on, move to the next phrase row (the next
   chain row after the phrase's last), then read the row's command and the
   step: B may mute the note, D delays it, G sets the groove, H hops */

void SndUpdateChan(void)
{
    UBYTE v, d, r;

    if (SndCountdownStep())
        return;
    if (!SndReadChainEnd())
        return;
    wKitNoteNibbleMask = 0xFF;
    SndAdvanceTableCursor();
    if (*wPtrPhraseRow == 0x10 || wPlayRestartPending) {
        *wPtrPhraseRow = 0;
        if (wPlayMode == 3 || wPlayMode == 4) {
            SndAdvanceChainRow();
            SndLiveStartQueued();
        } else {
            if (wSongCol == hSoundCh)
                *wPtrPhrase = wPhrase;
            SndSplitPhraseNibbles();
            if (wLiveStartCol == hSoundCh) {
                wChanInstrPtr[hSoundCh] += wPhraseRow;
                wPhraseCmdPtr[hSoundCh] += wPhraseRow;
                wPhraseCmdValPtr[hSoundCh] += wPhraseRow;
            }
        }
    }
    wTableHopDone = 0;
    for (;;) {
        switch (SndReadPhraseCmd()) {
        case 8:
            r = SndPhraseHop();
            if (r == 1)
                continue;
            if (r == 2)
                return;
            break;
        case 2:
            v = wChanCmdVal[hSoundCh];
            if ((v >> 4) <= SndRandom15())
                wKitNoteNibbleMask = 0x0F;
            if ((v & 0x0F) <= SndRandom15())
                wKitNoteNibbleMask &= 0xF0;
            if (!wKitNoteNibbleMask) {
                SndAdvanceGroove();
                wChanNoteTrigger[hSoundCh] = 0;
                return;
            }
            break;
        case 4:
            d = wChanCmdVal[hSoundCh];
            if (d) {
                wChanDelayTicks[hSoundCh] += d;
                wChanGrooveTicks[hSoundCh] = d;
                wChanNoteTrigger[hSoundCh] = 0;
                return;
            }
            break;
        case 7:
            SndMaskLow5(wChanCmdVal[hSoundCh]);
        case 0:
            break;
        }
        break;
    }
    SndReadChanStep();
}
