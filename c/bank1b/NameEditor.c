/*
 * The name editor -- LSDj 9.4.0, ROM bank $1B.
 *
 * The on-screen keyboard's cursor, the edited name field, commit and
 * backspace, the input.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wNameEditColumn;               /* $C42D */
extern UBYTE wNameEditRow;                  /* $C42E */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
UBYTE GetNextButtons(void);                 /* bank 0 */
UWORD StringLength(char *s);                /* bank 0 */
extern UBYTE wNameEditPosition;             /* $C42C */
extern char wNameEditBuffer[];              /* $C431, [5] + NUL (char: NameEditorInput) */
extern UBYTE wNameEditWrapLatch;            /* $C9A2 */
extern UBYTE wNameReturnScreen;             /* $C42F */
extern UBYTE wFileListActive;               /* $C998 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wNameEditIsInstrument;         /* $C430 */
extern UBYTE wInstrument;                   /* $C576 */
void FileStopSound(void);                   /* $1C:$5098 */
UBYTE SaveFile(UBYTE f);                    /* $1C:$4E10 */
void WriteInstrumentName(UBYTE instrument); /* $01:$4A49 */
void DrawProjectTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
void delay(UWORD d);
extern char AlphanumericCharacters[][11];   /* $1B:$4C6A, the name keyboard: "0123456789", "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZx >w" */
void CopyInstrumentName(UBYTE instrument);  /* $01:$493B */

/* $4C30: highlight the name editor's keyboard key under the cursor ((5, 8) +
   wNameEditColumn/Row; palette 3 CGB / 2 DMG) */
void DrawNameEditCursor(void)
{
    UBYTE x, y;

    x = wNameEditColumn + 5;
    y = wNameEditRow + 8;
    ColorField(x, y, 1, hGameboyType ? 3 : 2);
}

/* $4C96: un-highlight the name editor's keyboard key under the cursor
   (palette 0) */
void EraseNameEditCursor(void)
{
    ColorField(wNameEditColumn + 5, wNameEditRow + 8, 1, 0);
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
void DrawProjectTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h)
{
    UBYTE column, row;

    for (column = x; column < x + w; column++) {
        for (row = y; row < y + h; row++) {
            ColorField(column, row, 1, 0);
            SetVramCursor(column, row);
            wStringSource = IdentityCharacterMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            if (column == x) {
                if (row == y)
                    DrawStringIndexed(0);
                else if (row == y + h - 1)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if (column == x + w - 1) {
                if (row == y)
                    DrawStringIndexed(1);
                else if (row == y + h - 1)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (row == y)
                    DrawStringIndexed(4);
                else if (row == y + h - 1)
                    DrawStringIndexed(6);
                else {
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawStringIndexed(2);
                }
                wStringSource = AsciiCharacterMap - 0x20;
            }
        }
    }
    wStringSource = AsciiCharacterMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
}

/* $4E8A: save screen -> wNameReturnScreen, init letter input
   (wNameEditPosition) for instr/file/word names */
void OpenNameEditor(void)
{
    UBYTE letter, v;

    ClearMarkerSprites();
    wNameReturnScreen = wCurrentScreen;
    if (wNameEditIsInstrument)
        FarCallArgument(1, FAR_FN(CopyInstrumentName), wInstrument);
    wNameEditPosition = 0;
    while (wNameEditBuffer[wNameEditPosition]) {
        if (wNameEditPosition == (wNameEditIsInstrument ? 4 : 2))
            break;
        wNameEditPosition++;
    }
    if (wCurrentScreen == SCREEN_FILE) {
        DrawProjectTextField(4, 4, 0x0C, 9);
        wVramCursor = BG_MAP(5, 5);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("SAVE AS:");
    } else {
        DrawProjectTextField(4, 5, 0x0C, 8);
    }
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wCurrentScreen == SCREEN_FILE) {
        wVramCursor = BG_MAP(5, 6);
        DrawString(wNameEditBuffer);
        wNameEditPosition = StringLength(wNameEditBuffer);
        if (wNameEditPosition == 8)
            wNameEditPosition--;
        wNameEditColumn = 9;
        wNameEditRow = 3;
    } else {
        wVramCursor = BG_MAP(10, 6);
        for (letter = 0; letter < (wNameEditIsInstrument ? 5 : 3); letter++) {
            v = wNameEditBuffer[letter];
            DrawStringIndexed(v ? v : ' ');
        }
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor = BG_MAP(5, 6);
        DrawString("NAME:");
    }
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = BG_MAP(5, 8);
    DrawString(AlphanumericCharacters);
    wVramCursor = BG_MAP(5, 9);
    DrawString(AlphanumericCharacters[1]);
    wVramCursor = BG_MAP(5, 10);
    DrawString(AlphanumericCharacters[2]);
    wVramCursor = BG_MAP(5, 11);
    DrawString(AlphanumericCharacters[3]);
    if (wCurrentScreen == SCREEN_FILE) {
        ColorField(5, 6, 0x0A, hGameboyType ? 3 : 2);
        ColorField(wNameEditPosition + 5, 6, 1, hGameboyType ? 4 : 1);
    } else {
        ColorField(0x0A, 6, wNameEditIsInstrument ? 5 : 3, hGameboyType ? 3 : 2);
        ColorField(wNameEditPosition + 0x0A, 6, 1, hGameboyType ? 4 : 1);
    }
    ColorField(wNameEditColumn + 5, wNameEditRow + 8, 1, hGameboyType ? 3 : 2);
    wCurrentScreen = SCREEN_NAME_EDITOR;
}

/* $5126: name-editor done (start, or the keyboard's done key): store the
   name (instrument, speech word) or save the song under it (FILE flow:
   "SAVING..."), back to wNameReturnScreen */
void CommitNameEdit(void)
{
    UBYTE *p;

    if (wNameReturnScreen == 0x0D) {
        FarCall(0x1C, FAR_FN(FileStopSound));
        wFileListActive = 0;
        DrawProjectTextField(2, 6, 0x10, 4);
        wVramCursor = BG_MAP(3, 7);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("SAVING...");
        switch (FarCallArgumentByte(0x1C, (UWORD)SaveFile, wFileCursor)) {
        case 0:
            wVramCursor = BG_MAP(12, 7);
            DrawString("FAIL!");
            wVramCursor = BG_MAP(3, 8);
            DrawString("OUT OF MEMORY!");
            while (GetNextButtons())
                ;
            while (!GetNextButtons())
                ;
            break;
        case 1:
            wVramCursor = BG_MAP(12, 7);
            DrawString("OK!  ");
            delay(hGameboyType ? 1500 : 750);
            break;
        case 2:
            wVramCursor = BG_MAP(12, 7);
            DrawString("FAIL!");
            while (GetNextButtons())
                ;
            while (!GetNextButtons())
                ;
            break;
        }
        wRedrawPending = 0;
    } else if (wNameEditIsInstrument) {
        FarCallArgument(1, FAR_FN(WriteInstrumentName), wInstrument);
    } else {
        p = SramWordNames + wSpeechRow * 3;
        WriteSongByte(0, p++, wNameEditBuffer[0]);
        WriteSongByte(0, p++, wNameEditBuffer[1]);
        WriteSongByte(0, p, wNameEditBuffer[2]);
    }
    wCurrentScreen = wNameReturnScreen;
    FarCall(3, FAR_FN(RedrawScreen));
}

/* $5303: name-editor backspace (select, or the keyboard's backspace key):
   clear the letter at the last slot, else step the letter highlight back and
   clear the letter there */
void BackspaceNameEdit(UBYTE x, UBYTE n)
{
    UBYTE c;

    if (wNameEditPosition == n && (c = wNameEditBuffer[n])) {
        wNameEditBuffer[n] = 0;
    } else if (wNameEditPosition) {
        ColorField(x + wNameEditPosition, 6, 1, hGameboyType ? 3 : 2);
        wNameEditPosition--;
        wNameEditBuffer[wNameEditPosition] = 0;
        ColorField(x + wNameEditPosition, 6, 1, hGameboyType ? 4 : 1);
    }
    SetVramCursor(x + wNameEditPosition, 6);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawStringIndexed(wNameEditBuffer[wNameEditPosition] ? wNameEditBuffer[wNameEditPosition] : ' ');
}

/*
 * The name editor's input (screen 7): an on-screen keyboard (AlphanumericCharacters,
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
    UBYTE character, x, last;

    if (!wButtonsSame)
        wNameEditWrapLatch = 0;
    /* the name field's screen column and last letter */
    if (wNameReturnScreen == 0x0D) {
        x = 5;
        last = 7;
    } else {
        x = 0x0A;
        last = wNameEditIsInstrument ? 4 : 2;
    }
    switch (wButtons) {
    case J_A:                                  /* A */
        if (!wAPressEnabled)
            return 0;
        if (wNameEditRow == 3)
            switch (wNameEditColumn) {
            case 8:
                BackspaceNameEdit(x, last);
                return 1;
            case 9:
                CommitNameEdit();
                return 1;
            }
        character = AlphanumericCharacters[wNameEditRow][wNameEditColumn];
        /* the buffer must be in RAM; a char pointer compares signed */
        if ((int)(wNameEditBuffer + 1) > -1)
            DebugTrap();
        wNameEditBuffer[wNameEditPosition] = character;
        wNameEditBuffer[wNameEditPosition + 1] = 0;
        SetVramCursor(x + wNameEditPosition, 6);
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawStringIndexed(character);
        if (wNameEditPosition != last) {
            /* move the letter highlight on (storing the + 1 kept for the
               terminator, not an inc (hl)) */
            ColorField(x + wNameEditPosition, 6, 1, hGameboyType ? 3 : 2);
            wNameEditPosition = wNameEditPosition + 1;
            ColorField(x + wNameEditPosition, 6, 1, hGameboyType ? 4 : 1);
        }
        return 1;
    case J_SELECT:                                  /* select */
        BackspaceNameEdit(x, last);
        return 1;
    case J_START:                                  /* start */
        CommitNameEdit();
        return 1;
    case J_DOWN:                                  /* down */
        EraseNameEditCursor();
        if (wNameEditRow != 3 && !wNameEditWrapLatch)
            wNameEditRow++;
        else if (!wButtonsSame) {
            wNameEditRow = 0;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case J_RIGHT:                                  /* right */
        EraseNameEditCursor();
        if (wNameEditColumn != 9 && !wNameEditWrapLatch)
            wNameEditColumn++;
        else if (!wButtonsSame) {
            wNameEditColumn = 0;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case J_UP:                                  /* up */
        EraseNameEditCursor();
        if (wNameEditRow && !wNameEditWrapLatch)
            wNameEditRow--;
        else if (!wButtonsSame) {
            wNameEditRow = 3;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case J_LEFT:                                  /* left */
        EraseNameEditCursor();
        if (wNameEditColumn && !wNameEditWrapLatch)
            wNameEditColumn--;
        else if (!wButtonsSame) {
            wNameEditColumn = 9;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case J_B:                                  /* B: leave unchanged */
        wCurrentScreen = wNameReturnScreen;
        FarCall(3, FAR_FN(RedrawScreen));
        return 1;
    }
    return 0;
}
