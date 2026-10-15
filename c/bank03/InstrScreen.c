/*
 * The instrument screen -- LSDj 9.4.0, ROM bank $03 (its field accessors,
 * cursor steppers and small draw helpers, in ROM order).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The screen edits the current instrument (wInstr), a 16-byte record
 * of the working song read and written through ReadCurInstrByte /
 * WriteCurInstrByte (bank 0, c/bank00/InstrByte.c); constants/song.inc
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
void DrawFieldVal(UBYTE kind, UBYTE v);   /* bank 0 */
void OpenNameEditor(void);                  /* $1B:$4E8A */
void DrawInstrFieldHelp(void);              /* $05:$69BD */
void PasteInstr(void);                 /* $01:$5503 */
void CopyInstr(void);                  /* $01:$5253 */
void SpeechInstrInput(void);                 /* $1F:$724D */
void MapMoveLeft(void);                     /* $05:$4233 */
void MapMoveRight(void);                    /* $05:$4253 */
void MapMoveUp(void);                       /* $05:$426A */
void MapMoveDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);              /* $02:$78C4 */
void TogglePlayback(void);                  /* $02:$7C4C */
void WriteCurInstrByteC(UBYTE field, char val);   /* WriteCurInstrByte taking a char (aliased) */
void FarCallArgWC(UBYTE bank, UWORD fn, char arg);     /* FarCallArg with a word target and a char argument (aliased) */
void FarCallArgW(UBYTE bank, UWORD fn, UBYTE arg);     /* FarCallArg with a word target (aliased) */
void FarCallW(UBYTE bank, UWORD fn);                  /* FarCall with a word target (aliased) */
extern UBYTE wInstrFieldCount;              /* $C905 */
extern UBYTE wNameEditBuf[];                /* $C431 */
extern char InstrTypeNames[];               /* $03:$4FC4, [4][6] */
void DrawSpeechInstrScreen(void);           /* $1F:$7108 */
void DrawAllChanStatus(void);               /* $1F:$7072 */
void DrawPulseInstr(void);                  /* bank $1F */
void DrawWaveInstr(void);
void DrawKitInstr(void);
void DrawNoiseInstr(void);
void CopyInstrName(void);                   /* bank 1 */
UBYTE MulU8(UBYTE a, UBYTE b);

void WriteCurInstrByte(UBYTE field, UBYTE val)
{
    WriteInstrByte(wInstr, field, val);
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
    WriteCurInstrByte(6, ReadCurInstrByte(6) | 0x20);
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
    WriteCurInstrByte(5, v);
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
    strcpy(wStrBuf, "OUT OF SYNTHS! CLEAN INSTR DATA IN PROJECT SCREEN");
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
    WriteCurInstrByte(6, t | (v & 0xD0));
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableField));
}

void InstrSynthNew(void)
{
    UBYTE v;

    v = FindFreeSynth();
    if (v == 0xFF)
        return;
    GetTilemapAddr(InstrFieldX[1][9], InstrFieldY[1][9]);   /* WAVE: the synth field */
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    DrawByte(v);
    WriteCurInstrByte(3, v << 4);
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
    WriteCurInstrByte(3, (ReadCurInstrByte(3) & 0x0F) | (d << 4));
    ShowMessage("CLONED SYNTH");
}

void InstrScreenFrame(void)
{
    hBgDigitPal = 4;
    hBgPal = 1;
    FarCall(5, MapDrawTopRow);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    ClearMarkerSprites();
}

/* Step back to the previous allocated instrument (stopping at 0). */
void InstrSelectPrevUsed(void)
{
    wMessageStrPtr = 0;
    if (wInstr) {
        while (wInstr) {
            wInstr--;
            if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstr))
                break;
        }
        FarCall(1, FAR_FN(UiDrawTextHw));
        DrawInstrScreen();
    }
}

/* Page back 16 instruments, then to the previous allocated one. */
void InstrSelectPrevPage(void)
{
    if (wInstr) {
        wInstr -= 0x10;
        if (wInstr > 0x40)
            wInstr = 0;
        wInstr++;
        InstrSelectPrevUsed();
    }
}

void InstrSelectNextUsed(void)
{
    wMessageStrPtr = 0;
    while (wInstr != 0x40) {
        wInstr++;
        if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstr))
            break;
    }
    FarCall(1, FAR_FN(UiDrawTextHw));
    DrawInstrScreen();
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
        WriteCurInstrByte(8, v);
        DrawByte(v);
    }
}

void InstrCmdRateDec(void)
{
    UBYTE v;

    v = ReadCurInstrByte(8);
    if (v) {
        v--;
        WriteCurInstrByte(8, v);
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
        WriteCurInstrByte(5, ReadCurInstrByte(5) & 0xF7);
    else
        WriteCurInstrByte(6, ReadCurInstrByte(6) & 0xDF);
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableMode));
    wVramCursor++;
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableField));
}

void InstrTableModeNext(void)
{
    if (wButtonsSame)
        return;
    if (IsInstrTableOn())
        WriteCurInstrByte(5, ReadCurInstrByte(5) | 0x08);
    else
        InstrTableOn();
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    FarCall(0x1F, FAR_FN(DrawInstrTableMode));
    wVramCursor++;
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
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
        WriteCurInstrByte(6, v);
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
            WriteCurInstrByte(6, v);
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
            WriteCurInstrByte(6, v);
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
        WriteCurInstrByte(6, v);
        FarCall(0x1F, FAR_FN(DrawInstrTableField));
    }
}

void DrawWaveLen(void)
{
    wVramCursor = (UBYTE *)0x99CC;
    if (WavePlayIsManual())
        DrawStringIndexed('-');
    else
        DrawByte(GetWaveLen());
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
        d = GetWaveLen() - GetWaveLoopPos();
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
    WriteCurInstrByte(11, v - 3);
}

UBYTE GetWaveLen(void)
{
    return 0x0F - (ReadCurInstrByte(10) & 0x0F);
}

void SetInstrByte10(UBYTE v)
{
    if (v >= 0x10)
        DebugTrap();
    WriteCurInstrByte(10, 0x0F - v);
}

void SetInstrByte2(UBYTE v)
{
    WriteCurInstrByte(2, 0x0F ^ ((ReadCurInstrByte(2) & 0xF0) | v));
}

UBYTE GetVibShape(void)
{
    return ReadCurInstrByte(5) & 0x07;
}

/* $4B5E: store the vibrato shape in instrument byte 5 bits 2-0 (v: sin/saw/
   sqr << 1 | up) and redraw the PITCH field. A signed v: the OR reads it
   through a cast that sits between the mask and the OR, which keeps the
   masked byte in c; the callees take a char for the same reason. */
void SetVibShape(char v)
{
    char t;

    t = ReadCurInstrByte(5);
    t &= 0xF8;
    v |= t;
    WriteCurInstrByteC(5, v);
    FarCallArgWC(1, ReadCurInstrByte(0) == 3 ? (UWORD)DrawSpeedGlyph : (UWORD)DrawSpeedModeName, v);
}

void VibShapeNext(void)
{
    UBYTE v;

    v = GetVibShape();
    if (v < 5) {
        v += 2;
        if (v == 6)
            v = 1;
        SetVibShape(v);
    }
}

void VibShapePrev(void)
{
    UBYTE v;

    v = GetVibShape();
    if (v) {
        v -= 2;
        if (v == 0xFF)
            v = 4;
        SetVibShape(v);
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
    WriteCurInstrByte(5, v);
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
    WriteCurInstrByte(5, v);
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
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x9980;
    if (WavePlayIsManual()) {
        DrawString("WAVE ");
        wVramCursor = (UBYTE *)0x998B;
        ColorField(0x0B, 0x0C, 1, 1);
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
        DrawHexByte(ReadCurInstrByte(3));
    } else {
        DrawString("SYNTH");
        wVramCursor = (UBYTE *)0x998B;
        ColorField(0x0B, 0x0C, 1, 0);
        DrawStringIndexed(' ');
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
        DrawByte(ReadCurInstrByte(3) >> 4);
    }
}

void InstrEnvChanged(void)
{
    FarCall(0x1B, UpdateInstrEnv);
}

/* The instrument screen: ClaimInstrSlot (by address) for the instrument,
 * its type (an out-of-range type is reset to 0), the name and type labels
 * at the type's field positions, the type's own drawer by far call, the
 * channel status bar, and on CGB the field cursor erased field by field.
 *
 * lsdj-alloc(DrawInstrScreen): BUSYB=47-52
 * -- register b held from the `type * 18` multiply to the first
 * GetTilemapAddr's column read: type is evicted by the multiply's result,
 * that result and the row value are spilt in turn and the column takes c
 * (docs/toolchain.md, "The phantom register"). */
void DrawInstrScreen(void)
{
    UBYTE i;
    UBYTE type;
    UWORD fn;

    if (wInstr > 0x40)
        DebugTrap();
    if (wInstr == 0x40) {
        FarCall(0x1F, FAR_FN(DrawSpeechInstrScreen));
        return;
    }
    FarCallArgW(0x1F, 0x492A, wInstr);
    type = ReadCurInstrByte(0);
    if (type >= 4) {
        WriteCurInstrByte(0, 0);
        type = 0;
    }
    wInstrFieldCount = InstrFieldCounts[type];
    if (wInstrField >= wInstrFieldCount)
        wInstrField = wInstrFieldCount - 1;
    InstrScreenFrame();
    hBgDigitPal = wShadowDigitPal;
    hBgPal = wShadowPal;
    GetTilemapAddr(InstrFieldX[type][0], InstrFieldY[type][0]);
    FarCallArg(1, FAR_FN(CopyInstrName), wInstr);
    for (i = 0; i < 5; i++) {
        UBYTE c = wNameEditBuf[i];
        if (!c)
            c = ' ';
        DrawStringIndexed(c);
    }
    GetTilemapAddr(InstrFieldX[type][1], InstrFieldY[type][1]);
    DrawString(InstrTypeNames + type * 6);
    switch (type) {
    case 0:
        fn = (UWORD)DrawPulseInstr;
        break;
    case 1:
        fn = (UWORD)DrawWaveInstr;
        break;
    case 3:
        fn = (UWORD)DrawNoiseInstr;
        break;
    case 2:
        fn = (UWORD)DrawKitInstr;
        break;
    }
    FarCallW(0x1F, fn);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    if (hGameboyType) {
        i = wInstrField;
        wInstrField = 0;
        while (wInstrField < wInstrFieldCount) {
            EraseInstrCursor();
            wInstrField++;
        }
        wInstrField = i;
    }
    DrawInstrCursor();
    InstrEnvChanged();
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
    WriteCurInstrByte(field, t | kit);
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
    WriteCurInstrByte(0, t);
    FarCall(1, FAR_FN(UiDrawTextHw));
    if (t == 2)
        KitSelectEnsure();
    DrawInstrScreen();
}

void InstrFieldIncLo(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurInstrByte(field);
    t = v & 0x0F;
    if (t == 0x0F)
        return;
    v++;
    WriteCurInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvChanged();
}

void InstrFieldDecLo(UBYTE field)
{
    UBYTE v;

    v = ReadCurInstrByte(field);
    if ((v & 0x0F) == 0)
        return;
    v--;
    WriteCurInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvChanged();
}

void InstrFieldIncHi(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurInstrByte(field);
    t = v & 0xF0;
    if (t == 0xF0)
        return;
    v += 0x10;
    WriteCurInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvChanged();
}

void InstrFieldDecHi(UBYTE field)
{
    UBYTE v;

    v = ReadCurInstrByte(field);
    if ((v & 0xF0) == 0)
        return;
    v -= 0x10;
    WriteCurInstrByte(field, v);
    FarCall(0x1F, DrawInstrEnvFields);
    DrawInstrCursor();
    InstrEnvChanged();
}

void InstrLenInc(void)
{
    UBYTE v;

    v = ReadCurInstrByte(3);
    if (v & 0x40) {
        v--;
        if (v == 0x3F)
            v = 0;
    }
    WriteCurInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void InstrLenDec(void)
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
    WriteCurInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void InstrLenBigInc(void)
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
    WriteCurInstrByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void InstrLenBigDec(void)
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
    WriteCurInstrByte(3, v);
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
   instrument hands it to SpeechInstrInput): the directions move the cursor
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

    if (wInstr == 0x40)
        return FarCallByte(0x1F, FAR_FN(SpeechInstrInput));
    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    type = ReadCurInstrByte(0);
    if (wAPressEnabled)
        wInstrEditLatch = 0;
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wButtons & 0x10)
        WriteSongByte(1, (UWORD)wInstr + (UWORD)0xA040, 1);
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
                    WriteCurInstrByte(7, v);
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
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 7:
                InstrLenInc();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v < 0x7F)
                    ++v;
                WriteCurInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    VibShapeNext();
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
                WriteCurInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) + 1;
                DrawHexByte(v);
                WriteCurInstrByte(0x0B, v);
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
                WriteCurInstrByte(1, v);
                break;
            case 3:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 4:
                VibShapeNext();
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
                WriteCurInstrByte(3, v);
                break;
            case 8:
                v = ReadCurInstrByte(9);
                if (v != 4) {
                    if (v > 4)
                        v = 0;
                    else
                        ++v;
                    WriteCurInstrByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    WriteCurInstrByte(3, ReadCurInstrByte(3) & 0xF0);
                    hBgDigitPal = wShadowDigitPal;
                    hBgPal = wShadowPal;
                    DrawWaveSpeed();
                    DrawWaveLen();
                    DrawWaveLoopPos();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLen();
                if (v != 0x0F) {
                    ++v;
                    SetInstrByte10(v);
                    DrawByte(v);
                    hBgDigitPal = wShadowDigitPal;
                    hBgPal = wShadowPal;
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
                    WriteCurInstrByte(0x0C, v);
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
                WriteCurInstrByte(1, v);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 7:
                v = ReadCurInstrByte(8);
                ++v;
                DrawHexByte(v);
                WriteCurInstrByte(8, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(5);
                if (!(v & 64)) {
                    v |= 0x40;
                    DrawString(" ON");
                    WriteCurInstrByte(5, v);
                } else {
                    v = ReadCurInstrByte(2);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurInstrByte(2, v);
                }
                break;
            case 0x0D:
                v = ReadCurInstrByte(5);
                if (!(v & 32)) {
                    v |= 0x20;
                    DrawString(" ON");
                    WriteCurInstrByte(5, v);
                } else {
                    v = ReadCurInstrByte(9);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurInstrByte(9, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v < 0xFF)
                    ++v;
                DrawHexByte(v);
                WriteCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case 0x0B:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0B : 3);
                if (v < 0xFF)
                    ++v;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurInstrByte(2) & 0xBF;
                WriteCurInstrByte(2, v);
                DrawString("  1");
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    ++v;
                    WriteCurInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                VibShapeNext();
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
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 6:
                InstrLenInc();
                break;
            case 8:
                if (!wButtonsSame)
                    VibShapeNext();
                break;
            case 7:
                WriteCurInstrByte(2, 1);
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
                    WriteCurInstrByte(7, v);
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
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 7:
                InstrLenDec();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v)
                    --v;
                WriteCurInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    VibShapePrev();
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
                WriteCurInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) - 1;
                DrawHexByte(v);
                WriteCurInstrByte(0x0B, v);
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
                WriteCurInstrByte(1, v);
                break;
            case 3:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 4:
                VibShapePrev();
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
                WriteCurInstrByte(3, v);
                break;
            case 8:
                v = ReadCurInstrByte(9);
                if (v) {
                    if (v > 4)
                        v = 0;
                    else
                        --v;
                    WriteCurInstrByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    hBgDigitPal = wShadowDigitPal;
                    hBgPal = wShadowPal;
                    DrawWaveSpeed();
                    DrawWaveLen();
                    DrawWaveLoopPos();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLen();
                if (v & 0x0F) {
                    --v;
                    SetInstrByte10(v);
                    DrawByte(v);
                    if (v < GetWaveLoopPos())
                        SetInstrByte2(v);
                    hBgDigitPal = wShadowDigitPal;
                    hBgPal = wShadowPal;
                    DrawWaveLoopPos();
                }
                break;
            case 0x0C:
                v = GetWaveLoopPos();
                if (v < GetWaveLen()) {
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
                    WriteCurInstrByte(0x0C, v);
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
                WriteCurInstrByte(1, v);
                break;
            case 5:
                v = ReadCurInstrByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 7:
                v = ReadCurInstrByte(8);
                --v;
                DrawHexByte(v);
                WriteCurInstrByte(8, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(2);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurInstrByte(2, v);
                } else {
                    v = ReadCurInstrByte(5);
                    v = (int)(char)v & (int)0xBF;
                    DrawString("OFF");
                    WriteCurInstrByte(5, v);
                }
                break;
            case 0x0D:
                v = ReadCurInstrByte(9);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurInstrByte(9, v);
                } else {
                    v = ReadCurInstrByte(5);
                    v = (int)(char)v & (int)0xDF;
                    DrawString("OFF");
                    WriteCurInstrByte(5, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v)
                    --v;
                DrawHexByte(v);
                WriteCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
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
                WriteCurInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurInstrByte(2) | 0x40;
                WriteCurInstrByte(2, v);
                DrawString("0.5");
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    --v;
                    WriteCurInstrByte(0x0A, v);
                    DrawKitDistName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                VibShapePrev();
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
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, v);
                break;
            case 6:
                InstrLenDec();
                break;
            case 8:
                if (!wButtonsSame)
                    VibShapePrev();
                break;
            case 7:
                WriteCurInstrByte(2, 0);
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
                    WriteCurInstrByte(7, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7);
                v |= 3;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, 3);
                break;
            case 7:
                InstrLenBigInc();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if ((v & 0x70) != 0x70)
                    v += 0x10;
                WriteCurInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                InstrPitchModeDown();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                v += 0x0C;
                DrawHexByte(v);
                WriteCurInstrByte(2, v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) + 0x10;
                WriteCurInstrByte(0x0B, v);
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
                WriteCurInstrByte(1, 0x20);
                break;
            case 3:
                WriteCurInstrByte(7, 3);
                DrawFieldVal(0x0C, 3);
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
                        WriteCurInstrByte(3, v);
                    }
                }
                break;
            case 0x0B:
                v = 0x0F - GetWaveLen();
                SetInstrByte10(0x0F);
                DrawStringIndexed(0x46);
                v = GetWaveLoopPos() + v;
                if (v & 0xF0)
                    v = 0x0F;
                SetInstrByte2(v);
                hBgDigitPal = wShadowDigitPal;
                hBgPal = wShadowPal;
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
                WriteCurInstrByte(0x0C, v);
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
                WriteCurInstrByte(1, 0x20);
                break;
            case 5:
                WriteCurInstrByte(7, 3);
                DrawFieldVal(0x0C, 3);
                break;
            case 7:
                v = ReadCurInstrByte(8) + 0x10;
                DrawHexByte(v);
                WriteCurInstrByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawHexByte(v);
                WriteCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
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
                WriteCurInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    if (v > 0xC2 && v < 0xD3)
                        v = 0xD3;
                    else
                        v += 0x10;
                    WriteCurInstrByte(0x0A, v);
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
                WriteCurInstrByte(7, 3);
                DrawFieldVal(0x0C, 3);
                break;
            case 6:
                InstrLenBigInc();
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
                    WriteCurInstrByte(7, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case 6:
                v = ReadCurInstrByte(7) & 0xFC;
                WriteCurInstrByte(7, v);
                DrawFieldVal(0x0C, 0);
                break;
            case 7:
                InstrLenBigDec();
                break;
            case 8:
                v = ReadCurInstrByte(4);
                if (v & 0xF0)
                    v -= 0x10;
                WriteCurInstrByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                InstrPitchModeUp();
                break;
            case 0x0B:
                v = ReadCurInstrByte(2);
                v -= 0x0C;
                WriteCurInstrByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurInstrByte(0x0B) - 0x10;
                DrawHexByte(v);
                WriteCurInstrByte(0x0B, v);
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
                WriteCurInstrByte(1, 0);
                break;
            case 3:
                WriteCurInstrByte(7, 0);
                DrawFieldVal(0x0C, 0);
                break;
            case 4:
                InstrPitchModeUp();
                break;
            case 0x0B:
                SetInstrByte10(0);
                DrawStringIndexed(0x30);
                hBgDigitPal = wShadowDigitPal;
                hBgPal = wShadowPal;
            case 0x0C:
                SetInstrByte2(GetWaveLen());
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
                        WriteCurInstrByte(3, v);
                    }
                }
                break;
            case 6:
                v = ReadCurInstrByte(0x0C) - 0x10;
                if ((v & 0xF0) == 0x70)
                    v = 0x80;
                WriteCurInstrByte(0x0C, v);
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
                WriteCurInstrByte(1, 0);
                break;
            case 5:
                WriteCurInstrByte(7, 0);
                DrawFieldVal(0x0C, 0);
                break;
            case 7:
                v = ReadCurInstrByte(8) - 0x10;
                DrawHexByte(v);
                WriteCurInstrByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                DrawHexByte(v);
                WriteCurInstrByte(wInstrCursorCol ? 0x0D : 0x0C, v);
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
                WriteCurInstrByte(wInstrCursorCol ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurInstrByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    if (v > 0xD0 && v < 0xE0)
                        v = 0xD0;
                    else
                        v -= 0x10;
                    WriteCurInstrByte(0x0A, v);
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
                WriteCurInstrByte(7, 0);
                DrawFieldVal(0x0C, 0);
                break;
            case 6:
                InstrLenBigDec();
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
                hBgDigitPal = wShadowDigitPal;
                hBgPal = wShadowPal;
                FarCall(0x1F, DrawInstrTableMode);
                wVramCursor++;
                hBgDigitPal = wSelectDigitPal;
                hBgPal = wSelectPal;
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
        wInstr += 0x0F;
        if (wInstr > 0x40)
            wInstr = 0x40;
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
            FarCall(1, FAR_FN(PasteInstr));
        DrawInstrScreen();
        wSelectHeld = 0;
        return 1;
    case 0x70:
        wSelectHeld = 0;
        if (wInstrEditLatch) {
            CloneSynthOrTable();
            DrawInstrScreen();
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
        FarCall(1, FAR_FN(CopyInstr));
        wInstrEditLatch = 1;
        return 1;
    }
    return 0;
}
