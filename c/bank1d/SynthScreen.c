/*
 * The synth and groove screens -- LSDj 9.4.0, ROM bank $1D.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wGrooveCacheValid;             /* $C9D5 */
extern UBYTE wGrooveStepCache[];            /* $C9D6, [16] */
extern UBYTE *wGrooveCachePtr;              /* $C9E6 */
extern UBYTE wSynthFilterType;              /* $C9EB */
extern UBYTE wSynthFilterCutoff;            /* $C9EC */
extern UBYTE wSynthScreen;                  /* $C441, the synth being edited */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wGrooveRow;                    /* $C9D4 */
extern UBYTE wChanGroove[];                 /* $C150, [4] */
extern UBYTE wChanGrooveRow[];              /* $C158, [4] */

UBYTE ReadSynthParam(UBYTE field);          /* the current synth's param byte */
void WriteSynthParam(UBYTE field, UBYTE v);
void WriteGrooveByte(UBYTE step, UBYTE v);
UBYTE ReadGrooveStep(UBYTE step);
void ClearSynthWaveBufs(void);
void ClearSynthWaveBufs2(void);
void SynthFilterPass(void);
void SetSpriteX(UBYTE sprite, UBYTE col);
void SetSpriteTile3(UBYTE sprite);
void DrawGrooveSteps(void);
void DrawAllChanStatus(void);

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

void RedrawScreenFar(void)
{
    FarCall(1, FAR_FN(RedrawScreen));
}

void WriteSynthParam(UBYTE field, UBYTE v)
{
    UWORD p;

    p = (UWORD)SramSynthParams + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    WriteSongByte(1, p, v);
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
