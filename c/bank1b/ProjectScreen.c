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
