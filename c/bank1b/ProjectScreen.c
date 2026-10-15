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
extern UBYTE ProjectFieldX[];                  /* $1B:$575C, the 12 fields' x */
extern UBYTE ProjectFieldY[];                  /* $1B:$5768, and rows */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wProjectCursor;                   /* $C9A4 */
extern UBYTE wPlaySeconds;                 /* $C553 */
extern UBYTE wPlayMinutes;                 /* $C552 */
extern UBYTE wProjectDrawScratch;              /* $C9A7 */
extern UBYTE wProjectDrawScratch2;             /* $C9A8 */
extern UBYTE wProjectDrawScratch3;             /* $C9A9 */
void DrawProjectTempo(void);
UBYTE ProjectSyncModeIs015(void);
void DrawPrelistenField(void);
UBYTE GetNextButtons(void);                 /* bank 0 */
UBYTE ProjectCursorFieldWidth(void);
extern UBYTE wWorkClockSubFrame;            /* $C9AA */
extern UBYTE wTapTempoCount;                /* $C9AB */
extern UBYTE wTapTempoBuffer[];                /* $C9AC, [8] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAHeld;                        /* $C40C */
extern UBYTE wMapColumn;                       /* $C8FF */
extern UBYTE wProjectMenuActive;               /* $C9A5, the RESET CARTRIDGE? menu is open */
extern UBYTE wProjectMenuItem;                 /* $C9A6, its YES/NO */
extern UBYTE wTickRepeat;                   /* $CB70 */
extern UBYTE wFrameSkip;                    /* $CB6F */
extern UBYTE wTransposeDirty;               /* $C445 */
extern UBYTE wSongTransposeDisplay;            /* $C446 */
extern UBYTE wSyncState;                    /* $CBDE */
extern UBYTE wColorSet;                     /* $C52F */
extern UBYTE wKeyDelayTimer;                /* $C406 */
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wFileField;                    /* $C992 */
UBYTE ProjectSyncStateFlag(void);              /* $1B:$57C1 */
void SyncApplyMode(void);                   /* bank 0 */
void TapTempoStep(void);
void SetSyncSendRate(void);                 /* bank 7 */
void ApplyColorSet(void);                   /* bank 1 */
void LoadFontTiles(void);                   /* bank $1E */
void FormatFileSpace(void);                 /* bank $1C */
void CleanSongData(void);                   /* bank $1F */
void CleanInstrumentData(void);                  /* bank $1F */
void DrawCalibrationScreen(void);         /* bank 7 */
void FileValidateName(void);                /* bank $1C */
void LoadFileListState(void);               /* bank $1C */
void LoadProjectSettings(void);             /* bank 1 */
void FormatSramSong(void);                     /* bank 1 */
void MoveMapCursorRight(void);                    /* bank 5 */
void MoveMapCursorDown(void);                     /* bank 5 */
void SoundTogglePlayback(void);                    /* bank 2 */
extern UBYTE wSramHasFileBanks;                   /* $C9A3 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wCloneMode;                    /* $C531 */
UBYTE SramHasFileBanks(void);               /* $1C:$4057 */
void DrawSyncField(void);
void DrawFontColorFields(void);             /* $1B:$569C, its own unit */
extern UBYTE wFileListActive;               /* $C998 */
void FileStopSound(void);                   /* $1C:$5098 */
void DrawProjectTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
void delay(UWORD d);

void SaveSyncMode(void)
{
    WriteSongByte(1, SramPropertySyncMode, wSyncMode);
}

void DrawSyncMode2Field(void)
{
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    DrawHexByte(wSyncMode2);
    WriteSongByte(1, SramPropertySyncMode2, wSyncMode2);
}

void SaveTempoProperties(void)
{
    WriteSongByte(1, SramPropertyTempo, wTempo);
    WriteSongByte(1, SramPropertyTempoMode, wTempoMode);
}

void DrawProjectTempo(void)
{
    DrawTempo(GetTempoTimerValue());
}

void RedrawProjectTempo(void)
{
    while (wRedrawPending < 6)
        ;
    wVramCursor = (UBYTE *)BG_MAP(6, 2);
    DrawProjectTempo();
}

void DrawKeyRepeatField(void)
{
    DrawByte(wKeyDelay);
    DrawStringIndexed('/');
    DrawByte(wKeyRepeat);
}

UBYTE ProjectSyncModeIs567(void)
{
    if (wSyncMode == 5 || wSyncMode == 6 || wSyncMode == 7)
        return 1;
    return 0;
}

void EraseProjectCursor(void)
{
    UBYTE palette, y;

    palette = wProjectCursor < 8;
    wProjectDrawScratch2 = ProjectCursorFieldWidth();
    y = ProjectFieldY[wProjectCursor];
    ColorField(ProjectFieldX[wProjectCursor], y, wProjectDrawScratch2, palette);
}

void DrawProjectCursor(void)
{
    UBYTE palette, y;

    palette = hGameboyType ? 3 : 2;
    wProjectDrawScratch3 = ProjectCursorFieldWidth();
    y = ProjectFieldY[wProjectCursor];
    ColorField(ProjectFieldX[wProjectCursor], y, wProjectDrawScratch3, palette);
}

void DrawDecimalField(UBYTE v)
{
    if (v < 10)
        DrawStringIndexed('0');
    printf("%d", v);
}

UBYTE ProjectSyncModeIs015(void)
{
    if (wSyncMode == 0 || wSyncMode == 1 || wSyncMode == 5)
        return 1;
    return 0;
}

void DrawPrelistenField(void)
{
    if (ProjectSyncModeIs015())
        DrawString(wPrelisten ? "ON " : "OFF");
    else
        DrawString("N/A");
}

void RedrawProjectPrelisten(void)
{
    wVramCursor = (UBYTE *)BG_MAP(10, 9);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawPrelistenField();
}

void DrawSyncSubSetting(void)
{
    wVramCursor = BG_MAP(0, 16);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    if (wPlayMode) {
        printf("PLAY %d:", (UWORD)wPlayMinutes);
        DrawDecimalField(wPlaySeconds);
        wProjectDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
            wProjectDrawScratch = 0x0D;
        else
            DrawStringTile20();
        DrawStringTile20();
        DrawStringTile20();
    } else {
        printf("WORKED %d:", (UWORD)ReadSongByte(1, 0xBFB2));
        DrawDecimalField(ReadSongByte(1, 0xBFB3));
    }
}

void DrawProjectScreen(void)
{
    wSramHasFileBanks = FarCallByte(0x1C, FAR_FN(SramHasFileBanks));
    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = BG_MAP(6, 2);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawProjectTempo();
    wVramCursor = BG_MAP(10, 3);
    DrawHexByte(wSongTranspose);
    wVramCursor = BG_MAP(5, 7);
    DrawFontColorFields();
    wVramCursor = BG_MAP(6, 6);
    if (!wCloneMode)
        DrawString("DEEP");
    else
        DrawString("SLIM");
    wVramCursor = BG_MAP(17, 8);
    DrawKeyRepeatField();
    wVramCursor = BG_MAP(10, 9);
    DrawPrelistenField();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = BG_MAP(0, 14);
    if (wSramHasFileBanks)
        DrawString("LOAD/SAVE SONG");
    else
        DrawString("RESET MEMORY");
    wVramCursor = BG_MAP(5, 4);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawSyncField();
    DrawSyncSubSetting();
    wVramCursor = BG_MAP(6, 17);
    printf2("%d:%d:", ReadSongByte(1, 0xBFB6), ReadSongByte(1, 0xBFB7));
    DrawDecimalField(ReadSongByte(1, 0xBFB8));
    DrawProjectCursor();
    wTapTempoCount = 0xFF;
}

void RefreshProjectScreen(void)
{
    while (GetNextButtons())
        ;
    while (!GetNextButtons())
        ;
    if (hGameboyType)
        ClearBgTilemap();
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    DrawProjectScreen();
    FarCall(5, DrawMapTopRow);
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
UBYTE ProjectScreenInput(void)
{
    SetVramCursor(ProjectFieldX[wProjectCursor], ProjectFieldY[wProjectCursor]);
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wButtons != J_A)
        wTapTempoCount = 0xFF;
    switch (wButtons) {
    case (J_SELECT | J_DOWN):
        if (!wSelectHeld || wProjectMenuActive)
            return 0;
        wSelectHeld = 0;
        _asm
        ei
        _endasm;
        FarCall(5, MoveMapCursorDown);
        FarCall(5, FAR_FN(RedrawScreen));
        _asm
        ei
        _endasm;
        return 1;
    case (J_SELECT | J_RIGHT):
        if (!wSelectHeld || wProjectMenuActive)
            return 0;
        if (!(char)wMapColumn)
            return 0;
        wSelectHeld = 0;
        FarCall(5, MoveMapCursorRight);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case J_RIGHT:
        if (wProjectMenuActive)
            goto menuRight;
        return 0;
    case J_LEFT:
        if (wProjectMenuActive)
            goto menuLeft;
        return 0;
    case (J_A | J_RIGHT):
        switch (wProjectCursor) {
        case 0:
            if (ProjectSyncStateFlag()) {
                wTickRepeat++;
                DrawTempo(GetTempoTimerValue() + 1);
                RedrawProjectTempo();
                return 1;
            }
            if (wTempo != 0x27)
                wTempo++;
            else if (wTempoMode < 3)
                wTempoMode++;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjectTempo();
            SaveTempoProperties();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisplay = wSongTranspose;
                wTransposeDirty++;
            }
            wSongTransposeDisplay++;
            DrawHexByte(wSongTransposeDisplay);
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
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            DrawSyncField();
            RedrawProjectPrelisten();
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
    case (J_A | J_LEFT):
        switch (wProjectCursor) {
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
            if (ProjectSyncStateFlag()) {
                wFrameSkip++;
                DrawTempo(GetTempoTimerValue() - 1);
                RedrawProjectTempo();
                return 1;
            }
            if (wTempoMode)
                wTempoMode--;
            else if (wTempo != 0x28)
                wTempo--;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjectTempo();
            SaveTempoProperties();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisplay = wSongTranspose;
                wTransposeDirty++;
            }
            wSongTransposeDisplay--;
            DrawHexByte(wSongTransposeDisplay);
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
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            DrawSyncField();
            RedrawProjectPrelisten();
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
    case (J_A | J_UP):
        switch (wProjectCursor) {
        case 5:
            if (wButtonsSame)
                return 0;
            if (wFont != 2) {
                wFont++;
                WriteSongByte(1, 0xBFBC, wFont);
                DrawProjectScreen();
                FarCall(0x1E, FAR_FN(LoadFontTiles));
            }
            return 1;
        case 0:
            if (ProjectSyncStateFlag())
                return 0;
            wTempo += 10;
            if (wTempo >= 0x28 && wTempo < 0x32)
                wTempo = 0x27;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjectTempo();
            SaveTempoProperties();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisplay = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            wVramCursor = (UBYTE *)BG_MAP(10, 3);
            wSongTransposeDisplay += 12;
            DrawHexByte(wSongTransposeDisplay);
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
    case (J_A | J_DOWN):
        switch (wProjectCursor) {
        case 0:
            if (ProjectSyncStateFlag())
                return 0;
            wTempo -= 10;
            if (wTempo >= 0x1E && wTempo < 0x28)
                wTempo = 0x28;
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjectTempo();
            SaveTempoProperties();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisplay = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            wVramCursor = (UBYTE *)BG_MAP(10, 3);
            wSongTransposeDisplay -= 12;
            DrawHexByte(wSongTransposeDisplay);
            return 1;
        case 5:
            if (wButtonsSame)
                return 0;
            if (wFont) {
                wFont--;
                WriteSongByte(1, 0xBFBC, wFont);
                DrawProjectScreen();
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
    case J_DOWN:
        if (!wProjectMenuActive) {
            EraseProjectCursor();
            if (wProjectCursor < 0x0B)
                wProjectCursor++;
            if (wProjectCursor == 7 && !ProjectSyncModeIs015())
                wProjectCursor++;
            else if (wProjectCursor == 3 && !ProjectSyncModeIs567())
                wProjectCursor++;
            DrawProjectCursor();
        } else {
        menuRight:
            wProjectMenuItem = 1;
            ColorField(5, 9, 3, 0);
            ColorField(12, 9, 2, hGameboyType ? 3 : 2);
        }
        return 1;
    case J_UP:
        if (!wProjectMenuActive) {
            EraseProjectCursor();
            if (wProjectCursor)
                wProjectCursor--;
            if (wProjectCursor == 7 && !ProjectSyncModeIs015())
                wProjectCursor--;
            else if (wProjectCursor == 3 && !ProjectSyncModeIs567())
                wProjectCursor--;
            DrawProjectCursor();
        } else {
        menuLeft:
            wProjectMenuItem = 0;
            ColorField(5, 9, 3, hGameboyType ? 3 : 2);
            ColorField(12, 9, 2, 0);
        }
        return 1;
    case (J_SELECT | J_B | J_A):
        if (!wAPressEnabled)
            return 0;
        if (!wProjectMenuActive && wProjectCursor == 0x0B)
            goto openMenu;
        if (!wProjectMenuActive || wProjectMenuItem)
            break;
        ColorField(5, 9, 3, 0);
        ColorField(12, 9, 2, 0);
        FarCall(0x1C, FAR_FN(FormatFileSpace));
        WriteSongByte(1, 0xBFB6, 0);
        WriteSongByte(1, 0xBFB8, 0);
        WriteSongByte(1, 0xBFB7, 0);
        goto reset;
    case J_A:
        if (!wAPressEnabled)
            return 0;
        switch (wProjectCursor) {
        case 0:
            if (ProjectSyncStateFlag())
                break;
            TapTempoStep();
            FarCall(7, FAR_FN(SetSyncSendRate));
            DrawProjectTempo();
            SaveTempoProperties();
            return 1;
        case 9:
            FarCall(0x1C, FAR_FN(FileStopSound));
            EraseProjectCursor();
            DrawProjectTextField(3, 4, 0x0F, 8);
            FarCall(0x1F, FAR_FN(CleanSongData));
            RefreshProjectScreen();
            return 1;
        case 10:
            FarCall(0x1C, FAR_FN(FileStopSound));
            EraseProjectCursor();
            DrawProjectTextField(3, 4, 0x0F, 9);
            FarCall(0x1F, FAR_FN(CleanInstrumentData));
            RefreshProjectScreen();
            return 1;
        case 8:
            wCurrentScreen = SCREEN_HELP;
            FarCall(4, FAR_FN(RedrawScreen));
            return 1;
        case 3:
            if (wSyncMode == 5) {
                FarCall(0x1C, FAR_FN(FileStopSound));
                FarCall(7, DrawCalibrationScreen);
                DrawSyncMode2Field();
                FarCall(4, FAR_FN(RedrawScreen));
            }
            return 1;
        case 11:
            if (wSramHasFileBanks) {
                if (wProjectMenuActive)
                    goto closeMenu;
                wCurrentScreen = SCREEN_FILE;
                wFileField = 0;
                wFileListActive = 0;
                FarCall(0x1C, FAR_FN(FileValidateName));
                FarCall(4, FAR_FN(RedrawScreen));
            } else {
                if (wProjectMenuActive)
                    goto closeMenu;
            openMenu:
                hBgDigitPalette = 4;
                hBgPalette = 1;
                DrawProjectTextField(1, 6, 0x12, 5);
                wProjectMenuActive = 1;
                wProjectMenuItem = 1;
                wVramCursor = (UBYTE *)BG_MAP(2, 7);
                DrawString("RESET CARTRIDGE?");
                wVramCursor = (UBYTE *)BG_MAP(5, 9);
                DrawString("YES");
                wVramCursor = (UBYTE *)BG_MAP(12, 9);
                DrawString("NO");
                ColorField(12, 9, 2, hGameboyType ? 3 : 2);
                return 1;
            closeMenu:
                wProjectMenuActive = 0;
                ColorField(5, 9, 3, 0);
                ColorField(12, 9, 2, 0);
                if (wProjectMenuItem)
                    goto redraw;
            reset:
                FarCall(0x1C, FAR_FN(FileStopSound));
                if (wSramHasFileBanks) {
                    WriteSongByte(1, 0xBFB6, 0);
                    WriteSongByte(1, 0xBFB8, 0);
                    WriteSongByte(1, 0xBFB7, 0);
                    FarCall(0x1C, FAR_FN(FormatFileSpace));
                }
                FarCall(1, FAR_FN(FormatSramSong));
                WriteSongByte(4, 0xA140, 0xFF);
                FarCall(0x1C, FAR_FN(LoadFileListState));
                DrawProjectTextField(2, 7, 0x10, 3);
                wVramCursor = (UBYTE *)BG_MAP(3, 8);
                DrawString("RESET MEMORY!");
                delay(hGameboyType ? 2000 : 1000);
                FarCallArgument(1, FAR_FN(LoadProjectSettings), 0);
            redraw:
                if (hGameboyType)
                    ClearBgTilemap();
                else
                    FarCall(1, FAR_FN(DrawScreenPaletteMap));
                FarCall(0, FAR_FN(RedrawScreen));
                FarCall(5, DrawMapTopRow);
            }
            return 1;
        }
        return 0;
    case J_B:
        if (wProjectMenuActive) {
            wProjectMenuActive = 0;
            if (hGameboyType)
                ClearBgTilemap();
            FarCall(1, FAR_FN(DrawScreenPaletteMap));
            DrawProjectScreen();
            FarCall(5, DrawMapTopRow);
        }
        return 1;
    case J_START:
    case (J_SELECT | J_START):
        if (wButtonsSame || wBHeld)
            return 1;
        _asm
        di
        _endasm;
        wCurrentScreen = SCREEN_SONG;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        wCurrentScreen = SCREEN_PROJECT;
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
    UBYTE tap;

    sum = 0;
    if (wTapTempoCount == 0xFF || wWorkClockSubFrame == 0x80) {
        wTapTempoCount = 0;
        wWorkClockSubFrame = 0;
        return;
    }
    if (wTapTempoCount == 8) {
        memcpy(wTapTempoBuffer, wTapTempoBuffer + 1, 7);
        wTapTempoCount--;
    }
    wTapTempoBuffer[wTapTempoCount] = wWorkClockSubFrame;
    wWorkClockSubFrame = 0;
    wTapTempoCount++;
    if (wTapTempoCount < 3)
        return;
    for (tap = 0; tap < wTapTempoCount; tap++)
        sum += wTapTempoBuffer[tap];
    sum <<= 3;
    sum /= wTapTempoCount;
    sum = 0x6FFE / sum;
    wTempo = sum < 0x100 ? sum : 0xFF;
}
