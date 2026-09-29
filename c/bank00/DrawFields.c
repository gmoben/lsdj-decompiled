/*
 * The bank 0 field drawers -- LSDj 9.4.0, ROM bank $00:$2BC4-$2E6C.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The tempo (a stored 0-39 stands for 256-295; the three sync speeds show
 * as 2X/3X/6X), the channel header and the pulse duty / pan values. The
 * string literals come out in the order SDCC numbers them, and a block-local
 * initialiser is numbered before everything else in the function: the pan
 * default is `char *s = "LR";` inside its case, which puts "LR" ahead of
 * case 0's "--" in the ROM (DrawFieldValue).
 */

#include "lsdj.h"

UWORD GetTempoTimerValue(void)
{
    switch (wTempoMode) {
    case 1:
        return wIsCGB ? 0x132 : 0x12B;
    case 2:
        return wIsCGB ? 0x1CB : 0x1C0;
    case 3:
        return wIsCGB ? 0x396 : 0x380;
    }
    return wTempo < 40 ? (UWORD)wTempo + 0x100 : wTempo;
}

void DrawTempo(UWORD tempo)
{
    switch (wTempoMode) {
    case 1:
        DrawString("2X ");
        break;
    case 2:
        DrawString("3X ");
        break;
    case 3:
        DrawString("6X ");
        break;
    default:
        DrawFormat("%d", tempo);
        if (tempo < 100)
            DrawStringTile20();
    }
}

void DrawTempoField(void)
{
    wVramCursor = (UBYTE *)0x9891;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawTempo(GetTempoTimerValue());
}

void DrawChannelHeader(void)
{
    char *s;

    s = "NOI";
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    wVramCursor = (UBYTE *)0x9811;
    switch (wSongCol) {
    case 0:
        s = "PU1";
        break;
    case 1:
        s = "PU2";
        break;
    case 2:
        s = "WAV";
        break;
    }
    DrawString(s);
}

void DrawPulseDuty(UBYTE duty)
{
    char *s;

    s = "ef";
    switch (duty & 3) {
    case 0:
        s = "ad";
        break;
    case 1:
        s = "bd";
        break;
    case 2:
        s = "cd";
        break;
    }
    DrawString(s);
}

void DrawFieldValue(UBYTE kind, UBYTE v)
{
    switch (kind) {
    case 0:
        DrawString("--");
        break;
    case 0x12:
        if (wSongCol == 2)
            DrawHexByte(v);
        else
            DrawPulseDuty(v);
        break;
    case 0x0C: {
        char *s = "LR";

        switch (v & 3) {
        case 0:
            s = "gh";
            break;
        case 1:
            s = "Lh";
            break;
        case 2:
            s = "gR";
            break;
        }
        DrawString(s);
        break;
    }
    default:
        DrawHexByte(v);
    }
}
