/*
 * The project screen -- LSDj 9.4.0, ROM bank $1B.
 *
 * Its fields (sync and its sub-setting, tempo, key repeat, prelisten), the
 * cursor, the input and tap tempo.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wKeyDelay;                     /* $C52B */
extern UBYTE wKeyRepeat;                    /* $C52C */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPrelisten;                    /* $C56E */
extern UBYTE ProjFieldX[];                  /* $1B:$575C, the 12 fields' x */
extern UBYTE ProjFieldY[];                  /* $1B:$5768, and rows */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wProjCursor;                   /* $C9A4 */
extern UBYTE wPlaySeconds;                 /* $C553 */
extern UBYTE wPlayMinutes;                 /* $C552 */
extern UBYTE wProjDrawScratch;              /* $C9A7 */
extern UBYTE wProjDrawScratch2;             /* $C9A8 */
extern UBYTE wProjDrawScratch3;             /* $C9A9 */
void DrawProjTempo(void);
UBYTE ProjSyncModeIs015(void);
void DrawPrelistenField(void);
UBYTE GetNextButtons(void);                 /* bank 0 */
UBYTE ProjCursorFieldWidth(void);
extern UBYTE wWorkClockSubFrame;            /* $C9AA */
extern UBYTE wTapTempoCount;                /* $C9AB */
extern UBYTE wTapTempoBuf[];                /* $C9AC, [8] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAHeld;                        /* $C40C */
extern UBYTE wMapCol;                       /* $C8FF */
extern UBYTE wProjMenuActive;               /* $C9A5, the RESET CARTRIDGE? menu is open */
extern UBYTE wProjMenuItem;                 /* $C9A6, its YES/NO */
extern UBYTE wTickRepeat;                   /* $CB70 */
extern UBYTE wFrameSkip;                    /* $CB6F */
extern UBYTE wTransposeDirty;               /* $C445 */
extern UBYTE wSongTransposeDisp;            /* $C446 */
extern UBYTE wSyncState;                    /* $CBDE */
extern UBYTE wColorSet;                     /* $C52F */
extern UBYTE wKeyDelayTimer;                /* $C406 */
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wFileField;                    /* $C992 */
UBYTE ProjSyncStateFlag(void);              /* $1B:$57C1 */
void SyncApplyMode(void);                   /* bank 0 */
void TapTempoStep(void);
void SetSyncSendRate(void);                 /* bank 7 */
void ApplyColorSet(void);                   /* bank 1 */
void LoadFontTiles(void);                   /* bank $1E */
void FormatFileSpace(void);                 /* bank $1C */
void CleanSongData(void);                   /* bank $1F */
void CleanInstrData(void);                  /* bank $1F */
void DrawKbdCalibScreen(void);         /* bank 7 */
void FileValidateName(void);                /* bank $1C */
void LoadFileListState(void);               /* bank $1C */
void LoadProjSettings(void);             /* bank 1 */
void FormatSramSong(void);                     /* bank 1 */
void MapMoveRight(void);                    /* bank 5 */
void MapMoveDown(void);                     /* bank 5 */
void TogglePlayback(void);                    /* bank 2 */
extern UBYTE wSramFsFlag;                   /* $C9A3 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wCloneMode;                    /* $C531 */
UBYTE CheckFileMagic64(void);               /* $1C:$4057 */
void DrawSyncField(void);
void DrawFontColorFields(void);             /* $1B:$569C, its own unit */
extern UBYTE wFileListActive;               /* $C998 */
void FileStopSound(void);                   /* $1C:$5098 */
void DrawProjTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
void delay(UWORD d);

void SaveSyncMode(void)
{
    WriteSongByte(1, SramPropSyncMode, wSyncMode);
}

void DrawSyncMode2Field(void)
{
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    DrawHexByte(wSyncMode2);
    WriteSongByte(1, SramPropSyncMode2, wSyncMode2);
}

void SaveTempoProps(void)
{
    WriteSongByte(1, SramPropTempo, wTempo);
    WriteSongByte(1, SramPropTempoMode, wTempoMode);
}

void DrawProjTempo(void)
{
    DrawTempo(GetTempoTimerVal());
}

void RedrawProjTempo(void)
{
    while (wRedrawPending < 6)
        ;
    wVramCursor = (UBYTE *)0x9846;
    DrawProjTempo();
}

void DrawKeyRepeatField(void)
{
    DrawByte(wKeyDelay);
    DrawStringIndexed('/');
    DrawByte(wKeyRepeat);
}

UBYTE ProjSyncModeIs567(void)
{
    if (wSyncMode == 5 || wSyncMode == 6 || wSyncMode == 7)
        return 1;
    return 0;
}

void EraseProjCursor(void)
{
    UBYTE pal, y;

    pal = wProjCursor < 8;
    wProjDrawScratch2 = ProjCursorFieldWidth();
    y = ProjFieldY[wProjCursor];
    ColorField(ProjFieldX[wProjCursor], y, wProjDrawScratch2, pal);
}

void DrawProjCursor(void)
{
    UBYTE pal, y;

    pal = hGameboyType ? 3 : 2;
    wProjDrawScratch3 = ProjCursorFieldWidth();
    y = ProjFieldY[wProjCursor];
    ColorField(ProjFieldX[wProjCursor], y, wProjDrawScratch3, pal);
}

void DrawDecimalField(UBYTE v)
{
    if (v < 10)
        DrawStringIndexed('0');
    printf("%d", v);
}

UBYTE ProjSyncModeIs015(void)
{
    if (wSyncMode == 0 || wSyncMode == 1 || wSyncMode == 5)
        return 1;
    return 0;
}

void DrawPrelistenField(void)
{
    if (ProjSyncModeIs015())
        DrawString(wPrelisten ? "ON " : "OFF");
    else
        DrawString("N/A");
}

void RedrawProjPrelisten(void)
{
    wVramCursor = (UBYTE *)0x992A;
    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    DrawPrelistenField();
}

void DrawSyncSubSetting(void)
{
    wVramCursor = 0x9A00;
    hBgDigitPal = 4;
    hBgPal = 1;
    if (wPlayMode) {
        printf("PLAY %d:", (UWORD)wPlayMinutes);
        DrawDecimalField(wPlaySeconds);
        wProjDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
            wProjDrawScratch = 0x0D;
        else
            DrawStringTile20();
        DrawStringTile20();
        DrawStringTile20();
    } else {
        printf("WORKED %d:", (UWORD)ReadSongByte(1, 0xBFB2));
        DrawDecimalField(ReadSongByte(1, 0xBFB3));
    }
}

void DrawProjScreen(void)
{
    wSramFsFlag = FarCallByte(0x1C, FAR_FN(CheckFileMagic64));
    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = 0x9846;
    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    DrawProjTempo();
    wVramCursor = 0x986A;
    DrawHexByte(wSongTranspose);
    wVramCursor = 0x98E5;
    DrawFontColorFields();
    wVramCursor = 0x98C6;
    if (!wCloneMode)
        DrawString("DEEP");
    else
        DrawString("SLIM");
    wVramCursor = 0x9911;
    DrawKeyRepeatField();
    wVramCursor = 0x992A;
    DrawPrelistenField();
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = 0x99C0;
    if (wSramFsFlag)
        DrawString("LOAD/SAVE SONG");
    else
        DrawString("RESET MEMORY");
    wVramCursor = 0x9885;
    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    DrawSyncField();
    DrawSyncSubSetting();
    wVramCursor = 0x9A26;
    printf2("%d:%d:", ReadSongByte(1, 0xBFB6), ReadSongByte(1, 0xBFB7));
    DrawDecimalField(ReadSongByte(1, 0xBFB8));
    DrawProjCursor();
    wTapTempoCount = 0xFF;
}

void RefreshProjScreen(void)
{
    while (GetNextButtons())
        ;
    while (!GetNextButtons())
        ;
    if (hGameboyType)
        ClearBgTilemap();
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawProjScreen();
    FarCall(5, MapDrawTopRow);
    wRedrawPending = 0;
}

/*
 * The project screen's input (screen 9). A+right/left and A+up/down change
 * the field under the cursor: tempo (or the sync rate when slaved), the
 * transpose, sync mode and its setting, clone mode, colour set, font, key
 * repeat and delay, prelisten -- each saved to the song properties. A taps
 * the tempo, cleans songs or instruments, opens the keyboard calibration,
 * and on the last row opens the file screen, or without a file system the
 * RESET CARTRIDGE? menu: its YES clears SRAM and reloads the settings
 * (select+A+B on YES also formats the file area). start/select+start stop
 * playback. 1 when the key was taken.
 */
UBYTE ProjScreenInput(void)
{
    SetVramCursor(ProjFieldX[wProjCursor], ProjFieldY[wProjCursor]);
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    if (wButtons != 0x10)
        wTapTempoCount = 0xFF;
    switch (wButtons) {
    case 0x48:
        if (!wSelectHeld || wProjMenuActive)
            return 0;
        wSelectHeld = 0;
        _asm
        ei
        _endasm;
        FarCall(5, MapMoveDown);
        FarCall(5, FAR_FN(RedrawScreen));
        _asm
        ei
        _endasm;
        return 1;
    case 0x41:
        if (!wSelectHeld || wProjMenuActive)
            return 0;
        if (!(char)wMapCol)
            return 0;
        wSelectHeld = 0;
        FarCall(5, MapMoveRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x01:
        if (wProjMenuActive)
            goto menuRight;
        return 0;
    case 0x02:
        if (wProjMenuActive)
            goto menuLeft;
        return 0;
    case 0x11:
        switch (wProjCursor) {
        case 0:
            if (ProjSyncStateFlag()) {
                wTickRepeat++;
                DrawTempo(GetTempoTimerVal() + 1);
                RedrawProjTempo();
                return 1;
            }
            if (wTempo != 0x27)
                wTempo++;
            else if (wTempoMode < 3)
                wTempoMode++;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            wSongTransposeDisp++;
            DrawHexByte(wSongTransposeDisp);
            return 1;
        case 2:
            if (wButtonsSame)
                return 1;
            if (wSyncMode < 7) {
                wSyncMode++;
                if (wSyncMode == 4 || wSyncMode == 2)
                    wSyncMode++;
                SaveSyncMode();
            }
            wSyncState = 0;
            SyncApplyMode();
            hBgDigitPal = wHighlightDigitPal;
            hBgPal = wHighlightPal;
            DrawSyncField();
            RedrawProjPrelisten();
            return 1;
        case 3:
            if (wSyncMode2 < 0xFF) {
                wSyncMode2++;
                DrawSyncMode2Field();
            }
            return 1;
        case 4:
            if (!wCloneMode) {
                wCloneMode = 1;
                WriteSongByte(1, 0xBFC0, 1);
                DrawString("SLIM");
            }
            return 1;
        case 5:
            if (wButtonsSame)
                return 0;
            if (!hGameboyType && wColorSet == 3)
                return 0;
            if (hGameboyType && wColorSet == 6)
                return 0;
            wColorSet++;
            WriteSongByte(1, 0xBFBE, wColorSet);
            DrawFontColorFields();
            FarCall(1, FAR_FN(ApplyColorSet));
            if (!hGameboyType && wColorSet == 2)
                FarCall(0x1E, FAR_FN(LoadFontTiles));
            return 1;
        case 6:
            if (wKeyRepeat == 0x0F || !wAHeld)
                return 1;
            wAHeld = 0;
            wKeyDelayTimer = 1;
            wKeyRepeat++;
            DrawKeyRepeatField();
            WriteSongByte(1, 0xBFBB, wKeyRepeat);
            return 1;
        case 7:
            wPrelisten = 1;
            DrawPrelistenField();
            WriteSongByte(1, 0xBFC3, wPrelisten);
            return 1;
        }
        break;
    case 0x12:
        switch (wProjCursor) {
        case 5:
            if (!wColorSet || wButtonsSame)
                return 0;
            wColorSet--;
            WriteSongByte(1, 0xBFBE, wColorSet);
            FarCall(1, FAR_FN(ApplyColorSet));
            DrawFontColorFields();
            if (!hGameboyType && wColorSet == 1)
                FarCall(0x1E, FAR_FN(LoadFontTiles));
            return 1;
        case 0:
            if (ProjSyncStateFlag()) {
                wFrameSkip++;
                DrawTempo(GetTempoTimerVal() - 1);
                RedrawProjTempo();
                return 1;
            }
            if (wTempoMode)
                wTempoMode--;
            else if (wTempo != 0x28)
                wTempo--;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            wSongTransposeDisp--;
            DrawHexByte(wSongTransposeDisp);
            return 1;
        case 2:
            if (wButtonsSame)
                return 1;
            if (wSyncMode != 0) {
                wSyncMode--;
                if (wSyncMode == 4 || wSyncMode == 2)
                    wSyncMode--;
                SaveSyncMode();
            }
            wSyncState = 0;
            SyncApplyMode();
            hBgDigitPal = wHighlightDigitPal;
            hBgPal = wHighlightPal;
            DrawSyncField();
            RedrawProjPrelisten();
            return 1;
        case 3:
            if (wSyncMode2 > 1) {
                wSyncMode2--;
                DrawSyncMode2Field();
            }
            return 1;
        case 4:
            if (wCloneMode) {
                wCloneMode = 0;
                WriteSongByte(1, 0xBFC0, 0);
                DrawString("DEEP");
            }
            return 1;
        case 6:
            if (!wKeyRepeat || !wAHeld)
                return 0;
            wAHeld = 0;
            wKeyDelayTimer = 1;
            wKeyRepeat--;
            DrawKeyRepeatField();
            WriteSongByte(1, 0xBFBB, wKeyRepeat);
            return 1;
        case 7:
            wPrelisten = 0;
            DrawPrelistenField();
            WriteSongByte(1, 0xBFC3, wPrelisten);
            return 1;
        }
        break;
    case 0x14:
        switch (wProjCursor) {
        case 5:
            if (wButtonsSame)
                return 0;
            if (wFont != 2) {
                wFont++;
                WriteSongByte(1, 0xBFBC, wFont);
                DrawProjScreen();
                FarCall(0x1E, FAR_FN(LoadFontTiles));
            }
            return 1;
        case 0:
            if (ProjSyncStateFlag())
                return 0;
            wTempo += 10;
            if (wTempo >= 0x28 && wTempo < 0x32)
                wTempo = 0x27;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPal = wHighlightDigitPal;
            hBgPal = wHighlightPal;
            wVramCursor = (UBYTE *)0x986A;
            wSongTransposeDisp += 12;
            DrawHexByte(wSongTransposeDisp);
            return 1;
        case 6:
            if (wKeyDelay == 0x0F || !wAHeld)
                return 1;
            wAHeld = 0;
            wKeyDelayTimer = 1;
            wKeyDelay++;
            DrawKeyRepeatField();
            WriteSongByte(1, 0xBFBA, wKeyDelay);
            return 1;
        }
        break;
    case 0x18:
        switch (wProjCursor) {
        case 0:
            if (ProjSyncStateFlag())
                return 0;
            wTempo -= 10;
            if (wTempo >= 0x1E && wTempo < 0x28)
                wTempo = 0x28;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPal = wHighlightDigitPal;
            hBgPal = wHighlightPal;
            wVramCursor = (UBYTE *)0x986A;
            wSongTransposeDisp -= 12;
            DrawHexByte(wSongTransposeDisp);
            return 1;
        case 5:
            if (wButtonsSame)
                return 0;
            if (wFont) {
                wFont--;
                WriteSongByte(1, 0xBFBC, wFont);
                DrawProjScreen();
                FarCall(0x1E, FAR_FN(LoadFontTiles));
            }
            return 1;
        case 6:
            if (!wKeyDelay || !wAHeld)
                return 1;
            wAHeld = 0;
            wKeyDelayTimer = 1;
            wKeyDelay--;
            DrawKeyRepeatField();
            WriteSongByte(1, 0xBFBA, wKeyDelay);
            return 1;
        }
        break;
    case 0x08:
        if (!wProjMenuActive) {
            EraseProjCursor();
            if (wProjCursor < 0x0B)
                wProjCursor++;
            if (wProjCursor == 7 && !ProjSyncModeIs015())
                wProjCursor++;
            else if (wProjCursor == 3 && !ProjSyncModeIs567())
                wProjCursor++;
            DrawProjCursor();
        } else {
        menuRight:
            wProjMenuItem = 1;
            ColorField(5, 9, 3, 0);
            ColorField(12, 9, 2, hGameboyType ? 3 : 2);
        }
        return 1;
    case 0x04:
        if (!wProjMenuActive) {
            EraseProjCursor();
            if (wProjCursor)
                wProjCursor--;
            if (wProjCursor == 7 && !ProjSyncModeIs015())
                wProjCursor--;
            else if (wProjCursor == 3 && !ProjSyncModeIs567())
                wProjCursor--;
            DrawProjCursor();
        } else {
        menuLeft:
            wProjMenuItem = 0;
            ColorField(5, 9, 3, hGameboyType ? 3 : 2);
            ColorField(12, 9, 2, 0);
        }
        return 1;
    case 0x70:
        if (!wAPressEnabled)
            return 0;
        if (!wProjMenuActive && wProjCursor == 0x0B)
            goto openMenu;
        if (!wProjMenuActive || wProjMenuItem)
            break;
        ColorField(5, 9, 3, 0);
        ColorField(12, 9, 2, 0);
        FarCall(0x1C, FAR_FN(FormatFileSpace));
        WriteSongByte(1, 0xBFB6, 0);
        WriteSongByte(1, 0xBFB8, 0);
        WriteSongByte(1, 0xBFB7, 0);
        goto reset;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        switch (wProjCursor) {
        case 0:
            if (ProjSyncStateFlag())
                break;
            TapTempoStep();
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjTempo();
            SaveTempoProps();
            return 1;
        case 9:
            FarCall(0x1C, FAR_FN(FileStopSound));
            EraseProjCursor();
            DrawProjTextField(3, 4, 0x0F, 8);
            FarCall(0x1F, FAR_FN(CleanSongData));
            RefreshProjScreen();
            return 1;
        case 10:
            FarCall(0x1C, FAR_FN(FileStopSound));
            EraseProjCursor();
            DrawProjTextField(3, 4, 0x0F, 9);
            FarCall(0x1F, FAR_FN(CleanInstrData));
            RefreshProjScreen();
            return 1;
        case 8:
            wCurScreen = 0x0E;
            FarCall(4, FAR_FN(RedrawScreen));
            return 1;
        case 3:
            if (wSyncMode == 5) {
                FarCall(0x1C, FAR_FN(FileStopSound));
                FarCall(7, DrawKbdCalibScreen);
                DrawSyncMode2Field();
                FarCall(4, FAR_FN(RedrawScreen));
            }
            return 1;
        case 11:
            if (wSramFsFlag) {
                if (wProjMenuActive)
                    goto closeMenu;
                wCurScreen = 0x0D;
                wFileField = 0;
                wFileListActive = 0;
                FarCall(0x1C, FAR_FN(FileValidateName));
                FarCall(4, FAR_FN(RedrawScreen));
            } else {
                if (wProjMenuActive)
                    goto closeMenu;
            openMenu:
                hBgDigitPal = 4;
                hBgPal = 1;
                DrawProjTextField(1, 6, 0x12, 5);
                wProjMenuActive = 1;
                wProjMenuItem = 1;
                wVramCursor = (UBYTE *)0x98E2;
                DrawString("RESET CARTRIDGE?");
                wVramCursor = (UBYTE *)0x9925;
                DrawString("YES");
                wVramCursor = (UBYTE *)0x992C;
                DrawString("NO");
                ColorField(12, 9, 2, hGameboyType ? 3 : 2);
                return 1;
            closeMenu:
                wProjMenuActive = 0;
                ColorField(5, 9, 3, 0);
                ColorField(12, 9, 2, 0);
                if (wProjMenuItem)
                    goto redraw;
            reset:
                FarCall(0x1C, FAR_FN(FileStopSound));
                if (wSramFsFlag) {
                    WriteSongByte(1, 0xBFB6, 0);
                    WriteSongByte(1, 0xBFB8, 0);
                    WriteSongByte(1, 0xBFB7, 0);
                    FarCall(0x1C, FAR_FN(FormatFileSpace));
                }
                FarCall(1, FAR_FN(FormatSramSong));
                WriteSongByte(4, 0xA140, 0xFF);
                FarCall(0x1C, FAR_FN(LoadFileListState));
                DrawProjTextField(2, 7, 0x10, 3);
                wVramCursor = (UBYTE *)0x9903;
                DrawString("RESET MEMORY!");
                delay(hGameboyType ? 2000 : 1000);
                FarCallArg(1, FAR_FN(LoadProjSettings), 0);
            redraw:
                if (hGameboyType)
                    ClearBgTilemap();
                else
                    FarCall(1, FAR_FN(UiDrawTextHw));
                FarCall(0, FAR_FN(RedrawScreen));
                FarCall(5, MapDrawTopRow);
            }
            return 1;
        }
        return 0;
    case 0x20:
        if (wProjMenuActive) {
            wProjMenuActive = 0;
            if (hGameboyType)
                ClearBgTilemap();
            FarCall(1, FAR_FN(UiDrawTextHw));
            DrawProjScreen();
            FarCall(5, MapDrawTopRow);
        }
        return 1;
    case 0x80:
    case 0xC0:
        if (wButtonsSame || wBHeld)
            return 1;
        _asm
        di
        _endasm;
        wCurScreen = 4;
        FarCall(2, FAR_FN(TogglePlayback));
        wCurScreen = 9;
        if (wPlayMode)
            DrawSyncSubSetting();
        _asm
        ei
        _endasm;
        return 1;
    }
    return 0;
}

void TapTempoStep(void)
{
    UWORD sum;
    UBYTE i;

    sum = 0;
    if (wTapTempoCount == 0xFF || wWorkClockSubFrame == 0x80) {
        wTapTempoCount = 0;
        wWorkClockSubFrame = 0;
        return;
    }
    if (wTapTempoCount == 8) {
        memcpy(wTapTempoBuf, wTapTempoBuf + 1, 7);
        wTapTempoCount--;
    }
    wTapTempoBuf[wTapTempoCount] = wWorkClockSubFrame;
    wWorkClockSubFrame = 0;
    wTapTempoCount++;
    if (wTapTempoCount < 3)
        return;
    for (i = 0; i < wTapTempoCount; i++)
        sum += wTapTempoBuf[i];
    sum <<= 3;
    sum /= wTapTempoCount;
    sum = 0x6FFE / sum;
    wTempo = sum < 0x100 ? sum : 0xFF;
}
