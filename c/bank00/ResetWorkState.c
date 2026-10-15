/*
 * ResetWorkState -- LSDj 9.4.0, ROM bank $00 ($3A33).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies it against the ROM.
 *
 * The work-RAM reset InitAndLoadSong runs before a song is loaded: the four
 * channels' sequencer state, the editors' cursors, the clipboard caches and
 * the per-channel pointers the sound driver indexes through.
 *
 * The pointers are set with `&wChanChainRow[0]`, not `wChanChainRow`: four
 * of the arrays were indexed above, and the plain name would reuse that
 * `&array` temporary (CSE) and store the address as two literal bytes, where
 * the ROM loads it into de for every one of the nine.
 */

#include "lsdj.h"

extern UBYTE wChanOutput[];                 /* $C0A0, [4] */
extern UBYTE wChanStepCountdown[];          /* $C0C4, [4] */
extern UBYTE wChanNote[];                   /* $C0CC, [4] */
extern UWORD wPhraseCmdPtr[];               /* $C110, [4] */
extern UWORD wTableCmd1Ptr[];               /* $C118, [4] */
extern UWORD wTableCmd2Ptr[];               /* $C120, [4] */
extern UBYTE wChanGroove[];                 /* $C150, [4] */
extern UBYTE wChanTablePlayed[];            /* $C154, [4] */
extern UBYTE wChanGrooveRow[];              /* $C158, [8] */
extern UBYTE wChanNoteCountdown[];          /* $C160, [4] */
extern UBYTE wChanPhrase[];                 /* $C168, [4] */
extern UBYTE wChanPhraseRow[];              /* $C16C, [4] */
extern UBYTE wChanChainRow[];               /* $C17C, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChanTable[];                  /* $C204, [4] */
extern UBYTE wChanTableRow1[];              /* $C208, [4] */
extern UBYTE wChanTableRow2[];              /* $C20C, [4] */
extern UBYTE wChanInstr[];                  /* $C210, [4] */
extern UBYTE wChanNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChanTableStep[];              /* $C33F, [4] */
extern UBYTE wChanDispNote[];               /* $C41F, [4] */
extern UBYTE wColLastCmd[];                 /* $C44B, [4] */
extern UBYTE wColChain[];                   /* $C44F, [4] */
extern UBYTE wColPhrase[];                  /* $C453, [4] */
extern UBYTE wPhraseColLast[];              /* $C907, [4] */
extern UBYTE wWordClipCell0[];              /* $C935, [2] */
extern UBYTE wInstrRemapTable[];            /* $C9B8, [16] */
extern UBYTE *wPtrChainRow;                 /* $C8DF */
extern UBYTE *wPtrPhraseRow;                /* $C8E1 */
extern UBYTE *wPtrNoteCountdown;            /* $C8E3 */
extern UBYTE *wPtrGroove;                   /* $C8E5 */
extern UBYTE *wPtrGrooveRow;                /* $C8E7 */
extern UBYTE *wPtrPhrase;                   /* $C8EB */
extern UBYTE *wPtrNoteSrc;                  /* $C8ED */
extern UBYTE *wPtrStepCountdown;            /* $C8EF */
extern UBYTE *wPtrTableStep;                /* $C8FC */
extern UBYTE wChainScopeDirty;              /* $C403 */
extern UBYTE wSongScopeDirty;               /* $C404 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wPhraseCol;                    /* $C415 */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainCol;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wTable;                        /* $C41B */
extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wInstrCol;               /* $C428 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wWaveCursor;                   /* $C42B */
extern UBYTE wNameEditCol;                 /* $C42D */
extern UBYTE wNameEditRow;                 /* $C42E */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wSynthCursor;                  /* $C442 */
extern UBYTE wUnusedC443;                   /* $C443 */
extern UBYTE wPendingCmdTarget;             /* $C467 */
extern UBYTE wUnusedC46C;                   /* $C46C */
extern UBYTE wWavPlayMode;                  /* $C500 */
extern UBYTE wMessageNoPause;                   /* $C530 */
extern UBYTE wLiveStartCol;                 /* $C571 */
extern UBYTE wMapCol;                       /* $C8FF */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjCol;                /* $C901 */
extern UBYTE wMapSynthCol;                  /* $C902 */
extern UBYTE wDefaultNotes;                 /* $C904 */
extern UBYTE wTableCol;                     /* $C92F */
extern UBYTE wTableRow;                     /* $C930 */
extern UBYTE wTableEnvCache;                /* $C931 */
extern UBYTE wTableTransposeCache;              /* $C932 */
extern UBYTE wHelpOpenEntry;                /* $C933 */
extern UBYTE wHelpRow;                /* $C934 */
extern UBYTE wWordRow;                  /* $C937 */
extern UBYTE wWordCol;                  /* $C938 */
extern UBYTE wChainCellVal;               /* $C945 */
extern UBYTE wSramFsFlag;                   /* $C9A3 */
extern UBYTE wGrooveRow;                    /* $C9D4 */
extern UBYTE wAnalogOutTimer;               /* $CB68 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncRxPos;                    /* $CB71 */
extern UBYTE wKbdQueueTail;                 /* $CB7C */
extern UBYTE wKbdTranspose;                 /* $CB91 */

void ResetWorkState(void)
{
    wChanOutput[0] = 0;
    wChanOutput[1] = 0;
    wChanOutput[2] = 0;
    wChanOutput[3] = 0;
    wPhraseCmdPtr[0] = 0x0140;
    wPhraseCmdPtr[1] = 0x0140;
    wPhraseCmdPtr[2] = 0x0140;
    wPhraseCmdPtr[3] = 0x0140;
    wTableCmd1Ptr[0] = 0x0140;
    wTableCmd1Ptr[1] = 0x0140;
    wTableCmd1Ptr[2] = 0x0140;
    wTableCmd1Ptr[3] = 0x0140;
    wTableCmd2Ptr[0] = 0x0140;
    wTableCmd2Ptr[1] = 0x0140;
    wTableCmd2Ptr[2] = 0x0140;
    wTableCmd2Ptr[3] = 0x0140;
    wChanGroove[0] = 0;
    wChanGroove[1] = 0;
    wChanGroove[2] = 0;
    wChanGroove[3] = 0;
    wChanTablePlayed[0] = 0;
    wChanTablePlayed[1] = 0;
    wChanTablePlayed[2] = 0;
    wChanTablePlayed[3] = 0;
    wChanGrooveRow[0] = 0xFF;
    wChanGrooveRow[1] = 0xFF;
    wChanGrooveRow[2] = 0xFF;
    wChanGrooveRow[3] = 0xFF;
    wChanPhraseRow[0] = 0xFF;
    wChanPhraseRow[1] = 0xFF;
    wChanPhraseRow[2] = 0xFF;
    wChanPhraseRow[3] = 0xFF;
    wChanChainRow[0] = 0xFF;
    wChanChainRow[1] = 0xFF;
    wChanChainRow[2] = 0xFF;
    wChanChainRow[3] = 0xFF;
    wChanSongRow[0] = 0xFF;
    wChanSongRow[1] = 0xFF;
    wChanSongRow[2] = 0xFF;
    wChanSongRow[3] = 0xFF;
    wChanTable[0] = 0xFF;
    wChanTable[1] = 0xFF;
    wChanTable[2] = 0xFF;
    wChanTable[3] = 0xFF;
    wChanTableRow1[0] = 0xFF;
    wChanTableRow1[1] = 0xFF;
    wChanTableRow1[2] = 0xFF;
    wChanTableRow1[3] = 0xFF;
    wChanTableRow2[0] = 0xFF;
    wChanTableRow2[1] = 0xFF;
    wChanTableRow2[2] = 0xFF;
    wChanTableRow2[3] = 0xFF;
    wChanInstr[0] = 0xFF;
    wChanInstr[1] = 0xFF;
    wChanInstr[2] = 0xFF;
    wChanInstr[3] = 0xFF;
    wChanNextSongRow[0] = 0xFF;
    wChanNextSongRow[1] = 0xFF;
    wChanNextSongRow[2] = 0xFF;
    wChanNextSongRow[3] = 0xFF;
    wCurScreen = 4;
    wChainScopeDirty = 0;
    wSongScopeDirty = 0;
    wAPressEnabled = 1;
    wCursorMode = 1;
    wPhrase = 0;
    wPhraseCol = 0;
    wGroove = 0;
    wChain = 0;
    wChainCol = 0;
    wChainRow = 0;
    wTable = 0;
    wSongScroll = 0;
    wSongCol = 0;
    wSongRow = 0;
    wChanDispNote[0] = 0;
    wChanDispNote[1] = 0;
    wChanDispNote[2] = 0;
    wChanDispNote[3] = 0;
    wInstrField = 0;
    wInstrCol = 0;
    wSpeechRow = 0;
    wSynth = 0;
    wWaveCursor = 0;
    wNameEditCol = 9;
    wNameEditRow = 3;
    wSynthScreen = 0;
    wSynthCursor = 0;
    wUnusedC443 = 0;
    wColLastCmd[0] = 1;
    wColLastCmd[1] = 1;
    wColLastCmd[2] = 1;
    wColLastCmd[3] = 1;
    wColChain[0] = 0xFF;
    wColChain[1] = 0xFF;
    wColChain[2] = 0xFF;
    wColChain[3] = 0xFF;
    wColPhrase[0] = 0xFF;
    wColPhrase[1] = 0xFF;
    wColPhrase[2] = 0xFF;
    wColPhrase[3] = 0xFF;
    wPendingCmdTarget = 0xFF;
    wUnusedC46C = 0;
    wWavPlayMode = 0;
    wMessageNoPause = 1;
    wStringSrc = AsciiCharMap - 0x20;
    wShadowDigitPal = 0x49;
    wShadowPal = 0x46;
    wSelectDigitPal = 0x8E;
    wSelectPal = 0x8B;
    wLiveStartCol = 0xFF;
    wPtrChainRow = &wChanChainRow[0];
    wPtrPhraseRow = &wChanPhraseRow[0];
    wPtrNoteCountdown = &wChanNoteCountdown[0];
    wPtrGroove = &wChanGroove[0];
    wPtrGrooveRow = &wChanGrooveRow[0];
    wPtrPhrase = &wChanPhrase[0];
    wPtrNoteSrc = &wChanNote[0];
    wPtrStepCountdown = &wChanStepCountdown[0];
    wPtrTableStep = &wChanTableStep[0];
    wMapCol = 0;
    wMapRow = 1;
    wMapProjCol = 0;
    wMapSynthCol = 2;
    wDefaultNotes = 3;
    wPhraseColLast[0] = 1;
    wPhraseColLast[1] = 1;
    wPhraseColLast[2] = 1;
    wPhraseColLast[3] = 1;
    wTableCol = 0;
    wTableRow = 0;
    wTableEnvCache = 0;
    wTableTransposeCache = 0;
    wHelpOpenEntry = 0;
    wHelpRow = 0;
    wWordClipCell0[0] = 1;
    wWordClipCell0[1] = 0;
    wWordRow = 0;
    wWordCol = 0;
    wChainCellVal = 0;
    wSramFsFlag = 0;
    wInstrRemapTable[0] = 0xFF;
    wInstrRemapTable[1] = 0xFF;
    wInstrRemapTable[2] = 0xFF;
    wInstrRemapTable[3] = 0xFF;
    wInstrRemapTable[4] = 0xFF;
    wInstrRemapTable[5] = 0xFF;
    wInstrRemapTable[6] = 0xFF;
    wInstrRemapTable[7] = 0xFF;
    wInstrRemapTable[8] = 0xFF;
    wInstrRemapTable[9] = 0xFF;
    wInstrRemapTable[10] = 0xFF;
    wInstrRemapTable[11] = 0xFF;
    wInstrRemapTable[12] = 0xFF;
    wInstrRemapTable[13] = 0xFF;
    wInstrRemapTable[14] = 0xFF;
    wInstrRemapTable[15] = 0xFF;
    wGrooveRow = 0;
    wAnalogOutTimer = 3;
    wSyncTick = 0xFF;
    wSyncRxPos = 0xFF;
    wKbdQueueTail = 0xFF;
    wKbdTranspose = 9;
}
