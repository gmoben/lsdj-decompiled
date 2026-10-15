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
extern UBYTE wPlaySeconds;                 /* $C553 */
extern UBYTE wPlayMinutes;                 /* $C552 */
extern UBYTE wProjDrawScratch;              /* $C9A7 */
extern UBYTE wProjDrawScratch2;             /* $C9A8 */
extern UBYTE wProjDrawScratch3;             /* $C9A9 */

void DrawHelpMenu(void);
void ProjDrawHelpEntry(UBYTE selected);
void ProjRedrawHelpSelection(void);
void ProjDrawTempo(void);
UBYTE ProjSyncModeIs015(void);
void DrawPrelistenField(void);
UBYTE GetNextButtons(void);                 /* bank 0 */
UWORD StringLength(char *s);                /* bank 0 */
UBYTE ProjCursorFieldWidth(void);
void DrawNameEditArrows(char *s);
void ProjHelpSelectNext(void);
void ProjHelpOpenSelected(void);

extern UBYTE KitMixTable[];               /* $1B:$6C38, the 1152-byte nibble-sum LUT */
void MixerKernel(void);                     /* $1B:$4000, the 27-byte asm mixer step (code, hence a function); DrawHelpMenu follows it */

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
void DrawKeyboardCalibScreen(void);         /* bank 7 */
void FileValidateName(void);                /* bank $1C */
void LoadFileListState(void);               /* bank $1C */
void LoadProjSettings(void);             /* bank 1 */
void UiClearSram(void);                     /* bank 1 */
void MapMoveRight(void);                    /* bank 5 */
void MapMoveDown(void);                     /* bank 5 */
void TogglePlayback(void);                    /* bank 2 */

extern UBYTE wKbdLetterPos;                 /* $C42C */
extern char wNameEditBuf[];                 /* $C431, [5] + NUL (char: NameEditorInput) */
extern UBYTE wNameEditWrapLatch;            /* $C9A2 */

extern UBYTE wSramFsFlag;                   /* $C9A3 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wCloneMode;                    /* $C531 */
UBYTE CheckFileMagic64(void);               /* $1C:$4057 */
void DrawSyncField(void);
void DrawFontColorFields(void);             /* $1B:$569C, its own unit */

extern UBYTE ExpoCurve16[];                 /* $00:$300F, envelope speed -> frame count */
extern UBYTE wDecompressBuf[];                  /* $DA00 */
void PlotEnvCurve(UBYTE x, UBYTE y);
void OrBitsIntoDaBuf(void);              /* $00:$3261 */
void UploadTilesToVram(void);               /* $00:$3298 */
int Abs16(int v);                           /* $00:$343F */

extern UBYTE wNameReturnScreen;             /* $C42F */
extern UBYTE wFileListActive;               /* $C998 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wNameEditIsInstr;              /* $C430 */
extern UBYTE wInstr;                   /* $C576 */
void FileStopSound(void);                   /* $1C:$5098 */
UBYTE SaveFile(UBYTE f);                    /* $1C:$4E10 */
void WriteInstrName(UBYTE instr);           /* $01:$4A49 */
void DrawProjTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
void delay(UWORD d);
extern char AlnumChars[][11];               /* $1B:$4C6A, the name keyboard: "0123456789", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZx >w" */
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
    ColorField(0, e, len, selected ? 0 : 2);
}

void ProjRedrawHelpSelection(void)
{
    if (wHelpOpenEntry != 0)
        return;
    ProjDrawHelpEntry(0);
}

void DrawHelpScreen(void)
{
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawHelpMenu();
    ProjRedrawHelpSelection();
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

void GotoProjScreen(void)
{
    wCurScreen = 9;
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
            ColorField(start, row, col - start, 1);
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
                GotoProjScreen();
            else
                ProjHelpReset();
        }
        break;
    }
    return 1;
}

void DrawKbdCursor(void)
{
    UBYTE x, y;

    x = wKbdCursorCol + 5;
    y = wKbdCursorRow + 8;
    ColorField(x, y, 1, hGameboyType ? 3 : 2);
}

void EraseKbdCursor(void)
{
    ColorField(wKbdCursorCol + 5, wKbdCursorRow + 8, 1, 0);
}

/* A w x h frame at (x, y): corner/edge tiles through the identity map,
 * the interior cleared with tile 2 in palette 1. Inline bounds keep the
 * prologue free of `local := temp` copies, so SDCC 2.2.1's pre-loop dataflow
 * settles after two passes and the edge tests, `x + w - 1` and the ASCII
 * restore stay in the inner loop's preheader (an outer-loop invariant whose
 * definition has not reached that preheader is not hoisted further), where
 * the ROM has them. The `==` operands are written `i == x`: SDCC 2.2.1 swaps
 * them back.
 */
void DrawProjTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h)
{
    UBYTE i, j;

    for (i = x; i < x + w; i++) {
        for (j = y; j < y + h; j++) {
            ColorField(i, j, 1, 0);
            GetTilemapAddr(i, j);
            wStringSrc = IdentityCharMap;
            hBgPal = hGameboyType ? 0x48 : 0xD2;
            if (i == x) {
                if (j == y)
                    DrawStringIndexed(0);
                else if (j == y + h - 1)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if (i == x + w - 1) {
                if (j == y)
                    DrawStringIndexed(1);
                else if (j == y + h - 1)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (j == y)
                    DrawStringIndexed(4);
                else if (j == y + h - 1)
                    DrawStringIndexed(6);
                else {
                    hBgDigitPal = 4;
                    hBgPal = 1;
                    DrawStringIndexed(2);
                }
                wStringSrc = AsciiCharMap - 0x20;
            }
        }
    }
    wStringSrc = AsciiCharMap - 0x20;
    hBgDigitPal = 4;
    hBgPal = 1;
}

void OpenNameEditor(void)
{
    UBYTE i, v;

    ClearMarkerSprites();
    wNameReturnScreen = wCurScreen;
    if (wNameEditIsInstr)
        FarCallArg(1, FAR_FN(CopyInstrName), wInstr);
    wKbdLetterPos = 0;
    while (wNameEditBuf[wKbdLetterPos]) {
        if (wKbdLetterPos == (wNameEditIsInstr ? 4 : 2))
            break;
        wKbdLetterPos++;
    }
    if (wCurScreen == 0x0D) {
        DrawProjTextField(4, 4, 0x0C, 9);
        wVramCursor = 0x98A5;
        hBgDigitPal = 4;
        hBgPal = 1;
        DrawString("SAVE AS:");
    } else {
        DrawProjTextField(4, 5, 0x0C, 8);
    }
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    if (wCurScreen == 0x0D) {
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
        hBgDigitPal = 4;
        hBgPal = 1;
        wVramCursor = 0x98C5;
        DrawString("NAME:");
    }
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = 0x9905;
    DrawString(AlnumChars);
    wVramCursor = 0x9925;
    DrawString(AlnumChars[1]);
    wVramCursor = 0x9945;
    DrawString(AlnumChars[2]);
    wVramCursor = 0x9965;
    DrawString(AlnumChars[3]);
    if (wCurScreen == 0x0D) {
        ColorField(5, 6, 0x0A, hGameboyType ? 3 : 2);
        ColorField(wKbdLetterPos + 5, 6, 1, hGameboyType ? 4 : 1);
    } else {
        ColorField(0x0A, 6, wNameEditIsInstr ? 5 : 3, hGameboyType ? 3 : 2);
        ColorField(wKbdLetterPos + 0x0A, 6, 1, hGameboyType ? 4 : 1);
    }
    ColorField(wKbdCursorCol + 5, wKbdCursorRow + 8, 1, hGameboyType ? 3 : 2);
    wCurScreen = 7;
}

void CommitNameEdit(void)
{
    UBYTE *p;

    if (wNameReturnScreen == 0x0D) {
        FarCall(0x1C, FAR_FN(FileStopSound));
        wFileListActive = 0;
        DrawProjTextField(2, 6, 0x10, 4);
        wVramCursor = 0x98E3;
        hBgDigitPal = 4;
        hBgPal = 1;
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
        FarCallArg(1, FAR_FN(WriteInstrName), wInstr);
    } else {
        p = (UBYTE *)0xBDD0 + wSpeechCursorRow * 3;
        WriteSongByte(0, p++, wNameEditBuf[0]);
        WriteSongByte(0, p++, wNameEditBuf[1]);
        WriteSongByte(0, p, wNameEditBuf[2]);
    }
    wCurScreen = wNameReturnScreen;
    FarCall(3, FAR_FN(RedrawScreen));
}

void NameEditBackspace(UBYTE x, UBYTE n)
{
    UBYTE c;

    if (wKbdLetterPos == n && (c = wNameEditBuf[n])) {
        wNameEditBuf[n] = 0;
    } else if (wKbdLetterPos) {
        ColorField(x + wKbdLetterPos, 6, 1, hGameboyType ? 3 : 2);
        wKbdLetterPos--;
        wNameEditBuf[wKbdLetterPos] = 0;
        ColorField(x + wKbdLetterPos, 6, 1, hGameboyType ? 4 : 1);
    }
    GetTilemapAddr(x + wKbdLetterPos, 6);
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawStringIndexed(wNameEditBuf[wKbdLetterPos] ? wNameEditBuf[wKbdLetterPos] : ' ');
}

/*
 * The name editor's input (screen 7): an on-screen keyboard (AlnumChars,
 * drawn at (5, 8)) for an instrument name (5 letters), a speech word (3)
 * or, from the FILE screen's SAVE, the song's file name (8, at column 5).
 * The d-pad moves the keyboard cursor, wrapping round at an edge only on a
 * fresh press (wNameEditWrapLatch then holds it until the buttons change);
 * A types the key under it -- the last row's two right-hand keys are
 * backspace and done --, select is backspace, start is done and B leaves
 * without storing the name. 1 when the key was taken.
 */
UBYTE NameEditorInput(void)
{
    UBYTE ch, x, last;

    if (!wButtonsSame)
        wNameEditWrapLatch = 0;
    /* the name field's screen column and last letter */
    if (wNameReturnScreen == 0x0D) {
        x = 5;
        last = 7;
    } else {
        x = 0x0A;
        last = wNameEditIsInstr ? 4 : 2;
    }
    switch (wButtons) {
    case 0x10:                                  /* A */
        if (!wAPressEnabled)
            return 0;
        if (wKbdCursorRow == 3)
            switch (wKbdCursorCol) {
            case 8:
                NameEditBackspace(x, last);
                return 1;
            case 9:
                CommitNameEdit();
                return 1;
            }
        ch = AlnumChars[wKbdCursorRow][wKbdCursorCol];
        /* the buffer must be in RAM; a char pointer compares signed */
        if ((int)(wNameEditBuf + 1) > -1)
            DebugTrap();
        wNameEditBuf[wKbdLetterPos] = ch;
        wNameEditBuf[wKbdLetterPos + 1] = 0;
        GetTilemapAddr(x + wKbdLetterPos, 6);
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
        DrawStringIndexed(ch);
        if (wKbdLetterPos != last) {
            /* move the letter highlight on (storing the + 1 kept for the
               terminator, not an inc (hl)) */
            ColorField(x + wKbdLetterPos, 6, 1, hGameboyType ? 3 : 2);
            wKbdLetterPos = wKbdLetterPos + 1;
            ColorField(x + wKbdLetterPos, 6, 1, hGameboyType ? 4 : 1);
        }
        return 1;
    case 0x40:                                  /* select */
        NameEditBackspace(x, last);
        return 1;
    case 0x80:                                  /* start */
        CommitNameEdit();
        return 1;
    case 0x08:                                  /* down */
        EraseKbdCursor();
        if (wKbdCursorRow != 3 && !wNameEditWrapLatch)
            wKbdCursorRow++;
        else if (!wButtonsSame) {
            wKbdCursorRow = 0;
            wNameEditWrapLatch = 1;
        }
        DrawKbdCursor();
        return 1;
    case 0x01:                                  /* right */
        EraseKbdCursor();
        if (wKbdCursorCol != 9 && !wNameEditWrapLatch)
            wKbdCursorCol++;
        else if (!wButtonsSame) {
            wKbdCursorCol = 0;
            wNameEditWrapLatch = 1;
        }
        DrawKbdCursor();
        return 1;
    case 0x04:                                  /* up */
        EraseKbdCursor();
        if (wKbdCursorRow && !wNameEditWrapLatch)
            wKbdCursorRow--;
        else if (!wButtonsSame) {
            wKbdCursorRow = 3;
            wNameEditWrapLatch = 1;
        }
        DrawKbdCursor();
        return 1;
    case 0x02:                                  /* left */
        EraseKbdCursor();
        if (wKbdCursorCol && !wNameEditWrapLatch)
            wKbdCursorCol--;
        else if (!wButtonsSame) {
            wKbdCursorCol = 9;
            wNameEditWrapLatch = 1;
        }
        DrawKbdCursor();
        return 1;
    case 0x20:                                  /* B: leave unchanged */
        wCurScreen = wNameReturnScreen;
        FarCall(3, FAR_FN(RedrawScreen));
        return 1;
    }
    return 0;
}

void SaveSyncMode(void)
{
    WriteSongByte(1, SramPropSyncMode, wSyncMode);
}

void DrawSyncMode2Field(void)
{
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
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
    DrawTempo(GetTempoTimerVal());
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
    wProjDrawScratch2 = ProjCursorFieldWidth();
    y = ProjFieldY[wProjCursor];
    ColorField(ProjFieldX[wProjCursor], y, wProjDrawScratch2, pal);
}

void ProjDrawSyncFieldHw(void)
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
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawPrelistenField();
}

void DrawSyncSubSetting(void)
{
    wVramCursor = 0x9A00;
    hBgDigitPal = 4;
    hBgPal = 1;
    if (wPlayMode) {
        DrawFormat("PLAY %d:", (UWORD)wPlayMinutes);
        DrawDecimalField(wPlaySeconds);
        wProjDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
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

void DrawProjScreen(void)
{
    wSramFsFlag = FarCallByte(0x1C, FAR_FN(CheckFileMagic64));
    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = 0x9846;
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
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
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = 0x99C0;
    if (wSramFsFlag)
        DrawString("LOAD/SAVE SONG");
    else
        DrawString("RESET MEMORY");
    wVramCursor = 0x9885;
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawSyncField();
    DrawSyncSubSetting();
    wVramCursor = 0x9A26;
    DrawFormat2("%d:%d:", ReadSongByte(1, 0xBFB6), ReadSongByte(1, 0xBFB7));
    DrawDecimalField(ReadSongByte(1, 0xBFB8));
    ProjDrawSyncFieldHw();
    wTapTempoCount = 0xFF;
}

void ProjScreenRefresh(void)
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
    GetTilemapAddr(ProjFieldX[wProjCursor], ProjFieldY[wProjCursor]);
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
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
                ProjRedrawTempo();
                return 1;
            }
            if (wTempo != 0x27)
                wTempo++;
            else if (wTempoMode < 3)
                wTempoMode++;
            FarCall(7, FAR_FN(SetSyncSendRate));
            ProjDrawTempo();
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
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            DrawSyncField();
            ProjRedrawPrelisten();
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
                ProjRedrawTempo();
                return 1;
            }
            if (wTempoMode)
                wTempoMode--;
            else if (wTempo != 0x28)
                wTempo--;
            FarCall(7, FAR_FN(SetSyncSendRate));
            ProjDrawTempo();
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
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            DrawSyncField();
            ProjRedrawPrelisten();
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
            ProjDrawTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
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
            ProjDrawTempo();
            SaveTempoProps();
            return 1;
        case 1:
            if (!wTransposeDirty) {
                wSongTransposeDisp = wSongTranspose;
                wTransposeDirty++;
            }
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
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
            ProjDrawSyncField();
            if (wProjCursor < 0x0B)
                wProjCursor++;
            if (wProjCursor == 7 && !ProjSyncModeIs015())
                wProjCursor++;
            else if (wProjCursor == 3 && !ProjSyncModeIs567())
                wProjCursor++;
            ProjDrawSyncFieldHw();
        } else {
        menuRight:
            wProjMenuItem = 1;
            ColorField(5, 9, 3, 0);
            ColorField(12, 9, 2, hGameboyType ? 3 : 2);
        }
        return 1;
    case 0x04:
        if (!wProjMenuActive) {
            ProjDrawSyncField();
            if (wProjCursor)
                wProjCursor--;
            if (wProjCursor == 7 && !ProjSyncModeIs015())
                wProjCursor--;
            else if (wProjCursor == 3 && !ProjSyncModeIs567())
                wProjCursor--;
            ProjDrawSyncFieldHw();
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
            ProjDrawTempo();
            SaveTempoProps();
            return 1;
        case 9:
            FarCall(0x1C, FAR_FN(FileStopSound));
            ProjDrawSyncField();
            DrawProjTextField(3, 4, 0x0F, 8);
            FarCall(0x1F, FAR_FN(CleanSongData));
            ProjScreenRefresh();
            return 1;
        case 10:
            FarCall(0x1C, FAR_FN(FileStopSound));
            ProjDrawSyncField();
            DrawProjTextField(3, 4, 0x0F, 9);
            FarCall(0x1F, FAR_FN(CleanInstrData));
            ProjScreenRefresh();
            return 1;
        case 8:
            wCurScreen = 0x0E;
            FarCall(4, FAR_FN(RedrawScreen));
            return 1;
        case 3:
            if (wSyncMode == 5) {
                FarCall(0x1C, FAR_FN(FileStopSound));
                FarCall(7, DrawKeyboardCalibScreen);
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
                FarCall(1, FAR_FN(UiClearSram));
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

extern UBYTE wEnvPlotX;                     /* $CBD8, the last plotted column */

/* $7200: plot envelope column x (clamped to $3F) at height y into the
   graph buffer ($DA00, wDecompressBuf), filling the columns since wEnvPlotX
   with y and keeping the higher of the two at x. The buffer is addressed
   as a `char *`: the store's (char)y is the loop invariant the ROM holds in
   c, and the compare at x reuses that signed copy. */
void PlotEnvCurve(UBYTE x, UBYTE y)
{
    if (x > 0x3F)
        x = 0x3F;
    while (wEnvPlotX + 1 < x) {
        wEnvPlotX = wEnvPlotX + 1;
        if (wEnvPlotX >= 0x40)
            DebugTrap();
        ((char *)0xDA00)[wEnvPlotX] = y;
    }
    if (((char *)0xDA00)[x] < y)
        ((char *)0xDA00)[x] = y;
    wEnvPlotX = x;
}

/* The instrument screen's envelope graph: the attack/decay/release
   segments of instrument bytes 1, 9 and 10 plotted over 64 columns, the
   time axis scaled by GetInstrEnvBytes' total (500 at least) */
void ComputeEnv(void)
{
    UWORD pos;
    UWORD total;
    UBYTE v;
    UBYTE vol;
    UBYTE len;
    UBYTE v2;
    UBYTE vol2;
    UBYTE len2;
    UBYTE vol3;
    UBYTE len3;
    UBYTE v3;

    pos = 0;
    total = GetInstrEnvBytes();
    v = ReadCurInstrByte(1);
    vol = v >> 4;
    len = ExpoCurve16[v & 0x0F];
    if (total < 500)
        total = 500;
    total >>= 3;
    MemSet(wDecompressBuf, 0, 0xC0);
    PlotEnvCurve(0, vol);
    if (len) {
        v2 = ReadCurInstrByte(9);
        vol2 = v2 >> 4;
        len2 = ExpoCurve16[v2 & 0x0F];
        do {
            pos += len;
            PlotEnvCurve((pos << 3) / total, vol);
            if (vol2 < vol)
                vol--;
            else if (vol < vol2)
                vol++;
        } while (vol != vol2);
        if (len2) {
            v3 = ReadCurInstrByte(10);
            vol3 = v3 >> 4;
            len3 = ExpoCurve16[v3 & 0x0F];
            do {
                pos += len2;
                PlotEnvCurve((pos << 3) / total, vol);
                if (vol3 < vol)
                    vol--;
                else if (vol < vol3)
                    vol++;
            } while (vol != vol3);
            if (len3) {
                while (vol) {
                    pos += len3;
                    PlotEnvCurve((pos << 3) / total, vol);
                    vol--;
                }
            }
        }
    }
    PlotEnvCurve((pos << 3) / total, vol);
    PlotEnvCurve(0x3F, vol);
    OrBitsIntoDaBuf();
    UploadTilesToVram();
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

/* redraw the envelope graph and its sprites unless the instrument is a
   wave (1) or kit (2) */
void UpdateInstrEnv(void)
{
    UBYTE type;

    type = ReadCurInstrByte(0);
    if (type == 1 || type == 2)
        return;
    ComputeEnv();
    UpdateEnvSprites();
}
