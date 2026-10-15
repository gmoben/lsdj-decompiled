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

extern UBYTE wNameEditCol;                 /* $C42D */
extern UBYTE wNameEditRow;                 /* $C42E */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
UBYTE GetNextButtons(void);                 /* bank 0 */
UWORD StrLength(char *s);                /* bank 0 */
extern UBYTE wNameEditPos;                 /* $C42C */
extern char wNameEditBuf[];                 /* $C431, [5] + NUL (char: NameEditorInput) */
extern UBYTE wNameEditWrapLatch;            /* $C9A2 */
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

void DrawNameEditCursor(void)
{
    UBYTE x, y;

    x = wNameEditCol + 5;
    y = wNameEditRow + 8;
    ColorField(x, y, 1, hGameboyType ? 3 : 2);
}

void EraseNameEditCursor(void)
{
    ColorField(wNameEditCol + 5, wNameEditRow + 8, 1, 0);
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
            SetVramCursor(i, j);
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
    wNameEditPos = 0;
    while (wNameEditBuf[wNameEditPos]) {
        if (wNameEditPos == (wNameEditIsInstr ? 4 : 2))
            break;
        wNameEditPos++;
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
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    if (wCurScreen == 0x0D) {
        wVramCursor = 0x98C5;
        DrawString(wNameEditBuf);
        wNameEditPos = StrLength(wNameEditBuf);
        if (wNameEditPos == 8)
            wNameEditPos--;
        wNameEditCol = 9;
        wNameEditRow = 3;
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
        ColorField(wNameEditPos + 5, 6, 1, hGameboyType ? 4 : 1);
    } else {
        ColorField(0x0A, 6, wNameEditIsInstr ? 5 : 3, hGameboyType ? 3 : 2);
        ColorField(wNameEditPos + 0x0A, 6, 1, hGameboyType ? 4 : 1);
    }
    ColorField(wNameEditCol + 5, wNameEditRow + 8, 1, hGameboyType ? 3 : 2);
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
        p = (UBYTE *)0xBDD0 + wSpeechRow * 3;
        WriteSongByte(0, p++, wNameEditBuf[0]);
        WriteSongByte(0, p++, wNameEditBuf[1]);
        WriteSongByte(0, p, wNameEditBuf[2]);
    }
    wCurScreen = wNameReturnScreen;
    FarCall(3, FAR_FN(RedrawScreen));
}

void BackspaceNameEdit(UBYTE x, UBYTE n)
{
    UBYTE c;

    if (wNameEditPos == n && (c = wNameEditBuf[n])) {
        wNameEditBuf[n] = 0;
    } else if (wNameEditPos) {
        ColorField(x + wNameEditPos, 6, 1, hGameboyType ? 3 : 2);
        wNameEditPos--;
        wNameEditBuf[wNameEditPos] = 0;
        ColorField(x + wNameEditPos, 6, 1, hGameboyType ? 4 : 1);
    }
    SetVramCursor(x + wNameEditPos, 6);
    hBgDigitPal = wShadedDigitPal;
    hBgPal = wShadedPal;
    DrawStringIndexed(wNameEditBuf[wNameEditPos] ? wNameEditBuf[wNameEditPos] : ' ');
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
        if (wNameEditRow == 3)
            switch (wNameEditCol) {
            case 8:
                BackspaceNameEdit(x, last);
                return 1;
            case 9:
                CommitNameEdit();
                return 1;
            }
        ch = AlnumChars[wNameEditRow][wNameEditCol];
        /* the buffer must be in RAM; a char pointer compares signed */
        if ((int)(wNameEditBuf + 1) > -1)
            DebugTrap();
        wNameEditBuf[wNameEditPos] = ch;
        wNameEditBuf[wNameEditPos + 1] = 0;
        SetVramCursor(x + wNameEditPos, 6);
        hBgDigitPal = wShadedDigitPal;
        hBgPal = wShadedPal;
        DrawStringIndexed(ch);
        if (wNameEditPos != last) {
            /* move the letter highlight on (storing the + 1 kept for the
               terminator, not an inc (hl)) */
            ColorField(x + wNameEditPos, 6, 1, hGameboyType ? 3 : 2);
            wNameEditPos = wNameEditPos + 1;
            ColorField(x + wNameEditPos, 6, 1, hGameboyType ? 4 : 1);
        }
        return 1;
    case 0x40:                                  /* select */
        BackspaceNameEdit(x, last);
        return 1;
    case 0x80:                                  /* start */
        CommitNameEdit();
        return 1;
    case 0x08:                                  /* down */
        EraseNameEditCursor();
        if (wNameEditRow != 3 && !wNameEditWrapLatch)
            wNameEditRow++;
        else if (!wButtonsSame) {
            wNameEditRow = 0;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case 0x01:                                  /* right */
        EraseNameEditCursor();
        if (wNameEditCol != 9 && !wNameEditWrapLatch)
            wNameEditCol++;
        else if (!wButtonsSame) {
            wNameEditCol = 0;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case 0x04:                                  /* up */
        EraseNameEditCursor();
        if (wNameEditRow && !wNameEditWrapLatch)
            wNameEditRow--;
        else if (!wButtonsSame) {
            wNameEditRow = 3;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case 0x02:                                  /* left */
        EraseNameEditCursor();
        if (wNameEditCol && !wNameEditWrapLatch)
            wNameEditCol--;
        else if (!wButtonsSame) {
            wNameEditCol = 9;
            wNameEditWrapLatch = 1;
        }
        DrawNameEditCursor();
        return 1;
    case 0x20:                                  /* B: leave unchanged */
        wCurScreen = wNameReturnScreen;
        FarCall(3, FAR_FN(RedrawScreen));
        return 1;
    }
    return 0;
}
