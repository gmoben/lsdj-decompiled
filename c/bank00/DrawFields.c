/*
 * The bank 0 field drawers -- LSDj 9.4.0, ROM bank $00 ($1298, $2BC4-$2E6C).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * ColorField colours a field: on CGB it fills wAttributeRowBuffer with the palette
 * and writes it to VRAM bank 1 with set_bkg_tiles, on DMG it draws a tile run.
 * The tempo (a stored 0-39 stands for 256-295; the three sync speeds show
 * as 2X/3X/6X), the channel header and the pulse duty / pan values. The
 * string literals come out in the order SDCC numbers them, and a block-local
 * initialiser is numbered before everything else in the function: the pan
 * default is `char *s = "LR";` inside its case, which puts "LR" ahead of
 * case 0's "--" in the ROM (DrawFieldValue).
 */

#include "lsdj.h"

extern volatile sfr UBYTE VBK_REG;          /* $FF4F */
extern UBYTE wAttributeRowBuffer[];                 /* $C677, [20] */
void DrawTileRun(UBYTE x, UBYTE y, UBYTE w, UBYTE h, UBYTE tile);   /* asm */

void ColorField(UBYTE x, UBYTE y, UBYTE w, UBYTE palette)
{
    if (!w)
        return;
    if (hGameboyType) {
        if (w > 20)
            DebugTrap();
        FillMemory(wAttributeRowBuffer, palette, w);
        VBK_REG = 1;
        set_bkg_tiles(x, y, w, 1, wAttributeRowBuffer);
        VBK_REG = 0;
    } else
        DrawTileRun(x, y, w, 1, palette);
}

UWORD GetTempoTimerValue(void)
{
    switch (wTempoMode) {
    case TEMPO_2X:
        return wIsCgb ? 0x132 : 0x12B;
    case TEMPO_3X:
        return wIsCgb ? 0x1CB : 0x1C0;
    case TEMPO_6X:
        return wIsCgb ? 0x396 : 0x380;
    }
    return wTempo < 40 ? (UWORD)wTempo + 0x100 : wTempo;
}

void DrawTempo(UWORD tempo)
{
    switch (wTempoMode) {
    case TEMPO_2X:
        DrawString("2X ");
        break;
    case TEMPO_3X:
        DrawString("3X ");
        break;
    case TEMPO_6X:
        DrawString("6X ");
        break;
    default:
        printf("%d", tempo);
        if (tempo < 100)
            DrawStringTile20();
    }
}

void DrawTempoField(void)
{
    wVramCursor = (UBYTE *)BG_MAP(17, 4);
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawTempo(GetTempoTimerValue());
}

void DrawChannelHeader(void)
{
    char *s;

    s = "NOI";
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    wVramCursor = (UBYTE *)BG_MAP(17, 0);
    switch (wSongColumn) {
    case CHANNEL_PU1:
        s = "PU1";
        break;
    case CHANNEL_PU2:
        s = "PU2";
        break;
    case CHANNEL_WAV:
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
        if (wSongColumn == CHANNEL_WAV)
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
