/*
 * The help screen -- LSDj 9.4.0, ROM bank $1B.
 *
 * The menu, its entries and pages, the input.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wHelpOpenEntry;                /* $C933, the open HELP entry (0 = none) */
extern UBYTE wHelpRow;                /* $C934 */
extern char *HelpMenuPtrs[];                /* $1B:$4058, NULL-terminated */
extern UBYTE HelpEntryLines[];             /* $1B:$408E, the selectable lines */
extern char **HelpPageRefs[];               /* $1B:$4BAF, &HelpPagePtrs[k] per line */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
void DrawHelpMenu(void);
void DrawHelpCursor(UBYTE erase);
void RedrawHelpCursor(void);
UWORD StrLength(char *s);                /* bank 0 */
void DrawHelpPage(char *s);
void MoveHelpCursorDown(void);
void OpenHelpEntry(void);

void DrawHelpMenu(void)
{
    UBYTE i;

    for (i = 0; HelpMenuPtrs[i]; i++) {
        SetVramCursor(0, i);
        DrawString(HelpMenuPtrs[i]);
    }
}

void DrawHelpCursor(UBYTE erase)
{
    UBYTE e;
    UBYTE len;

    e = HelpEntryLines[wHelpRow];
    len = StrLength(HelpMenuPtrs[e]);
    ColorField(0, e, len, erase ? 0 : 2);
}

void RedrawHelpCursor(void)
{
    if (wHelpOpenEntry != 0)
        return;
    DrawHelpCursor(0);
}

void DrawHelpScreen(void)
{
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    DrawHelpMenu();
    RedrawHelpCursor();
}

void MoveHelpCursorDown(void)
{
    if ((UWORD)wHelpRow == 9)
        return;
    DrawHelpCursor(1);
    wHelpRow++;
    DrawHelpCursor(0);
}

void MoveHelpCursorUp(void)
{
    if (wHelpRow) {
        DrawHelpCursor(1);
        wHelpRow--;
        DrawHelpCursor(0);
    }
}

void GotoProjectScreen(void)
{
    wCurrentScreen = 9;
    FarCall(4, FAR_FN(RedrawScreen));
}

void CloseHelpEntry(void)
{
    wHelpOpenEntry = 0;
    ClearBgTilemap();
    DrawHelpScreen();
}

void DrawHelpPage(char *s)
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
            SetVramCursor(0, ++row);
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

void OpenHelpEntry(void)
{
    DrawHelpCursor(1);
    ClearBgTilemap();
    wHelpOpenEntry = HelpEntryLines[wHelpRow];
    DrawHelpPage(*HelpPageRefs[wHelpRow]);
}

UBYTE HelpScreenInput(void)
{
    UBYTE closed;

    closed = wHelpOpenEntry == 0;
    switch (wButtons) {
    case 8:
        if (closed)
            MoveHelpCursorDown();
        break;
    case 4:
        if (closed)
            MoveHelpCursorUp();
        break;
    case 0x10:
        if (wAPressEnabled && closed)
            OpenHelpEntry();
        break;
    case 0x20:
        if (wAPressEnabled) {
            if (closed)
                GotoProjectScreen();
            else
                CloseHelpEntry();
        }
        break;
    }
    return 1;
}
