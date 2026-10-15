/*
 * The synth and groove screens -- LSDj 9.4.0, ROM bank $1D.
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
extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
extern UBYTE wSynthWaveBuf[];               /* $CA34, [32] */
extern UBYTE wSynthBufA[];                  /* $CAB8, [64]: filter accumulator, sample * weight per slot */
extern UBYTE wSynthBufB[];                  /* $CAF8, [64]: filter accumulator, weight per slot */
extern UBYTE *wSynthFilterPtr;              /* $CB38 */
extern UWORD wSynthFilterPtrW;              /* $CB38, the same word (aliased by match_function.py) */
extern UWORD wSynthFilterVal;               /* $CB3A */
extern UBYTE wSynthWaveTmp[];               /* $CB40, [32] */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthCursor;                  /* $C442 */
extern UBYTE SynthFieldLayout[];            /* $1D:$60E5, rows of the 16 fields */
extern UBYTE SynthFieldX[];                 /* $1D:$60F5 */
extern UBYTE SynthFieldWidth[];             /* $1D:$6105 */
extern UBYTE wSynthScreen;                  /* $C441, the synth being edited */
extern UBYTE wOverwriteStart;               /* $CB61 */
extern UBYTE wOverwriteEnd;                 /* $CB62 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wGroove;                       /* $C417 */
extern char wGrooveRow;                     /* $C9D4, a plain char */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wMapCol;                       /* $C8FF */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSynthNavSide;                 /* $CB63 */
extern UBYTE wChanGroove[];                 /* $C150, [4] */
extern UBYTE wChanGrooveRow[];              /* $C158, [4] */

UBYTE ReadSynthParam(UBYTE field);          /* the current synth's param byte */
void WriteSynthParam(UBYTE field, UBYTE v);
void WriteGrooveByte(UBYTE step, UBYTE v);
UBYTE ReadGrooveStep(UBYTE step);
void SynthFilterBandPass(void);
void SynthFilterPass(void);
void DrawGrooveSteps(void);
void DrawGrooveRow(void);
void SendLinkClipboard(void);                   /* $1C:$71CD */
void DrawAllChanStatus(void);
void DrawSynthNumber(void);
void BuildOverwritePrompt(void);
void RedrawScreenFar(void);
void SynthClearFlag(UBYTE x);
void GrooveCutCopy(UBYTE cut);
void PasteGrooveSelection(void);
void GrooveDrawSelection(void);
void MapMoveUp(void);                       /* bank 5 */
void MapMoveLeft(void);                     /* bank 5 */
void MapMoveRight(void);                    /* bank 5 */
void MapMoveDown(void);                     /* bank 5 */
void GetSynthFieldHelp(void);               /* bank 5 */
void SaveSelectionState(void);              /* bank 5 */
void ToggleSongPlayback(void);                 /* bank 2 */
void TogglePlayback(void);                    /* bank 2 */

extern UBYTE wSynthRenderIdx;               /* $C9F0 */
extern UBYTE wSynthSegLen1;                 /* $C9E8 */
extern UBYTE wSynthSegLen2;                 /* $C9EA */
extern UBYTE wSynthSegLen3;                 /* $C9EF */
extern UBYTE wSynthSignalType;              /* $C9ED */
extern UBYTE wSynthCurveSeg2[];             /* $CA54, [16] */
extern UBYTE wSynthCurveCutoff[];           /* $CA64, [16] */
extern UBYTE wSynthCurveSeg1[];             /* $CA74, [16] */
extern UBYTE wSynthCurveSeg3[];             /* $CA84, [16] */
extern UBYTE wSynthCurveAmp[];              /* $CA94, [16] */
extern UBYTE wSynthCurveLimit[];            /* $CAA4, [16], biased by $0F */
extern UBYTE wSynthShapeBuf[];              /* $C9F4, byte view of 32 signed words */
extern BYTE SynthShapeTableA[];             /* $1D:$4EF0, signed correction samples */
extern BYTE SynthShapeTableB[];             /* $1D:$50F0 */
extern char SynthSeedTableA[];              /* $1E:$71EC, shape 0 / signal 0 */
extern char SynthSeedTableB[];              /* $1E:$73EC, shape 0 / signal 1 */
extern char SynthSeedTableC[];              /* $1E:$74EC, shape 1 / signal 0 */
extern char SynthSeedTableD[];              /* $1E:$76EC, shape 1 / signal 1 */
extern char SynthSeedTableE[];              /* $1E:$77EC, shape 0 / signal 2 */
extern char SynthSeedTableF[];              /* $1E:$78EC, shape 1 / signal 2 */
UWORD SynthCalcParamAddr(UBYTE x);
void SynthApplyFilter(void);
void SynthLoadSignalWave(void);
void SynthRenderWaveType(void);
void SynthComposeWave(void);
void SynthWriteWaveCol(void);
void SynthRenderWaveA(UBYTE amp);
void SynthRenderWaveB(UBYTE amp);

extern UBYTE wSynthWaveShape;               /* $C9E9 */
extern UBYTE wSynthFilterParam;             /* $C9EE, distortion: CLIP/FOLD/WRAP */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE *wSynthRenderDst;              /* $C9F2 */
extern UWORD *wSynthBandDst;                /* $CB3C, band-pass filter: output pointer / the slot value */
extern UWORD wSynthBandDstW;                /* $CB3C, the same word (aliased by match_function.py) */
extern UBYTE *wSynthBandSlot;               /* $CB3E, band-pass filter: the slot pointer / the BufB value */
extern UWORD wSynthBandSlotW;               /* $CB3E, the same word (aliased by match_function.py) */
extern UBYTE wWaveEditX;                    /* $CB60 */
void SynthFillWaveRange(UBYTE a, UBYTE b, UBYTE *buf);

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
        GetTilemapAddr(1, i + 2);
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
            GetTilemapAddr(5, i + 2);
            if (i & 2) {
                hBgDigitPal = 4;
                hBgPal = 1;
            } else {
                hBgDigitPal = wShadowDigitPal;
                hBgPal = wShadowPal;
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

void GrooveBuildCache(void)
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

void GrooveRowPalettes(UBYTE row)
{
    UBYTE a, b;

    a = (wCursorMode == 4) ? wSelectionTop : wGrooveRow;
    b = wGrooveRow;
    SortBytePair(&a, &b);
    if (row >= a && b >= row) {
        hBgDigitPal = wSelectDigitPal;
        hBgPal = wSelectPal;
    } else if (row & 2) {
        hBgDigitPal = 4;
        hBgPal = 1;
    } else {
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
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
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
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
            GetTilemapAddr(2, v);
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
            GrooveDrawSelection();
        DrawGrooveRow();
        return 1;
    case 0x08:
        EraseGrooveCursor();
        wGrooveRow++;
        if (wGrooveRow == 16 || !ReadGrooveStep(wGrooveRow - 1))
            wGrooveRow--;
        DrawGrooveCursor();
        if (wCursorMode == 4)
            GrooveDrawSelection();
        DrawGrooveRow();
        return 1;
    case 0x11:
        GrooveBuildCache();
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
            GetTilemapAddr(2, top + 2);
            DrawHexByte(v);
        }
        DrawGrooveRow();
        return 1;
    case 0x12:
        GrooveBuildCache();
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
            GetTilemapAddr(2, top + 2);
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
        GrooveBuildCache();
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
                GetTilemapAddr(2, top + 2);
                GrooveRowPalettes(top);
                DrawHexByte(v);
                wGrooveStepCache[top + 1] = v2;
                GetTilemapAddr(2, top + 3);
                GrooveRowPalettes(top + 1);
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
            GrooveCutCopy(1);
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
        GrooveCutCopy(0);
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
                GrooveDrawSelection();
                return 1;
            }
            GrooveCutCopy(0);
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

void GrooveCutCopy(UBYTE cut)
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

void GrooveDrawSelection(void)
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

void GrooveResetState(void)
{
    UBYTE i;

    wGrooveCacheValid = 0;
    for (i = 0; i != 16; i++)
        WriteGrooveByte(i, wGrooveStepCache[i]);
}

/* Add the current table cycle, weighted by the fractional segment parameter.
 * Keep the two signal-type comparisons separate: SDCC shares their boolean
 * temporary, but naming it changes the register spills. The signed byte is
 * widened through WORD before storage in samp; the final shift is explicitly
 * sign-extended because the period compiler shifts words logically.
 */
void SynthRenderWaveA(UBYTE amp)
{
    UBYTE vol;
    UBYTE i;
    BYTE *tbl;
    UWORD samp;

    vol = wSynthSegLen1 >> 4;
    if (wSynthSignalType == 2)
        tbl = &SynthShapeTableB[0];
    else
        tbl = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        vol &= 0x0E;
        if (wSynthSignalType == 2) {
            vol >>= 1;
            if (vol > 4)
                vol = 4;
        }
    }
    for (i = 0; i < vol; i++)
        tbl += 0x20;
    if (wSynthSignalType) {
        vol = wSynthSegLen1 & 0x1F;
        vol >>= 1;
    } else {
        vol = wSynthSegLen1 & 0x0F;
    }
    if (!vol)
        return;
    vol = vol * amp;
    for (i = 0; i < 0x20; i++) {
        samp = (WORD)*tbl;
        samp = samp * vol;
        if ((WORD)samp < 0) {
            samp >>= 4;
            samp |= 0xF000;
        } else {
            samp >>= 4;
        }
        *(UWORD *)(wSynthShapeBuf + (i << 1)) += samp;
        tbl++;
    }
}

/* The complementary weight comes from the preceding cycle. There is no
 * preceding cycle at segment index zero, so that contribution is skipped.
 */
void SynthRenderWaveB(UBYTE amp)
{
    UBYTE i;
    UBYTE vol;
    BYTE *tbl;
    UWORD samp;

    if (wSynthSignalType == 2)
        tbl = &SynthShapeTableB[0];
    else
        tbl = &SynthShapeTableA[0];
    if (wSynthSignalType) {
        vol = wSynthSegLen1 >> 5;
        if (!vol)
            return;
        vol--;
        vol <<= 1;
        if (wSynthSignalType == 2) {
            vol >>= 1;
            if (vol > 4)
                vol = 4;
        }
    } else {
        vol = wSynthSegLen1 >> 4;
        if (!vol)
            return;
        vol--;
    }
    for (i = 0; i < vol; i++)
        tbl += 0x20;
    if (wSynthSignalType) {
        vol = wSynthSegLen1 & 0x1F;
        vol >>= 1;
    } else {
        vol = wSynthSegLen1 & 0x0F;
    }
    vol ^= 0x0F;
    if (!vol)
        return;
    vol = vol * amp;
    for (i = 0; i < 0x20; i++) {
        samp = (WORD)*tbl;
        samp = samp * vol;
        if ((WORD)samp < 0) {
            samp >>= 4;
            samp |= 0xF000;
        } else {
            samp >>= 4;
        }
        *(UWORD *)(wSynthShapeBuf + (i << 1)) += samp;
        tbl++;
    }
}

/* Scale the signed accumulator, apply the selected distortion within a
 * per-frame window, and quantize to the wave buffer. Keep the shifts and
 * sign extension explicit for SDCC 2.2.1; FOLD's >= hi + 1 also preserves
 * the loop-invariant temporary in the ROM's stack frame.
 */
void SynthComposeWave(void)
{
    UBYTE *dst;
    UBYTE i;
    WORD span;
    WORD lo;
    WORD hi;
    WORD v;

    dst = &wSynthWaveBuf[0];
    span = 0x10 - (UWORD)wSynthCurveLimit[wSynthRenderIdx];
    lo = (span >> 1) << 4;
    hi = 0x100 - (((span + 1) >> 1) << 4);
    for (i = 0; i < 0x20; i++) {
        v = *(WORD *)(wSynthShapeBuf + (i << 1));
        v = v * wSynthSegLen3;
        v >>= 4;
        if (v & 0x0800)
            v |= 0xF000;
        v += 0x80;
        v += wSynthSegLen2;
        switch (wSynthFilterParam) {
        case 0:
            if (v < lo)
                v = lo;
            else if (v > hi)
                v = hi;
            break;
        case 1:
            v &= 0xFFF0;
            if (hi == lo) {
                v = hi;
                break;
            }
            for (;;) {
                if (v >= hi + 1)
                    v = (hi << 1) - v;
                else if (v < lo)
                    v = (lo << 1) - v;
                else
                    break;
            }
            break;
        case 2:
            v &= 0xFFF0;
            if (hi == lo) {
                v = hi;
                break;
            }
            while (v > hi) {
                v = v - (hi + 0x10);
                v = v + lo;
            }
            while (v < lo) {
                v = v + (hi + 0x10);
                v = v - lo;
            }
            break;
        }
        *dst = v >> 4;
        dst++;
    }
}

/* Seed the signed accumulator from bank $1E, or zero it for shape 2, then
 * add the weighted bank-$1D cycles. p holds both the cycle offset and the
 * resulting banked address; the cast at LoadSynthShape preserves its ABI.
 */
void SynthRenderWaveType(void)
{
    UBYTE amp;
    UBYTE i;
    UWORD p;

    amp = wSynthCurveAmp[wSynthRenderIdx];
    switch (wSynthWaveShape) {
    case 0:
        switch (wSynthSignalType) {
        default:
            p = wSynthSegLen1 & 0xF0;
            p = (UWORD)(SynthSeedTableA + (p << 1));
            break;
        case 1:
            p = wSynthSegLen1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableB + (p << 1));
            break;
        case 2:
            p = wSynthSegLen1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableE + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        SynthRenderWaveA(amp + 1);
        if (amp)
            SynthRenderWaveB(amp);
        break;
    case 1:
        switch (wSynthSignalType) {
        case 0:
            p = wSynthSegLen1 & 0xF0;
            p = (UWORD)(SynthSeedTableC + (p << 1));
            break;
        case 1:
            p = wSynthSegLen1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableD + (p << 1));
            break;
        case 2:
            p = wSynthSegLen1 & 0xE0;
            p >>= 1;
            p = (UWORD)(SynthSeedTableF + (p << 1));
            break;
        }
        LoadSynthShape((char *)p);
        SynthRenderWaveB(amp + 1);
        if (amp)
            SynthRenderWaveA(amp);
        break;
    case 2:
        i = 0x20;
        do {
            *(UWORD *)(wSynthShapeBuf + ((i - 1) << 1)) = 0;
        } while (--i);
        SynthRenderWaveA(amp ? amp : 1);
        SynthRenderWaveB(amp ? amp : 1);
        break;
    case 3:
        switch (wSynthSignalType) {
        case 0:
            p = (UWORD)(SynthSeedTableA + 0xF0);
            break;
        case 1:
            p = (UWORD)(SynthSeedTableB + 0x70);
            break;
        case 2:
            p = (UWORD)(SynthSeedTableE + 0x70);
            break;
        }
        LoadSynthShape((char *)p);
        SynthRenderWaveA(amp);
        SynthRenderWaveB(amp);
        break;
    }
}

UBYTE SynthWrapIndex32(UBYTE v)
{
    return (v + 1) & 0x1F;
}

/* $58E6: the LOWP/HIGHP filter (every type but BANDP and ALLP). Each of
   the 32 samples is scattered over two accumulator slots at an 8.8 position
   that advances by a cutoff-driven step (the slower the step, the more
   samples a slot averages: a low cutoff smears the wave); HIGHP reverses
   direction after 15 samples. A second pass divides each slot's sum by its
   weight; LOWP fills a slot's missing weight with the mid level 8.
   The slot pointer and value live in two scratch words ($CB38/$CB3A); the
   second half reuses the value's low byte for the doubled index and the
   pointer word for the value -- the index is a block-scope extern, which
   this compiler keeps in a register and spills into the global. */
void SynthFilterLowHigh(void)
{
    UWORD acc;
    UBYTE step;
    UBYTE i;
    UBYTE pos;
    UWORD frac;
    UWORD sample;
    UWORD ua;
    UWORD ub;
    UBYTE next2;
    extern UBYTE wSynthFilterIdx;           /* $CB3A, wSynthFilterVal's low byte (aliased by match_function.py) */

    acc = 0;
    step = -(wSynthFilterCutoff + (wSynthFilterCutoff >> 5));
    MemSet(wSynthBufA, 0, 0x40);
    MemSet(wSynthBufB, 0, 0x40);
    for (i = 0; i < 0x20; i++) {
        pos = acc >> 8;
        frac = (UBYTE)acc;
        sample = wSynthWaveBuf[SynthWrapIndex32(i)];
        next2 = ((pos + 1) & 0x1F) << 1;
        wSynthFilterPtr = wSynthBufA + next2;
        wSynthFilterVal = *(UWORD *)wSynthFilterPtr;
        *(UWORD *)wSynthFilterPtr = wSynthFilterVal + sample * frac;
        wSynthFilterVal = *(UWORD *)(wSynthBufB + next2);
        wSynthFilterVal += frac;
        *(UWORD *)(wSynthBufB + next2) = wSynthFilterVal;
        frac = 0x100 - frac;
        wSynthFilterIdx = pos << 1;
        wSynthFilterPtrW = *(UWORD *)(wSynthBufA + wSynthFilterIdx);
        *(UWORD *)(wSynthBufA + wSynthFilterIdx) = wSynthFilterPtrW + sample * frac;
        *(UWORD *)(wSynthBufB + wSynthFilterIdx) += frac;
        if (wSynthFilterType == 0 || i < 0x0F)
            acc += step;
        else
            acc += 0x200 - step;
    }
    for (i = 0; i < 0x20; i++) {
        ua = *(UWORD *)(wSynthBufA + (i << 1));
        ub = *(UWORD *)(wSynthBufB + (i << 1));
        if (wSynthFilterType == 0) {
            if ((ub - 0x100) & 0x8000) {
                ua += (0x100 - ub) << 3;
                ub = 0x100;
            }
        }
        wSynthWaveBuf[SynthWrapIndex32(i)] = ua / ub;
    }
}

/* The BANDP filter: halve the wave an octave per cutoff doubling over a
 * shortened n (the two copies through a volatile k), then the LOWP/HIGHP
 * scatter core in a `for (;;)` whose head computes pos/frac/sample and, at
 * pos == 0x20, runs the normalise loop and returns.
 *
 * lsdj-alloc(SynthFilterBandPass): BUSYC=82-82
 * -- register c held when `next2 = i << 1` is allocated, so next2 takes b
 * and the BufB word comes out as the pair bc (docs/toolchain.md, "The
 * phantom register"). */
void SynthFilterBandPass(void)
{
    UWORD acc;
    UBYTE step;
    UBYTE i;
    UBYTE n;
    volatile UBYTE k;
    UBYTE pos;
    UWORD frac;
    UWORD sample;
    UBYTE next2;
    extern UBYTE wSynthBandIdx;             /* $CB3E */

    acc = 0;
    step = -wSynthFilterCutoff;
    i = 0;
    n = 0x20;
    MemSet(wSynthBufA, 0, 0x40);
    MemSet(wSynthBufB, 0, 0x40);
    while (step < 0x80) {
        k = 0;
        step <<= 1;
        n >>= 1;
        for (i = 0; i < n; i++) {
            wSynthBandDst = (UWORD *)(wSynthWaveBuf + i);
            *(UBYTE *)wSynthBandDst = (wSynthWaveBuf[k++] + wSynthWaveBuf[k++]) >> 1;
        }
    }
    n--;
    for (;;) {
        pos = acc >> 8;
        frac = (UBYTE)acc;
        sample = wSynthWaveBuf[SynthWrapIndex32(i)];
        if (pos == 0x20) {
            for (i = 0; i < 0x20; i++) {
                wSynthBandDst = (UWORD *)(wSynthWaveBuf + SynthWrapIndex32(i));
                next2 = i << 1;
                wSynthBandSlotW = *(UWORD *)(wSynthBufA + next2);
                *(UBYTE *)wSynthBandDst = wSynthBandSlotW / *(UWORD *)(wSynthBufB + next2);
            }
            return;
        }
        next2 = ((pos + 1) & 0x1F) << 1;
        wSynthBandSlot = wSynthBufA + next2;
        wSynthBandDstW = *(UWORD *)wSynthBandSlot;
        *(UWORD *)wSynthBandSlot = wSynthBandDstW + sample * frac;
        wSynthBandSlotW = *(UWORD *)(wSynthBufB + next2);
        wSynthBandSlotW += frac;
        *(UWORD *)(wSynthBufB + next2) = wSynthBandSlotW;
        frac = 0x100 - frac;
        wSynthBandIdx = pos << 1;
        wSynthBandDstW = *(UWORD *)(wSynthBufA + wSynthBandIdx);
        *(UWORD *)(wSynthBufA + wSynthBandIdx) = wSynthBandDstW + sample * frac;
        *(UWORD *)(wSynthBufB + wSynthBandIdx) += frac;
        acc += step;
        i++;
        i &= n;
    }
}

void SynthFilterPass(void)
{
    UBYTE i;
    UBYTE n;
    UBYTE k;

    n = 0x20 - (wSynthFilterCutoff >> 3);
    for (i = 0; i < 0x20; i++) {
        k = (((UWORD)i << 5) + 0x10) / n % 0x20;
        wSynthWaveTmp[SynthWrapIndex32(i)] = wSynthWaveBuf[SynthWrapIndex32(k)];
    }
    memcpy(wSynthWaveBuf, wSynthWaveTmp, 0x20);
}

void SynthApplyFilter(void)
{
    if (wSynthFilterCutoff) {
        switch (wSynthFilterType) {
        case 2:
            SynthFilterBandPass();
            break;
        case 3:
            SynthFilterPass();
            break;
        default:
            SynthFilterLowHigh();
        }
    }
}

/* lsdj-alloc(SynthWriteWaveCol): BUSYC=1-17
 * -- register c held from the first instruction to the loop body's last
 * byte temporary, so i takes b and is evicted by the body, and the pointer
 * pair comes out as bc (docs/toolchain.md, "The phantom register"). */
void SynthWriteWaveCol(void)
{
    UBYTE i;

    for (i = 0; i < 0x20; i += 2) {
        wWaveEditX = wSynthWaveBuf[i] << 4;
        wWaveEditX = (wSynthWaveBuf[i + 1] + wWaveEditX) ^ 0xFF;
        WriteSongByte(3, (UWORD)wSynthRenderDst++, wWaveEditX);
    }
}

/* $5F90: load the custom-wave SIGNAL (W.FX: wave $F0 + synth, 16 packed
   bytes at SRAM $AF00 + 16 * synth) into wSynthShapeBuf as 32 signed words,
   each nibble n becoming 120 - 16 * n. */
void SynthLoadSignalWave(void)
{
    UBYTE i;
    UWORD p;
    char v;
    int w;

    i = 0;
    p = wSynthScreen;
    p <<= 4;
    p += (UWORD)0xAF00;
    while (i != 0x20) {
        v = 0xFF ^ ReadSongByte(3, p);
        w = (char)((v & 0xF0) + 0x88);
        *(int *)(wSynthShapeBuf + (i++ << 1)) = w;
        w = (char)(((v & 0x0F) << 4) + 0x88);
        *(int *)(wSynthShapeBuf + (i++ << 1)) = w;
        p++;
    }
}

void RenderSynthWave(void)
{
    if (wSynthRenderIdx >= 0x10)
        DebugTrap();
    wSynthSegLen3 = wSynthCurveSeg3[wSynthRenderIdx];
    wSynthSegLen1 = wSynthCurveSeg1[wSynthRenderIdx];
    wSynthSegLen2 = wSynthCurveSeg2[wSynthRenderIdx];
    wSynthFilterCutoff = wSynthCurveCutoff[wSynthRenderIdx];
    if (wSynthSignalType == 3)
        SynthLoadSignalWave();
    else
        SynthRenderWaveType();
    SynthComposeWave();
    SynthApplyFilter();
    SynthWriteWaveCol();
}

UWORD SynthCalcParamAddr(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

UBYTE SynthReadParam(UBYTE x)
{
    UBYTE v;

    v = ReadSongByte(1, SynthCalcParamAddr(x));
    return v & (1 << (x & 7));
}

void SynthClearFlag(UBYTE x)
{
    UWORD p;
    UBYTE v;

    p = SynthCalcParamAddr(x);
    v = ReadSongByte(1, p);
    x &= 7;
    v &= ~(1 << x);
    WriteSongByte(1, p, v);
}

void SynthFillWaveRange(UBYTE a, UBYTE b, UBYTE *buf)
{
    UBYTE i;

    if (b >= a) {
        b -= a;
        if (b < 0xFF)
            b++;
        for (i = 0; i < 16; i++)
            *buf++ = ((UWORD)i * b >> 4) + a;
    } else {
        a -= b;
        if (a < 0xFF)
            a++;
        for (i = 15; i != 0xFF; i--)
            *buf++ = b + ((UWORD)i * a >> 4);
    }
}

void DrawSynthField(UBYTE v)
{
    switch (v) {
    case 0:
        DrawString(" PINCH");
        break;
    case 1:
        DrawString("  WARP");
        break;
    case 2:
        DrawString("RESYNC");
        break;
    case 3:
        DrawString("RESYN2");
        break;
    }
}

UBYTE ReadSynthParam(UBYTE field)
{
    UWORD p;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    return ReadSongByte(1, p);
}

UBYTE SynthSignalIsFrame(void)
{
    return ReadSynthParam(0) == 3;
}

void DrawSynthFilterField(void)
{
    wVramCursor = (UBYTE *)0x9868;
    if (SynthSignalIsFrame()) {
        DrawString("-----");
    } else {
        switch (ReadSynthParam(1)) {
        case 0:
            DrawString(" LOWP");
            break;
        case 1:
            DrawString("HIGHP");
            break;
        case 2:
            DrawString("BANDP");
            break;
        case 3:
            DrawString(" ALLP");
            break;
        case 4:
            DrawString("NOTCH");
            break;
        }
    }
}

void DrawSynthWaveField(void)
{
    wVramCursor = (UBYTE *)0x9849;
    switch (ReadSynthParam(0)) {
    case 0:
        DrawString("iiii");
        break;
    case 1:
        DrawString("mmmm");
        break;
    case 2:
        DrawString("kkkk");
        break;
    case 3:
        DrawString("W.F");
        DrawByte(wSynthScreen);
        break;
    default:
        DrawString("??");
        break;
    }
}

void EraseSynthCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        ColorField(x, SynthFieldLayout[wSynthCursor], 1, 1);
}

void DrawSynthCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        ColorField(x, SynthFieldLayout[wSynthCursor], 1, hGameboyType ? 3 : 2);
}

void DrawSynthDistField(UBYTE v)
{
    switch (v) {
    case 0:
        DrawString("CLIP");
        break;
    case 1:
        DrawString("FOLD");
        break;
    case 2:
        DrawString("WRAP");
        break;
    }
}

/* Redraw both endpoints of the synth parameters. The integer SRAM cursor
 * is incremented between fields even when a saved-frame source hides one.
 */
void DrawSynthNumber(void)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x98E8;
    DrawByte(wSynthScreen);
    wVramCursor = (UBYTE *)0x98EB;
    DrawByte(wSynthScreen);
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawSynthWaveField();
    p++;
    DrawSynthFilterField();
    p++;
    v = ReadSongByte(1, (UWORD)p++);
    wVramCursor = (UBYTE *)0x9969;
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v >> 4);
    wVramCursor = (UBYTE *)0x996C;
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v & 0x0F);
    wVramCursor = (UBYTE *)0x9889;
    DrawSynthDistField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x98A7;
    DrawSynthField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9928;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9948;
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)0x99C8;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x9988;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x992B;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x994B;
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)0x99CB;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x998B;
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)0x99A8;
    DrawHexByte(ReadSongByte(1, (UWORD)p++) + 0x0F);
    wVramCursor = (UBYTE *)0x99AB;
    DrawHexByte(ReadSongByte(1, (UWORD)p) + 0x0F);
    DrawSynthCursor();
}

void DrawSynthScreen(void)
{
    wSynth &= 0x0F;
    wSynth = wSynth + (wSynthScreen << 4);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    DrawSynthNumber();
    FarCall(5, MapDrawTopRow);
}

void SynthLoadParams(void)
{
    volatile UWORD p;
    volatile UWORD o;
    volatile UWORD s;
    UBYTE v;
    UBYTE x, y;

    s = wSynthScreen;
    o = s << 4;
    p = (UWORD)SramSynthParams + o;
    wSynthSignalType = ReadSongByte(1, p++);
    wSynthWaveShape = ReadSongByte(1, p++);
    v = ReadSongByte(1, p++);
    SynthFillWaveRange(v >> 4, v & 0x0F, wSynthCurveAmp);
    wSynthFilterParam = ReadSongByte(1, p++);
    wSynthFilterType = ReadSongByte(1, p++);
    wSynthSegLen3 = ReadSongByte(1, p++);
    wSynthSegLen1 = ReadSongByte(1, p++);
    wSynthFilterCutoff = ReadSongByte(1, p++) << 3;
    wSynthSegLen2 = ReadSongByte(1, p++);
    SynthFillWaveRange(wSynthSegLen3, ReadSongByte(1, p++), wSynthCurveSeg3);
    SynthFillWaveRange(wSynthSegLen1, ReadSongByte(1, p++), wSynthCurveSeg1);
    SynthFillWaveRange(wSynthFilterCutoff, ReadSongByte(1, p++) << 3, wSynthCurveCutoff);
    SynthFillWaveRange(wSynthSegLen2, ReadSongByte(1, p++), wSynthCurveSeg2);
    x = ReadSongByte(1, p++);
    y = ReadSongByte(1, p);
    SynthFillWaveRange(x + 0x0F, y + 0x0F, wSynthCurveLimit);
    wSynthRenderDst = (UBYTE *)((s << 8) + (UWORD)0xA000);
    wSynthRenderIdx = 0;
    wSynthRenderActive = 1;
}

/* The confirmation dialog's frame: border tiles through the identity map,
 * the interior cleared with tile 2 in palette 1. The in-loop restore of the
 * ASCII map sits at the end of the middle-column branch: there SDCC 2.2.1
 * hoists it out of the inner loop only (its definition does not reach the
 * inner preheader in the pre-loop dataflow), which is where the ROM has it.
 * At the end of the whole body it is hoisted out of both loops.
 */
void BuildOverwritePrompt(void)
{
    UBYTE x;
    UBYTE y;

    for (x = 3; (UWORD)x < 15; x++) {
        for (y = 4; (UWORD)y < 11; y++) {
            wStringSrc = IdentityCharMap;
            hBgPal = hGameboyType ? 0x48 : 0xD2;
            ColorField(x, y, 1, 0);
            GetTilemapAddr(x, y);
            if (x == 3) {
                if (y == 4)
                    DrawStringIndexed(0);
                else if ((UWORD)y == 10)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if ((UWORD)x == 14) {
                if (y == 4)
                    DrawStringIndexed(1);
                else if ((UWORD)y == 10)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (y == 4)
                    DrawStringIndexed(4);
                else if ((UWORD)y == 10)
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

void DrawOverwriteChoice(void)
{
    UBYTE x1, w2, x2, w1;

    if (wOverwriteEnd) {
        x2 = 0x0B;
        x1 = 4;
        w2 = 2;
        w1 = 3;
    } else {
        x2 = 4;
        x1 = 0x0B;
        w2 = 3;
        w1 = 2;
    }
    ColorField(x1, 9, w1, 0);
    ColorField(x2, 9, w2, hGameboyType ? 3 : 2);
}

/* Open the confirmation dialog; OverwritePromptInput handles the choice. */
void SynthOverwriteWaves(void)
{
    hBgDigitPal = 4;
    hBgPal = 1;
    BuildOverwritePrompt();
    wOverwriteStart = 1;
    wOverwriteEnd = 1;
    wVramCursor = (UBYTE *)0x98A4;
    DrawString("OVERWRITE");
    wVramCursor = (UBYTE *)0x98C4;
    DrawString("HAND-DRAWN");
    wVramCursor = (UBYTE *)0x98E4;
    DrawString("WAVES?");
    wVramCursor = (UBYTE *)0x9924;
    DrawString("YES");
    wVramCursor = (UBYTE *)0x992B;
    DrawString("NO");
    DrawOverwriteChoice();
}

void RedrawScreenFar(void)
{
    FarCall(1, FAR_FN(RedrawScreen));
}

void OverwritePromptInput(void)
{
    if (wAPressEnabled) {
        switch (wButtons) {
        case 0x20:
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        case 2:
            wOverwriteEnd = 0;
            DrawOverwriteChoice();
            break;
        case 1:
            wOverwriteEnd = 1;
            DrawOverwriteChoice();
            break;
        case 0x10:
            if (!wOverwriteEnd)
                SynthClearFlag(wSynthScreen);
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        }
    }
}

void WriteSynthParam(UBYTE field, UBYTE v)
{
    UWORD p;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    WriteSongByte(1, p, v);
}

void SynthParamAddClamp(UBYTE field, UBYTE max, UBYTE inc)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v += (UWORD)inc;
    if (v > max)
        v = max;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void SynthParamSubClamp(UBYTE field, UBYTE dec)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v -= dec;
    if (v & 0x8000)
        v = 0;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void SynthParamIncTo(UBYTE field, UBYTE max)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v != max) {
        v++;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

void SynthParamDecTo0(UBYTE field)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v) {
        v--;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

UBYTE SynthFieldIncHi(void)
{
    UBYTE v, t;

    v = ReadSynthParam(2);
    t = v & 0xF0;
    if (t != 0xF0) {
        v += 0x10;
        WriteSynthParam(2, v);
    }
    return v >> 4;
}

UBYTE SynthFieldIncLo(void)
{
    UBYTE v, t;

    v = ReadSynthParam(2);
    t = v & 0x0F;
    if (t != 0x0F) {
        v++;
        WriteSynthParam(2, v);
    }
    return v & 0x0F;
}

UBYTE SynthFieldDecHi(void)
{
    UBYTE v;

    v = ReadSynthParam(2);
    if (v & 0xF0) {
        v -= 0x10;
        WriteSynthParam(2, v);
    }
    return v >> 4;
}

UBYTE SynthFieldDecLo(void)
{
    UBYTE v;

    v = ReadSynthParam(2);
    if (v & 0x0F) {
        v--;
        WriteSynthParam(2, v);
    }
    return v & 0x0F;
}

/*
 * The synth screen's input. A on a field that would overwrite the synth's
 * waves asks first (SynthOverwriteWaves / the prompt's own input); A alone
 * shows the field's help after two presses; the directions walk the
 * two-column field layout (skipping the rows a framed signal hides);
 * A+right/left step the field under the cursor, A+up/down by the coarse
 * step; B+direction picks another synth; select+B / select+A copy and paste
 * its 16 parameter bytes; start and select+start stop playback. 1 when the
 * key was taken.
 */
UBYTE SynthScreenInput(void)
{
    UBYTE v;
    UWORD p;
    UWORD q;

    if ((wButtons & 0x10) && SynthReadParam(wSynthScreen) && !wOverwriteStart) {
        if (wAPressEnabled)
            SynthOverwriteWaves();
        return 1;
    }
    if (wOverwriteStart) {
        OverwritePromptInput();
        return 1;
    }
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    if (wButtons != 0x10)
        wDrawStep = 0;
    switch (wButtons) {
    case 0x10:
        if (wAPressEnabled && ++wDrawStep == 2)
            FarCall(5, GetSynthFieldHelp);
        return 1;
    case 0x04:
        if (!wSynthCursor)
            break;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x0A:
            wSynthCursor = 3;
            break;
        case 0x07:
        case 0x0D:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        case 0x02:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        default:
            wSynthCursor--;
        }
        DrawSynthCursor();
        return 1;
    case 0x08:
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x03:
            wSynthCursor = wSynthNavSide ? 4 : 10;
            break;
        case 0x04:
        case 0x0A:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        case 0x00:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        default:
            wSynthCursor++;
        case 0x09:
        case 0x0F:
            break;
        }
        DrawSynthCursor();
        return 1;
    case 0x02:
        wSynthNavSide = 1;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F:
            wSynthCursor -= 6;
        }
        DrawSynthCursor();
        return 1;
    case 0x01:
        wSynthNavSide = 0;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
            wSynthCursor += 6;
        }
        DrawSynthCursor();
        return 1;
    case 0x11:
        GetTilemapAddr(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 0:
            if (wButtonsSame)
                break;
            switch (ReadSynthParam(0)) {
            case 0:
                WriteSynthParam(0, 1);
                break;
            case 1:
                WriteSynthParam(0, 2);
                break;
            default:
                WriteSynthParam(0, 3);
                DrawSynthScreen();
                hBgDigitPal = wSelectDigitPal;
                hBgPal = wSelectPal;
                DrawSynthWaveField();
            }
            DrawSynthWaveField();
            break;
        case 1:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(1);
            if (v == 3)
                break;
            v++;
            WriteSynthParam(1, v);
            DrawSynthFilterField();
            break;
        case 2:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(3);
            if (v >= 2)
                break;
            v++;
            WriteSynthParam(3, v);
            DrawSynthDistField(v);
            break;
        case 8:
            v = ReadSynthParam(0x0D) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParam(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParam(0x0E) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParam(0x0E, v - 0x0F);
            break;
        case 3:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(4);
            if (v >= 3)
                break;
            v++;
            DrawSynthField(v);
            WriteSynthParam(4, v);
            break;
        case 6:
            DrawByte(SynthFieldIncHi());
            break;
        case 12:
            DrawByte(SynthFieldIncLo());
            break;
        case 4:
            SynthParamIncTo(5, 0xFF);
            break;
        case 10:
            SynthParamIncTo(9, 0xFF);
            break;
        case 5:
            SynthParamIncTo(6, 0xFF);
            break;
        case 11:
            SynthParamIncTo(0x0A, 0xFF);
            break;
        case 7:
            SynthParamIncTo(8, 0xFF);
            break;
        case 13:
            SynthParamIncTo(0x0C, 0xFF);
            break;
        case 9:
            SynthParamIncTo(7, 0x1F);
            break;
        case 15:
            SynthParamIncTo(0x0B, 0x1F);
            break;
        }
        SynthLoadParams();
        return 1;
    case 0x12:
        GetTilemapAddr(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 0:
            if (wButtonsSame)
                break;
            switch (ReadSynthParam(0)) {
            default:
                WriteSynthParam(0, 0);
                break;
            case 2:
                WriteSynthParam(0, 1);
                break;
            case 3:
                WriteSynthParam(0, 2);
                DrawSynthScreen();
                hBgDigitPal = wSelectDigitPal;
                hBgPal = wSelectPal;
                DrawSynthWaveField();
            }
            DrawSynthWaveField();
            break;
        case 1:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(1);
            if (!v)
                break;
            v--;
            WriteSynthParam(1, v);
            DrawSynthFilterField();
            break;
        case 2:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(3);
            if (!v)
                break;
            v--;
            WriteSynthParam(3, v);
            DrawSynthDistField(v);
            break;
        case 8:
            v = ReadSynthParam(0x0D) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParam(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParam(0x0E) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParam(0x0E, v - 0x0F);
            break;
        case 3:
            if (wButtonsSame)
                break;
            v = ReadSynthParam(4);
            if (!v)
                break;
            v--;
            DrawSynthField(v);
            WriteSynthParam(4, v);
            break;
        case 6:
            DrawByte(SynthFieldDecHi());
            break;
        case 12:
            DrawByte(SynthFieldDecLo());
            break;
        case 4:
            SynthParamDecTo0(5);
            break;
        case 10:
            SynthParamDecTo0(9);
            break;
        case 5:
            SynthParamDecTo0(6);
            break;
        case 11:
            SynthParamDecTo0(0x0A);
            break;
        case 7:
            SynthParamDecTo0(8);
            break;
        case 13:
            SynthParamDecTo0(0x0C);
            break;
        case 9:
            SynthParamDecTo0(7);
            break;
        case 15:
            SynthParamDecTo0(0x0B);
            break;
        }
        SynthLoadParams();
        return 1;
    case 0x14:
        GetTilemapAddr(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 6:
            SynthFieldIncHi();
            DrawByte(SynthFieldIncHi());
            break;
        case 12:
            SynthFieldIncLo();
            DrawByte(SynthFieldIncLo());
            break;
        case 9:
            SynthParamAddClamp(7, 0x1F, 8);
            break;
        case 15:
            SynthParamAddClamp(0x0B, 0x1F, 8);
            break;
        case 4:
            SynthParamAddClamp(5, 0xFF, 0x10);
            break;
        case 10:
            SynthParamAddClamp(9, 0xFF, 0x10);
            break;
        case 8:
            v = ReadSynthParam(0x0D) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParam(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParam(0x0E) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParam(0x0E, v - 0x0F);
            break;
        case 5:
            SynthParamAddClamp(6, 0xFF, 0x10);
            break;
        case 11:
            SynthParamAddClamp(0x0A, 0xFF, 0x10);
            break;
        case 7:
            SynthParamAddClamp(8, 0xFF, 0x10);
            break;
        case 13:
            SynthParamAddClamp(0x0C, 0xFF, 0x10);
            break;
        }
        SynthLoadParams();
        return 1;
    case 0x18:
        GetTilemapAddr(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case 6:
            SynthFieldDecHi();
            DrawByte(SynthFieldDecHi());
            break;
        case 12:
            SynthFieldDecLo();
            DrawByte(SynthFieldDecLo());
            break;
        case 9:
            SynthParamSubClamp(7, 8);
            break;
        case 15:
            SynthParamSubClamp(0x0B, 8);
            break;
        case 4:
            SynthParamSubClamp(5, 0x10);
            break;
        case 10:
            SynthParamSubClamp(9, 0x10);
            break;
        case 8:
            v = ReadSynthParam(0x0D) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParam(0x0D, v - 0x0F);
            break;
        case 14:
            v = ReadSynthParam(0x0E) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParam(0x0E, v - 0x0F);
            break;
        case 5:
            SynthParamSubClamp(6, 0x10);
            break;
        case 11:
            SynthParamSubClamp(0x0A, 0x10);
            break;
        case 7:
            SynthParamSubClamp(8, 0x10);
            break;
        case 13:
            SynthParamSubClamp(0x0C, 0x10);
            break;
        }
        SynthLoadParams();
        return 1;
    case 0x21:
    case 0x24:
        wMessageStrPtr = 0;
        if (wSynthScreen != 0x0F) {
            wSynthScreen++;
            DrawSynthScreen();
        }
        return 1;
    case 0x22:
    case 0x28:
        wMessageStrPtr = 0;
        if (wSynthScreen) {
            wSynthScreen--;
            DrawSynthScreen();
        }
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    case 0x48:
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveDown);
        RedrawScreenFar();
        return 1;
    case 0x42:
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveLeft);
        RedrawScreenFar();
        return 1;
    case 0x41:
        if (wButtonsSame)
            return 0;
        if (wSynthCursor == 0 && SynthSignalIsFrame()) {
            wSynth = wSynthScreen + 0xF0;
        } else {
            wSynth &= 0x0F;
            wSynth += wSynthScreen << 4;
        }
        FarCall(5, MapMoveRight);
        FarCall(7, RedrawScreen);
        return 1;
    case 0x60:
        ShowMessage("COPIED SYNTH");
        wCursorMode = 2;
        wClipboardType = 0x0A;
        p = ((UWORD)wSynthScreen << 4) + (UWORD)0xBEB2;
        for (v = 0; v < 0x10; v++) {
            WriteClipboardCell(0, v, ReadSongByte(1, p));
            p++;
        }
        wSelectionLeft = 0;
        wSelectionRight = 1;
        wSelectionTop = 0;
        wSelectionBottom = 0x10;
        FarCall(0x1C, FAR_FN(SendLinkClipboard));
        return 1;
    case 0x50:
        if (wCursorMode == 1 || wClipboardType != 0x0A)
            return 0;
        ShowMessage("PASTED SYNTH");
        q = ((UWORD)wSynthScreen << 4) + (UWORD)0xBEB2;
        for (v = 0; v < 0x10; v++) {
            WriteSongByte(1, q, ReadClipboardCell(0, v));
            q++;
        }
        SynthLoadParams();
        DrawSynthNumber();
        return 1;
    }
    return 0;
}
