/*
 * The synth screen -- LSDj 9.4.0, ROM bank $1D.
 *
 * Its fields and input, the per-wave parameter curves and their loader, the
 * PHASE stage (PINCH, WARP, RESYNC, RESYN2), the overwrite prompt.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void LoadSynthParameters(void);
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wClipboardType;                /* $C411 */
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
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wSynthNavigationSide;                 /* $CB63 */
UBYTE ReadSynthParameter(UBYTE field);          /* the current synth's param byte */
void WriteSynthParameter(UBYTE field, UBYTE v);
void SendLinkClipboard(void);                   /* $1C:$71CD */
void DrawAllChannelStatus(void);
void DrawSynthNumber(void);
void BuildOverwritePrompt(void);
void RedrawScreenFar(void);
void ClearSynthFlag(UBYTE x);
void MoveMapCursorLeft(void);                     /* bank 5 */
void MoveMapCursorRight(void);                    /* bank 5 */
void MoveMapCursorDown(void);                     /* bank 5 */
void ShowSynthFieldHelp(void);               /* bank 5 */
void SoundToggleSongPlayback(void);                 /* bank 2 */
void SoundTogglePlayback(void);                    /* bank 2 */
UWORD SynthLockByteAddressB1D(UBYTE x);

UWORD SynthLockByteAddressB1D(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

UBYTE IsSynthLocked(UBYTE x)
{
    UBYTE v;

    v = ReadSongByte(1, SynthLockByteAddressB1D(x));
    return v & (1 << (x & 7));
}

void ClearSynthFlag(UBYTE x)
{
    UWORD p;
    UBYTE v;

    p = SynthLockByteAddressB1D(x);
    v = ReadSongByte(1, p);
    x &= 7;
    v &= ~(1 << x);
    WriteSongByte(1, p, v);
}

void DrawSynthPhaseField(UBYTE v)
{
    switch (v) {
    case SYNTH_PHASE_PINCH:
        DrawString(" PINCH");
        break;
    case SYNTH_PHASE_WARP:
        DrawString("  WARP");
        break;
    case SYNTH_PHASE_RESYNC:
        DrawString("RESYNC");
        break;
    case SYNTH_PHASE_RESYNC2:
        DrawString("RESYN2");
        break;
    }
}

UBYTE ReadSynthParameter(UBYTE field)
{
    UWORD p;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    return ReadSongByte(1, p);
}

UBYTE SynthSignalIsFrame(void)
{
    return ReadSynthParameter(SYNTH_BYTE_SIGNAL) == 3;
}

void DrawSynthFilterField(void)
{
    wVramCursor = (UBYTE *)BG_MAP(8, 3);
    if (SynthSignalIsFrame()) {
        DrawString("-----");
    } else {
        switch (ReadSynthParameter(SYNTH_BYTE_FILTER)) {
        case SYNTH_FILTER_LOWPASS:
            DrawString(" LOWP");
            break;
        case SYNTH_FILTER_HIGHPASS:
            DrawString("HIGHP");
            break;
        case SYNTH_FILTER_BANDPASS:
            DrawString("BANDP");
            break;
        case SYNTH_FILTER_ALLPASS:
            DrawString(" ALLP");
            break;
        case SYNTH_FILTER_NOTCH:
            DrawString("NOTCH");
            break;
        }
    }
}

void DrawSynthSignalField(void)
{
    wVramCursor = (UBYTE *)BG_MAP(9, 2);
    switch (ReadSynthParameter(SYNTH_BYTE_SIGNAL)) {
    case SYNTH_SIGNAL_SQUARE:
        DrawString("iiii");
        break;
    case SYNTH_SIGNAL_SAW:
        DrawString("mmmm");
        break;
    case SYNTH_SIGNAL_TRIANGLE:
        DrawString("kkkk");
        break;
    case SYNTH_SIGNAL_WAVE_FRAMES:
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

void DrawSynthDistortionField(UBYTE v)
{
    switch (v) {
    case SYNTH_DISTORTION_CLIP:
        DrawString("CLIP");
        break;
    case SYNTH_DISTORTION_FOLD:
        DrawString("FOLD");
        break;
    case SYNTH_DISTORTION_WRAP:
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

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(8, 7);
    DrawByte(wSynthScreen);
    wVramCursor = (UBYTE *)BG_MAP(11, 7);
    DrawByte(wSynthScreen);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawSynthSignalField();
    p++;
    DrawSynthFilterField();
    p++;
    v = ReadSongByte(1, (UWORD)p++);
    wVramCursor = (UBYTE *)BG_MAP(9, 11);
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v >> 4);
    wVramCursor = (UBYTE *)BG_MAP(12, 11);
    if (SynthSignalIsFrame())
        DrawStringIndexed('-');
    else
        DrawByte(v & 0x0F);
    wVramCursor = (UBYTE *)BG_MAP(9, 4);
    DrawSynthDistortionField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(7, 5);
    DrawSynthPhaseField(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(8, 9);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(8, 10);
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)BG_MAP(8, 14);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(8, 12);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(11, 9);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(11, 10);
    if (SynthSignalIsFrame())
        DrawString("--");
    else
        DrawHexByte(ReadSongByte(1, (UWORD)p));
    p++;
    wVramCursor = (UBYTE *)BG_MAP(11, 14);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(11, 12);
    DrawHexByte(ReadSongByte(1, (UWORD)p++));
    wVramCursor = (UBYTE *)BG_MAP(8, 13);
    DrawHexByte(ReadSongByte(1, (UWORD)p++) + 0x0F);
    wVramCursor = (UBYTE *)BG_MAP(11, 13);
    DrawHexByte(ReadSongByte(1, (UWORD)p) + 0x0F);
    DrawSynthCursor();
}

void DrawSynthScreen(void)
{
    wSynth &= 0x0F;
    wSynth = wSynth + (wSynthScreen << 4);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    DrawSynthNumber();
    FarCall(5, DrawMapTopRow);
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
            wStringSource = IdentityCharacterMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            ColorField(x, y, 1, 0);
            SetVramCursor(x, y);
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
void OverwriteSynthWaves(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    BuildOverwritePrompt();
    wOverwriteStart = 1;
    wOverwriteEnd = 1;
    wVramCursor = (UBYTE *)BG_MAP(4, 5);
    DrawString("OVERWRITE");
    wVramCursor = (UBYTE *)BG_MAP(4, 6);
    DrawString("HAND-DRAWN");
    wVramCursor = (UBYTE *)BG_MAP(4, 7);
    DrawString("WAVES?");
    wVramCursor = (UBYTE *)BG_MAP(4, 9);
    DrawString("YES");
    wVramCursor = (UBYTE *)BG_MAP(11, 9);
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
        case J_B:
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        case J_LEFT:
            wOverwriteEnd = 0;
            DrawOverwriteChoice();
            break;
        case J_RIGHT:
            wOverwriteEnd = 1;
            DrawOverwriteChoice();
            break;
        case J_A:
            if (!wOverwriteEnd)
                ClearSynthFlag(wSynthScreen);
            wOverwriteStart = 0;
            RedrawScreenFar();
            break;
        }
    }
}

void WriteSynthParameter(UBYTE field, UBYTE v)
{
    UWORD p;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    WriteSongByte(1, p, v);
}

void AddSynthParameterClamped(UBYTE field, UBYTE maximum, UBYTE increment)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v += (UWORD)increment;
    if (v > maximum)
        v = maximum;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void SubSynthParameterClamped(UBYTE field, UBYTE decrement)
{
    UWORD p;
    UWORD v;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    v -= decrement;
    if (v & 0x8000)
        v = 0;
    DrawHexByte(v);
    WriteSongByte(1, p, v);
}

void RaiseSynthParameterTo(UBYTE field, UBYTE maximum)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v != maximum) {
        v++;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

void LowerSynthParameterTo0(UBYTE field)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)SramSynthParameters + ((UWORD)wSynthScreen << 4);
    p += (UWORD)field;
    v = ReadSongByte(1, p);
    if (v) {
        v--;
        DrawHexByte(v);
        WriteSongByte(1, p, v);
    }
}

UBYTE RaiseSynthFieldHigh(void)
{
    UBYTE v, t;

    v = ReadSynthParameter(SYNTH_BYTE_Q);
    t = v & 0xF0;
    if (t != 0xF0) {
        v += 0x10;
        WriteSynthParameter(SYNTH_BYTE_Q, v);
    }
    return v >> 4;
}

UBYTE RaiseSynthFieldLow(void)
{
    UBYTE v, t;

    v = ReadSynthParameter(SYNTH_BYTE_Q);
    t = v & 0x0F;
    if (t != 0x0F) {
        v++;
        WriteSynthParameter(SYNTH_BYTE_Q, v);
    }
    return v & 0x0F;
}

UBYTE LowerSynthFieldHigh(void)
{
    UBYTE v;

    v = ReadSynthParameter(SYNTH_BYTE_Q);
    if (v & 0xF0) {
        v -= 0x10;
        WriteSynthParameter(SYNTH_BYTE_Q, v);
    }
    return v >> 4;
}

UBYTE LowerSynthFieldLow(void)
{
    UBYTE v;

    v = ReadSynthParameter(SYNTH_BYTE_Q);
    if (v & 0x0F) {
        v--;
        WriteSynthParameter(SYNTH_BYTE_Q, v);
    }
    return v & 0x0F;
}

/*
 * The synth screen's input. A on a field that would overwrite the synth's
 * waves asks first (OverwriteSynthWaves / the prompt's own input); A alone
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

    if ((wButtons & J_A) && IsSynthLocked(wSynthScreen) && !wOverwriteStart) {
        if (wAPressEnabled)
            OverwriteSynthWaves();
        return 1;
    }
    if (wOverwriteStart) {
        OverwritePromptInput();
        return 1;
    }
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    if (wButtons != J_A)
        wDrawStep = 0;
    switch (wButtons) {
    case J_A:
        if (wAPressEnabled && ++wDrawStep == 2)
            FarCall(5, ShowSynthFieldHelp);
        return 1;
    case J_UP:
        if (!wSynthCursor)
            break;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case SYNTH_FIELD_VOLUME_END:
            wSynthCursor = SYNTH_FIELD_PHASE_TYPE;
            break;
        case SYNTH_FIELD_VSHIFT_START:
        case SYNTH_FIELD_VSHIFT_END:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        case SYNTH_FIELD_DISTORTION:
            if (SynthSignalIsFrame())
                wSynthCursor--;
        default:
            wSynthCursor--;
        }
        DrawSynthCursor();
        return 1;
    case J_DOWN:
        EraseSynthCursor();
        switch (wSynthCursor) {
        case SYNTH_FIELD_PHASE_TYPE:
            wSynthCursor = wSynthNavigationSide ? 4 : 10;
            break;
        case SYNTH_FIELD_VOLUME_START:
        case SYNTH_FIELD_VOLUME_END:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        case SYNTH_FIELD_SIGNAL:
            if (SynthSignalIsFrame())
                wSynthCursor++;
        default:
            wSynthCursor++;
        case SYNTH_FIELD_PHASE_START:
        case SYNTH_FIELD_PHASE_END:
            break;
        }
        DrawSynthCursor();
        return 1;
    case J_LEFT:
        wSynthNavigationSide = 1;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case SYNTH_FIELD_VOLUME_END:
        case SYNTH_FIELD_CUTOFF_END:
        case SYNTH_FIELD_Q_END:
        case SYNTH_FIELD_VSHIFT_END:
        case SYNTH_FIELD_LIMIT_END:
        case SYNTH_FIELD_PHASE_END:
            wSynthCursor -= 6;
        }
        DrawSynthCursor();
        return 1;
    case J_RIGHT:
        wSynthNavigationSide = 0;
        EraseSynthCursor();
        switch (wSynthCursor) {
        case SYNTH_FIELD_VOLUME_START:
        case SYNTH_FIELD_CUTOFF_START:
        case SYNTH_FIELD_Q_START:
        case SYNTH_FIELD_VSHIFT_START:
        case SYNTH_FIELD_LIMIT_START:
        case SYNTH_FIELD_PHASE_START:
            wSynthCursor += 6;
        }
        DrawSynthCursor();
        return 1;
    case (J_A | J_RIGHT):
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case SYNTH_FIELD_SIGNAL:
            if (wButtonsSame)
                break;
            switch (ReadSynthParameter(SYNTH_BYTE_SIGNAL)) {
            case SYNTH_SIGNAL_SQUARE:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_SAW);
                break;
            case SYNTH_SIGNAL_SAW:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_TRIANGLE);
                break;
            default:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_WAVE_FRAMES);
                DrawSynthScreen();
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawSynthSignalField();
            }
            DrawSynthSignalField();
            break;
        case SYNTH_FIELD_FILTER:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_FILTER);
            if (v == 3)
                break;
            v++;
            WriteSynthParameter(SYNTH_BYTE_FILTER, v);
            DrawSynthFilterField();
            break;
        case SYNTH_FIELD_DISTORTION:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_DISTORTION);
            if (v >= 2)
                break;
            v++;
            WriteSynthParameter(SYNTH_BYTE_DISTORTION, v);
            DrawSynthDistortionField(v);
            break;
        case SYNTH_FIELD_LIMIT_START:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_START) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_START, v - 0x0F);
            break;
        case SYNTH_FIELD_LIMIT_END:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_END) + 0x0F;
            if (v == 0xFF)
                break;
            v++;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_END, v - 0x0F);
            break;
        case SYNTH_FIELD_PHASE_TYPE:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_PHASE_TYPE);
            if (v >= 3)
                break;
            v++;
            DrawSynthPhaseField(v);
            WriteSynthParameter(SYNTH_BYTE_PHASE_TYPE, v);
            break;
        case SYNTH_FIELD_Q_START:
            DrawByte(RaiseSynthFieldHigh());
            break;
        case SYNTH_FIELD_Q_END:
            DrawByte(RaiseSynthFieldLow());
            break;
        case SYNTH_FIELD_VOLUME_START:
            RaiseSynthParameterTo(SYNTH_BYTE_VOLUME_START, 0xFF);
            break;
        case SYNTH_FIELD_VOLUME_END:
            RaiseSynthParameterTo(SYNTH_BYTE_VOLUME_END, 0xFF);
            break;
        case SYNTH_FIELD_CUTOFF_START:
            RaiseSynthParameterTo(SYNTH_BYTE_CUTOFF_START, 0xFF);
            break;
        case SYNTH_FIELD_CUTOFF_END:
            RaiseSynthParameterTo(SYNTH_BYTE_CUTOFF_END, 0xFF);
            break;
        case SYNTH_FIELD_VSHIFT_START:
            RaiseSynthParameterTo(SYNTH_BYTE_VSHIFT_START, 0xFF);
            break;
        case SYNTH_FIELD_VSHIFT_END:
            RaiseSynthParameterTo(SYNTH_BYTE_VSHIFT_END, 0xFF);
            break;
        case SYNTH_FIELD_PHASE_START:
            RaiseSynthParameterTo(SYNTH_BYTE_PHASE_START, 0x1F);
            break;
        case SYNTH_FIELD_PHASE_END:
            RaiseSynthParameterTo(SYNTH_BYTE_PHASE_END, 0x1F);
            break;
        }
        LoadSynthParameters();
        return 1;
    case (J_A | J_LEFT):
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case SYNTH_FIELD_SIGNAL:
            if (wButtonsSame)
                break;
            switch (ReadSynthParameter(SYNTH_BYTE_SIGNAL)) {
            default:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_SQUARE);
                break;
            case SYNTH_SIGNAL_TRIANGLE:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_SAW);
                break;
            case SYNTH_SIGNAL_WAVE_FRAMES:
                WriteSynthParameter(SYNTH_BYTE_SIGNAL, SYNTH_SIGNAL_TRIANGLE);
                DrawSynthScreen();
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                DrawSynthSignalField();
            }
            DrawSynthSignalField();
            break;
        case SYNTH_FIELD_FILTER:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_FILTER);
            if (!v)
                break;
            v--;
            WriteSynthParameter(SYNTH_BYTE_FILTER, v);
            DrawSynthFilterField();
            break;
        case SYNTH_FIELD_DISTORTION:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_DISTORTION);
            if (!v)
                break;
            v--;
            WriteSynthParameter(SYNTH_BYTE_DISTORTION, v);
            DrawSynthDistortionField(v);
            break;
        case SYNTH_FIELD_LIMIT_START:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_START) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_START, v - 0x0F);
            break;
        case SYNTH_FIELD_LIMIT_END:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_END) + 0x0F;
            if (!v)
                break;
            v--;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_END, v - 0x0F);
            break;
        case SYNTH_FIELD_PHASE_TYPE:
            if (wButtonsSame)
                break;
            v = ReadSynthParameter(SYNTH_BYTE_PHASE_TYPE);
            if (!v)
                break;
            v--;
            DrawSynthPhaseField(v);
            WriteSynthParameter(SYNTH_BYTE_PHASE_TYPE, v);
            break;
        case SYNTH_FIELD_Q_START:
            DrawByte(LowerSynthFieldHigh());
            break;
        case SYNTH_FIELD_Q_END:
            DrawByte(LowerSynthFieldLow());
            break;
        case SYNTH_FIELD_VOLUME_START:
            LowerSynthParameterTo0(SYNTH_BYTE_VOLUME_START);
            break;
        case SYNTH_FIELD_VOLUME_END:
            LowerSynthParameterTo0(SYNTH_BYTE_VOLUME_END);
            break;
        case SYNTH_FIELD_CUTOFF_START:
            LowerSynthParameterTo0(SYNTH_BYTE_CUTOFF_START);
            break;
        case SYNTH_FIELD_CUTOFF_END:
            LowerSynthParameterTo0(SYNTH_BYTE_CUTOFF_END);
            break;
        case SYNTH_FIELD_VSHIFT_START:
            LowerSynthParameterTo0(SYNTH_BYTE_VSHIFT_START);
            break;
        case SYNTH_FIELD_VSHIFT_END:
            LowerSynthParameterTo0(SYNTH_BYTE_VSHIFT_END);
            break;
        case SYNTH_FIELD_PHASE_START:
            LowerSynthParameterTo0(SYNTH_BYTE_PHASE_START);
            break;
        case SYNTH_FIELD_PHASE_END:
            LowerSynthParameterTo0(SYNTH_BYTE_PHASE_END);
            break;
        }
        LoadSynthParameters();
        return 1;
    case (J_A | J_UP):
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case SYNTH_FIELD_Q_START:
            RaiseSynthFieldHigh();
            DrawByte(RaiseSynthFieldHigh());
            break;
        case SYNTH_FIELD_Q_END:
            RaiseSynthFieldLow();
            DrawByte(RaiseSynthFieldLow());
            break;
        case SYNTH_FIELD_PHASE_START:
            AddSynthParameterClamped(7, 0x1F, 8);
            break;
        case SYNTH_FIELD_PHASE_END:
            AddSynthParameterClamped(0x0B, 0x1F, 8);
            break;
        case SYNTH_FIELD_VOLUME_START:
            AddSynthParameterClamped(5, 0xFF, 0x10);
            break;
        case SYNTH_FIELD_VOLUME_END:
            AddSynthParameterClamped(9, 0xFF, 0x10);
            break;
        case SYNTH_FIELD_LIMIT_START:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_START) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_START, v - 0x0F);
            break;
        case SYNTH_FIELD_LIMIT_END:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_END) + 0x0F;
            v += 0x10;
            if (!(v & 0xF0))
                v = 0xFF;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_END, v - 0x0F);
            break;
        case SYNTH_FIELD_CUTOFF_START:
            AddSynthParameterClamped(6, 0xFF, 0x10);
            break;
        case SYNTH_FIELD_CUTOFF_END:
            AddSynthParameterClamped(0x0A, 0xFF, 0x10);
            break;
        case SYNTH_FIELD_VSHIFT_START:
            AddSynthParameterClamped(8, 0xFF, 0x10);
            break;
        case SYNTH_FIELD_VSHIFT_END:
            AddSynthParameterClamped(0x0C, 0xFF, 0x10);
            break;
        }
        LoadSynthParameters();
        return 1;
    case (J_A | J_DOWN):
        SetVramCursor(SynthFieldX[wSynthCursor], SynthFieldLayout[wSynthCursor]);
        switch (wSynthCursor) {
        case SYNTH_FIELD_Q_START:
            LowerSynthFieldHigh();
            DrawByte(LowerSynthFieldHigh());
            break;
        case SYNTH_FIELD_Q_END:
            LowerSynthFieldLow();
            DrawByte(LowerSynthFieldLow());
            break;
        case SYNTH_FIELD_PHASE_START:
            SubSynthParameterClamped(7, 8);
            break;
        case SYNTH_FIELD_PHASE_END:
            SubSynthParameterClamped(0x0B, 8);
            break;
        case SYNTH_FIELD_VOLUME_START:
            SubSynthParameterClamped(5, 0x10);
            break;
        case SYNTH_FIELD_VOLUME_END:
            SubSynthParameterClamped(9, 0x10);
            break;
        case SYNTH_FIELD_LIMIT_START:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_START) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_START, v - 0x0F);
            break;
        case SYNTH_FIELD_LIMIT_END:
            v = ReadSynthParameter(SYNTH_BYTE_LIMIT_END) + 0x0F;
            v -= 0x10;
            if ((v & 0xF0) == 0xF0)
                v = 0;
            DrawHexByte(v);
            WriteSynthParameter(SYNTH_BYTE_LIMIT_END, v - 0x0F);
            break;
        case SYNTH_FIELD_CUTOFF_START:
            SubSynthParameterClamped(6, 0x10);
            break;
        case SYNTH_FIELD_CUTOFF_END:
            SubSynthParameterClamped(0x0A, 0x10);
            break;
        case SYNTH_FIELD_VSHIFT_START:
            SubSynthParameterClamped(8, 0x10);
            break;
        case SYNTH_FIELD_VSHIFT_END:
            SubSynthParameterClamped(0x0C, 0x10);
            break;
        }
        LoadSynthParameters();
        return 1;
    case (J_B | J_RIGHT):
    case (J_B | J_UP):
        wMessageStringPointer = 0;
        if (wSynthScreen != 0x0F) {
            wSynthScreen++;
            DrawSynthScreen();
        }
        return 1;
    case (J_B | J_LEFT):
    case (J_B | J_DOWN):
        wMessageStringPointer = 0;
        if (wSynthScreen) {
            wSynthScreen--;
            DrawSynthScreen();
        }
        return 1;
    case (J_SELECT | J_START):
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case (J_SELECT | J_DOWN):
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        RedrawScreenFar();
        return 1;
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorLeft);
        RedrawScreenFar();
        return 1;
    case (J_SELECT | J_RIGHT):
        if (wButtonsSame)
            return 0;
        if (wSynthCursor == SYNTH_FIELD_SIGNAL && SynthSignalIsFrame()) {
            wSynth = wSynthScreen + 0xF0;
        } else {
            wSynth &= 0x0F;
            wSynth += wSynthScreen << 4;
        }
        FarCall(5, MoveMapCursorRight);
        FarCall(7, RedrawScreen);
        return 1;
    case (J_SELECT | J_B):
        ShowMessage("COPIED SYNTH");
        wCursorMode = CURSOR_COPIED;
        wClipboardType = SCREEN_SYNTH;
        p = ((UWORD)wSynthScreen << 4) + (UWORD)SRAM_SYNTH_PARAMETERS;
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
    case (J_SELECT | J_A):
        if (wCursorMode == CURSOR_NORMAL || wClipboardType != SCREEN_SYNTH)
            return 0;
        ShowMessage("PASTED SYNTH");
        q = ((UWORD)wSynthScreen << 4) + (UWORD)SRAM_SYNTH_PARAMETERS;
        for (v = 0; v < 0x10; v++) {
            WriteSongByte(1, q, ReadClipboardCell(0, v));
            q++;
        }
        LoadSynthParameters();
        DrawSynthNumber();
        return 1;
    }
    return 0;
}
