/* PARKED (c/parked.txt): SndPhraseHop, 3 bytes from the ROM -- see there.
 * Bank $02: the sequencer's per-channel step, in C (the sound engine's other
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
extern UBYTE wKbdInstrument;                /* $CB93 */
extern UBYTE wChanDelayTicks[];             /* $C178, [4] */
extern UBYTE wChanInstr[];                  /* $C210, [4] */
extern UBYTE wSndUpdateFlags;               /* $C8F5 */
extern UBYTE wKitNoteNibbleMask;            /* $C8FA */

void SndReadCursorByte(void);               /* asm */
void SndAdvanceGroove(void);                /* asm */
void SndLoadInstrument(UBYTE n);            /* asm (takes it in a) */
void SndLoadChannelInstr(void);             /* asm */
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
    if (v & 0xF0) {
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
