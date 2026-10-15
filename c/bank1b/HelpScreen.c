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
extern UBYTE wHelpRow;                      /* $C934 */
extern char *HelpMenuPointers[];            /* $1B:$4058, NULL-terminated */
extern UBYTE HelpEntryLines[];              /* $1B:$408E, the selectable lines */
extern char **HelpPageRefs[];               /* $1B:$4BAF, &HelpPagePointers[k] per line */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
void DrawHelpMenu(void);
void DrawHelpCursor(UBYTE erase);
void RedrawHelpCursor(void);
UWORD StringLength(char *s);                /* bank 0 */
void DrawHelpPage(char *s);
void MoveHelpCursorDown(void);
void OpenHelpEntry(void);

/* $401B: draw the HELP menu: HelpMenuPointers[i] at column 0, row i, up to
   the NULL */
void DrawHelpMenu(void)
{
    UBYTE line;

    for (line = 0; HelpMenuPointers[line]; line++) {
        SetVramCursor(0, line);
        DrawString(HelpMenuPointers[line]);
    }
}

/* $49F8: the help menu's cursor: colour menu line HelpEntryLines[wHelpRow]
   across its text, palette 2 (highlight) or, with erase, 0 */
void DrawHelpCursor(UBYTE erase)
{
    UBYTE e;
    UBYTE length;

    e = HelpEntryLines[wHelpRow];
    length = StringLength(HelpMenuPointers[e]);
    ColorField(0, e, length, erase ? 0 : 2);
}

/* $4A4D: draw the help menu's cursor unless an entry is open
   (DrawHelpScreen's last step) */
void RedrawHelpCursor(void)
{
    if (wHelpOpenEntry != 0)
        return;
    DrawHelpCursor(0);
}

/* $4A62: Help screen (idx 14): renders the help/command-list text */
void DrawHelpScreen(void)
{
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    DrawHelpMenu();
    RedrawHelpCursor();
}

/* $4A79: move the help menu's cursor down (to row 9 at most): erase it,
   wHelpRow++, draw it */
void MoveHelpCursorDown(void)
{
    if ((UWORD)wHelpRow == 9)
        return;
    DrawHelpCursor(1);
    wHelpRow++;
    DrawHelpCursor(0);
}

/* $4AA6: move the help menu's cursor up (to row 0 at most): erase it,
   wHelpRow--, draw it */
void MoveHelpCursorUp(void)
{
    if (wHelpRow) {
        DrawHelpCursor(1);
        wHelpRow--;
        DrawHelpCursor(0);
    }
}

/* $4AC5: leave the help screen: wCurrentScreen = the project screen,
   RedrawScreen with bank 4 mapped */
void GotoProjectScreen(void)
{
    wCurrentScreen = SCREEN_PROJECT;
    FarCall(4, FAR_FN(RedrawScreen));
}

/* $4ADB: close the open help entry: wHelpOpenEntry = 0, ClearBgTilemap,
   DrawHelpScreen */
void CloseHelpEntry(void)
{
    wHelpOpenEntry = 0;
    ClearBgTilemap();
    DrawHelpScreen();
}

/* $4AE7: draw help page s from $9800: '\n' starts the next row, a '<'..'>'
   span is highlighted (ColorField), other bytes are glyphs */
void DrawHelpPage(char *s)
{
    UBYTE row;
    UBYTE column;
    UBYTE start;

    row = 0;
    column = 0;
    wVramCursor = BG_MAP(0, 0);
    while (*s) {
        switch (*s) {
        case '\n':
            SetVramCursor(0, ++row);
            column = 0;
            break;
        case '<':
            start = column;
            break;
        case '>':
            ColorField(start, row, column - start, 1);
            break;
        default:
            DrawStringIndexed(*s);
            column++;
        }
        ++s;
    }
}

/* $4B6D: open the help entry at wHelpRow: unhighlight its menu line, clear
   the BG map, set wHelpOpenEntry, draw its page (DrawHelpPage) */
void OpenHelpEntry(void)
{
    DrawHelpCursor(1);
    ClearBgTilemap();
    wHelpOpenEntry = HelpEntryLines[wHelpRow];
    DrawHelpPage(*HelpPageRefs[wHelpRow]);
}

/* $4BC3: HELP screen input: UP/DOWN move the selection, A opens the entry, B
   closes it or returns to the project screen */
UBYTE HelpScreenInput(void)
{
    UBYTE closed;

    closed = wHelpOpenEntry == 0;
    switch (wButtons) {
    case J_DOWN:
        if (closed)
            MoveHelpCursorDown();
        break;
    case J_UP:
        if (closed)
            MoveHelpCursorUp();
        break;
    case J_A:
        if (wAPressEnabled && closed)
            OpenHelpEntry();
        break;
    case J_B:
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
