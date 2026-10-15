/*
 * The instrument screen's bank-$1F half -- LSDj 9.4.0, ROM bank $1F.
 *
 * Each instrument type's fields (pulse, wave, kit, noise), the envelope and
 * table fields.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */
extern char PlayModeNames[];                /* $00:$0839, five 9-byte names "  MANUAL".."  RESYNC" */
void DrawPulseDuty(UBYTE duty);             /* bank 0 */
void DrawWaveFinetune(void);                 /* $03:$4CD9 */
void DrawWaveSynthField(void);              /* $03:$4D15 */
void DrawWaveLength(void);                 /* $03:$498B */
void DrawWaveLoopPosition(void);                  /* $03:$49FD */
void DrawWaveSpeed(void);                 /* $03:$49B7 */

void DrawInstrumentTableMode(void)
{
    char *s;

    switch (ReadCurrentInstrumentByte(0)) {
    case 0:
        wVramCursor = (UBYTE *)BG_MAP(6, 17);
        break;
    case 1:
        wVramCursor = (UBYTE *)BG_MAP(6, 17);
        break;
    case 2:
        wVramCursor = (UBYTE *)BG_MAP(6, 17);
        break;
    case 3:
        wVramCursor = (UBYTE *)BG_MAP(6, 14);
        break;
    }
    s = (ReadCurrentInstrumentByte(6) & 0x20) ? ((ReadCurrentInstrumentByte(5) & 8) ? "STEP" : "TICK") : " OFF";
    DrawString(s);
}

void DrawInstrumentTableField(void)
{
    char v;

    v = ReadCurrentInstrumentByte(6);
    if (v & 0x20)
        DrawHexByte(v & 0x1F);
    else
        DrawString("--");
}

/*
 * One kit's LOOP setting: OFF unless `loopBit` of byte 5 is set, then ATK
 * (loop from the start) when bit 7 of byte `atkByte` is set, else ON (loop
 * from OFFSET). The literal first in `0x80 & ...` keeps the masked byte in
 * a register for the test (`and; ld c,a; or a,c`): SDCC fuses `x & lit`
 * into its `if` only with the literal on the right.
 */
void DrawKitLoopMode(UBYTE loopBit, UBYTE attackByte)
{
    if (ReadCurrentInstrumentByte(5) & loopBit) {
        if (0x80 & ReadCurrentInstrumentByte(attackByte))
            DrawString("ATK");
        else
            DrawString(" ON");
    } else
        DrawString("OFF");
}

void DrawKitLoopField(void)
{
    DrawKitLoopMode(0x40, 2);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawKitLoopMode(0x20, 9);
}

UBYTE InstrumentEnvelopeHasSecond(void)
{
    return ReadCurrentInstrumentByte(1) & 0x0F;
}

UBYTE InstrumentEnvelopeHasThird(void)
{
    UBYTE a, b;

    a = InstrumentEnvelopeHasSecond();
    b = ReadCurrentInstrumentByte(9) & 0x0F;
    return a && b;
}

void DrawEnvelopeFields(void)
{
    UBYTE v;

    wVramCursor = BG_MAP(7, 5);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawString("/--/--");
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    wVramCursor = BG_MAP(5, 5);
    DrawHexByte(ReadCurrentInstrumentByte(1));
    if (InstrumentEnvelopeHasSecond()) {
        wVramCursor = BG_MAP(8, 5);
        DrawHexByte(ReadCurrentInstrumentByte(9));
        if (InstrumentEnvelopeHasThird()) {
            v = ReadCurrentInstrumentByte(10);
            wVramCursor = BG_MAP(11, 5);
            if (v & 0xF0)
                DrawHexByte(v);
            else {
                DrawByte(v >> 4);
                DrawStringIndexed('-');
            }
        }
    }
}

void DrawPulseInstrument(void)
{
    UBYTE v;

    DrawEnvelopeFields();
    wVramCursor = BG_MAP(11, 7);
    v = ReadCurrentInstrumentByte(7);
    DrawPulseDuty(v >> 6);
    wVramCursor = BG_MAP(11, 8);
    DrawFieldValue(0x0C, ReadCurrentInstrumentByte(7));
    wVramCursor = BG_MAP(8, 9);
    v = ReadCurrentInstrumentByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = BG_MAP(11, 10);
    DrawHexByte(ReadCurrentInstrumentByte(4) & 0x7F);
    wVramCursor = BG_MAP(7, 11);
    v = ReadCurrentInstrumentByte(5);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = BG_MAP(10, 12);
    FarCallArgument(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = BG_MAP(12, 15);
    DrawByte(ReadCurrentInstrumentByte(8));
    wVramCursor = BG_MAP(11, 13);
    DrawHexByte(ReadCurrentInstrumentByte(2));
    wVramCursor = BG_MAP(11, 14);
    DrawHexByte(ReadCurrentInstrumentByte(0x0B));
    wVramCursor = BG_MAP(6, 17);
    DrawInstrumentTableMode();
    wVramCursor++;
    DrawInstrumentTableField();
}

void DrawNoiseInstrument(void)
{
    UBYTE v;

    wVramCursor = BG_MAP(5, 5);
    DrawEnvelopeFields();
    wVramCursor = BG_MAP(11, 7);
    DrawFieldValue(0x0C, ReadCurrentInstrumentByte(7));
    wVramCursor = BG_MAP(8, 8);
    v = ReadCurrentInstrumentByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = BG_MAP(9, 9);
    DrawString(ReadCurrentInstrumentByte(2) ? "SAFE" : "FREE");
    wVramCursor = BG_MAP(11, 10);
    v = ReadCurrentInstrumentByte(5);
    FarCallArgument(1, FAR_FN(DrawSpeedGlyph), v);
    wVramCursor = BG_MAP(10, 11);
    FarCallArgument(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = BG_MAP(12, 12);
    DrawByte(ReadCurrentInstrumentByte(8));
    wVramCursor = BG_MAP(6, 14);
    DrawInstrumentTableMode();
    wVramCursor++;
    DrawInstrumentTableField();
}

void DrawKitNameCell(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
}

void DrawKitDistortionName(UBYTE v);              /* bank 3 */

/*
 * The KIT instrument's fields: both kits' names, VOLUME (byte 1 bits 6-5,
 * shown 3..0 for $20/$40/$60/0), OUTPUT, LOOP, SPEED (byte 2 bit 6: 0.5X or
 * 1X), the DIST mode, FINETUNE, both OFFSETs and LENs (0 = ALL), the pitch
 * mode and the table.
 */
void DrawKitInstrument(void)
{
    UBYTE v, flags;

    wVramCursor = BG_MAP(4, 5);
    v = ReadCurrentInstrumentByte(2) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = BG_MAP(4, 6);
    v = ReadCurrentInstrumentByte(9) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = BG_MAP(9, 8);
    v = ReadCurrentInstrumentByte(1);
    switch (v & 0x60) {
    case 0x20:
        v = '3';
        break;
    case 0x40:
        v = '2';
        break;
    case 0x60:
        v = '1';
        break;
    default:
        v = '0';
    }
    DrawStringIndexed((char)v);             /* via a copy: ld b,(hl); ld a,b */
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawFieldValue(0x0C, ReadCurrentInstrumentByte(7));
    wVramCursor = BG_MAP(6, 13);
    DrawKitLoopField();
    wVramCursor = BG_MAP(9, 14);
    if (0x40 & ReadCurrentInstrumentByte(2))         /* literal first: tested in b */
        DrawString("0.5");
    else
        DrawString("  1");
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('X');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    wVramCursor = BG_MAP(8, 15);
    v = ReadCurrentInstrumentByte(0x0A);
    FarCallArgument(3, FAR_FN(DrawKitDistortionName), v);
    wVramCursor = BG_MAP(11, 10);
    DrawHexByte(ReadCurrentInstrumentByte(8));
    wVramCursor = BG_MAP(8, 11);
    v = ReadCurrentInstrumentByte(0x0C);
    DrawHexByte(v);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    v = ReadCurrentInstrumentByte(0x0D);
    DrawHexByte(v);
    wVramCursor = BG_MAP(6, 12);
    v = ReadCurrentInstrumentByte(3);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    v = ReadCurrentInstrumentByte(0x0B);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    wVramCursor = BG_MAP(7, 9);
    flags = ReadCurrentInstrumentByte(5);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), flags);
    wVramCursor = BG_MAP(6, 17);
    DrawInstrumentTableMode();
    wVramCursor++;
    DrawInstrumentTableField();
}

void DrawWaveInstrument(void)
{
    UBYTE v;

    wVramCursor = BG_MAP(9, 5);
    v = ReadCurrentInstrumentByte(1);
    switch (v & 0x60) {
    case 0x20:
        v = '3';
        break;
    case 0x40:
        v = '2';
        break;
    case 0x60:
        v = '1';
        break;
    default:
        v = '0';
        break;
    }
    DrawStringIndexed((char)v);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('/');
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    DrawFieldValue(0x0C, ReadCurrentInstrumentByte(7));
    wVramCursor = BG_MAP(7, 6);
    v = ReadCurrentInstrumentByte(5);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = BG_MAP(10, 7);
    FarCallArgument(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = BG_MAP(12, 9);
    DrawByte(ReadCurrentInstrumentByte(8));
    FarCall(3, DrawWaveFinetune);
    FarCall(3, DrawWaveSynthField);
    FarCall(3, DrawWaveLength);
    FarCall(3, DrawWaveLoopPosition);
    FarCall(3, DrawWaveSpeed);
    wVramCursor = BG_MAP(5, 11);
    v = ReadCurrentInstrumentByte(9);
    DrawString(PlayModeNames + (v < 5 ? v : 5) * 9);
    wVramCursor = BG_MAP(6, 17);
    DrawInstrumentTableMode();
    wVramCursor++;
    DrawInstrumentTableField();
}
