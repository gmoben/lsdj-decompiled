/*
 * Editor state -- LSDj 9.4.0, ROM bank $01.
 *
 * The selection and clipboard state reset, and the song's project settings
 * loaded.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE NR50_REG;         /* $FF24 */
extern volatile sfr UBYTE NR51_REG;         /* $FF25 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern UBYTE wColorSet;
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wQuickMarking;             /* $C412 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wClipboardBuffer[];               /* $C57A, [252] */
extern UBYTE wClipboardSave[];              /* $C7C6, [252] */
void ResetWorkState(void);                  /* bank 0 */
void ApplyColorSet(void);
void SyncLoadSetting(void);                 /* bank 0 */
void LoadFontTiles(void);              /* bank $1E */
void SetSyncSendRate(void);                 /* bank 7 */
void LoadFileListState(void);               /* bank $1C */
void FileUpgradeSong(void);                 /* bank $1C */
void DrawKeyboardInstrumentState(void);               /* bank 7 */
extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wColumnInstrument[];              /* $C468, [4] */
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wKeyDelay;                     /* $C52B */
extern UBYTE wKeyDelayTimer;                /* $C406 */
extern UBYTE wKeyRepeat;                    /* $C52C */
extern UBYTE wKeyRepeatTimer;               /* $C407 */
extern UBYTE wSongTranspose;                /* $C444 */

void ResetEditorState(UBYTE keep)
{
    UBYTE top, right, bottom, ctype, ctype1, mode, left, live;

    left = wSelectionLeft;
    top = wSelectionTop;
    right = wSelectionRight;
    bottom = wSelectionBottom;
    ctype = wClipboardType;
    ctype1 = wQuickMarking;
    mode = wCursorMode;
    live = wLiveMode;
    if (keep)
        memcpy(wClipboardSave, wClipboardBuffer, 0xFC);
    rRAMG = 0x0A;
    ResetWorkState();
    rRAMG = 0;
    if (keep) {
        memcpy(wClipboardBuffer, wClipboardSave, 0xFC);
        wSelectionLeft = left;
        wSelectionRight = right;
        wSelectionTop = top;
        wSelectionBottom = bottom;
        wClipboardType = ctype;
        wQuickMarking = ctype1;
        wCursorMode = mode;
        wLiveMode = live;
    }
}

void LoadProjectSettings(UBYTE keep)
{
    UBYTE column;

    ResetEditorState(keep);
    NR52_REG = 0;
    NR51_REG = 0;
    NR50_REG = 0;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    wFont = *SramPropertyFont;
    if (wFont > 2) {
        wFont = 0;
        *SramPropertyFont = 0;
    }
    SyncLoadSetting();
    rRAMG = 0x0A;
    rRAMB = 1;
    wCloneMode = *SramPropertyCloneMode;
    wColorSet = *SramPropertyColorSet;
    if (!hGameboyType && wColorSet > 3)
        wColorSet = 0;
    rRAMG = 0;
    FarCall(0x1E, FAR_FN(LoadFontTiles));
    hBgDigitPalette = 4;
    hBgPalette = 1;
    ApplyColorSet();
    hSoundChannel = 0;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    wTempo = *SramPropertyTempo;
    wTempoMode = *SramPropertyTempoMode;
    wSongTranspose = *SramPropertyTranspose;
    memcpy(wColumnInstrument, SramPropertyColumnInstruments, 4);
    column = 4;
    do {
        if (wColumnInstrument[column - 1] > 0x40 && wColumnInstrument[column - 1] != 0xFF)
            wColumnInstrument[column - 1] = 0;
    } while (--column);
    wKeyDelay = *SramPropertyKeyDelay & 0x0F;
    wKeyRepeat = *SramPropertyKeyRepeat & 0x0F;
    rRAMG = 0;
    FarCall(7, FAR_FN(SetSyncSendRate));
    wKeyDelayTimer = wKeyDelay;
    wKeyRepeatTimer = wKeyRepeat;
    FarCall(0x1C, FAR_FN(LoadFileListState));
    FarCall(0x1C, FAR_FN(FileUpgradeSong));
    if (wSyncMode == SYNC_KEYBOARD)
        FarCall(7, FAR_FN(DrawKeyboardInstrumentState));
}
