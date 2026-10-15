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

void DrawFieldVal(UBYTE kind, UBYTE v);   /* bank 0, DrawFields.c */
extern char PlayModeNames[];                /* $00:$0839, five 9-byte names "  MANUAL".."  RESYNC" */
void DrawPulseDuty(UBYTE duty);             /* bank 0 */
void DrawWaveFinetune(void);                 /* $03:$4CD9 */
void DrawWaveSynthField(void);              /* $03:$4D15 */
void DrawWaveLen(void);                 /* $03:$498B */
void DrawWaveLoopPos(void);                  /* $03:$49FD */
void DrawWaveSpeed(void);                 /* $03:$49B7 */

void DrawInstrTableMode(void)
{
    char *s;

    switch (ReadCurInstrByte(0)) {
    case 0:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 1:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 2:
        wVramCursor = (UBYTE *)0x9A26;
        break;
    case 3:
        wVramCursor = (UBYTE *)0x99C6;
        break;
    }
    s = (ReadCurInstrByte(6) & 0x20) ? ((ReadCurInstrByte(5) & 8) ? "STEP" : "TICK") : " OFF";
    DrawString(s);
}

void DrawInstrTableField(void)
{
    char v;

    v = ReadCurInstrByte(6);
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
void DrawKitLoopMode(UBYTE loopBit, UBYTE atkByte)
{
    if (ReadCurInstrByte(5) & loopBit) {
        if (0x80 & ReadCurInstrByte(atkByte))
            DrawString("ATK");
        else
            DrawString(" ON");
    } else
        DrawString("OFF");
}

void DrawKitLoopField(void)
{
    DrawKitLoopMode(0x40, 2);
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('/');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawKitLoopMode(0x20, 9);
}

UBYTE InstrEnvHasSecond(void)
{
    return ReadCurInstrByte(1) & 0x0F;
}

UBYTE InstrEnvHasThird(void)
{
    UBYTE a, b;

    a = InstrEnvHasSecond();
    b = ReadCurInstrByte(9) & 0x0F;
    return a && b;
}

void DrawInstrEnvFields(void)
{
    UBYTE v;

    wVramCursor = 0x98A7;
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawString("/--/--");
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    wVramCursor = 0x98A5;
    DrawHexByte(ReadCurInstrByte(1));
    if (InstrEnvHasSecond()) {
        wVramCursor = 0x98A8;
        DrawHexByte(ReadCurInstrByte(9));
        if (InstrEnvHasThird()) {
            v = ReadCurInstrByte(10);
            wVramCursor = 0x98AB;
            if (v & 0xF0)
                DrawHexByte(v);
            else {
                DrawByte(v >> 4);
                DrawStringIndexed('-');
            }
        }
    }
}

void DrawPulseInstr(void)
{
    UBYTE v;

    DrawInstrEnvFields();
    wVramCursor = 0x98EB;
    v = ReadCurInstrByte(7);
    DrawPulseDuty(v >> 6);
    wVramCursor = 0x990B;
    DrawFieldVal(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x9928;
    v = ReadCurInstrByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = 0x994B;
    DrawHexByte(ReadCurInstrByte(4) & 0x7F);
    wVramCursor = 0x9967;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = 0x998A;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x99EC;
    DrawByte(ReadCurInstrByte(8));
    wVramCursor = 0x99AB;
    DrawHexByte(ReadCurInstrByte(2));
    wVramCursor = 0x99CB;
    DrawHexByte(ReadCurInstrByte(0x0B));
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawNoiseInstr(void)
{
    UBYTE v;

    wVramCursor = 0x98A5;
    DrawInstrEnvFields();
    wVramCursor = 0x98EB;
    DrawFieldVal(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x9908;
    v = ReadCurInstrByte(3);
    if (!(v & 0x40)) {
        DrawString("UNLIM");
    } else {
        DrawString("   ");
        DrawHexByte((0x3F - v) & 0x3F);
    }
    wVramCursor = 0x9929;
    DrawString(ReadCurInstrByte(2) ? "SAFE" : "FREE");
    wVramCursor = 0x994B;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedGlyph), v);
    wVramCursor = 0x996A;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x998C;
    DrawByte(ReadCurInstrByte(8));
    wVramCursor = 0x99C6;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawKitNameCell(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
}

void DrawKitDistName(UBYTE v);              /* bank 3 */

/*
 * The KIT instrument's fields: both kits' names, VOLUME (byte 1 bits 6-5,
 * shown 3..0 for $20/$40/$60/0), OUTPUT, LOOP, SPEED (byte 2 bit 6: 0.5X or
 * 1X), the DIST mode, FINETUNE, both OFFSETs and LENs (0 = ALL), the pitch
 * mode and the table.
 */
void DrawKitInstr(void)
{
    UBYTE v, flags;

    wVramCursor = 0x98A4;
    v = ReadCurInstrByte(2) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = 0x98C4;
    v = ReadCurInstrByte(9) & 0x3F;
    DrawKitNameCell(v);
    wVramCursor = 0x9909;
    v = ReadCurInstrByte(1);
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
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('/');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawFieldVal(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x99A6;
    DrawKitLoopField();
    wVramCursor = 0x99C9;
    if (0x40 & ReadCurInstrByte(2))         /* literal first: tested in b */
        DrawString("0.5");
    else
        DrawString("  1");
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('X');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    wVramCursor = 0x99E8;
    v = ReadCurInstrByte(0x0A);
    FarCallArg(3, FAR_FN(DrawKitDistName), v);
    wVramCursor = 0x994B;
    DrawHexByte(ReadCurInstrByte(8));
    wVramCursor = 0x9968;
    v = ReadCurInstrByte(0x0C);
    DrawHexByte(v);
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('/');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    v = ReadCurInstrByte(0x0D);
    DrawHexByte(v);
    wVramCursor = 0x9986;
    v = ReadCurInstrByte(3);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('/');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    v = ReadCurInstrByte(0x0B);
    if (v) {
        DrawStringTile20();
        DrawHexByte(v);
    } else
        DrawString("ALL");
    wVramCursor = 0x9927;
    flags = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), flags);
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}

void DrawWaveInstr(void)
{
    UBYTE v;

    wVramCursor = 0x98A9;
    v = ReadCurInstrByte(1);
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
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('/');
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    DrawFieldVal(0x0C, ReadCurInstrByte(7));
    wVramCursor = 0x98C7;
    v = ReadCurInstrByte(5);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
    wVramCursor = 0x98EA;
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
    wVramCursor = 0x992C;
    DrawByte(ReadCurInstrByte(8));
    FarCall(3, DrawWaveFinetune);
    FarCall(3, DrawWaveSynthField);
    FarCall(3, DrawWaveLen);
    FarCall(3, DrawWaveLoopPos);
    FarCall(3, DrawWaveSpeed);
    wVramCursor = 0x9965;
    v = ReadCurInstrByte(9);
    DrawString(PlayModeNames + (v < 5 ? v : 5) * 9);
    wVramCursor = 0x9A26;
    DrawInstrTableMode();
    wVramCursor++;
    DrawInstrTableField();
}
