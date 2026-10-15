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
extern UBYTE *wGrooveCachePointer;              /* $C9E6 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wGroove;                       /* $C417 */
extern char wGrooveRow;                     /* $C9D4, a plain char */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wMapColumn;                       /* $C8FF */
extern UBYTE wChannelGroove[];                 /* $C150, [4] */
extern UBYTE wChannelGrooveRow[];              /* $C158, [4] */
void WriteGrooveByte(UBYTE step, UBYTE v);
UBYTE ReadGrooveStep(UBYTE step);
void DrawGrooveSteps(void);
void DrawGrooveRow(void);
void SendLinkClipboard(void);                   /* $1C:$71CD */
void DrawAllChannelStatus(void);
void CutCopyGrooveSelection(UBYTE cut);
void PasteGrooveSelection(void);
void DrawGrooveSelection(void);
void MoveMapCursorUp(void);                       /* bank 5 */
void SaveSelectionState(void);              /* bank 5 */
void SoundToggleSongPlayback(void);                 /* bank 2 */
void SoundTogglePlayback(void);                    /* bank 2 */

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
    UBYTE r, palette;

    r = wGrooveRow;
    if (r & 0xF0)
        DebugTrap();
    palette = (wGrooveRow & 2) ? 0 : 1;
    ColorField(2, wGrooveRow + 2, 2, palette);
}

void DrawGrooveSteps(void)
{
    UBYTE step;
    UBYTE v;

    if (wGroove >= 0x20)
        DebugTrap();
    ReadSongBytes(0, (UBYTE *)(((UWORD)wGroove << 4) + (UWORD)0xB090), wSongReadBuffer);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    for (step = 0; step < 0x10; step++) {
        SetVramCursor(1, step + 2);
        v = wSongReadBuffer[step];
        if (!v)
            DrawString(" --    ");
        else {
            DrawStringTile20();
            DrawHexByte(v);
            DrawString("    ");
        }
        if (step == wGrooveRow)
            v = hGameboyType ? 3 : 2;
        else
            v = (step & 2) ? 0 : 1;
        ColorField(2, step + 2, 2, v);
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
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
}

void GrooveScreenUpdate(void)
{
    if (wPlayMode && wGroove == wChannelGroove[wSongColumn])
        SetSpriteY(0, wChannelGrooveRow[wSongColumn]);
    else
        ClearSpriteY(0);
}

void DrawGrooveRow(void)
{
    UBYTE step;
    UWORD a, b;
    UBYTE q;

    step = 0;
    while (step != 0x10) {
        if (wGrooveCacheValid) {
            a = wGrooveStepCache[step];
            b = wGrooveStepCache[step + 1];
        } else {
            a = ReadGrooveStep(step);
            b = ReadGrooveStep(step + 1);
        }
        if (b) {
            SetVramCursor(5, step + 2);
            if (step & 2) {
                hBgDigitPalette = 4;
                hBgPalette = 1;
            } else {
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
            }
            q = a * 100 / (a + b);
            printf("%d", (UWORD)q);
            DrawStringIndexed('%');
            if (q < 10)
                DrawStringTile20();
        }
        step += 2;
    }
}

void BuildGrooveCache(void)
{
    UBYTE step, v;

    if (wGrooveCacheValid)
        return;
    wGrooveCacheValid = 1;
    for (step = 0; step != 16; step++) {
        wGrooveCachePointer = wGrooveStepCache + step;
        v = ReadGrooveStep(step);
        *wGrooveCachePointer = v;
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

void SetGrooveRowPalettes(UBYTE row)
{
    UBYTE a, b;

    a = (wCursorMode == 4) ? wSelectionTop : wGrooveRow;
    b = wGrooveRow;
    SortBytePair(&a, &b);
    if (row >= a && b >= row) {
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
    } else if (row & 2) {
        hBgDigitPalette = 4;
        hBgPalette = 1;
    } else {
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
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
    UBYTE bottom;
    UBYTE v;
    UBYTE v2;
    UBYTE end;

    v = wGrooveRow + 2;
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    switch (wButtons) {
    case J_SELECT_TAP:
        if (wCursorMode == 4) {
            wGrooveRow = wSelectionTop;
            FarCall(5, 0x747D);
            if (hGameboyType) {
                FarCall(1, FAR_FN(DrawScreenPaletteMap));
                FarCall(5, DrawMapTopRow);
            }
            DrawGrooveScreen();
            DrawGrooveCursor();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case J_A:
        if (!wAPressEnabled)
            break;
        if (!ReadGrooveStep(wGrooveRow)) {
            SetVramCursor(2, v);
            DrawHexByte(6);
            WriteGrooveByte(wGrooveRow, 6);
        }
        DrawGrooveRow();
        return 1;
    case J_UP:
        EraseGrooveCursor();
        if (wGrooveRow)
            wGrooveRow--;
        DrawGrooveCursor();
        if (wCursorMode == 4)
            DrawGrooveSelection();
        DrawGrooveRow();
        return 1;
    case J_DOWN:
        EraseGrooveCursor();
        wGrooveRow++;
        if (wGrooveRow == 16 || !ReadGrooveStep(wGrooveRow - 1))
            wGrooveRow--;
        DrawGrooveCursor();
        if (wCursorMode == 4)
            DrawGrooveSelection();
        DrawGrooveRow();
        return 1;
    case (J_A | J_RIGHT):
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bottom = wGrooveRow;
            SortBytePair(&top, &bottom);
        } else {
            bottom = wGrooveRow;
            top = wGrooveRow;
        }
        bottom++;
        for (; top < bottom; top++) {
            v = wGrooveStepCache[top];
            if (v < 0xFF)
                v++;
            wGrooveStepCache[top] = v;
            SetVramCursor(2, top + 2);
            DrawHexByte(v);
        }
        DrawGrooveRow();
        return 1;
    case (J_A | J_LEFT):
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bottom = wGrooveRow;
            SortBytePair(&top, &bottom);
        } else {
            bottom = wGrooveRow;
            top = wGrooveRow;
        }
        end = bottom;
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
    case (J_A | J_UP):
    case (J_A | J_DOWN):
        BuildGrooveCache();
        if (wCursorMode == 4 && (!wSelectionLeft || !wSelectionRight)) {
            top = wSelectionTop;
            bottom = wGrooveRow;
            SortBytePair(&top, &bottom);
        } else {
            bottom = wGrooveRow;
            top = wGrooveRow;
        }
        top &= 0xFE;
        bottom = (bottom & 0xFE) + 2;
        while (top != bottom) {
            v2 = wGrooveStepCache[top + 1];
            v = wGrooveStepCache[top];
            if (!v2)
                v2 = v;
            if (wButtons == (J_A | J_UP)) {
                v++;
                v2--;
            } else {
                v--;
                v2++;
            }
            if (v2 && v) {
                wGrooveStepCache[top] = v;
                SetVramCursor(2, top + 2);
                SetGrooveRowPalettes(top);
                DrawHexByte(v);
                wGrooveStepCache[top + 1] = v2;
                SetVramCursor(2, top + 3);
                SetGrooveRowPalettes(top + 1);
                DrawHexByte(v2);
            }
            top += 2;
        }
        DrawGrooveRow();
        return 1;
    case (J_B | J_RIGHT):
        if (wGroove < 0x1F)
            wGroove++;
        wMessageStringPointer = 0;
        DrawGrooveScreen();
        return 1;
    case (J_B | J_UP):
        if (wGroove < 0x10)
            wGroove += 0x10;
        else
            wGroove = 0x1F;
        wMessageStringPointer = 0;
        DrawGrooveScreen();
        return 1;
    case (J_B | J_DOWN):
        if (wGroove & 0x10)
            wGroove -= 0x10;
        else
            wGroove = 0;
        wMessageStringPointer = 0;
        DrawGrooveScreen();
        return 1;
    case (J_B | J_LEFT):
        if (wGroove)
            wGroove--;
        wMessageStringPointer = 0;
        DrawGrooveScreen();
        return 1;
    case (J_B | J_A):
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
        wClipboardType = SCREEN_GROOVE;
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
    case (J_SELECT | J_A):
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
    case (J_SELECT | J_START):
        if (!wButtonsSame)
            FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (wButtonsSame || wBHeld)
            return 1;
        switch ((char)wMapColumn) {
        case 0:
            wCurrentScreen = SCREEN_SONG;
            break;
        case 1:
            wCurrentScreen = SCREEN_CHAIN;
            break;
        }
        FarCall(2, FAR_FN(SoundTogglePlayback));
        wCurrentScreen = SCREEN_GROOVE;
        return 1;
    case (J_SELECT | J_UP):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        FarCall(5, FAR_FN(RedrawScreen));
        return 1;
    case J_B:
        if (wCursorMode != 4)
            break;
        if (!wAPressEnabled)
            break;
        CutCopyGrooveSelection(0);
        return 1;
    case (J_SELECT | J_B):
        if (!wSelectHeld)
            break;
        if (wCursorMode == 4 && wClipboardType == SCREEN_GROOVE) {
            top = wSelectionTop;
            bottom = wGrooveRow;
            SortBytePair(&top, &bottom);
            if (top || bottom != 0x0F) {
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
    wClipboardType = SCREEN_GROOVE;
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
    UBYTE row;

    if (wClipboardType != SCREEN_GROOVE)
        return;
    if ((UBYTE)(wSelectionBottom - wSelectionTop) > 1)
        ShowMessage("PASTE");
    for (row = wSelectionTop; row < wSelectionBottom; row++) {
        WriteGrooveByte(wGrooveRow, ReadClipboardCell(0, row));
        if (++wGrooveRow == 0x10)
            wGrooveRow = 0;
    }
    DrawGrooveSteps();
}

void DrawGrooveSelection(void)
{
    UBYTE end, y, i;

    if (wClipboardType != SCREEN_GROOVE)
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
    UBYTE step;

    wGrooveCacheValid = 0;
    for (step = 0; step != 16; step++)
        WriteGrooveByte(step, wGrooveStepCache[step]);
}
