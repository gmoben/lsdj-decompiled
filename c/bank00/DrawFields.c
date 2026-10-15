/*
 * The bank 0 field drawers -- LSDj 9.4.0, ROM bank $00 ($1298, $2BC4-$2E6C).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * ColorField colours a field: on CGB it fills wAttrRowBuf with the palette
 * and writes it to VRAM bank 1 with set_bkg_tiles, on DMG it draws a tile run.
 * The tempo (a stored 0-39 stands for 256-295; the three sync speeds show
 * as 2X/3X/6X), the channel header and the pulse duty / pan values. The
 * string literals come out in the order SDCC numbers them, and a block-local
 * initialiser is numbered before everything else in the function: the pan
 * default is `char *s = "LR";` inside its case, which puts "LR" ahead of
 * case 0's "--" in the ROM (DrawFieldVal).
 */

#include "lsdj.h"

extern volatile sfr UBYTE VBK_REG;          /* $FF4F */
extern UBYTE wAttrRowBuf[];                 /* $C677, [20] */
void DrawTileRun(UBYTE x, UBYTE y, UBYTE w, UBYTE h, UBYTE tile);   /* asm */

void ColorField(UBYTE x, UBYTE y, UBYTE w, UBYTE pal)
{
    if (!w)
        return;
    if (hGameboyType) {
        if (w > 20)
            DebugTrap();
        MemSet(wAttrRowBuf, pal, w);
        VBK_REG = 1;
        set_bkg_tiles(x, y, w, 1, wAttrRowBuf);
        VBK_REG = 0;
    } else
        DrawTileRun(x, y, w, 1, pal);
}

UWORD GetTempoTimerVal(void)
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
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawTempo(GetTempoTimerVal());
}

void DrawChanHeader(void)
{
    char *s;

    s = "NOI";
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
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

void DrawFieldVal(UBYTE kind, UBYTE v)
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
