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
extern UBYTE wTempo;                        /* $C529 */
extern UBYTE wTempoMode;                    /* $C52A */
extern UBYTE wKeyDelay;                     /* $C52B */
extern UBYTE wKeyRepeat;                    /* $C52C */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPrelisten;                    /* $C56E */
extern char *HelpMenuPtrs[];                /* $1B:$4058, NULL-terminated */

UWORD GetTempoTimerValue(void);
void DrawTempo(UWORD v);
void ClearBgTilemap(void);
void DrawFormat(char *fmt, UWORD v);        /* printf-style, varargs */
void DrawHelpMenu(void);
void ProjDrawHelpEntry(UBYTE selected);
void ProjRedrawHelpSel(void);
void ProjDrawTempo(void);
UBYTE ProjSyncModeIs015(void);
void DrawPrelistenField(void);

void DrawHelpMenu(void)
{
    UBYTE i;

    for (i = 0; HelpMenuPtrs[i]; i++) {
        GetTilemapAddr(0, i);
        DrawString(HelpMenuPtrs[i]);
    }
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
