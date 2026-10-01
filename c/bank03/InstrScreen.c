/*
 * The instrument screen -- LSDj 9.4.0, ROM bank $03 (its field accessors,
 * cursor steppers and small draw helpers, in ROM order).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The screen edits the current instrument (wInstrument), a 16-byte record
 * of the working song read and written through ReadCurInstrByte /
 * WriteCurrentInstrByte (bank 0, c/bank00/InstrByte.c); constants/song.inc
 * has the per-type field layout (byte 5 = pitch flags, bit 5 transpose;
 * byte 6 = table on (bit 5) + table number (bits 4-0)).
 */

#define SONG_BYTE_PTR               /* this bank's ReadSongByte takes a pointer (CloneSynth) */
#include "lsdj.h"

extern UBYTE DefaultWaveFrameB3[];          /* $03:$4015, 16 bytes */
extern char TxtUnlim[];                     /* $03:$4025 "UNLIM" */
extern UBYTE InstrFieldX[][18];             /* $03:$402B, [type][field] cursor X */
extern UBYTE InstrFieldY[][18];             /* $03:$406E */
extern UBYTE InstrFieldW[][18];             /* $03:$40B1 */
extern UBYTE InstrFieldCounts[];            /* $03:$40F4, [4] fields per instrument type */

void WriteCurrentInstrByte(UBYTE field, UBYTE val)
{
    WriteInstrByte(wInstrument, field, val);
}

UBYTE GetInstrByte9(void)
{
    return ReadCurInstrByte(9);
}

UBYTE InstrByte9IsZero(void)
{
    return GetInstrByte9() == 0;
}

UBYTE IsInstrTableOn(void)
{
    UBYTE r;

    if (ReadCurInstrByte(6) & 0x20)
        r = 1;
    else
        r = 0;
    return r;
}

void InstrTableOn(void)
{
    WriteCurrentInstrByte(6, ReadCurInstrByte(6) | 0x20);
}

UBYTE InstrCheckFieldOption(UBYTE type)
{
    return wInstrField == InstrFieldCounts[type] - 1;
}

void DrawInstrCursor(void)
{
    UBYTE type;

    type = ReadCurInstrByte(0) & 3;
    if (type == 1 && InstrByte9IsZero() && wInstrField == 9) {
        DrawTextField(0x0B, 0x0C, 2, hGameboyType ? 3 : 2);
        return;
    }
    DrawTextField(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField],
                  InstrFieldW[type][wInstrField], hGameboyType ? 3 : 2);
}

void SetInstrPitchByte(UBYTE v)
{
    WriteCurrentInstrByte(5, v);
    FarCallArg(1, FAR_FN(DrawOnOffName), v);
}

void InstrTransposeOn(void)
{
    SetInstrPitchByte(ReadCurInstrByte(5) | 0x20);
}

void InstrTransposeOff(void)
{
    SetInstrPitchByte(ReadCurInstrByte(5) & 0xDF);
}

UBYTE SynthWavesModified(UBYTE synth)
{
    UBYTE *p;

    p = SramWaveFrames + ((UWORD)synth << 8);
    do {
        if (ReadSongByte(3, p) != DefaultWaveFrameB3[(UBYTE)p & 0x0F])
            return 1;
        p++;
    } while ((UBYTE)p != 0);
    return 0;
}

UBYTE FindFreeSynth(void)
{
    UBYTE i;

    for (i = 0; i < 16; i++) {
        if (!SynthWavesModified(i))
            return i;
    }
    strcpy(wStrBuffer, "OUT OF SYNTHS! CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

void InstrTableNew(void)
{
    UBYTE t, v;

    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    t |= 0x20;
    v = ReadCurInstrByte(6);
    WriteCurrentInstrByte(6, t | (v & 0xD0));
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableField));
}

void SetInstrLen(void)
{
    UBYTE v;

    v = FindFreeSynth();
    if (v == 0xFF)
        return;
    GetTilemapAddr(InstrFieldX[1][9], InstrFieldY[1][9]);   /* WAVE: the synth field */
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    DrawByte(v);
    WriteCurrentInstrByte(3, v << 4);
}

void CloneSynth(void)
{
    UBYTE s, d, i;
    UWORD ps, pd;
    UBYTE v, w;

    s = ReadCurInstrByte(3) >> 4;
    d = FindFreeSynth();
    if (d == 0xFF)
        return;
    for (i = 0; i < 0x10; i++) {
        v = ReadSongByte(1, SramSynthParams + (UWORD)(s << 4) + (UWORD)i);
        WriteSongByte(1, (UWORD)(d << 4) + (UWORD)0xBEB2 + (UWORD)i, v);
    }
    i = 0;
    ps = s;
    ps <<= 8;
    ps |= 0xA000;
    pd = d;
    pd <<= 8;
    pd |= 0xA000;
    do {
        w = ReadSongByte(3, (UBYTE *)ps++);
        WriteSongByte(3, pd++, w);
    } while (++i);
    WriteCurrentInstrByte(3, (ReadCurInstrByte(3) & 0x0F) | (d << 4));
    ShowMessage("CLONED SYNTH");
}

void InstrScreenFrame(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    FarCall(5, MapDrawTopRow);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    ClearMarkerSprites();
}

/* Step back to the previous allocated instrument (stopping at 0). */
void InstrSelectPrevUsed(void)
{
    wMsgStrPtr = 0;
    if (wInstrument) {
        while (wInstrument) {
            wInstrument--;
            if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstrument))
                break;
        }
        FarCall(1, FAR_FN(UiDrawTextHw));
        DrawInstrumentScreen();
    }
}

/* Page back 16 instruments, then to the previous allocated one. */
void InstrSelectPrevPage(void)
{
    if (wInstrument) {
        wInstrument -= 0x10;
        if (wInstrument > 0x40)
            wInstrument = 0;
        wInstrument++;
        InstrSelectPrevUsed();
    }
}

void InstrSelectNextUsed(void)
{
    wMsgStrPtr = 0;
    while (wInstrument != 0x40) {
        wInstrument++;
        if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstrument))
            break;
    }
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawInstrumentScreen();
}

void EraseInstrCursor(void)
{
    UBYTE type, x, y, r;

    type = ReadCurInstrByte(0) & 3;
    if (type == 1 && InstrByte9IsZero() && wInstrField == 9) {
        DrawTextField(0x0B, 0x0C, 2, 1);
        return;
    }
    x = InstrFieldX[type][wInstrField];
    r = InstrFieldW[type][wInstrField] + x;
    y = InstrFieldY[type][wInstrField];
    DrawTextField(x, y, r - x, 1);
}

void SetInstrByte8(void)
{
    UBYTE v;

    v = ReadCurInstrByte(8);
    if (v < 0x0F) {
        v++;
        WriteCurrentInstrByte(8, v);
        DrawByte(v);
    }
}

void SetInstrByte82(void)
{
    UBYTE v;

    v = ReadCurInstrByte(8);
    if (v) {
        v--;
        WriteCurrentInstrByte(8, v);
        DrawByte(v);
    }
}

UBYTE GetInstrByte5Flag8(void)
{
    return ReadCurInstrByte(5) & 0x08;
}

void InstrTableModePrev(void)
{
    if (wButtonsSame)
        return;
    if (ReadCurInstrByte(5) & 0x08)
        WriteCurrentInstrByte(5, ReadCurInstrByte(5) & 0xF7);
    else
        WriteCurrentInstrByte(6, ReadCurInstrByte(6) & 0xDF);
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableMode));
    wVramCursor++;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableField));
}

void InstrTableModeNext(void)
{
    if (wButtonsSame)
        return;
    if (IsInstrTableOn())
        WriteCurrentInstrByte(5, ReadCurInstrByte(5) | 0x08);
    else
        InstrTableOn();
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableMode));
    wVramCursor++;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableField));
}

void InstrTableNext(void)
{
    UBYTE v, t;

    v = ReadCurInstrByte(6);
    if (v & 0x20) {
        t = v & 0x1F;
        if (t == 0x1F)
            return;
        v++;
        WriteCurrentInstrByte(6, v);
        FarCall(0x1F, FAR_FN(DrawInstrTableField));
    }
}

void SetInstrTable5(void)
{
    UBYTE v;

    v = ReadCurInstrByte(6);
    if (v & 0x20) {
        if ((v & 0x1F) == 0) {
            if (!wButtonsSame) {
                InstrTableModePrev();
                InstrTableModePrev();
                EraseInstrCursor();
                wInstrField--;
                DrawInstrCursor();
            }
        } else {
            v--;
            WriteCurrentInstrByte(6, v);
            FarCall(0x1F, FAR_FN(DrawInstrTableField));
        }
    }
}

void InstrTableAdd16(void)
{
    UBYTE v;

    v = ReadCurInstrByte(6);
    if (v & 0x20) {
        if (!wButtonsSame) {
            v |= 0x10;
            WriteCurrentInstrByte(6, v);
            FarCall(0x1F, FAR_FN(DrawInstrTableField));
        }
    }
}

void InstrTableSub16(void)
{
    UBYTE v;

    v = ReadCurInstrByte(6);
    if (v & 0x20) {
        v &= 0xEF;
        WriteCurrentInstrByte(6, v);
        FarCall(0x1F, FAR_FN(DrawInstrTableField));
    }
}

void DrawInstrByte10(void)
{
    wVramCursor = (UBYTE *)0x99CC;
    if (InstrByte9IsZero())
        DrawStringIndexed('-');
    else
        DrawByte(GetInstrByte10());
}

void DrawInstrByte11(void)
{
    wVramCursor = (UBYTE *)0x99AB;
    if (InstrByte9IsZero())
        DrawString("--");                   /* the literal lands right after the function ($49E4) */
    else
        DrawHexByte(GetInstrByte11() + 1);
}

UBYTE GetInstrByte2(void)
{
    return 0x0F ^ (ReadCurInstrByte(2) & 0x0F);
}

void DrawInstrRow14(void)
{
    UBYTE v, d;

    wVramCursor = (UBYTE *)0x99EC;
    v = GetInstrByte9();
    if (v == 2 || v == 3 || v == 4) {
        d = GetInstrByte10() - GetInstrByte2();
        if (d & 0xF0)
            d = 0;
        DrawByte(d);
    } else
        DrawStringIndexed('-');
}

void DrawKitDistName(UBYTE v)
{
    switch (v) {
    case 0xD0:
        DrawString(" HARD");
        break;
    case 0xD1:
        DrawString(" SOFT");
        break;
    case 0xD2:
        DrawString(" FOLD");
        break;
    case 0xD3:
        DrawString(" WRAP");
        break;
    default:
        DrawStringIndexed('?');
        DrawHexByte(v);
        DrawString("00");
    }
}

UBYTE GetInstrByte11(void)
{
    return ReadCurInstrByte(11) + 3;
}

void SetInstrByte11(UBYTE v)
{
    WriteCurrentInstrByte(11, v - 3);
}

UBYTE GetInstrByte10(void)
{
    return 0x0F - (ReadCurInstrByte(10) & 0x0F);
}

void SetInstrByte10(UBYTE v)
{
    if (v >= 0x10)
        DebugTrap();
    WriteCurrentInstrByte(10, 0x0F - v);
}

void SetInstrByte2(UBYTE v)
{
    WriteCurrentInstrByte(2, 0x0F ^ ((ReadCurInstrByte(2) & 0xF0) | v));
}

UBYTE GetInstrFlags53(void)
{
    return ReadCurInstrByte(5) & 0x07;
}

void InstrField5Next(void)
{
    UBYTE v;

    v = GetInstrFlags53();
    if (v < 5) {
        v += 2;
        if (v == 6)
            v = 1;
        SetInstrFlags54(v);
    }
}

void InstrField5Prev(void)
{
    UBYTE v;

    v = GetInstrFlags53();
    if (v) {
        v -= 2;
        if (v == 0xFF)
            v = 4;
        SetInstrFlags54(v);
    }
}

UBYTE GetInstrType3(void)
{
    return ReadCurInstrByte(0) - 2;
}

void InstrPitchModeUp(void)
{
    UBYTE v;

    v = ReadCurInstrByte(5);
    if (v & 0x80)
        return;
    if (v & 0x10)
        v ^= 0x90;
    else if ((v & 0x40) && GetInstrType3())
        v ^= 0x40;
    else
        v |= 0x10;
    WriteCurrentInstrByte(5, v);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
}

void InstrPitchModeDown(void)
{
    UBYTE v;

    v = ReadCurInstrByte(5);
    if (v & 0x80)
        v ^= 0x90;
    else if (v & 0x10) {
        if (GetInstrType3())
            v &= 0xAF;
        else
            v ^= 0x10;
    } else if (GetInstrType3())
        v |= 0x40;
    WriteCurrentInstrByte(5, v);
    FarCallArg(1, FAR_FN(DrawSpeedModeName), v);
}

void DrawInstrByte12(void)
{
    UBYTE v;

    v = ReadCurInstrByte(12);
    wVramCursor = (UBYTE *)0x990A;
    if (v & 0x80) {
        DrawStringIndexed('-');
        DrawHexByte(-v);
    } else {
        DrawStringTile20();
        DrawHexByte(v);
    }
}

void DrawWaveSynthField(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9980;
    if (InstrByte9IsZero()) {
        DrawString("WAVE ");
        wVramCursor = (UBYTE *)0x998B;
        DrawTextField(0x0B, 0x0C, 1, 1);
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawHexByte(ReadCurInstrByte(3));
    } else {
        DrawString("SYNTH");
        wVramCursor = (UBYTE *)0x998B;
        DrawTextField(0x0B, 0x0C, 1, 0);
        DrawStringIndexed(' ');
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawByte(ReadCurInstrByte(3) >> 4);
    }
}

void InstrEnvelopeChanged(void)
{
    FarCall(0x1B, UpdateInstrEnvelope);
}

void DrawKitNameField(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
}

void WriteKitNumber(UBYTE field, UBYTE kit)
{
    WORD t;

    t = (BYTE)ReadCurInstrByte(field) & (WORD)0xC0;
    WriteCurrentInstrByte(field, t | kit);
}

UBYTE KitSelectNext(void)
{
    UBYTE field, kit, orig;

    field = (wInstrField == 2) ? 2 : 9;
    kit = ReadCurInstrByte(field) & 0x3F;
    orig = kit;
    do {
        if (!kit)
            return orig;
        kit--;
    } while (!KitExists(kit));
    WriteKitNumber(field, kit);
    return kit;
}

UBYTE KitSelectPrev(void)
{
    UBYTE field, kit, orig;

    field = (wInstrField == 2) ? 2 : 9;
    kit = ReadCurInstrByte(field) & 0x3F;
    orig = kit;
    do {
        if (kit == 0x3F)
            return orig;
        kit++;
    } while (!KitExists(kit));
    WriteKitNumber(field, kit);
    return kit;
}

void KitSelectEnsure(void)
{
    UBYTE i, field;

    for (i = 0; i < 2; i++) {
        field = i ? 2 : 9;
        if (!KitExists(ReadCurInstrByte(field) & 0x3F))
            WriteKitNumber(field, 0);
    }
}

void SetInstrType(UBYTE type)
{
    UBYTE t, n;

    if (wButtonsSame)
        return;
    t = type;
    n = InstrFieldCounts[t];
    if (wInstrField >= n)
        wInstrField = n - 1;
    WriteCurrentInstrByte(0, t);
    FarCall(1, FAR_FN(UiDrawTextHw));
    if (t == 2)
        KitSelectEnsure();
    DrawInstrumentScreen();
}

void InstrFieldIncLo(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurInstrByte(field);
    t = v & 0x0F;
    if (t == 0x0F)
        return;
    v++;
    WriteCurrentInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvelopeChanged();
}

void InstrFieldDecLo(UBYTE field)
{
    UBYTE v;

    v = ReadCurInstrByte(field);
    if ((v & 0x0F) == 0)
        return;
    v--;
    WriteCurrentInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvelopeChanged();
}

void InstrFieldIncHi(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurInstrByte(field);
    t = v & 0xF0;
    if (t == 0xF0)
        return;
    v += 0x10;
    WriteCurrentInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvelopeChanged();
}

void InstrFieldDecHi(UBYTE field)
{
    UBYTE v;

    v = ReadCurInstrByte(field);
    if ((v & 0xF0) == 0)
        return;
    v -= 0x10;
    WriteCurrentInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvelopeChanged();
}

void SetInstrLen4(void)
{
    UBYTE v;

    v = ReadCurInstrByte(3);
    if (v & 0x40) {
        v--;
        if (v == 0x3F)
            v = 0;
    }
    WriteCurrentInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void SetInstrLen5(void)
{
    UBYTE v, t;

    v = ReadCurInstrByte(3);
    if (!(v & 0x40)) {
        v &= 0x80;
        v += 0x40;
    } else {
        t = v & 0x3F;
        if (t != 0x3F)
            v++;
    }
    WriteCurrentInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void SetInstrLen6(void)
{
    UBYTE v, t;

    v = ReadCurInstrByte(3);
    if (v & 0x40) {
        t = v & 0x3F;
        if (t > 0x0F)
            v -= 0x10;
        else
            v = 0;
    }
    WriteCurrentInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void SetInstrLen7(void)
{
    UBYTE v, t;

    v = ReadCurInstrByte(3);
    if (!(v & 0x40))
        v = 0x4F;
    else {
        t = v & 0x3F;
        if (t < 0x30)
            v += 0x10;
        else
            v |= 0x3F;
    }
    WriteCurrentInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void GetInstrType4(void)
{
    UBYTE t;

    t = ReadCurInstrByte(0) & 3;
    if (t == 1 && wInstrField == 9 && !InstrByte9IsZero())
        CloneSynth();
    else
        FarCall(1, FAR_FN(CloneInstrTable));
    wInstrEditLatch = 0;
}
