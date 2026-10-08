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
extern UBYTE wDefaultNotes;                 /* $C904 */
extern UBYTE wAHeld;                        /* $C40C */
extern UBYTE wNameEditIsInstr;              /* $C430 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wInstrCursorCol;               /* $C428 */
extern UBYTE wTable;                        /* $C41B */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wDrawStep;                     /* $C405 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern char PlayModeNames[];                /* $00:$0839, [4][9] */
void DrawPulseDuty(UBYTE duty);             /* bank 0 */
void DrawFieldValue(UBYTE kind, UBYTE v);   /* bank 0 */
void OpenNameEditor(void);                  /* $1B:$4E8A */
void DrawInstrFieldHelp(void);              /* $05:$69BD */
void PasteInstrument(void);                 /* $01:$5503 */
void CopyInstrument(void);                  /* $01:$5253 */
void WordScreenInput(void);                 /* $1F:$724D */
void MapMoveLeft(void);                     /* $05:$4233 */
void MapMoveRight(void);                    /* $05:$4253 */
void MapMoveUp(void);                       /* $05:$426A */
void MapMoveDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);              /* $02:$78C4 */
void TogglePlayback(void);                  /* $02:$7C4C */

void WriteCurrentInstrByte(UBYTE field, UBYTE val)
{
    WriteInstrByte(wInstrument, field, val);
}

UBYTE GetWavePlayMode(void)
{
    return ReadCurInstrByte(9);
}

UBYTE WavePlayIsManual(void)
{
    return GetWavePlayMode() == 0;
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
    if (type == 1 && WavePlayIsManual() && wInstrField == 9) {
        ColorField(0x0B, 0x0C, 2, hGameboyType ? 3 : 2);
        return;
    }
    ColorField(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField],
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

void InstrSynthNew(void)
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
    if (type == 1 && WavePlayIsManual() && wInstrField == 9) {
        ColorField(0x0B, 0x0C, 2, 1);
        return;
    }
    x = InstrFieldX[type][wInstrField];
    r = InstrFieldW[type][wInstrField] + x;
    y = InstrFieldY[type][wInstrField];
    ColorField(x, y, r - x, 1);
}

void InstrCmdRateInc(void)
{
    UBYTE v;

    v = ReadCurInstrByte(8);
    if (v < 0x0F) {
        v++;
        WriteCurrentInstrByte(8, v);
        DrawByte(v);
    }
}

void InstrCmdRateDec(void)
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

void InstrTablePrev(void)
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

void DrawWaveLength(void)
{
    wVramCursor = (UBYTE *)0x99CC;
    if (WavePlayIsManual())
        DrawStringIndexed('-');
    else
        DrawByte(GetWaveLength());
}

void DrawWaveSpeed(void)
{
    wVramCursor = (UBYTE *)0x99AB;
    if (WavePlayIsManual())
        DrawString("--");                   /* the literal lands right after the function ($49E4) */
    else
        DrawHexByte(GetWaveSpeed() + 1);
}

UBYTE GetWaveLoopPos(void)
{
    return 0x0F ^ (ReadCurInstrByte(2) & 0x0F);
}

void DrawWaveLoopPos(void)
{
    UBYTE v, d;

    wVramCursor = (UBYTE *)0x99EC;
    v = GetWavePlayMode();
    if (v == 2 || v == 3 || v == 4) {
        d = GetWaveLength() - GetWaveLoopPos();
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

UBYTE GetWaveSpeed(void)
{
    return ReadCurInstrByte(11) + 3;
}

void SetInstrByte11(UBYTE v)
{
    WriteCurrentInstrByte(11, v - 3);
}

UBYTE GetWaveLength(void)
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

UBYTE GetVibratoShape(void)
{
    return ReadCurInstrByte(5) & 0x07;
}

void VibratoShapeNext(void)
{
    UBYTE v;

    v = GetVibratoShape();
    if (v < 5) {
        v += 2;
        if (v == 6)
            v = 1;
        SetVibratoShape(v);
    }
}

void VibratoShapePrev(void)
{
    UBYTE v;

    v = GetVibratoShape();
    if (v) {
        v -= 2;
        if (v == 0xFF)
            v = 4;
        SetVibratoShape(v);
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

void DrawWaveFinetune(void)
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
    if (WavePlayIsManual()) {
        DrawString("WAVE ");
        wVramCursor = (UBYTE *)0x998B;
        ColorField(0x0B, 0x0C, 1, 1);
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawHexByte(ReadCurInstrByte(3));
    } else {
        DrawString("SYNTH");
        wVramCursor = (UBYTE *)0x998B;
        ColorField(0x0B, 0x0C, 1, 0);
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

void InstrLengthInc(void)
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

void InstrLengthDec(void)
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

void InstrLengthBigInc(void)
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

void InstrLengthBigDec(void)
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

void CloneSynthOrTable(void)
{
    UBYTE t;

    t = ReadCurInstrByte(0) & 3;
    if (t == 1 && wInstrField == 9 && !WavePlayIsManual())
        CloneSynth();
    else
        FarCall(1, FAR_FN(CloneInstrTable));
    wInstrEditLatch = 0;
}

/* $541F: the instrument screen's input, a switch on wButtons (a speech
   instrument hands it to WordScreenInput): the directions move the cursor
   over the type's fields, skipping those its envelope or type leaves out;
   A+RIGHT/LEFT step the field by 1 and A+UP/DOWN by 16 (a big step), per
   instrument type (pulse, wave, kit, noise) -- on TYPE they change the
   type, on the kits the sample (16 at a time); A on the name opens the name
   editor, a second A on a field shows its help or opens a new table, then
   turns the table on; B+RIGHT/LEFT select the next/previous used
   instrument, B+UP/DOWN page; select+directions move on the screen map
   (UP to a wave instrument's synth, RIGHT to the instrument's table);
   select+A pastes, select+A+B clones, select+B copies; start and
   select+start play. 1 when taken */

UBYTE InstrScreenInput(void)
{
    UBYTE v, type;

    if (wInstrument == 0x40)
        return FarCallByte(0x1F, FAR_FN(WordScreenInput));
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    type = ReadCurInstrByte(0);
    if (wAPressEnabled)
        wInstrEditLatch = 0;
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wButtons & 0x10)
        WriteSongByte(1, (UWORD)wInstrument + (UWORD)0xA040, 1);
    switch (wButtons) {
    case 0x04:
        if (!wInstrField)
            break;
        EraseInstrCursor();
        wInstrField--;
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
            case 3:
                wInstrField = 1;
                break;
            case 4:
                wInstrField -= 2;
                break;
            case 0x0E:
                wInstrField--;
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                wInstrField--;
                break;
            case 0x0D:
                wInstrField--;
            case 0x0C:
                switch (GetWavePlayMode()) {
                case 1:
                    wInstrField--;
                    break;
                case 0:
                    wInstrField -= 3;
                }
            }
            break;
        case 2:
            if (wInstrField == 0x10)
                wInstrField--;
            else
                switch (wInstrField + wInstrCursorCol - 1) {
                case 4:
                case 8:
                case 0x0A:
                case 0x0C:
                    wInstrField--;
                }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
            case 3:
                wInstrField = 1;
                break;
            case 4:
                wInstrField -= 2;
                break;
            case 0x0B:
                wInstrField--;
            }
        }
        DrawInstrCursor();
        return 1;
    case 0x08:
        EraseInstrCursor();
        if (InstrFieldCounts[type] - wInstrField - 1) {
            if (type == 1)
                switch (wInstrField) {
                case 1:
                case 2:
                case 0x0C:
                    wInstrField++;
                    break;
                case 0x0B:
                    if (GetWavePlayMode() == 0 || GetWavePlayMode() == 1)
                        wInstrField += 2;
                    break;
                case 9:
                    if (WavePlayIsManual())
                        wInstrField += 4;
                }
            wInstrField++;
            switch (type) {
            case 0:
                switch (wInstrField) {
                case 3:
                case 4:
                    wInstrField = 5;
                    break;
                case 0x0E:
                    wInstrField++;
                }
                break;
            case 2:
                switch (wInstrField) {
                case 5:
                case (int)9:
                case 0x0A:
                case (int)0x0B:
                case 0x0C:
                case (int)0x0D:
                case 0x10:
                    wInstrField++;
                    break;
                case 4:
                case 8:
                    if (wInstrCursorCol)
                        wInstrField++;
                }
                break;
            case 3:
                switch (wInstrField) {
                case 3:
                case 4:
                    wInstrField = 5;
                    break;
                case 0x0B:
                    wInstrField++;
                }
            }
        }
        DrawInstrCursor();
        return 1;
    case 0x02:
        EraseInstrCursor();
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 3:
            case 4:
            case 0x0F:
                wInstrField--;
            }
            break;
        case 1:
            switch (wInstrField) {
            case 3:
            case 0x0E:
                wInstrField--;
            }
            break;
        case 2:
            switch (wInstrField) {
            case 5:
            case (int)9:
            case (int)0x0B:
            case (int)0x0D:
            case 0x11:
                wInstrField--;
                wInstrCursorCol = 0;
            }
            break;
        case 3:
            switch (wInstrField) {
            case 3:
            case 4:
            case 0x0C:
                wInstrField--;
            }
        }
        DrawInstrCursor();
        return 1;
    case 0x01:
        EraseInstrCursor();
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
                if (FarCallByteU(0x1F, InstrEnvHasSecond))
                    wInstrField++;
                break;
            case 3:
                if (FarCallByteU(0x1F, InstrEnvHasThird))
                    wInstrField++;
                break;
            case 0x0E:
                wInstrField++;
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                wInstrField++;
                break;
            case 0x0D:
                wInstrField++;
            }
            break;
        case 2:
            switch (wInstrField) {
            case 4:
            case 8:
            case 0x0A:
            case 0x0C:
            case 0x10:
                wInstrField++;
                wInstrCursorCol = 1;
            }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
                if (FarCallByteU(0x1F, InstrEnvHasSecond))
                    wInstrField++;
                break;
            case 3:
                if (FarCallByteU(0x1F, InstrEnvHasThird))
                    wInstrField++;
                break;
            case 0x0B:
                wInstrField++;
            }
        }
        DrawInstrCursor();
        return 1;
    case 0x11:
        GetTilemapAddr(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField]);
        if (wInstrField == 1) {
            if (type < 3)
                SetInstrType(type + 1);
            return 1;
        }
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
                InstrFieldIncLo(1);
                break;
            case 3:
                InstrFieldIncLo(9);
                break;
            case 4:
                InstrFieldIncLo(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrByte(7, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 2;
                }
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                InstrLengthInc();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v < 0x7F)
                    ++v;
                WriteCurrentInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    VibratoShapeNext();
                break;
            case 0x0A:
                InstrTransposeOff();
                break;
            case 0x0D:
                InstrCmdRateInc();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                ++v;
                WriteCurrentInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) + 1;
                DrawHexByte(v);
                WriteCurrentInstrByte(0x0B, v);
                break;
            case 0x0E:
                InstrTableModeNext();
                break;
            case 0x0F:
                InstrTableNext();
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                v = ReadCurInstrByte(1) & 0x60;
                switch (v) {
                case 0:
                    v = 0x60;
                    DrawStringIndexed(0x31);
                    break;
                case 0x20:
                    DrawStringIndexed(0x33);
                    break;
                case 0x40:
                    v = 0x20;
                    DrawStringIndexed(0x33);
                    break;
                case 0x60:
                    v = 0x40;
                    DrawStringIndexed(0x32);
                }
                WriteCurrentInstrByte(1, v);
                break;
            case 3:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 4:
                VibratoShapeNext();
                break;
            case 9:
                v = ReadCurInstrByte(3);
                if (WavePlayIsManual()) {
                    if (v != 0xFF) {
                        wVramCursor--;
                        ++v;
                        DrawHexByte(v);
                    }
                } else if ((v & 0xF0) != 0xF0) {
                    v += 0x10;
                    DrawByte(v >> 4);
                }
                WriteCurrentInstrByte(3, v);
                break;
            case 8:
                v = ReadCurInstrByte(9);
                if (v != 4) {
                    if (v > 4)
                        v = 0;
                    else
                        ++v;
                    WriteCurrentInstrByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    WriteCurrentInstrByte(3, ReadCurInstrByte(3) & 0xF0);
                    hBgDigitPalette = wShadowDigitPal;
                    hBgPalette = wShadowPal;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPos();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLength();
                if (v != 0x0F) {
                    ++v;
                    SetInstrByte10(v);
                    DrawByte(v);
                    hBgDigitPalette = wShadowDigitPal;
                    hBgPalette = wShadowPal;
                    DrawWaveLoopPos();
                }
                break;
            case 0x0C:
                v = GetWaveLoopPos();
                if (v & 0x0F) {
                    --v;
                    SetInstrByte2(v);
                    DrawWaveLoopPos();
                }
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0xFE) {
                    ++v;
                    SetInstrByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 5:
                InstrTransposeOff();
                break;
            case 7:
                InstrCmdRateInc();
                break;
            case 6:
                v = ReadCurInstrByte(0x0C) + 1;
                if (v != 0x80) {
                    WriteCurrentInstrByte(0x0C, v);
                    DrawWaveFinetune();
                }
                break;
            case 0x0D:
                InstrTableModeNext();
                break;
            case 0x0E:
                InstrTableNext();
            }
            break;
        case 2:
            switch (wInstrField) {
            case 2:
            case 3:
                DrawKitNameField(KitSelectPrev());
                break;
            case 4:
                v = ReadCurInstrByte(1) & 0x60;
                switch (v) {
                case 0:
                    v = 0x60;
                    DrawStringIndexed(0x31);
                    break;
                case 0x20:
                    DrawStringIndexed(0x33);
                    break;
                case 0x40:
                    v = 0x20;
                    DrawStringIndexed(0x33);
                    break;
                case 0x60:
                    v = 0x40;
                    DrawStringIndexed(0x32);
                }
                WriteCurrentInstrByte(1, v);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                v = ReadCurInstrByte(8);
                ++v;
                DrawHexByte(v);
                WriteCurrentInstrByte(8, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(5);
                if (!(v & 64)) {
                    v |= 0x40;
                    DrawString(" ON");
                    WriteCurrentInstrByte(5, v);
                } else {
                    v = ReadCurInstrByte(2);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrByte(2, v);
                }
                break;
            case 0x0D:
                v = ReadCurInstrByte(5);
                if (!(v & 32)) {
                    v |= 0x20;
                    DrawString(" ON");
                    WriteCurrentInstrByte(5, v);
                } else {
                    v = ReadCurInstrByte(9);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrByte(9, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v < 0xFF)
                    ++v;
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case 0x0B:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0B : 3);
                if (v < 0xFF)
                    ++v;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurInstrByte(2) & 0xBF;
                WriteCurrentInstrByte(2, v);
                DrawString("  1");
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    ++v;
                    WriteCurrentInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                VibratoShapeNext();
                break;
            case 0x10:
                InstrTableModeNext();
                break;
            case 0x11:
                InstrTableNext();
            }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
                InstrFieldIncLo(1);
                break;
            case 3:
                InstrFieldIncLo(9);
                break;
            case 4:
                InstrFieldIncLo(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 6:
                InstrLengthInc();
                break;
            case 8:
                if (!wButtonsSame)
                    VibratoShapeNext();
                break;
            case 7:
                WriteCurrentInstrByte(2, 1);
                DrawString("SAFE");
                break;
            case 9:
                InstrTransposeOff();
                break;
            case 0x0A:
                InstrCmdRateInc();
                break;
            case 0x0B:
                InstrTableModeNext();
                break;
            case 0x0C:
                InstrTableNext();
            }
        }
        return 1;
    case 0x12:
        if (wInstrField == 1) {
            if (type)
                SetInstrType(type - 1);
            return 1;
        }
        GetTilemapAddr(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField]);
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
                InstrFieldDecLo(1);
                break;
            case 3:
                InstrFieldDecLo(9);
                break;
            case 4:
                InstrFieldDecLo(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrByte(7, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 1;
                }
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                InstrLengthDec();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v)
                    --v;
                WriteCurrentInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    VibratoShapePrev();
                break;
            case 0x0A:
                InstrTransposeOn();
                break;
            case 0x0D:
                InstrCmdRateDec();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                --v;
                WriteCurrentInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) - 1;
                DrawHexByte(v);
                WriteCurrentInstrByte(0x0B, v);
                break;
            case 0x0E:
                InstrTableModePrev();
                break;
            case 0x0F:
                InstrTablePrev();
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                v = ReadCurInstrByte(1) & 0x60;
                switch (v) {
                case 0:
                    DrawStringIndexed(0x30);
                    break;
                case 0x20:
                    v = 0x40;
                    DrawStringIndexed(0x32);
                    break;
                case 0x40:
                    v = 0x60;
                    DrawStringIndexed(0x31);
                    break;
                case 0x60:
                    v = 0;
                    DrawStringIndexed(0x30);
                }
                WriteCurrentInstrByte(1, v);
                break;
            case 3:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 4:
                VibratoShapePrev();
                break;
            case 9:
                v = ReadCurInstrByte(3);
                if (WavePlayIsManual()) {
                    if (v) {
                        wVramCursor--;
                        --v;
                        DrawHexByte(v);
                    }
                } else if (v & 0xF0) {
                    v -= 0x10;
                    DrawByte(v >> 4);
                }
                WriteCurrentInstrByte(3, v);
                break;
            case 8:
                v = ReadCurInstrByte(9);
                if (v) {
                    if (v > 4)
                        v = 0;
                    else
                        --v;
                    WriteCurrentInstrByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    hBgDigitPalette = wShadowDigitPal;
                    hBgPalette = wShadowPal;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPos();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLength();
                if (v & 0x0F) {
                    --v;
                    SetInstrByte10(v);
                    DrawByte(v);
                    if (v < GetWaveLoopPos())
                        SetInstrByte2(v);
                    hBgDigitPalette = wShadowDigitPal;
                    hBgPalette = wShadowPal;
                    DrawWaveLoopPos();
                }
                break;
            case 0x0C:
                v = GetWaveLoopPos();
                if (v < GetWaveLength()) {
                    ++v;
                    SetInstrByte2(v);
                    DrawWaveLoopPos();
                }
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v) {
                    --v;
                    SetInstrByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 5:
                InstrTransposeOn();
                break;
            case 7:
                InstrCmdRateDec();
                break;
            case 6:
                v = ReadCurInstrByte(0x0C) - 1;
                if (v != 0x7F) {
                    WriteCurrentInstrByte(0x0C, v);
                    DrawWaveFinetune();
                }
                break;
            case 0x0D:
                InstrTableModePrev();
                break;
            case 0x0E:
                InstrTablePrev();
            }
            break;
        case 2:
            switch (wInstrField) {
            case 2:
            case 3:
                DrawKitNameField(KitSelectNext());
                break;
            case 4:
                v = ReadCurInstrByte(1) & 0x60;
                switch (v) {
                case 0:
                    DrawStringIndexed(0x30);
                    break;
                case 0x20:
                    v = 0x40;
                    DrawStringIndexed(0x32);
                    break;
                case 0x40:
                    v = 0x60;
                    DrawStringIndexed(0x31);
                    break;
                case 0x60:
                    v = 0;
                    DrawStringIndexed(0x30);
                }
                WriteCurrentInstrByte(1, v);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                v = ReadCurInstrByte(8);
                --v;
                DrawHexByte(v);
                WriteCurrentInstrByte(8, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(2);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrByte(2, v);
                } else {
                    v = ReadCurInstrByte(5);
                    v = (int)(char)v & (int)0xBF;
                    DrawString("OFF");
                    WriteCurrentInstrByte(5, v);
                }
                break;
            case 0x0D:
                v = ReadCurInstrByte(9);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrByte(9, v);
                } else {
                    v = ReadCurInstrByte(5);
                    v = (int)(char)v & (int)0xDF;
                    DrawString("OFF");
                    WriteCurrentInstrByte(5, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v)
                    --v;
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case 0x0B:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0B : 3);
                if (v)
                    --v;
                if (v) {
                    DrawStringTile20();
                    DrawHexByte(v);
                } else
                    DrawString("ALL");
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurInstrByte(2) | 0x40;
                WriteCurrentInstrByte(2, v);
                DrawString("0.5");
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    --v;
                    WriteCurrentInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                VibratoShapePrev();
                break;
            case 0x10:
                InstrTableModePrev();
                break;
            case 0x11:
                InstrTablePrev();
            }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
                InstrFieldDecLo(1);
                break;
            case 3:
                InstrFieldDecLo(9);
                break;
            case 4:
                InstrFieldDecLo(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 6:
                InstrLengthDec();
                break;
            case 8:
                if (!wButtonsSame)
                    VibratoShapePrev();
                break;
            case 7:
                WriteCurrentInstrByte(2, 0);
                DrawString("FREE");
                break;
            case 9:
                InstrTransposeOn();
                break;
            case 0x0A:
                InstrCmdRateDec();
                break;
            case 0x0B:
                InstrTableModePrev();
                break;
            case 0x0C:
                InstrTablePrev();
            }
        }
        return 1;
    case 0x14:
        GetTilemapAddr(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField]);
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
                InstrFieldIncHi(1);
                break;
            case 3:
                InstrFieldIncHi(9);
                break;
            case 4:
                InstrFieldIncHi(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrByte(7, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7);
                v |= 3;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, 3);
                break;
            case 7:
                InstrLengthBigInc();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if ((v & 0x70) != 0x70)
                    v += 0x10;
                WriteCurrentInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                InstrPitchModeDown();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                v += 0x0C;
                DrawHexByte(v);
                WriteCurrentInstrByte(2, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) + 0x10;
                WriteCurrentInstrByte(0x0B, v);
                DrawHexByte(v);
                break;
            case 0x0E:
                InstrTableModeNext();
                wVramCursor -= 7;
                InstrTableModeNext();
                break;
            case 0x0F:
                InstrTableAdd16();
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                DrawStringIndexed(0x33);
                WriteCurrentInstrByte(1, 0x20);
                break;
            case 3:
                WriteCurrentInstrByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 4:
                InstrPitchModeDown();
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0xEF) {
                    v += 0x10;
                    SetInstrByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 9:
                if (WavePlayIsManual()) {
                    v = ReadCurInstrByte(3);
                    if (v < 0xF0) {
                        wVramCursor--;
                        v += 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrByte(3, v);
                    }
                }
                break;
            case 0x0B:
                v = 0x0F - GetWaveLength();
                SetInstrByte10(0x0F);
                DrawStringIndexed(0x46);
                v = GetWaveLoopPos() + v;
                if (v & 0xF0)
                    v = 0x0F;
                SetInstrByte2(v);
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
                DrawWaveLoopPos();
                break;
            case 0x0C:
                SetInstrByte2(0);
                DrawWaveLoopPos();
                break;
            case 6:
                v = ReadCurInstrByte(0x0C) + 0x10;
                if ((v & 0xF0) == 0x80)
                    v = 0x7F;
                WriteCurrentInstrByte(0x0C, v);
                DrawWaveFinetune();
                break;
            case 0x0D:
                InstrTableModeNext();
                wVramCursor -= 7;
                InstrTableModeNext();
                break;
            case 0x0E:
                InstrTableAdd16();
            }
            break;
        case 2:
            switch (wInstrField) {
            case 2:
            case 3: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = KitSelectPrev();
                DrawKitNameField(kit);
                break;
            }
            case 4:
                DrawStringIndexed(0x33);
                WriteCurrentInstrByte(1, 0x20);
                break;
            case 5:
                WriteCurrentInstrByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 7:
                v = ReadCurInstrByte(8) + 0x10;
                DrawHexByte(v);
                WriteCurrentInstrByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case (int)0x0B:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0B : 3);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    if (v > 0xC2 && v < 0xD3)
                        v = 0xD3;
                    else
                        v += 0x10;
                    WriteCurrentInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                InstrPitchModeDown();
                break;
            case 0x10:
                InstrTableModeNext();
                wVramCursor -= 7;
                InstrTableModeNext();
                break;
            case 0x11:
                InstrTableAdd16();
            }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
                InstrFieldIncHi(1);
                break;
            case 3:
                InstrFieldIncHi(9);
                break;
            case 4:
                InstrFieldIncHi(0x0A);
                break;
            case 5:
                WriteCurrentInstrByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 6:
                InstrLengthBigInc();
                break;
            case 0x0B:
                InstrTableModeNext();
                wVramCursor -= 7;
                InstrTableModeNext();
                break;
            case 0x0C:
                InstrTableAdd16();
            }
        }
        return 1;
    case 0x18:
        GetTilemapAddr(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField]);
        switch (type) {
        case 0:
            switch (wInstrField) {
            case 2:
                InstrFieldDecHi(1);
                break;
            case 3:
                InstrFieldDecHi(9);
                break;
            case 4:
                InstrFieldDecHi(0x0A);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrByte(7, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7) & 0xFC;
                WriteCurrentInstrByte(7, v);
                DrawFieldValue(0x0C, 0);
                break;
            case 7:
                InstrLengthBigDec();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v & 0xF0)
                    v -= 0x10;
                WriteCurrentInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                InstrPitchModeUp();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                v -= 0x0C;
                WriteCurrentInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrByte(0x0B, v);
                break;
            case 0x0E:
                InstrTableModePrev();
                wVramCursor -= 7;
                InstrTableModePrev();
                break;
            case 0x0F:
                InstrTableSub16();
            }
            break;
        case 1:
            switch (wInstrField) {
            case 2:
                DrawStringIndexed(0x30);
                WriteCurrentInstrByte(1, 0);
                break;
            case 3:
                WriteCurrentInstrByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 4:
                InstrPitchModeUp();
                break;
            case 0x0B:
                SetInstrByte10(0);
                DrawStringIndexed(0x30);
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
            case 0x0C:
                SetInstrByte2(GetWaveLength());
                DrawWaveLoopPos();
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0x10)
                    v = 0;
                else
                    v -= 0x10;
                SetInstrByte11(v);
                DrawHexByte(v + 1);
                break;
            case 9:
                if (WavePlayIsManual()) {
                    v = ReadCurInstrByte(3);
                    if (v >= 0x10) {
                        wVramCursor--;
                        v -= 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrByte(3, v);
                    }
                }
                break;
            case 6:
                v = ReadCurInstrByte(0x0C) - 0x10;
                if ((v & 0xF0) == 0x70)
                    v = 0x80;
                WriteCurrentInstrByte(0x0C, v);
                DrawWaveFinetune();
                break;
            case 0x0D:
                InstrTableModePrev();
                wVramCursor -= 7;
                InstrTableModePrev();
                break;
            case 0x0E:
                InstrTableSub16();
            }
            break;
        case 2:
            switch (wInstrField) {
            case 2:
            case 3: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = KitSelectNext();
                DrawKitNameField(kit);
                break;
            }
            case 4:
                DrawStringIndexed(0x30);
                WriteCurrentInstrByte(1, 0);
                break;
            case 5:
                WriteCurrentInstrByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 7:
                v = ReadCurInstrByte(8) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                DrawHexByte(v);
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case (int)0x0B:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0B : 3);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                if (v) {
                    DrawStringTile20();
                    DrawHexByte(v);
                } else
                    DrawString("ALL");
                WriteCurrentInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    if (v > 0xD0 && v < 0xE0)
                        v = 0xD0;
                    else
                        v -= 0x10;
                    WriteCurrentInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                InstrPitchModeUp();
                break;
            case 0x10:
                InstrTableModePrev();
                wVramCursor -= 7;
                InstrTableModePrev();
                break;
            case 0x11:
                InstrTableSub16();
            }
            break;
        case 3:
            switch (wInstrField) {
            case 2:
                InstrFieldDecHi(1);
                break;
            case 3:
                InstrFieldDecHi(9);
                break;
            case 4:
                InstrFieldDecHi(0x0A);
                break;
            case 5:
                WriteCurrentInstrByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 6:
                InstrLengthBigDec();
                break;
            case 0x0B:
                InstrTableModePrev();
                wVramCursor -= 7;
                InstrTableModePrev();
                break;
            case 0x0C:
                InstrTableSub16();
            }
        }
        return 1;
    case 0x10:
        if (wAPressEnabled) {
            if (!wInstrField) {
                wNameEditIsInstr = 1;
                FarCall(0x1B, FAR_FN(OpenNameEditor));
            } else if (wDrawStep++ == 1) {
                if (InstrCheckFieldOption(type)) {
                    GetTilemapAddr(InstrFieldX[type][wInstrField], InstrFieldY[type][wInstrField]);
                    InstrTableNew();
                } else if (type == 1 && wInstrField == 9 && !WavePlayIsManual()) {
                    InstrSynthNew();
                    return 1;
                }
                FarCallArg(5, DrawInstrFieldHelp, type);
            } else if (wDrawStep == 1 && InstrCheckFieldOption(type) && !IsInstrTableOn()) {
                InstrTableOn();
                GetTilemapAddr(InstrFieldX[type][wInstrField - 1], InstrFieldY[type][wInstrField - 1]);
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
                FarCall(0x1F, DrawInstrTableMode);
                wVramCursor++;
                hBgDigitPalette = wSelectDigitPal;
                hBgPalette = wSelectPal;
                FarCall(0x1F, DrawInstrTableField);
            }
        }
        return 1;
    case 0x21:
        InstrSelectNextUsed();
        return 1;
    case 0x22:
        InstrSelectPrevUsed();
        return 1;
    case 0x24:
        wInstrument += 0x0F;
        if (wInstrument > 0x40)
            wInstrument = 0x40;
        InstrSelectNextUsed();
        return 1;
    case 0x28:
        InstrSelectPrevPage();
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        if (type == 1) {
            wSynth = ReadCurInstrByte(3);
            wSynthScreen = wSynth >> 4;
        }
        FarCall(5, MapMoveUp);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveDown);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x41:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveRight);
        v = ReadCurInstrByte(6);
        if (v & 0x20)
            wTable = v & 0x1F;
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x50:
        if (!wSelectHeld)
            return 0;
        if (wInstrEditLatch)
            CloneSynthOrTable();
        else
            FarCall(1, FAR_FN(PasteInstrument));
        DrawInstrumentScreen();
        wSelectHeld = 0;
        return 1;
    case 0x70:
        wSelectHeld = 0;
        if (wInstrEditLatch) {
            CloneSynthOrTable();
            DrawInstrumentScreen();
        }
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    case 0x60:
        if (!wSelectHeld)
            return 1;
        wSelectHeld = 0;
        FarCall(1, FAR_FN(CopyInstrument));
        wInstrEditLatch = 1;
        return 1;
    }
    return 0;
}
