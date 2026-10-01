/*
 * The synth and groove screens -- LSDj 9.4.0, ROM bank $1D.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wGrooveCacheValid;             /* $C9D5 */
extern UBYTE wGrooveStepCache[];            /* $C9D6, [16] */
extern UBYTE *wGrooveCachePtr;              /* $C9E6 */
extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
extern UBYTE wSynthWaveBuf[];               /* $CA34, [32] */
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
extern UBYTE wChanGroove[];                 /* $C150, [4] */
extern UBYTE wChanGrooveRow[];              /* $C158, [4] */

UBYTE ReadSynthParam(UBYTE field);          /* the current synth's param byte */
void WriteSynthParam(UBYTE field, UBYTE v);
void WriteGrooveByte(UBYTE step, UBYTE v);
UBYTE ReadGrooveStep(UBYTE step);
void ClearSynthWaveBufs(void);
void ClearSynthWaveBufs2(void);
void SynthFilterPass(void);
void DrawGrooveSteps(void);
void DrawGrooveRow(void);
void CopySelection(void);                   /* $1C:$71CD */
void DrawAllChanStatus(void);
void DrawSynthNumber(void);
void BuildOverwritePrompt(void);
void RedrawScreenFar(void);
void SynthClearFlag(UBYTE x);

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
void SynthReadWaveSram(void);
void SynthRenderWaveType(void);
void SynthComposeWave(void);
void SynthWriteWaveCol(void);
void SynthRenderWaveA(UBYTE amp);
void SynthRenderWaveB(UBYTE amp);

extern UBYTE wSynthWaveShape;               /* $C9E9 */
extern UBYTE wSynthFilterParam;             /* $C9EE, distortion: CLIP/FOLD/WRAP */
extern UBYTE wSynthRenderActive;            /* $C9F1 */
extern UBYTE *wSynthRenderDst;              /* $C9F2 */
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

void GrooveDrawField(void)
{
    UBYTE r, pal;

    r = wGrooveRow;
    if (r & 0xF0)
        DebugTrap();
    pal = (wGrooveRow & 2) ? 0 : 1;
    DrawTextField(2, wGrooveRow + 2, 2, pal);
}

void DrawGrooveSteps(void)
{
    UBYTE i;
    UBYTE v;

    if (wGroove >= 0x20)
        DebugTrap();
    ReadSongBytes(0, (UBYTE *)(((UWORD)wGroove << 4) + (UWORD)0xB090), wSongReadBuf);
    hBgDigitPalette = 4;
    hBgPalette = 1;
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
        DrawTextField(2, i + 2, 2, v);
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
                hBgDigitPalette = 4;
                hBgPalette = 1;
            } else {
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
            }
            q = a * 100 / (a + b);
            DrawFormat("%d", (UWORD)q);
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

void GrooveDrawFieldHw(void)
{
    UBYTE r;

    r = wGrooveRow;
    if (r & 0xF0)
        DebugTrap();
    DrawTextField(2, wGrooveRow + 2, 2, hGameboyType ? 3 : 2);
}

void GrooveRowPalettes(UBYTE row)
{
    UBYTE a, b;

    a = (wCursorMode == 4) ? wSelTop : wGrooveRow;
    b = wGrooveRow;
    SortBytePair(&a, &b);
    if (row >= a && b >= row) {
        hBgDigitPalette = wSelectDigitPal;
        hBgPalette = wSelectPal;
    } else if (row & 2) {
        hBgDigitPalette = 4;
        hBgPalette = 1;
    } else {
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
    }
}

void GrooveCutCopy(UBYTE cut)
{
    UBYTE row;

    row = wGrooveRow;
    SortBytePair(&wSelTop, &row);
    if (ReadGrooveStep(row))
        row++;
    if (wSelTop == row)
        return;
    wCursorMode = 2;
    wClipboardType = 2;
    wSelLeft = 0;
    wSelRight = 1;
    wSelBottom = row;
    ShowMessage(cut ? "CUT" : "COPY");
    for (row = wSelTop; row < wSelBottom; row++) {
        wGrooveRow = row;
        GrooveDrawField();
        WriteClipboardCell(0, row, ReadGrooveStep(row));
        if (cut)
            WriteGrooveByte(row, row ? 0 : 6);
    }
    wGrooveRow = wSelTop;
    FarCall(0x1C, FAR_FN(CopySelection));
    DrawGrooveSteps();
}

void PasteGrooveSelection(void)
{
    UBYTE i;

    if (wClipboardType != 2)
        return;
    if ((UBYTE)(wSelBottom - wSelTop) > 1)
        ShowMessage("PASTE");
    for (i = wSelTop; i < wSelBottom; i++) {
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
    if (wSelTop < wGrooveRow) {
        i = wSelTop;
        end = wGrooveRow + 1;
    } else {
        end = wSelTop + 1;
        i = wGrooveRow;
    }
    for (; i < end; i++) {
        y = i + 2;
        if (i & 0xF0)
            DebugTrap();
        DrawTextField(2, y, 2, hGameboyType ? 3 : 2);
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
            ClearSynthWaveBufs2();
            break;
        case 3:
            SynthFilterPass();
            break;
        default:
            ClearSynthWaveBufs();
        }
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
        SynthReadWaveSram();
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

void SynthEraseCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        DrawTextField(x, SynthFieldLayout[wSynthCursor], 1, 1);
}

void SynthDrawCursor(void)
{
    UBYTE x;

    for (x = SynthFieldX[wSynthCursor]; x < SynthFieldX[wSynthCursor] + SynthFieldWidth[wSynthCursor]; x++)
        DrawTextField(x, SynthFieldLayout[wSynthCursor], 1, hGameboyType ? 3 : 2);
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
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x98E8;
    DrawByte(wSynthScreen);
    wVramCursor = (UBYTE *)0x98EB;
    DrawByte(wSynthScreen);
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
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
    SynthDrawCursor();
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
    DrawTextField(x1, 9, w1, 0);
    DrawTextField(x2, 9, w2, hGameboyType ? 3 : 2);
}

/* Open the confirmation dialog; OverwritePromptInput handles the choice. */
void SynthOverwriteWaves(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
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
