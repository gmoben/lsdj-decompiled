/*
 * The synth and groove screens -- LSDj 9.4.0, ROM bank $1D.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wGrooveCacheValid;             /* $C9D5 */
extern UBYTE wGrooveStepCache[];            /* $C9D6, [16] */
extern UBYTE *wGrooveCachePtr;              /* $C9E6 */
extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
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
void DrawAllChanStatus(void);
void DrawSynthNumber(void);
void RedrawScreenFar(void);
void SynthClearFlag(UBYTE x);

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

UBYTE SynthWrapIndex32(UBYTE v)
{
    return (v + 1) & 0x1F;
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

UWORD SynthCalcParamAddr(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
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
