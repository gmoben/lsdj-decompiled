/*
 * InitializeAndLoadSong, ProcessInput and main -- LSDj 9.4.0, ROM bank $00 ($0DDB,
 * $0ED6, $1306).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The C entry point Start jumps to: detect the hardware, InitializeAndLoadSong,
 * the first redraw, the title message and the file-system check, then the
 * endless frame loop (main.mainLoop: idle synth rendering until a frame is
 * due, then input, the screen update, the work clock and the idle halt).
 *
 * hGameboyType is read everywhere as a plain global (`ld hl,#0xFF90; or
 * a,(hl)`), but main stores it with `ldh`: this file saw it as an sfr, so it
 * gets a second, absolute sfr name here (`sfr hGameboyTypeIo = 0xFF90;`,
 * which needs no linker symbol).
 */

#define SND_DIRECT          /* ProcessInput calls SoundGetChainPhrase with bank 2 paged in */
#include "lsdj.h"

sfr hGameboyTypeIo = 0xFF90;
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
extern volatile sfr UBYTE TMA_REG;          /* $FF06 */
extern volatile sfr UBYTE TAC_REG;          /* $FF07 */
extern UBYTE wFilesystemCheckResult;                /* $C56B */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPrelisten;                    /* $C56E */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wTimerReload;                  /* $C578 */
extern UBYTE wTimerTickIncrement;                 /* $C579 */
extern UBYTE wWorkTimeTicks;                /* $C577 */
extern UBYTE wIdleHalt;                     /* $C68B */
extern UBYTE wIdleFrames;                   /* $C68C */
extern UBYTE wSynthRenderIndex;               /* $C9F0 */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wBootHardwareId;                     /* $CBD9 */
extern char TxtTitle[];                     /* $01:$6F7E */

void SwitchToDoubleSpeed(void);             /* asm */
void ResetWorkState(void);
void InitializeKitMixer(void);                    /* bank $1B */
void InitializeScreen(void);                    /* bank 1 */
UBYTE CheckSongInitializedMarker(void);              /* bank 1 */
void LoadFontTiles(void);              /* bank $1E */
void DrawCartTest(void);                    /* bank 1 */
void FormatSramSong(void);                     /* bank 1 */
void WriteSongInitializedMarker(void);               /* bank 1 */
UBYTE SramHasFileBanks(void);               /* bank $1C */
UBYTE CheckJkMarker(void);                  /* bank 1 */
void FormatFileSpace(void);                 /* bank $1C */
void LoadProjectSettings(void);             /* bank 1 */
UBYTE CheckFileMagicAll(void);              /* bank $1C */
void RenderSynthWave(void);                 /* bank $1D */
UBYTE ProcessLinkQueue(void);               /* bank 7 */
UBYTE GetNextButtons(void);
UBYTE SoundGetChainPhrase(UBYTE chain, UBYTE row);    /* bank 2 */
void MarkPhraseUsed(UBYTE phrase);          /* bank 1 */
void MarkChainUsed(UBYTE chain);            /* bank 1 */
void InitializeRemapTable(void);                  /* bank $1F */
void SoundTogglePlayback(void);                  /* bank 2 */
void SyncApplyMode(void);
void WritePhraseCommand(UBYTE phrase, UBYTE row, UBYTE command);  /* bank 4 */
void WriteTableCommand(UBYTE row, UBYTE command);    /* bank 5 */
void ResetGrooveState(void);                /* bank $1D */
UBYTE HandleButtonEvent(void);
extern UBYTE wKeyboardActionChannel;                  /* $CB9A */
extern UBYTE wLinkQueueCount;               /* $CB7D */
extern UBYTE wKeyboardHeldNote;                  /* $CB7E */
extern UBYTE wSelectHoldFrames;                /* $C676 */
extern UBYTE wChainScopeDirty;              /* $C403 */
extern UBYTE wSongScopeDirty;               /* $C404 */
extern UBYTE wChain;                        /* $C418 */
extern char wChainRow;                      /* $C41A: a char (copied through b to be pushed) */
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
extern UBYTE wColumnChain[];                   /* $C44F, [4] */
extern UBYTE wColumnLastCommand[];                 /* $C44B, [4] */
extern UBYTE wInstrumentRemapDirty;              /* $C9B7 */
extern UBYTE wTransposeDirty;               /* $C445 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wSongTransposeDisplay;            /* $C446 */
extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UBYTE wSavedLiveMode;                /* $C572 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wPendingCommandTarget;             /* $C467 */
extern UBYTE wPendingCommands[];                /* $C457, [16] */
extern UWORD wTableCellAPointer, wTableCellBPointer;    /* $C447, $C449 */
extern UBYTE wGrooveCacheValid;             /* $C9D5 */
extern UBYTE *wTopBarMarkPointer;               /* $C562 */
extern UBYTE wKeyDelay, wKeyRepeat;         /* $C52B, $C52C */
extern UBYTE wKeyDelayTimer, wKeyRepeatTimer;   /* $C406, $C407 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
void DispatchScreenUpdate(void);
UBYTE AnyChannelFlagSet(void);
void UpdatePlayClock(void);                   /* bank 1 */
void UpdateWorkClock(void);                  /* bank 1 */
UBYTE IsMessageActive(void);                  /* bank 1 */
void UpdateMessage(void);                 /* bank 1 */
void SetInterruptEnable(UBYTE interruptMask);          /* asm */
UBYTE joypad(void);                         /* GBDK */
void WaitInterrupt(void);                   /* asm */

/* $0ED6: the frame's input: the keyboard's queued events (the first that
   is a button state) or the joypad's next buttons; SELECT tapped alone
   becomes $43. Releasing A commits what was edited while it was held (the
   chain or song cursor's phrase or chain marked used, the instrument remap,
   the transpose, a queued live start, held command edits), then a button
   change goes to HandleButtonEvent at the key delay and repeat rates. 1
   when nothing was handled */

UBYTE ProcessInput(void)
{
    UBYTE buttons;
    UBYTE r;
    UBYTE v;
    UBYTE *p;
    UBYTE row;
    UBYTE ok;

    wKeyboardActionChannel = 0xFF;
    if (wLinkQueueCount) {
        SWITCH_ROM(7);
        do
            buttons = ProcessLinkQueue();
        while (--wLinkQueueCount && buttons == 0xFF);
        if (buttons == 0xFF)
            return 1;
    } else {
        buttons = GetNextButtons();
        if (wKeyboardHeldNote) {
            if (buttons)
                wKeyboardHeldNote = 0;
            else
                return 1;
        }
    }
    if (buttons) {
        if (buttons != J_SELECT)
            wSelectHoldFrames = 0;
        else if (!wButtons)
            wSelectHoldFrames = 1;
    } else if (wSelectHoldFrames) {
        buttons = J_SELECT_TAP;
        wSelectHoldFrames = 0;
    }
    if ((wButtons & J_A) && !(buttons & J_A)) {
        if (wChainScopeDirty) {
            wChainScopeDirty = 0;
            SWITCH_ROM(2);
            r = SoundGetChainPhrase(wChain, wChainRow);
            if (r != 0xFF) {
                SWITCH_ROM(1);
                MarkPhraseUsed(r);
                wColumnPhrase[wSongColumn] = r;
            }
        }
        if (wSongScopeDirty) {
            wSongScopeDirty = 0;
            SWITCH_ROM(2);
            r = ReadSequenceByte(wSongColumn, wSongRow);
            if (r < 0x80) {
                SWITCH_ROM(1);
                MarkChainUsed(r);
                wColumnChain[wSongColumn] = r;
            }
        }
        if (wInstrumentRemapDirty) {
            SWITCH_ROM(0x1F);
            InitializeRemapTable();
        }
        if (wTransposeDirty) {
            wTransposeDirty = 0;
            wSongTranspose = wSongTransposeDisplay;
            WriteSongByte(1, 0xBFB5, wSongTranspose);
        }
        if (wLiveStartColumn != 0xFF) {
            wLiveStartColumn = 0xFF;
            SWITCH_ROM(2);
            SoundTogglePlayback();
            wLiveMode = wSavedLiveMode;
            SyncApplyMode();
            return 0;
        }
        if (wPendingCommandTarget != 0xFF) {
            for (row = 0; row < 0x10; row++) {
                p = &wPendingCommands[row];
                v = *p;
                if (v) {
                    if (v > 0x13)
                        DebugTrap();
                    switch (wPendingCommandTarget) {
                    case 0:
                        SWITCH_ROM(4);
                        WritePhraseCommand(wPhrase, row, v);
                        break;
                    case 1:
                        SWITCH_ROM(5);
                        wTableCellAPointer = 0xB680;
                        wTableCellBPointer = 0xB880;
                        WriteTableCommand(row, v);
                        break;
                    case 2:
                        SWITCH_ROM(5);
                        wTableCellAPointer = 0xBA80;
                        wTableCellBPointer = 0xBC80;
                        WriteTableCommand(row, v);
                        break;
                    }
                    wColumnLastCommand[wSongColumn] = v;
                    *p = 0;
                }
            }
            wPendingCommandTarget = 0xFF;
        }
        if (wGrooveCacheValid) {
            SWITCH_ROM(0x1D);
            ResetGrooveState();
        }
    }
    if (buttons) {
        ok = 1;
        if (*wTopBarMarkPointer != 0x21 && (buttons & J_A)
                && (buttons & (J_SELECT | J_B | J_UP | J_DOWN | J_LEFT | J_RIGHT)))
            WriteSongByte(1, 0xBFC1, *wTopBarMarkPointer = 0x21);
        wButtonsSame = wButtons == buttons;
        if (!wButtonsSame) {
            wKeyDelayTimer = wKeyDelay;
            wKeyRepeatTimer = wKeyRepeat;
        }
        wButtons = buttons;
        if (wKeyDelay == wKeyDelayTimer) {
            SWITCH_ROM(3);
            if (ok = HandleButtonEvent())
                wKeyDelayTimer--;
        } else if (wKeyDelayTimer)
            wKeyDelayTimer--;
        else {
            if (wKeyRepeatTimer == wKeyRepeat) {
                SWITCH_ROM(3);
                if (!(ok = HandleButtonEvent())) {
                    wKeyRepeatTimer = wKeyRepeat;
                    wKeyDelayTimer = wKeyDelay;
                }
            }
            if (wKeyRepeatTimer)
                wKeyRepeatTimer--;
            else
                wKeyRepeatTimer = wKeyRepeat;
        }
        if (ok && (wButtons & (J_SELECT | J_START | J_B | J_A)))
            wAPressEnabled = 0;
        return !ok;
    }
    if (wButtons) {
        wButtons = 0;
        wSelectHeld = 0;
        wAPressEnabled = 1;
        wBHeld = 0;
        wKeyDelayTimer = wKeyDelay;
        wKeyRepeatTimer = wKeyRepeat;
    }
    return 1;
}

void InitializeAndLoadSong(void)
{
    SwitchToDoubleSpeed();
    ClearBgTilemap();
    rRAMG = 0;
    SWITCH_ROM(0x1B);
    InitializeKitMixer();
    SWITCH_ROM(0x01);
    InitializeScreen();
    if (!CheckSongInitializedMarker()) {
        ResetWorkState();
        FarCall(0x1E, FAR_FN(LoadFontTiles));
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawCartTest();
        WriteSongByte(1, 0xBFB6, 0);
        WriteSongByte(1, 0xBFB8, 0);
        WriteSongByte(1, 0xBFB7, 0);
        FormatSramSong();
    }
    WriteSongInitializedMarker();
    SWITCH_ROM(0x1C);
    if (SramHasFileBanks()) {
        SWITCH_ROM(0x01);
        if (!CheckJkMarker()) {
            SWITCH_ROM(0x1C);
            FormatFileSpace();
        }
    }
    _asm di _endasm;
    STAT_REG |= 0x40;
    TMA_REG = 0x49;
    TAC_REG = 6;
    wTimerReload = hGameboyType ? 0x6E : 0xB7;
    wTimerTickIncrement = hGameboyType ? 0x0C : 0x06;
    FarCallArgument(1, LoadProjectSettings, 0);
    _asm ei _endasm;
}

void main(void)
{
    switch (wBootHardwareId) {
    case 0x01:
        wIsCgb = NR52_REG == 0xF0;
        break;
    case 0x11:
        hGameboyTypeIo = 1;
        break;
    }
    NR52_REG = 0;
    InitializeAndLoadSong();
    _asm di _endasm;
    SWITCH_ROM(2);
    RedrawScreen();
    _asm ei _endasm;
    SWITCH_ROM(1);
    ShowMessage(TxtTitle);
    SWITCH_ROM(0x1C);
    wFilesystemCheckResult = CheckFileMagicAll();
    wPrelisten = ReadSongByte(1, 0xBFC3);
    while (1) {
        if (!wRedrawPending) {
            if (wSynthRenderActive) {
                SWITCH_ROM(0x1D);
                RenderSynthWave();
                if (++wSynthRenderIndex == 0x10)
                    wSynthRenderActive = 0;
            }
            continue;
        }
        wRedrawPending--;
        if (!ProcessInput() || wPlayMode || wSyncMode == 6 || wSyncTick != 0xFF)
            wIdleFrames = 0;
        DispatchScreenUpdate();
        if (AnyChannelFlagSet())
            wIdleFrames = 0;
        SWITCH_ROM(1);
        UpdatePlayClock();
        if (wWorkTimeTicks) {
            wWorkTimeTicks--;
            wIdleFrames = 0;
            UpdateWorkClock();
        }
        if (IsMessageActive())
            wIdleFrames = 0;
        if (!wRedrawPending)
            UpdateMessage();
        else
            wIdleFrames = 0;
        if (++wIdleFrames > 7) {
            wIdleHalt = 1;
            SetInterruptEnable(0x19);
            while (wIdleHalt && wButtons == joypad())
                WaitInterrupt();
            wIdleHalt = 0;
            wIdleFrames = 0;
            if (!wPlayMode)
                SetInterruptEnable(wSyncSerialIeMask | 1);
        }
    }
}
