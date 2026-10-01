/*
 * InitAndLoadSong and main -- LSDj 9.4.0, ROM bank $00 ($0DDB, $1306).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The C entry point Start jumps to: detect the hardware, InitAndLoadSong,
 * the first redraw, the title message and the file-system check, then the
 * endless frame loop (main.mainLoop: idle synth rendering until a frame is
 * due, then input, the screen update, the work clock and the idle halt).
 *
 * hGameboyType is read everywhere as a plain global (`ld hl,#0xFF90; or
 * a,(hl)`), but main stores it with `ldh`: this file saw it as an sfr, so it
 * gets a second, absolute sfr name here (`sfr hGameboyTypeIo = 0xFF90;`,
 * which needs no linker symbol).
 */

#include "lsdj.h"

sfr hGameboyTypeIo = 0xFF90;
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
extern volatile sfr UBYTE TMA_REG;          /* $FF06 */
extern volatile sfr UBYTE TAC_REG;          /* $FF07 */
extern UBYTE wFsCheckResult;                /* $C56B */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPrelisten;                    /* $C56E */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wTimerReload;                  /* $C578 */
extern UBYTE wTimerTickInc;                 /* $C579 */
extern UBYTE wWorkTimeTicks;                /* $C577 */
extern UBYTE wIdleHalt;                     /* $C68B */
extern UBYTE wIdleFrames;                   /* $C68C */
extern UBYTE wSynthRenderIdx;               /* $C9F0 */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE wSyncTick;                     /* $CB69 */
extern UBYTE wSyncSerialIeMask;             /* $CB6D */
extern UBYTE wBootHwId;                     /* $CBD9 */
extern char TxtTitle[];                     /* $01:$6F7E */

void SwitchToDoubleSpeed(void);             /* asm */
void ResetWorkState(void);
void InitKitMixer(void);                    /* bank $1B */
void UiInitScreen(void);                    /* bank 1 */
UBYTE CheckSongInitFlag(void);              /* bank 1 */
void InitScreenPalettes(void);              /* bank $1E */
void DrawCartTest(void);                    /* bank 1 */
void UiClearSram(void);                     /* bank 1 */
void WriteSongInitFlag(void);               /* bank 1 */
UBYTE CheckFileMagic64(void);               /* bank $1C */
UBYTE CheckJkMarker(void);                  /* bank 1 */
void FormatFileSpace(void);                 /* bank $1C */
void LoadProjectSettings(void);             /* bank 1 */
UBYTE CheckFileMagicAll(void);              /* bank $1C */
void RenderSynthWave(void);                 /* bank $1D */
UBYTE ProcessInput(void);
void ScreenUpdateDispatch(void);
UBYTE AnyChanFlagSet(void);
void UiSetPlayMode(void);                   /* bank 1 */
void UiReadSongProp(void);                  /* bank 1 */
UBYTE UiShowMessage(void);                  /* bank 1 */
void UiUpdateMessage(void);                 /* bank 1 */
void SetInterruptEnable(UBYTE ie);          /* asm */
UBYTE joypad(void);                         /* GBDK */
void WaitInterrupt(void);                   /* asm */

void InitAndLoadSong(void)
{
    SwitchToDoubleSpeed();
    ClearBgTilemap();
    rRAMG = 0;
    SWITCH_ROM(0x1B);
    InitKitMixer();
    SWITCH_ROM(0x01);
    UiInitScreen();
    if (!CheckSongInitFlag()) {
        ResetWorkState();
        FarCall(0x1E, FAR_FN(InitScreenPalettes));
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawCartTest();
        WriteSongByte(1, 0xBFB6, 0);
        WriteSongByte(1, 0xBFB8, 0);
        WriteSongByte(1, 0xBFB7, 0);
        UiClearSram();
    }
    WriteSongInitFlag();
    SWITCH_ROM(0x1C);
    if (CheckFileMagic64()) {
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
    wTimerTickInc = hGameboyType ? 0x0C : 0x06;
    FarCallArg(1, LoadProjectSettings, 0);
    _asm ei _endasm;
}

void main(void)
{
    switch (wBootHwId) {
    case 0x01:
        wIsCGB = NR52_REG == 0xF0;
        break;
    case 0x11:
        hGameboyTypeIo = 1;
        break;
    }
    NR52_REG = 0;
    InitAndLoadSong();
    _asm di _endasm;
    SWITCH_ROM(2);
    RedrawScreen();
    _asm ei _endasm;
    SWITCH_ROM(1);
    ShowMessage(TxtTitle);
    SWITCH_ROM(0x1C);
    wFsCheckResult = CheckFileMagicAll();
    wPrelisten = ReadSongByte(1, 0xBFC3);
    while (1) {
        if (!wRedrawPending) {
            if (wSynthRenderActive) {
                SWITCH_ROM(0x1D);
                RenderSynthWave();
                if (++wSynthRenderIdx == 0x10)
                    wSynthRenderActive = 0;
            }
            continue;
        }
        wRedrawPending--;
        if (!ProcessInput() || wPlayMode || wSyncMode == 6 || wSyncTick != 0xFF)
            wIdleFrames = 0;
        ScreenUpdateDispatch();
        if (AnyChanFlagSet())
            wIdleFrames = 0;
        SWITCH_ROM(1);
        UiSetPlayMode();
        if (wWorkTimeTicks) {
            wWorkTimeTicks--;
            wIdleFrames = 0;
            UiReadSongProp();
        }
        if (UiShowMessage())
            wIdleFrames = 0;
        if (!wRedrawPending)
            UiUpdateMessage();
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
