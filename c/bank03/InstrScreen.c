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

#include "lsdj.h"

extern UBYTE DefaultWaveFrameB3[];          /* $03:$4015, 16 bytes */
extern UBYTE InstrFieldXKit[];              /* $03:$4046 */
extern UBYTE InstrFieldYWave[];             /* $03:$4089 */
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

void SetInstrPitchByte(UBYTE v)
{
    WriteCurrentInstrByte(5, v);
    FarCallArg(1, (UWORD)DrawOnOffName, v);
}

void InstrTransposeOn(void)
{
    SetInstrPitchByte(ReadCurInstrByte(5) | 0x20);
}

void InstrTransposeOff(void)
{
    SetInstrPitchByte(ReadCurInstrByte(5) & 0xDF);
}

UBYTE InstrReadPhraseNote(UBYTE synth)
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

void SetInstrLen(void)
{
    UBYTE v;

    v = ShowOutOfSynths();
    if (v == 0xFF)
        return;
    GetTilemapAddr(InstrFieldXKit[0], InstrFieldYWave[0]);
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    DrawByte(v);
    WriteCurrentInstrByte(3, v << 4);
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
        FarCall(0x1F, FAR_FN(GetInstrTable1F2));
    }
}

void SetInstrTable5(void)
{
    UBYTE v;

    v = ReadCurInstrByte(6);
    if (v & 0x20) {
        if ((v & 0x1F) == 0) {
            if (!wButtonsSame) {
                SetInstrFlags52();
                SetInstrFlags52();
                GetInstrType2();
                wInstrField--;
                GetInstrType();
            }
        } else {
            v--;
            WriteCurrentInstrByte(6, v);
            FarCall(0x1F, FAR_FN(GetInstrTable1F2));
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
            FarCall(0x1F, FAR_FN(GetInstrTable1F2));
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
        FarCall(0x1F, FAR_FN(GetInstrTable1F2));
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

void InstrFieldIncLo(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurInstrByte(field);
    t = v & 0x0F;
    if (t == 0x0F)
        return;
    v++;
    WriteCurrentInstrByte(field, v);
    FarCall(0x1F, GetInstrEnv1F2);
    GetInstrType();
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
    FarCall(0x1F, GetInstrEnv1F2);
    GetInstrType();
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
    FarCall(0x1F, GetInstrEnv1F2);
    GetInstrType();
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
    FarCall(0x1F, GetInstrEnv1F2);
    GetInstrType();
    InstrEnvelopeChanged();
}
