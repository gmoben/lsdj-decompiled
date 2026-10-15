/*
 * The groove screen -- LSDj 9.4.0, ROM bank $1D.
 *
 * Its steps and rows, the cursor, the update and the step cache, the input,
 * cut/copy/paste and the selection.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wGrooveCacheValid;             /* $C9D5 */
extern UBYTE wGrooveStepCache[];            /* $C9D6, [16] */
extern UBYTE *wGrooveCachePtr;              /* $C9E6 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wGroove;                       /* $C417 */
extern char wGrooveRow;                     /* $C9D4, a plain char */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wMapCol;                       /* $C8FF */
extern UBYTE wChanGroove[];                 /* $C150, [4] */
extern UBYTE wChanGrooveRow[];              /* $C158, [4] */
void WriteGrooveByte(UBYTE step, UBYTE v);
UBYTE ReadGrooveStep(UBYTE step);
void DrawGrooveSteps(void);
void DrawGrooveRow(void);
void SendLinkClipboard(void);                   /* $1C:$71CD */
void DrawAllChanStatus(void);
void CutCopyGrooveSelection(UBYTE cut);
void PasteGrooveSelection(void);
void DrawGrooveSelection(void);
void MapMoveUp(void);                       /* bank 5 */
void SaveSelectionState(void);              /* bank 5 */
void ToggleSongPlayback(void);                 /* bank 2 */
void TogglePlayback(void);                    /* bank 2 */

void WriteGrooveByte(UBYTE step, UBYTE v)
{
    UBYTE *p;

    if (step & 0xF0)
        DebugTrap();
    if (wGroove >= 0x20)
        DebugTrap();
    p = SramGrooves + ((UWORD)wGroove << 4);
    WriteSongByte(0, p + (unsigned int)step, v);
}

UBYTE ReadGrooveStep(UBYTE step)
{
    UBYTE *p;

    if (step & 0xF0)
        DebugTrap();
    if (wGroove >= 0x20)
        DebugTrap();
    p = SramGrooves + ((UWORD)wGroove << 4);
    return ReadSongByte(0, p + (unsigned int)step);
}

void EraseGrooveCursor(void)
{
    UBYTE r, pal;

    r = wGrooveRow;
    if (r & 0xF0)
        DebugTrap();
    pal = (wGrooveRow & 2) ? 0 : 1;
    ColorField(2, wGrooveRow + 2, 2, pal);
}

void DrawGrooveSteps(void)
{
    UBYTE i;
    UBYTE v;

    if (wGroove >= 0x20)
        DebugTrap();
    ReadSongBytes(0, (UBYTE *)(((UWORD)wGroove << 4) + (UWORD)0xB090), wSongReadBuf);
    hBgDigitPal = 4;
    hBgPal = 1;
    for (i = 0; i < 0x10; i++) {
        SetVramCursor(1, i + 2);
        v = wSongReadBuf[i];
        if (!v)
            DrawString(" --    ");
        else {
            DrawStringTile20();
            DrawHexByte(v);
            DrawString("    ");
        }
        if (i == wGrooveRow)
            v = hGameboyType ? 3 : 2;
        else
            v = (i & 2) ? 0 : 1;
        ColorField(2, i + 2, 2, v);
    }
    DrawGrooveRow();
}

void DrawGrooveScreen(void)
{
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    DrawGrooveSteps();
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
}

void GrooveScreenUpdate(void)
{
    if (wPlayMode && wGroove == wChanGroove[wSongCol])
        SetSpriteY(0, wChanGrooveRow[wSongCol]);
    else
        ClearSpriteY(0);
}

void DrawGrooveRow(void)
{
    UBYTE i;
    UWORD a, b;
    UBYTE q;

    i = 0;
    while (i != 0x10) {
        if (wGrooveCacheValid) {
            a = wGrooveStepCache[i];
            b = wGrooveStepCache[i + 1];
        } else {
            a = ReadGrooveStep(i);
            b = ReadGrooveStep(i + 1);
        }
        if (b) {
            SetVramCursor(5, i + 2);
            if (i & 2) {
                hBgDigitPal = 4;
                hBgPal = 1;
            } else {
                hBgDigitPal = wShadedDigitPal;
                hBgPal = wShadedPal;
            }
            q = a * 100 / (a + b);
            printf("%d", (UWORD)q);
            DrawStringIndexed('%');
            if (q < 10)
                DrawStringTile20();
        }
        i += 2;
    }
}

void BuildGrooveCache(void)
{
    UBYTE i, v;

    if (wGrooveCacheValid)
        return;
    wGrooveCacheValid = 1;
    for (i = 0; i != 16; i++) {
        wGrooveCachePtr = wGrooveStepCache + i;
        v = ReadGrooveStep(i);
        *wGrooveCachePtr = v;
    }
}

void DrawGrooveCursor(void)
{
    UBYTE r;

    r = wGrooveRow;
    if (r & 0xF0)
        DebugTrap();
    ColorField(2, wGrooveRow + 2, 2, hGameboyType ? 3 : 2);
}

void SetGrooveRowPals(UBYTE row)
{
    UBYTE a, b;

    a = (wCursorMode == 4) ? wSelectionTop : wGrooveRow;
    b = wGrooveRow;
    SortBytePair(&a, &b);
    if (row >= a && b >= row) {
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
    } else if (row & 2) {
        hBgDigitPal = 4;
        hBgPal = 1;
    } else {
        hBgDigitPal = wShadedDigitPal;
        hBgPal = wShadedPal;
    }
}

/*
 * The groove screen's input (screen 2): A sets an empty step to 6, A+left/
 * right step the values under the cursor or the selection, A+up/down move
 * ticks between the two steps of each pair, A+B deletes a step and pulls
 * the rest up, B+direction picks another groove, select+B/A/B start, copy,
 * cut and paste a selection, start leaves for the screen to the left of
 * the groove on the map. 1 when the key was taken.
 */
UBYTE GrooveScreenInput(void)
{
    UBYTE top;
    UBYTE bot;
    UBYTE v;
    UBYTE v2;
    UBYTE end;

    v = wGrooveRow + 2;
    hBgDigitPal = wHighlightDigitPal;
    hBgPal = wHighlightPal;
    switch (wButtons) {
    case 0x43:
        if (wCursorMode == 4) {
            wGrooveRow = wSelectionTop;
            FarCall(5, 0x747D);
            if (hGameboyType) {
                FarCall(1, FAR_FN(UiDrawTextHw));
                FarCall(5, MapDrawTopRow);
            }
            DrawGrooveScreen();
            DrawGrooveCursor();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case 0x10:
        if (!wAPressEnabled)
            break;
        if (!ReadGrooveStep(wGrooveRow)) {
            SetVramCursor(2, v);
            DrawHexByte(6);
            WriteGrooveByte(wGrooveRow, 6);
        }
        DrawGrooveRow();
        return 1;
    case 0x04:
        EraseGrooveCursor();
        if (wGrooveRow)
            wGrooveRow--;
        DrawGrooveCursor();
        if (wCursorMode == 4)
            DrawGrooveSelection();
        DrawGrooveRow();
        return 1;
    case 0x08:
        EraseGrooveCursor();
        wGrooveRow++;
        if (wGrooveRow == 16 || !ReadGrooveStep(wGrooveRow - 1))
            wGrooveRow--;
        DrawGrooveCursor();
        if (wCursorMode == 4)
            DrawGrooveSelection();
        DrawGrooveRow();
        return 1;
    case 0x11:
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bot = wGrooveRow;
            SortBytePair(&top, &bot);
        } else {
            bot = wGrooveRow;
            top = wGrooveRow;
        }
        bot++;
        for (; top < bot; top++) {
            v = wGrooveStepCache[top];
            if (v < 0xFF)
                v++;
            wGrooveStepCache[top] = v;
            SetVramCursor(2, top + 2);
            DrawHexByte(v);
        }
        DrawGrooveRow();
        return 1;
    case 0x12:
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bot = wGrooveRow;
            SortBytePair(&top, &bot);
        } else {
            bot = wGrooveRow;
            top = wGrooveRow;
        }
        end = bot;
        end++;
        for (; top < end; top++) {
            SetVramCursor(2, top + 2);
            v = wGrooveStepCache[top];
            if (v > 1)
                v--;
            wGrooveStepCache[top] = v;
            DrawHexByte(v);
        }
        DrawGrooveRow();
        return 1;
    case 0x14:
    case 0x18:
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bot = wGrooveRow;
            SortBytePair(&top, &bot);
        } else {
            bot = wGrooveRow;
            top = wGrooveRow;
        }
        top &= 0xFE;
        bot = (bot & 0xFE) + 2;
        while (top != bot) {
            v2 = wGrooveStepCache[top + 1];
            v = wGrooveStepCache[top];
            if (!v2)
                v2 = v;
            if (wButtons == 0x14) {
                v++;
                v2--;
            } else {
                v--;
                v2++;
            }
            if (v2 && v) {
                wGrooveStepCache[top] = v;
                SetVramCursor(2, top + 2);
                SetGrooveRowPals(top);
                DrawHexByte(v);
                wGrooveStepCache[top + 1] = v2;
                SetVramCursor(2, top + 3);
                SetGrooveRowPals(top + 1);
                DrawHexByte(v2);
            }
            top += 2;
        }
        DrawGrooveRow();
        return 1;
    case 0x21:
        if (wGroove < 0x1F)
            wGroove++;
        wMessageStrPtr = 0;
        DrawGrooveScreen();
        return 1;
    case 0x24:
        if (wGroove < 0x10)
            wGroove += 0x10;
        else
            wGroove = 0x1F;
        wMessageStrPtr = 0;
        DrawGrooveScreen();
        return 1;
    case 0x28:
        if (wGroove & 0x10)
            wGroove -= 0x10;
        else
            wGroove = 0;
        wMessageStrPtr = 0;
        DrawGrooveScreen();
        return 1;
    case 0x22:
        if (wGroove)
            wGroove--;
        wMessageStrPtr = 0;
        DrawGrooveScreen();
        return 1;
    case 0x30:
        if (!wAPressEnabled)
            break;
        wAPressEnabled = 0;
        if (!ReadGrooveStep(wGrooveRow))
            break;
        if (!wGrooveRow && !ReadGrooveStep(1))
            break;
        EraseGrooveCursor();
        wCursorMode = 2;
        wSelectionLeft = 0;
        wSelectionRight = 1;
        wSelectionTop = wGrooveRow;
        wSelectionBottom = wSelectionTop + 1;
        wClipboardType = 2;
        WriteClipboardCell(0, wGrooveRow, ReadGrooveStep(wGrooveRow));
        top = wGrooveRow;
        while (1) {
            if (top == 0x0F) {
                WriteGrooveByte(top, 0);
                break;
            }
            WriteGrooveByte(top, ReadGrooveStep(top + 1));
            top = top + 1;
        }
        DrawGrooveScreen();
        return 1;
    case 0x50:
        if (wCursorMode == 1)
            break;
        if (!wSelectHeld)
            break;
        if (wCursorMode == 4) {
            wSelectHeld = 0;
            CutCopyGrooveSelection(1);
            break;
        }
        wSelectHeld = 0;
        if (wSelectionLeft)
            break;
        EraseGrooveCursor();
        PasteGrooveSelection();
        return 1;
    case 0xC0:
        if (!wButtonsSame)
            FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        switch ((char)wMapCol) {
        case 0:
            wCurScreen = 4;
            break;
        case 1:
            wCurScreen = 3;
            break;
        }
        FarCall(2, FAR_FN(TogglePlayback));
        wCurScreen = 2;
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveUp);
        FarCall(5, FAR_FN(RedrawScreen));
        return 1;
    case 0x20:
        if (wCursorMode != 4)
            break;
        if (!wAPressEnabled)
            break;
        CutCopyGrooveSelection(0);
        return 1;
    case 0x60:
        if (!wSelectHeld)
            break;
        if (wCursorMode == 4 && wClipboardType == 2) {
            top = wSelectionTop;
            bot = wGrooveRow;
            SortBytePair(&top, &bot);
            if (top || bot != 0x0F) {
                wGrooveRow = 0x0F;
                while (wGrooveRow && !ReadGrooveStep(wGrooveRow))
                    wGrooveRow--;
                wSelectionTop = 0;
                wSelectHeld = 0;
                DrawGrooveSelection();
                return 1;
            }
            CutCopyGrooveSelection(0);
            return 1;
        }
        ShowMessage("SELECT");
        FarCall(5, SaveSelectionState);
        wSelectionLeft = 0;
        wSelectionTop = wGrooveRow;
        wSelectHeld = 0;
        return 1;
    }
    return 0;
}

void CutCopyGrooveSelection(UBYTE cut)
{
    UBYTE row;

    row = wGrooveRow;
    SortBytePair(&wSelectionTop, &row);
    if (ReadGrooveStep(row))
        row++;
    if (wSelectionTop == row)
        return;
    wCursorMode = 2;
    wClipboardType = 2;
    wSelectionLeft = 0;
    wSelectionRight = 1;
    wSelectionBottom = row;
    ShowMessage(cut ? "CUT" : "COPY");
    for (row = wSelectionTop; row < wSelectionBottom; row++) {
        wGrooveRow = row;
        EraseGrooveCursor();
        WriteClipboardCell(0, row, ReadGrooveStep(row));
        if (cut)
            WriteGrooveByte(row, row ? 0 : 6);
    }
    wGrooveRow = wSelectionTop;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    DrawGrooveSteps();
}

void PasteGrooveSelection(void)
{
    UBYTE i;

    if (wClipboardType != 2)
        return;
    if ((UBYTE)(wSelectionBottom - wSelectionTop) > 1)
        ShowMessage("PASTE");
    for (i = wSelectionTop; i < wSelectionBottom; i++) {
        WriteGrooveByte(wGrooveRow, ReadClipboardCell(0, i));
        if (++wGrooveRow == 0x10)
            wGrooveRow = 0;
    }
    DrawGrooveSteps();
}

void DrawGrooveSelection(void)
{
    UBYTE end, y, i;

    if (wClipboardType != 2)
        return;
    if (wSelectionTop < wGrooveRow) {
        i = wSelectionTop;
        end = wGrooveRow + 1;
    } else {
        end = wSelectionTop + 1;
        i = wGrooveRow;
    }
    for (; i < end; i++) {
        y = i + 2;
        if (i & 0xF0)
            DebugTrap();
        ColorField(2, y, 2, hGameboyType ? 3 : 2);
    }
}

void ResetGrooveState(void)
{
    UBYTE i;

    wGrooveCacheValid = 0;
    for (i = 0; i != 16; i++)
        WriteGrooveByte(i, wGrooveStepCache[i]);
}
