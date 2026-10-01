/*
 * The project and help screens -- LSDj 9.4.0, ROM bank $1B.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wHelpOpenEntry;                /* $C933, the open HELP entry (0 = none) */
extern UBYTE wHelpCursorRow;                /* $C934 */
extern UBYTE wKbdCursorCol;                 /* $C42D */
extern UBYTE wKbdCursorRow;                 /* $C42E */
extern UBYTE wKeyDelay;                     /* $C52B */
extern UBYTE wKeyRepeat;                    /* $C52C */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPrelisten;                    /* $C56E */
extern char *HelpMenuPtrs[];                /* $1B:$4058, NULL-terminated */
extern UBYTE HelpSelectLines[];             /* $1B:$408E, the selectable lines */
extern char **HelpPageRefs[];               /* $1B:$4BAF, &HelpPagePtrs[k] per line */
extern UBYTE ProjFieldX[];                  /* $1B:$575C, the 12 fields' x */
extern UBYTE ProjFieldY[];                  /* $1B:$5768, and rows */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wProjCursor;                   /* $C9A4 */
extern UBYTE wFrameCounter;                 /* $C553 */
extern UBYTE wFrameCounterHi;               /* $C552 */
extern UBYTE wProjDrawScratch;              /* $C9A7 */
extern UBYTE wProjDrawScratch2;             /* $C9A8 */
extern UBYTE wProjDrawScratch3;             /* $C9A9 */

void DrawHelpMenu(void);
void ProjDrawHelpEntry(UBYTE selected);
void ProjRedrawHelpSel(void);
void ProjDrawTempo(void);
UBYTE ProjSyncModeIs015(void);
void DrawPrelistenField(void);
UBYTE GetNextButtons(void);                 /* bank 0 */
UWORD StringLength(char *s);                /* bank 0 */
UBYTE ProjCalcFieldTile(void);
void DrawNameEditArrows(char *s);
void ProjHelpSelectNext(void);
void ProjHelpOpenSelected(void);

extern UBYTE KitMixTable[];               /* $1B:$6C38, the 1152-byte nibble-sum LUT */
void MixerKernel(void);                     /* $1B:$4000, the 27-byte asm mixer step (code, hence a function); DrawHelpMenu follows it */

extern UBYTE wWorkClockSubFrame;            /* $C9AA */
extern UBYTE wTapTempoCount;                /* $C9AB */
extern UBYTE wTapTempoBuf[];                /* $C9AC, [8] */

extern UBYTE wKbdLetterPos;                 /* $C42C */
extern UBYTE wNameEditBuf[];                /* $C431, [5] */

extern UBYTE wSramFsFlag;                   /* $C9A3 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wCloneMode;                    /* $C531 */
UBYTE CheckFileMagic64(void);               /* $1C:$4057 */
void DrawSyncField(void);
void DrawFontColorFields(void);             /* $1B:$569C, its own unit */

extern UBYTE ExpoCurve16[];                 /* $00:$300F, envelope speed -> frame count */
int Abs16(int v);                           /* $00:$343F */

extern UBYTE wNameReturnScreen;             /* $C42F */
extern UBYTE wFileListActive;               /* $C998 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wNameEditIsInstr;              /* $C430 */
extern UBYTE wInstrument;                   /* $C576 */
void FileStopSound(void);                   /* $1C:$5098 */
UBYTE SaveFile(UBYTE f);                    /* $1C:$4E10 */
void WriteInstrName(UBYTE instr);           /* $01:$4A49 */
void DrawProjTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
void delay(UWORD d);
extern char AlnumChars[];                   /* $1B:$4C6A, four 11-byte rows: "0123456789", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZx >w" */
void CopyInstrName(UBYTE instr);            /* $01:$493B */

void DrawHelpMenu(void)
{
    UBYTE i;

    for (i = 0; HelpMenuPtrs[i]; i++) {
        GetTilemapAddr(0, i);
        DrawString(HelpMenuPtrs[i]);
    }
}

void ProjDrawHelpEntry(UBYTE selected)
{
    UBYTE e;
    UBYTE len;

    e = HelpSelectLines[wHelpCursorRow];
    len = StringLength(HelpMenuPtrs[e]);
    DrawTextField(0, e, len, selected ? 0 : 2);
}

void ProjRedrawHelpSel(void)
{
    if (wHelpOpenEntry != 0)
        return;
    ProjDrawHelpEntry(0);
}

void DrawHelpScreen(void)
{
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawHelpMenu();
    ProjRedrawHelpSel();
}

void ProjHelpSelectNext(void)
{
    if ((UWORD)wHelpCursorRow == 9)
        return;
    ProjDrawHelpEntry(1);
    wHelpCursorRow++;
    ProjDrawHelpEntry(0);
}

void ProjHelpSelectPrev(void)
{
    if (wHelpCursorRow) {
        ProjDrawHelpEntry(1);
        wHelpCursorRow--;
        ProjDrawHelpEntry(0);
    }
}

void GotoProjectScreen(void)
{
    wCurrentScreen = 9;
    FarCall(4, FAR_FN(RedrawScreen));
}

void ProjHelpReset(void)
{
    wHelpOpenEntry = 0;
    ClearBgTilemap();
    DrawHelpScreen();
}

void DrawNameEditArrows(char *s)
{
    UBYTE row;
    UBYTE col;
    UBYTE start;

    row = 0;
    col = 0;
    wVramCursor = 0x9800;
    while (*s) {
        switch (*s) {
        case '\n':
            GetTilemapAddr(0, ++row);
            col = 0;
            break;
        case '<':
            start = col;
            break;
        case '>':
            DrawTextField(start, row, col - start, 1);
            break;
        default:
            DrawStringIndexed(*s);
            col++;
        }
        ++s;
    }
}

void ProjHelpOpenSelected(void)
{
    ProjDrawHelpEntry(1);
    ClearBgTilemap();
    wHelpOpenEntry = HelpSelectLines[wHelpCursorRow];
    DrawNameEditArrows(*HelpPageRefs[wHelpCursorRow]);
}

UBYTE HelpScreenInput(void)
{
    UBYTE closed;

    closed = wHelpOpenEntry == 0;
    switch (wButtons) {
    case 8:
        if (closed)
            ProjHelpSelectNext();
        break;
    case 4:
        if (closed)
            ProjHelpSelectPrev();
        break;
    case 0x10:
        if (wAPressEnabled && closed)
            ProjHelpOpenSelected();
        break;
    case 0x20:
        if (wAPressEnabled) {
            if (closed)
                GotoProjectScreen();
            else
                ProjHelpReset();
        }
        break;
    }
    return 1;
}

void ProjDrawNameFieldHw(void)
{
    UBYTE x, y;

    x = wKbdCursorCol + 5;
    y = wKbdCursorRow + 8;
    DrawTextField(x, y, 1, hGameboyType ? 3 : 2);
}

void ProjDrawNameField(void)
{
    DrawTextField(wKbdCursorCol + 5, wKbdCursorRow + 8, 1, 0);
}

void OpenNameEditor(void)
{
    UBYTE i, v;

    ClearMarkerSprites();
    wNameReturnScreen = wCurrentScreen;
    if (wNameEditIsInstr)
        FarCallArg(1, FAR_FN(CopyInstrName), wInstrument);
    wKbdLetterPos = 0;
    while (wNameEditBuf[wKbdLetterPos]) {
        if (wKbdLetterPos == (wNameEditIsInstr ? 4 : 2))
            break;
        wKbdLetterPos++;
    }
    if (wCurrentScreen == 0x0D) {
        DrawProjTextField(4, 4, 0x0C, 9);
        wVramCursor = 0x98A5;
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("SAVE AS:");
    } else {
        DrawProjTextField(4, 5, 0x0C, 8);
    }
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    if (wCurrentScreen == 0x0D) {
        wVramCursor = 0x98C5;
        DrawString(wNameEditBuf);
        wKbdLetterPos = StringLength(wNameEditBuf);
        if (wKbdLetterPos == 8)
            wKbdLetterPos--;
        wKbdCursorCol = 9;
        wKbdCursorRow = 3;
    } else {
        wVramCursor = 0x98CA;
        for (i = 0; i < (wNameEditIsInstr ? 5 : 3); i++) {
            v = wNameEditBuf[i];
            DrawStringIndexed(v ? v : ' ');
        }
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor = 0x98C5;
        DrawString("NAME:");
    }
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = 0x9905;
    DrawString(AlnumChars);
    wVramCursor = 0x9925;
    DrawString(AlnumChars + 11);
    wVramCursor = 0x9945;
    DrawString(AlnumChars + 22);
    wVramCursor = 0x9965;
    DrawString(AlnumChars + 33);
    if (wCurrentScreen == 0x0D) {
        DrawTextField(5, 6, 0x0A, hGameboyType ? 3 : 2);
        DrawTextField(wKbdLetterPos + 5, 6, 1, hGameboyType ? 4 : 1);
    } else {
        DrawTextField(0x0A, 6, wNameEditIsInstr ? 5 : 3, hGameboyType ? 3 : 2);
        DrawTextField(wKbdLetterPos + 0x0A, 6, 1, hGameboyType ? 4 : 1);
    }
    DrawTextField(wKbdCursorCol + 5, wKbdCursorRow + 8, 1, hGameboyType ? 3 : 2);
    wCurrentScreen = 7;
}

void CloseNameEditor(void)
{
    UBYTE *p;

    if (wNameReturnScreen == 0x0D) {
        FarCall(0x1C, FAR_FN(FileStopSound));
        wFileListActive = 0;
        DrawProjTextField(2, 6, 0x10, 4);
        wVramCursor = 0x98E3;
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("SAVING...");
        switch (FarCallArgByte(0x1C, (UWORD)SaveFile, wFileCursor)) {
        case 0:
            wVramCursor = 0x98EC;
            DrawString("FAIL!");
            wVramCursor = 0x9903;
            DrawString("OUT OF MEMORY!");
            while (GetNextButtons())
                ;
            while (!GetNextButtons())
                ;
            break;
        case 1:
            wVramCursor = 0x98EC;
            DrawString("OK!  ");
            delay(hGameboyType ? 1500 : 750);
            break;
        case 2:
            wVramCursor = 0x98EC;
            DrawString("FAIL!");
            while (GetNextButtons())
                ;
            while (!GetNextButtons())
                ;
            break;
        }
        wRedrawPending = 0;
    } else if (wNameEditIsInstr) {
        FarCallArg(1, FAR_FN(WriteInstrName), wInstrument);
    } else {
        p = (UBYTE *)0xBDD0 + wSpeechCursorRow * 3;
        WriteSongByte(0, p++, wNameEditBuf[0]);
        WriteSongByte(0, p++, wNameEditBuf[1]);
        WriteSongByte(0, p, wNameEditBuf[2]);
    }
    wCurrentScreen = wNameReturnScreen;
    FarCall(3, FAR_FN(RedrawScreen));
}

void DrawNameEditCursor(UBYTE x, UBYTE n)
{
    UBYTE c;

    if (wKbdLetterPos == n && (c = wNameEditBuf[n])) {
        wNameEditBuf[n] = 0;
    } else if (wKbdLetterPos) {
        DrawTextField(x + wKbdLetterPos, 6, 1, hGameboyType ? 3 : 2);
        wKbdLetterPos--;
        wNameEditBuf[wKbdLetterPos] = 0;
        DrawTextField(x + wKbdLetterPos, 6, 1, hGameboyType ? 4 : 1);
    }
    GetTilemapAddr(x + wKbdLetterPos, 6);
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawStringIndexed(wNameEditBuf[wKbdLetterPos] ? wNameEditBuf[wKbdLetterPos] : ' ');
}

void SaveSyncMode(void)
{
    WriteSongByte(1, SramPropSyncMode, wSyncMode);
}

void DrawSyncMode2Field(void)
{
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    DrawHexByte(wSyncMode2);
    WriteSongByte(1, SramPropSyncMode2, wSyncMode2);
}

void SaveTempoProps(void)
{
    WriteSongByte(1, SramPropTempo, wTempo);
    WriteSongByte(1, SramPropTempoMode, wTempoMode);
}

void ProjDrawTempo(void)
{
    DrawTempo(GetTempoTimerValue());
}

void ProjRedrawTempo(void)
{
    while (wRedrawPending < 6)
        ;
    wVramCursor = (UBYTE *)0x9846;
    ProjDrawTempo();
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

void ProjDrawSyncField(void)
{
    UBYTE pal, y;

    pal = wProjCursor < 8;
    wProjDrawScratch2 = ProjCalcFieldTile();
    y = ProjFieldY[wProjCursor];
    DrawTextField(ProjFieldX[wProjCursor], y, wProjDrawScratch2, pal);
}

void ProjDrawSyncFieldHw(void)
{
    UBYTE pal, y;

    pal = hGameboyType ? 3 : 2;
    wProjDrawScratch3 = ProjCalcFieldTile();
    y = ProjFieldY[wProjCursor];
    DrawTextField(ProjFieldX[wProjCursor], y, wProjDrawScratch3, pal);
}

void DrawDecimalField(UBYTE v)
{
    if (v < 10)
        DrawStringIndexed('0');
    DrawFormat("%d", v);
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

void ProjRedrawPrelisten(void)
{
    wVramCursor = (UBYTE *)0x992A;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawPrelistenField();
}

void DrawSyncSubSetting(void)
{
    wVramCursor = 0x9A00;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    if (wPlayMode) {
        DrawFormat("PLAY %d:", (UWORD)wFrameCounterHi);
        DrawDecimalField(wFrameCounter);
        wProjDrawScratch = 0x0C;
        if (wFrameCounterHi > 9)
            wProjDrawScratch = 0x0D;
        else
            DrawStringTile20();
        DrawStringTile20();
        DrawStringTile20();
    } else {
        DrawFormat("WORKED %d:", (UWORD)ReadSongByte(1, 0xBFB2));
        DrawDecimalField(ReadSongByte(1, 0xBFB3));
    }
}

void DrawProjectScreen(void)
{
    wSramFsFlag = FarCallByte(0x1C, FAR_FN(CheckFileMagic64));
    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = 0x9846;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    ProjDrawTempo();
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
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = 0x99C0;
    if (wSramFsFlag)
        DrawString("LOAD/SAVE SONG");
    else
        DrawString("RESET MEMORY");
    wVramCursor = 0x9885;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawSyncField();
    DrawSyncSubSetting();
    wVramCursor = 0x9A26;
    DrawFormat2("%d:%d:", ReadSongByte(1, 0xBFB6), ReadSongByte(1, 0xBFB7));
    DrawDecimalField(ReadSongByte(1, 0xBFB8));
    ProjDrawSyncFieldHw();
    wTapTempoCount = 0xFF;
}

void ProjectScreenRefresh(void)
{
    while (GetNextButtons())
        ;
    while (!GetNextButtons())
        ;
    if (hGameboyType)
        ClearBgTilemap();
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawProjectScreen();
    FarCall(5, MapDrawTopRow);
    wRedrawPending = 0;
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

void InitKitMixer(void)
{
    UBYTE *dst;
    UBYTE *src;
    UBYTE i;
    UBYTE b;

    dst = (UBYTE *)0xD480;
    memcpy((UBYTE *)0xD000, KitMixTable, 0x480);
    for (i = 0; i != 0x10; i++) {
        src = (UBYTE *)(UWORD)MixerKernel;
        do {
            b = *src;
            *dst = b;
            dst = dst + 1;
            src = src + 1;
        } while (src != (UBYTE *)(UWORD)DrawHelpMenu);
        if (i != 0x0F) {
            *dst++ = 0x0C;
            *dst++ = 0x1C;
        }
    }
    *dst = 0xC9;
}

UWORD GetInstrEnvBytes(void)
{
    UWORD total;
    UBYTE v, v2, v3;
    char vol;
    UBYTE len;
    char vol2;
    UBYTE len2;
    char vol3;
    UBYTE len3;
    int d;

    total = 0;
    v = ReadCurInstrByte(1);
    vol = v >> 4;
    len = ExpoCurve16[v & 0x0F];
    if (len) {
        v2 = ReadCurInstrByte(9);
        vol2 = v2 >> 4;
        len2 = ExpoCurve16[v2 & 0x0F];
        total = Abs16(vol2 - vol);
        if (total == 0)
            total = 1;
        total = total * len;
        if (len2) {
            v3 = ReadCurInstrByte(10);
            vol3 = v3 >> 4;
            len3 = ExpoCurve16[v3 & 0x0F];
            d = vol3 - vol2;
            if (Abs16(d))
                d = Abs16(d);
            else
                d = 1;
            total += d * len2;
            d = vol3;
            total += d * len3;
        }
    }
    return total;
}

void UpdateEnvSprites(void)
{
    UBYTE i, t, u;

    for (i = 0; i != 8; i++) {
        t = i << 2;
        SetOAMByte(t + 0x0C, 0x40);
        u = (i << 3) + 0x30;
        SetOAMByte(t + 0x0D, u);
        SetOAMByte(t + 0x0E, u = i + 6);
    }
}
