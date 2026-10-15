/*
 * ResetWorkState -- LSDj 9.4.0, ROM bank $00 ($3A33).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies it against the ROM.
 *
 * The work-RAM reset InitializeAndLoadSong runs before a song is loaded: the four
 * channels' sequencer state, the editors' cursors, the clipboard caches and
 * the per-channel pointers the sound driver indexes through.
 *
 * The pointers are set with `&wChannelChainRow[0]`, not `wChannelChainRow`: four
 * of the arrays were indexed above, and the plain name would reuse that
 * `&array` temporary (CSE) and store the address as two literal bytes, where
 * the ROM loads it into de for every one of the nine.
 */

#include "lsdj.h"

extern UBYTE wChannelOutput[];                 /* $C0A0, [4] */
extern UBYTE wChannelStepCountdown[];          /* $C0C4, [4] */
extern UBYTE wChannelNote[];                   /* $C0CC, [4] */
extern UWORD wPhraseCommandPointer[];               /* $C110, [4] */
extern UWORD wTableCommand1Pointer[];               /* $C118, [4] */
extern UWORD wTableCommand2Pointer[];               /* $C120, [4] */
extern UBYTE wChannelGroove[];                 /* $C150, [4] */
extern UBYTE wChannelTablePlayed[];            /* $C154, [4] */
extern UBYTE wChannelGrooveRow[];              /* $C158, [8] */
extern UBYTE wChannelNoteCountdown[];          /* $C160, [4] */
extern UBYTE wChannelPhrase[];                 /* $C168, [4] */
extern UBYTE wChannelPhraseRow[];              /* $C16C, [4] */
extern UBYTE wChannelChainRow[];               /* $C17C, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
extern UBYTE wChannelTable[];                  /* $C204, [4] */
extern UBYTE wChannelTableRow1[];              /* $C208, [4] */
extern UBYTE wChannelTableRow2[];              /* $C20C, [4] */
extern UBYTE wChannelInstrument[];                  /* $C210, [4] */
extern UBYTE wChannelNextSongRow[];            /* $C2FC, [4] */
extern UBYTE wChannelTableStep[];              /* $C33F, [4] */
extern UBYTE wChannelDisplayNote[];               /* $C41F, [4] */
extern UBYTE wColumnLastCommand[];                 /* $C44B, [4] */
extern UBYTE wColumnChain[];                   /* $C44F, [4] */
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
extern UBYTE wPhraseColumnLast[];              /* $C907, [4] */
extern UBYTE wWordClipboardCell0[];              /* $C935, [2] */
extern UBYTE wInstrumentRemapTable[];            /* $C9B8, [16] */
extern UBYTE *wPointerChainRow;                 /* $C8DF */
extern UBYTE *wPointerPhraseRow;                /* $C8E1 */
extern UBYTE *wPointerNoteCountdown;            /* $C8E3 */
extern UBYTE *wPointerGroove;                   /* $C8E5 */
extern UBYTE *wPointerGrooveRow;                /* $C8E7 */
extern UBYTE *wPointerPhrase;                   /* $C8EB */
extern UBYTE *wPointerNoteSource;                  /* $C8ED */
extern UBYTE *wPointerStepCountdown;            /* $C8EF */
extern UBYTE *wPointerTableStep;                /* $C8FC */
extern UBYTE wChainScopeDirty;              /* $C403 */
extern UBYTE wSongScopeDirty;               /* $C404 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wPhraseColumn;                    /* $C415 */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainColumn;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wTable;                        /* $C41B */
extern UBYTE wSongScroll;                   /* $C41C */
extern UBYTE wInstrumentColumn;               /* $C428 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wWaveCursor;                   /* $C42B */
extern UBYTE wNameEditColumn;                 /* $C42D */
extern UBYTE wNameEditRow;                 /* $C42E */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wSynthCursor;                  /* $C442 */
extern UBYTE wUnusedC443;                   /* $C443 */
extern UBYTE wPendingCommandTarget;             /* $C467 */
extern UBYTE wUnusedC46C;                   /* $C46C */
extern UBYTE wWavPlayMode;                  /* $C500 */
extern UBYTE wMessageNoPause;                   /* $C530 */
extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UBYTE wMapColumn;                       /* $C8FF */
extern UBYTE wMapRow;                       /* $C900 */
extern UBYTE wMapProjectColumn;                /* $C901 */
extern UBYTE wMapSynthColumn;                  /* $C902 */
extern UBYTE wDefaultNotes;                 /* $C904 */
extern UBYTE wTableColumn;                     /* $C92F */
extern UBYTE wTableRow;                     /* $C930 */
extern UBYTE wTableEnvelopeCache;                /* $C931 */
extern UBYTE wTableTransposeCache;              /* $C932 */
extern UBYTE wHelpOpenEntry;                /* $C933 */
extern UBYTE wHelpRow;                /* $C934 */
extern UBYTE wWordRow;                  /* $C937 */
extern UBYTE wWordColumn;                  /* $C938 */
extern UBYTE wChainCellValue;               /* $C945 */
extern UBYTE wSramHasFileBanks;                   /* $C9A3 */
extern UBYTE wGrooveRow;                    /* $C9D4 */
extern UBYTE wAnalogOutTimer;               /* $CB68 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncReceivePosition;                    /* $CB71 */
extern UBYTE wKeyboardQueueTail;                 /* $CB7C */
extern UBYTE wKeyboardTranspose;                 /* $CB91 */

void ResetWorkState(void)
{
    wChannelOutput[0] = 0;
    wChannelOutput[1] = 0;
    wChannelOutput[2] = 0;
    wChannelOutput[3] = 0;
    wPhraseCommandPointer[0] = 0x0140;
    wPhraseCommandPointer[1] = 0x0140;
    wPhraseCommandPointer[2] = 0x0140;
    wPhraseCommandPointer[3] = 0x0140;
    wTableCommand1Pointer[0] = 0x0140;
    wTableCommand1Pointer[1] = 0x0140;
    wTableCommand1Pointer[2] = 0x0140;
    wTableCommand1Pointer[3] = 0x0140;
    wTableCommand2Pointer[0] = 0x0140;
    wTableCommand2Pointer[1] = 0x0140;
    wTableCommand2Pointer[2] = 0x0140;
    wTableCommand2Pointer[3] = 0x0140;
    wChannelGroove[0] = 0;
    wChannelGroove[1] = 0;
    wChannelGroove[2] = 0;
    wChannelGroove[3] = 0;
    wChannelTablePlayed[0] = 0;
    wChannelTablePlayed[1] = 0;
    wChannelTablePlayed[2] = 0;
    wChannelTablePlayed[3] = 0;
    wChannelGrooveRow[0] = 0xFF;
    wChannelGrooveRow[1] = 0xFF;
    wChannelGrooveRow[2] = 0xFF;
    wChannelGrooveRow[3] = 0xFF;
    wChannelPhraseRow[0] = 0xFF;
    wChannelPhraseRow[1] = 0xFF;
    wChannelPhraseRow[2] = 0xFF;
    wChannelPhraseRow[3] = 0xFF;
    wChannelChainRow[0] = 0xFF;
    wChannelChainRow[1] = 0xFF;
    wChannelChainRow[2] = 0xFF;
    wChannelChainRow[3] = 0xFF;
    wChannelSongRow[0] = 0xFF;
    wChannelSongRow[1] = 0xFF;
    wChannelSongRow[2] = 0xFF;
    wChannelSongRow[3] = 0xFF;
    wChannelTable[0] = 0xFF;
    wChannelTable[1] = 0xFF;
    wChannelTable[2] = 0xFF;
    wChannelTable[3] = 0xFF;
    wChannelTableRow1[0] = 0xFF;
    wChannelTableRow1[1] = 0xFF;
    wChannelTableRow1[2] = 0xFF;
    wChannelTableRow1[3] = 0xFF;
    wChannelTableRow2[0] = 0xFF;
    wChannelTableRow2[1] = 0xFF;
    wChannelTableRow2[2] = 0xFF;
    wChannelTableRow2[3] = 0xFF;
    wChannelInstrument[0] = 0xFF;
    wChannelInstrument[1] = 0xFF;
    wChannelInstrument[2] = 0xFF;
    wChannelInstrument[3] = 0xFF;
    wChannelNextSongRow[0] = 0xFF;
    wChannelNextSongRow[1] = 0xFF;
    wChannelNextSongRow[2] = 0xFF;
    wChannelNextSongRow[3] = 0xFF;
    wCurrentScreen = SCREEN_SONG;
    wChainScopeDirty = 0;
    wSongScopeDirty = 0;
    wAPressEnabled = 1;
    wCursorMode = CURSOR_NORMAL;
    wPhrase = 0;
    wPhraseColumn = PHRASE_COLUMN_LEFT_NOTE;
    wGroove = 0;
    wChain = 0;
    wChainColumn = 0;
    wChainRow = 0;
    wTable = 0;
    wSongScroll = 0;
    wSongColumn = 0;
    wSongRow = 0;
    wChannelDisplayNote[0] = 0;
    wChannelDisplayNote[1] = 0;
    wChannelDisplayNote[2] = 0;
    wChannelDisplayNote[3] = 0;
    wInstrumentField = 0;
    wInstrumentColumn = 0;
    wSpeechRow = 0;
    wSynth = 0;
    wWaveCursor = 0;
    wNameEditColumn = 9;
    wNameEditRow = 3;
    wSynthScreen = 0;
    wSynthCursor = SYNTH_FIELD_SIGNAL;
    wUnusedC443 = 0;
    wColumnLastCommand[0] = 1;
    wColumnLastCommand[1] = 1;
    wColumnLastCommand[2] = 1;
    wColumnLastCommand[3] = 1;
    wColumnChain[0] = 0xFF;
    wColumnChain[1] = 0xFF;
    wColumnChain[2] = 0xFF;
    wColumnChain[3] = 0xFF;
    wColumnPhrase[0] = 0xFF;
    wColumnPhrase[1] = 0xFF;
    wColumnPhrase[2] = 0xFF;
    wColumnPhrase[3] = 0xFF;
    wPendingCommandTarget = 0xFF;
    wUnusedC46C = 0;
    wWavPlayMode = 0;
    wMessageNoPause = 1;
    wStringSource = AsciiCharacterMap - 0x20;
    wShadedDigitPalette = 0x49;
    wShadedPalette = 0x46;
    wHighlightDigitPalette = 0x8E;
    wHighlightPalette = 0x8B;
    wLiveStartColumn = 0xFF;
    wPointerChainRow = &wChannelChainRow[0];
    wPointerPhraseRow = &wChannelPhraseRow[0];
    wPointerNoteCountdown = &wChannelNoteCountdown[0];
    wPointerGroove = &wChannelGroove[0];
    wPointerGrooveRow = &wChannelGrooveRow[0];
    wPointerPhrase = &wChannelPhrase[0];
    wPointerNoteSource = &wChannelNote[0];
    wPointerStepCountdown = &wChannelStepCountdown[0];
    wPointerTableStep = &wChannelTableStep[0];
    wMapColumn = 0;
    wMapRow = 1;
    wMapProjectColumn = 0;
    wMapSynthColumn = 2;
    wDefaultNotes = 3;
    wPhraseColumnLast[0] = 1;
    wPhraseColumnLast[1] = 1;
    wPhraseColumnLast[2] = 1;
    wPhraseColumnLast[3] = 1;
    wTableColumn = TABLE_COLUMN_VOLUME;
    wTableRow = 0;
    wTableEnvelopeCache = 0;
    wTableTransposeCache = 0;
    wHelpOpenEntry = 0;
    wHelpRow = 0;
    wWordClipboardCell0[0] = 1;
    wWordClipboardCell0[1] = 0;
    wWordRow = 0;
    wWordColumn = 0;
    wChainCellValue = 0;
    wSramHasFileBanks = 0;
    wInstrumentRemapTable[0] = 0xFF;
    wInstrumentRemapTable[1] = 0xFF;
    wInstrumentRemapTable[2] = 0xFF;
    wInstrumentRemapTable[3] = 0xFF;
    wInstrumentRemapTable[4] = 0xFF;
    wInstrumentRemapTable[5] = 0xFF;
    wInstrumentRemapTable[6] = 0xFF;
    wInstrumentRemapTable[7] = 0xFF;
    wInstrumentRemapTable[8] = 0xFF;
    wInstrumentRemapTable[9] = 0xFF;
    wInstrumentRemapTable[10] = 0xFF;
    wInstrumentRemapTable[11] = 0xFF;
    wInstrumentRemapTable[12] = 0xFF;
    wInstrumentRemapTable[13] = 0xFF;
    wInstrumentRemapTable[14] = 0xFF;
    wInstrumentRemapTable[15] = 0xFF;
    wGrooveRow = 0;
    wAnalogOutTimer = 3;
    wSyncTick = 0xFF;
    wSyncReceivePosition = 0xFF;
    wKeyboardQueueTail = 0xFF;
    wKeyboardTranspose = 9;
}
