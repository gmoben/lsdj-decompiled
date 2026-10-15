/*
 * The instrument screen -- LSDj 9.4.0, ROM bank $03 (its field accessors,
 * cursor steppers and small draw helpers, in ROM order).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The screen edits the current instrument (wInstrument), a 16-byte record
 * of the working song read and written through ReadCurrentInstrumentByte /
 * WriteCurrentInstrumentByte (bank 0, c/bank00/InstrByte.c); constants/song.inc
 * has the per-type field layout (byte 5 = pitch flags, bit 5 transpose;
 * byte 6 = table on (bit 5) + table number (bits 4-0)).
 */

#define SONG_BYTE_PTR               /* this bank's ReadSongByte takes a pointer (CloneSynth) */
#include "lsdj.h"

extern UBYTE DefaultWaveFrameB3[];          /* $03:$4015, 16 bytes */
extern char TxtUnlim[];                     /* $03:$4025 "UNLIM" */
extern UBYTE InstrumentFieldX[][18];             /* $03:$402B, [type][field] cursor X */
extern UBYTE InstrumentFieldY[][18];             /* $03:$406E */
extern UBYTE InstrumentFieldW[][18];             /* $03:$40B1 */
extern UBYTE InstrumentFieldCounts[];            /* $03:$40F4, [4] fields per instrument type */
extern UBYTE wDefaultNotes;                 /* $C904 */
extern UBYTE wAHeld;                        /* $C40C */
extern UBYTE wNameEditIsInstrument;              /* $C430 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wInstrumentColumn;               /* $C428 */
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
void ShowInstrumentFieldHelp(void);              /* $05:$69BD */
void PasteInstrument(void);                 /* $01:$5503 */
void CopyInstrument(void);                  /* $01:$5253 */
void SpeechInstrumentInput(void);                 /* $1F:$724D */
void MoveMapCursorLeft(void);                     /* $05:$4233 */
void MoveMapCursorRight(void);                    /* $05:$4253 */
void MoveMapCursorUp(void);                       /* $05:$426A */
void MoveMapCursorDown(void);                     /* $05:$4287 */
void SoundToggleSongPlayback(void);              /* $02:$78C4 */
void SoundTogglePlayback(void);                  /* $02:$7C4C */
void WriteCurrentInstrumentByteC(UBYTE field, char val);   /* WriteCurrentInstrumentByte taking a char (aliased) */
void FarCallArgumentWC(UBYTE bank, UWORD fn, char arg);     /* FarCallArgument with a word target and a char argument (aliased) */
void FarCallArgumentW(UBYTE bank, UWORD fn, UBYTE arg);     /* FarCallArgument with a word target (aliased) */
void FarCallW(UBYTE bank, UWORD fn);                  /* FarCall with a word target (aliased) */
extern UBYTE wInstrumentFieldCount;              /* $C905 */
extern UBYTE wNameEditBuffer[];                /* $C431 */
extern char InstrumentTypeNames[];               /* $03:$4FC4, [4][6] */
void DrawSpeechInstrumentScreen(void);           /* $1F:$7108 */
void DrawAllChannelStatus(void);               /* $1F:$7072 */
void DrawPulseInstrument(void);                  /* bank $1F */
void DrawWaveInstrument(void);
void DrawKitInstrument(void);
void DrawNoiseInstrument(void);
void CopyInstrumentName(void);                   /* bank 1 */
UBYTE MulU8(UBYTE a, UBYTE b);

void WriteCurrentInstrumentByte(UBYTE field, UBYTE val)
{
    WriteInstrumentByte(wInstrument, field, val);
}

UBYTE GetWavePlayMode(void)
{
    return ReadCurrentInstrumentByte(9);
}

UBYTE WavePlayIsManual(void)
{
    return GetWavePlayMode() == 0;
}

UBYTE IsInstrumentTableOn(void)
{
    UBYTE r;

    if (ReadCurrentInstrumentByte(6) & 0x20)
        r = 1;
    else
        r = 0;
    return r;
}

void SetInstrumentTableOn(void)
{
    WriteCurrentInstrumentByte(6, ReadCurrentInstrumentByte(6) | 0x20);
}

UBYTE CheckInstrumentFieldOption(UBYTE type)
{
    return wInstrumentField == InstrumentFieldCounts[type] - 1;
}

void DrawInstrumentCursor(void)
{
    UBYTE type;

    type = ReadCurrentInstrumentByte(0) & 3;
    if (type == 1 && WavePlayIsManual() && wInstrumentField == 9) {
        ColorField(0x0B, 0x0C, 2, hGameboyType ? 3 : 2);
        return;
    }
    ColorField(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField],
                  InstrumentFieldW[type][wInstrumentField], hGameboyType ? 3 : 2);
}

void SetInstrumentPitchByte(UBYTE v)
{
    WriteCurrentInstrumentByte(5, v);
    FarCallArgument(1, FAR_FN(DrawOnOffName), v);
}

void SetInstrumentTransposeOn(void)
{
    SetInstrumentPitchByte(ReadCurrentInstrumentByte(5) | 0x20);
}

void SetInstrumentTransposeOff(void)
{
    SetInstrumentPitchByte(ReadCurrentInstrumentByte(5) & 0xDF);
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
    strcpy(wStringBuffer, "OUT OF SYNTHS! CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

void AllocInstrumentTable(void)
{
    UBYTE t, v;

    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    t |= 0x20;
    v = ReadCurrentInstrumentByte(6);
    WriteCurrentInstrumentByte(6, t | (v & 0xD0));
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
}

void AllocInstrumentSynth(void)
{
    UBYTE v;

    v = FindFreeSynth();
    if (v == 0xFF)
        return;
    SetVramCursor(InstrumentFieldX[1][9], InstrumentFieldY[1][9]);   /* WAVE: the synth field */
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    DrawByte(v);
    WriteCurrentInstrumentByte(3, v << 4);
}

void CloneSynth(void)
{
    UBYTE s, d, i;
    UWORD ps, pd;
    UBYTE v, w;

    s = ReadCurrentInstrumentByte(3) >> 4;
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
    WriteCurrentInstrumentByte(3, (ReadCurrentInstrumentByte(3) & 0x0F) | (d << 4));
    ShowMessage("CLONED SYNTH");
}

void DrawInstrumentScreenFrame(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    FarCall(5, DrawMapTopRow);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    ClearMarkerSprites();
}

/* Step back to the previous allocated instrument (stopping at 0). */
void PreviousUsedInstrument(void)
{
    wMessageStringPointer = 0;
    if (wInstrument) {
        while (wInstrument) {
            wInstrument--;
            if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstrument))
                break;
        }
        FarCall(1, FAR_FN(DrawScreenPaletteMap));
        DrawInstrumentScreen();
    }
}

/* Page back 16 instruments, then to the previous allocated one. */
void PreviousInstrumentPage(void)
{
    if (wInstrument) {
        wInstrument -= 0x10;
        if (wInstrument > 0x40)
            wInstrument = 0;
        wInstrument++;
        PreviousUsedInstrument();
    }
}

void NextUsedInstrument(void)
{
    wMessageStringPointer = 0;
    while (wInstrument != 0x40) {
        wInstrument++;
        if (ReadSongByte(1, SramInstrAlloc + (UWORD)wInstrument))
            break;
    }
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    DrawInstrumentScreen();
}

void EraseInstrumentCursor(void)
{
    UBYTE type, x, y, r;

    type = ReadCurrentInstrumentByte(0) & 3;
    if (type == 1 && WavePlayIsManual() && wInstrumentField == 9) {
        ColorField(0x0B, 0x0C, 2, 1);
        return;
    }
    x = InstrumentFieldX[type][wInstrumentField];
    r = InstrumentFieldW[type][wInstrumentField] + x;
    y = InstrumentFieldY[type][wInstrumentField];
    ColorField(x, y, r - x, 1);
}

void RaiseInstrumentCommandRate(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(8);
    if (v < 0x0F) {
        v++;
        WriteCurrentInstrumentByte(8, v);
        DrawByte(v);
    }
}

void LowerInstrumentCommandRate(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(8);
    if (v) {
        v--;
        WriteCurrentInstrumentByte(8, v);
        DrawByte(v);
    }
}

UBYTE GetInstrumentByte5Flag8(void)
{
    return ReadCurrentInstrumentByte(5) & 0x08;
}

void PreviousInstrumentTableMode(void)
{
    if (wButtonsSame)
        return;
    if (ReadCurrentInstrumentByte(5) & 0x08)
        WriteCurrentInstrumentByte(5, ReadCurrentInstrumentByte(5) & 0xF7);
    else
        WriteCurrentInstrumentByte(6, ReadCurrentInstrumentByte(6) & 0xDF);
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableMode));
    wVramCursor++;
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
}

void NextInstrumentTableMode(void)
{
    if (wButtonsSame)
        return;
    if (IsInstrumentTableOn())
        WriteCurrentInstrumentByte(5, ReadCurrentInstrumentByte(5) | 0x08);
    else
        SetInstrumentTableOn();
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableMode));
    wVramCursor++;
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
}

void NextInstrumentTable(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(6);
    if (v & 0x20) {
        t = v & 0x1F;
        if (t == 0x1F)
            return;
        v++;
        WriteCurrentInstrumentByte(6, v);
        FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
    }
}

void PreviousInstrumentTable(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(6);
    if (v & 0x20) {
        if ((v & 0x1F) == 0) {
            if (!wButtonsSame) {
                PreviousInstrumentTableMode();
                PreviousInstrumentTableMode();
                EraseInstrumentCursor();
                wInstrumentField--;
                DrawInstrumentCursor();
            }
        } else {
            v--;
            WriteCurrentInstrumentByte(6, v);
            FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
        }
    }
}

void AddInstrumentTable16(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(6);
    if (v & 0x20) {
        if (!wButtonsSame) {
            v |= 0x10;
            WriteCurrentInstrumentByte(6, v);
            FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
        }
    }
}

void SubInstrumentTable16(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(6);
    if (v & 0x20) {
        v &= 0xEF;
        WriteCurrentInstrumentByte(6, v);
        FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
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

UBYTE GetWaveLoopPosition(void)
{
    return 0x0F ^ (ReadCurrentInstrumentByte(2) & 0x0F);
}

void DrawWaveLoopPosition(void)
{
    UBYTE v, d;

    wVramCursor = (UBYTE *)0x99EC;
    v = GetWavePlayMode();
    if (v == 2 || v == 3 || v == 4) {
        d = GetWaveLength() - GetWaveLoopPosition();
        if (d & 0xF0)
            d = 0;
        DrawByte(d);
    } else
        DrawStringIndexed('-');
}

void DrawKitDistortionName(UBYTE v)
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
    return ReadCurrentInstrumentByte(11) + 3;
}

void SetInstrumentByte11(UBYTE v)
{
    WriteCurrentInstrumentByte(11, v - 3);
}

UBYTE GetWaveLength(void)
{
    return 0x0F - (ReadCurrentInstrumentByte(10) & 0x0F);
}

void SetInstrumentByte10(UBYTE v)
{
    if (v >= 0x10)
        DebugTrap();
    WriteCurrentInstrumentByte(10, 0x0F - v);
}

void SetInstrumentByte2(UBYTE v)
{
    WriteCurrentInstrumentByte(2, 0x0F ^ ((ReadCurrentInstrumentByte(2) & 0xF0) | v));
}

UBYTE GetVibratoShape(void)
{
    return ReadCurrentInstrumentByte(5) & 0x07;
}

/* $4B5E: store the vibrato shape in instrument byte 5 bits 2-0 (v: sin/saw/
   sqr << 1 | up) and redraw the PITCH field. A signed v: the OR reads it
   through a cast that sits between the mask and the OR, which keeps the
   masked byte in c; the callees take a char for the same reason. */
void SetVibratoShape(char v)
{
    char t;

    t = ReadCurrentInstrumentByte(5);
    t &= 0xF8;
    v |= t;
    WriteCurrentInstrumentByteC(5, v);
    FarCallArgumentWC(1, ReadCurrentInstrumentByte(0) == 3 ? (UWORD)DrawSpeedGlyph : (UWORD)DrawSpeedModeName, v);
}

void NextVibratoShape(void)
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

void PreviousVibratoShape(void)
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

UBYTE GetInstrumentType3(void)
{
    return ReadCurrentInstrumentByte(0) - 2;
}

void NextInstrumentPitchMode(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(5);
    if (v & 0x80)
        return;
    if (v & 0x10)
        v ^= 0x90;
    else if ((v & 0x40) && GetInstrumentType3())
        v ^= 0x40;
    else
        v |= 0x10;
    WriteCurrentInstrumentByte(5, v);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
}

void PreviousInstrumentPitchMode(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(5);
    if (v & 0x80)
        v ^= 0x90;
    else if (v & 0x10) {
        if (GetInstrumentType3())
            v &= 0xAF;
        else
            v ^= 0x10;
    } else if (GetInstrumentType3())
        v |= 0x40;
    WriteCurrentInstrumentByte(5, v);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
}

void DrawWaveFinetune(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(12);
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
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawHexByte(ReadCurrentInstrumentByte(3));
    } else {
        DrawString("SYNTH");
        wVramCursor = (UBYTE *)0x998B;
        ColorField(0x0B, 0x0C, 1, 0);
        DrawStringIndexed(' ');
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawByte(ReadCurrentInstrumentByte(3) >> 4);
    }
}

void InstrumentEnvelopeChanged(void)
{
    FarCall(0x1B, UpdateInstrumentEnvelope);
}

/* The instrument screen: ClaimInstrumentSlot (by address) for the instrument,
 * its type (an out-of-range type is reset to 0), the name and type labels
 * at the type's field positions, the type's own drawer by far call, the
 * channel status bar, and on CGB the field cursor erased field by field.
 *
 * lsdj-alloc(DrawInstrumentScreen): BUSYB=47-52
 * -- register b held from the `type * 18` multiply to the first
 * SetVramCursor's column read: type is evicted by the multiply's result,
 * that result and the row value are spilt in turn and the column takes c
 * (docs/toolchain.md, "The phantom register"). */
void DrawInstrumentScreen(void)
{
    UBYTE i;
    UBYTE type;
    UWORD fn;

    if (wInstrument > 0x40)
        DebugTrap();
    if (wInstrument == 0x40) {
        FarCall(0x1F, FAR_FN(DrawSpeechInstrumentScreen));
        return;
    }
    FarCallArgumentW(0x1F, 0x492A, wInstrument);
    type = ReadCurrentInstrumentByte(0);
    if (type >= 4) {
        WriteCurrentInstrumentByte(0, 0);
        type = 0;
    }
    wInstrumentFieldCount = InstrumentFieldCounts[type];
    if (wInstrumentField >= wInstrumentFieldCount)
        wInstrumentField = wInstrumentFieldCount - 1;
    DrawInstrumentScreenFrame();
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    SetVramCursor(InstrumentFieldX[type][0], InstrumentFieldY[type][0]);
    FarCallArgument(1, FAR_FN(CopyInstrumentName), wInstrument);
    for (i = 0; i < 5; i++) {
        UBYTE c = wNameEditBuffer[i];
        if (!c)
            c = ' ';
        DrawStringIndexed(c);
    }
    SetVramCursor(InstrumentFieldX[type][1], InstrumentFieldY[type][1]);
    DrawString(InstrumentTypeNames + type * 6);
    switch (type) {
    case 0:
        fn = (UWORD)DrawPulseInstrument;
        break;
    case 1:
        fn = (UWORD)DrawWaveInstrument;
        break;
    case 3:
        fn = (UWORD)DrawNoiseInstrument;
        break;
    case 2:
        fn = (UWORD)DrawKitInstrument;
        break;
    }
    FarCallW(0x1F, fn);
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    if (hGameboyType) {
        i = wInstrumentField;
        wInstrumentField = 0;
        while (wInstrumentField < wInstrumentFieldCount) {
            EraseInstrumentCursor();
            wInstrumentField++;
        }
        wInstrumentField = i;
    }
    DrawInstrumentCursor();
    InstrumentEnvelopeChanged();
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

    t = (BYTE)ReadCurrentInstrumentByte(field) & (WORD)0xC0;
    WriteCurrentInstrumentByte(field, t | kit);
}

UBYTE NextKit(void)
{
    UBYTE field, kit, orig;

    field = (wInstrumentField == 2) ? 2 : 9;
    kit = ReadCurrentInstrumentByte(field) & 0x3F;
    orig = kit;
    do {
        if (!kit)
            return orig;
        kit--;
    } while (!KitExists(kit));
    WriteKitNumber(field, kit);
    return kit;
}

UBYTE PreviousKit(void)
{
    UBYTE field, kit, orig;

    field = (wInstrumentField == 2) ? 2 : 9;
    kit = ReadCurrentInstrumentByte(field) & 0x3F;
    orig = kit;
    do {
        if (kit == 0x3F)
            return orig;
        kit++;
    } while (!KitExists(kit));
    WriteKitNumber(field, kit);
    return kit;
}

void EnsureKitExists(void)
{
    UBYTE i, field;

    for (i = 0; i < 2; i++) {
        field = i ? 2 : 9;
        if (!KitExists(ReadCurrentInstrumentByte(field) & 0x3F))
            WriteKitNumber(field, 0);
    }
}

void SetInstrumentType(UBYTE type)
{
    UBYTE t, n;

    if (wButtonsSame)
        return;
    t = type;
    n = InstrumentFieldCounts[t];
    if (wInstrumentField >= n)
        wInstrumentField = n - 1;
    WriteCurrentInstrumentByte(0, t);
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    if (t == 2)
        EnsureKitExists();
    DrawInstrumentScreen();
}

void RaiseInstrumentFieldLow(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(field);
    t = v & 0x0F;
    if (t == 0x0F)
        return;
    v++;
    WriteCurrentInstrumentByte(field, v);
    FarCall(0x1F, DrawEnvelopeFields);
    DrawInstrumentCursor();
    InstrumentEnvelopeChanged();
}

void LowerInstrumentFieldLow(UBYTE field)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(field);
    if ((v & 0x0F) == 0)
        return;
    v--;
    WriteCurrentInstrumentByte(field, v);
    FarCall(0x1F, DrawEnvelopeFields);
    DrawInstrumentCursor();
    InstrumentEnvelopeChanged();
}

void RaiseInstrumentFieldHigh(UBYTE field)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(field);
    t = v & 0xF0;
    if (t == 0xF0)
        return;
    v += 0x10;
    WriteCurrentInstrumentByte(field, v);
    FarCall(0x1F, DrawEnvelopeFields);
    DrawInstrumentCursor();
    InstrumentEnvelopeChanged();
}

void LowerInstrumentFieldHigh(UBYTE field)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(field);
    if ((v & 0xF0) == 0)
        return;
    v -= 0x10;
    WriteCurrentInstrumentByte(field, v);
    FarCall(0x1F, DrawEnvelopeFields);
    DrawInstrumentCursor();
    InstrumentEnvelopeChanged();
}

void RaiseInstrumentLength(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(3);
    if (v & 0x40) {
        v--;
        if (v == 0x3F)
            v = 0;
    }
    WriteCurrentInstrumentByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void LowerInstrumentLength(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(3);
    if (!(v & 0x40)) {
        v &= 0x80;
        v += 0x40;
    } else {
        t = v & 0x3F;
        if (t != 0x3F)
            v++;
    }
    WriteCurrentInstrumentByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void RaiseInstrumentLengthBig(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(3);
    if (v & 0x40) {
        t = v & 0x3F;
        if (t > 0x0F)
            v -= 0x10;
        else
            v = 0;
    }
    WriteCurrentInstrumentByte(3, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

void LowerInstrumentLengthBig(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(3);
    if (!(v & 0x40))
        v = 0x4F;
    else {
        t = v & 0x3F;
        if (t < 0x30)
            v += 0x10;
        else
            v |= 0x3F;
    }
    WriteCurrentInstrumentByte(3, v);
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

    t = ReadCurrentInstrumentByte(0) & 3;
    if (t == 1 && wInstrumentField == 9 && !WavePlayIsManual())
        CloneSynth();
    else
        FarCall(1, FAR_FN(CloneInstrumentTable));
    wInstrumentEditLatch = 0;
}

/* $541F: the instrument screen's input, a switch on wButtons (a speech
   instrument hands it to SpeechInstrumentInput): the directions move the cursor
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

UBYTE InstrumentScreenInput(void)
{
    UBYTE v, type;

    if (wInstrument == 0x40)
        return FarCallByte(0x1F, FAR_FN(SpeechInstrumentInput));
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    type = ReadCurrentInstrumentByte(0);
    if (wAPressEnabled)
        wInstrumentEditLatch = 0;
    if (wButtons != 0x10)
        wDrawStep = 0;
    if (wButtons & 0x10)
        WriteSongByte(1, (UWORD)wInstrument + (UWORD)0xA040, 1);
    switch (wButtons) {
    case 0x04:
        if (!wInstrumentField)
            break;
        EraseInstrumentCursor();
        wInstrumentField--;
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
            case 3:
                wInstrumentField = 1;
                break;
            case 4:
                wInstrumentField -= 2;
                break;
            case 0x0E:
                wInstrumentField--;
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                wInstrumentField--;
                break;
            case 0x0D:
                wInstrumentField--;
            case 0x0C:
                switch (GetWavePlayMode()) {
                case 1:
                    wInstrumentField--;
                    break;
                case 0:
                    wInstrumentField -= 3;
                }
            }
            break;
        case 2:
            if (wInstrumentField == 0x10)
                wInstrumentField--;
            else
                switch (wInstrumentField + wInstrumentColumn - 1) {
                case 4:
                case 8:
                case 0x0A:
                case 0x0C:
                    wInstrumentField--;
                }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
            case 3:
                wInstrumentField = 1;
                break;
            case 4:
                wInstrumentField -= 2;
                break;
            case 0x0B:
                wInstrumentField--;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case 0x08:
        EraseInstrumentCursor();
        if (InstrumentFieldCounts[type] - wInstrumentField - 1) {
            if (type == 1)
                switch (wInstrumentField) {
                case 1:
                case 2:
                case 0x0C:
                    wInstrumentField++;
                    break;
                case 0x0B:
                    if (GetWavePlayMode() == 0 || GetWavePlayMode() == 1)
                        wInstrumentField += 2;
                    break;
                case 9:
                    if (WavePlayIsManual())
                        wInstrumentField += 4;
                }
            wInstrumentField++;
            switch (type) {
            case 0:
                switch (wInstrumentField) {
                case 3:
                case 4:
                    wInstrumentField = 5;
                    break;
                case 0x0E:
                    wInstrumentField++;
                }
                break;
            case 2:
                switch (wInstrumentField) {
                case 5:
                case (int)9:
                case 0x0A:
                case (int)0x0B:
                case 0x0C:
                case (int)0x0D:
                case 0x10:
                    wInstrumentField++;
                    break;
                case 4:
                case 8:
                    if (wInstrumentColumn)
                        wInstrumentField++;
                }
                break;
            case 3:
                switch (wInstrumentField) {
                case 3:
                case 4:
                    wInstrumentField = 5;
                    break;
                case 0x0B:
                    wInstrumentField++;
                }
            }
        }
        DrawInstrumentCursor();
        return 1;
    case 0x02:
        EraseInstrumentCursor();
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 3:
            case 4:
            case 0x0F:
                wInstrumentField--;
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 3:
            case 0x0E:
                wInstrumentField--;
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 5:
            case (int)9:
            case (int)0x0B:
            case (int)0x0D:
            case 0x11:
                wInstrumentField--;
                wInstrumentColumn = 0;
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 3:
            case 4:
            case 0x0C:
                wInstrumentField--;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case 0x01:
        EraseInstrumentCursor();
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasSecond))
                    wInstrumentField++;
                break;
            case 3:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasThird))
                    wInstrumentField++;
                break;
            case 0x0E:
                wInstrumentField++;
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                wInstrumentField++;
                break;
            case 0x0D:
                wInstrumentField++;
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 4:
            case 8:
            case 0x0A:
            case 0x0C:
            case 0x10:
                wInstrumentField++;
                wInstrumentColumn = 1;
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasSecond))
                    wInstrumentField++;
                break;
            case 3:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasThird))
                    wInstrumentField++;
                break;
            case 0x0B:
                wInstrumentField++;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case 0x11:
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        if (wInstrumentField == 1) {
            if (type < 3)
                SetInstrumentType(type + 1);
            return 1;
        }
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
                RaiseInstrumentFieldLow(1);
                break;
            case 3:
                RaiseInstrumentFieldLow(9);
                break;
            case 4:
                RaiseInstrumentFieldLow(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrumentByte(7, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case 6:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 1)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 2;
                }
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                RaiseInstrumentLength();
                break;
            case 8:
                v = ReadCurrentInstrumentByte(4);
                if (v < 0x7F)
                    ++v;
                WriteCurrentInstrumentByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    NextVibratoShape();
                break;
            case 0x0A:
                SetInstrumentTransposeOff();
                break;
            case 0x0D:
                RaiseInstrumentCommandRate();
                break;
            case 0x0B:
                v = ReadCurrentInstrumentByte(2);
                ++v;
                WriteCurrentInstrumentByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(0x0B) + 1;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(0x0B, v);
                break;
            case 0x0E:
                NextInstrumentTableMode();
                break;
            case 0x0F:
                NextInstrumentTable();
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                v = ReadCurrentInstrumentByte(1) & 0x60;
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
                WriteCurrentInstrumentByte(1, v);
                break;
            case 3:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 4:
                NextVibratoShape();
                break;
            case 9:
                v = ReadCurrentInstrumentByte(3);
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
                WriteCurrentInstrumentByte(3, v);
                break;
            case 8:
                v = ReadCurrentInstrumentByte(9);
                if (v != 4) {
                    if (v > 4)
                        v = 0;
                    else
                        ++v;
                    WriteCurrentInstrumentByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    WriteCurrentInstrumentByte(3, ReadCurrentInstrumentByte(3) & 0xF0);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPosition();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLength();
                if (v != 0x0F) {
                    ++v;
                    SetInstrumentByte10(v);
                    DrawByte(v);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveLoopPosition();
                }
                break;
            case 0x0C:
                v = GetWaveLoopPosition();
                if (v & 0x0F) {
                    --v;
                    SetInstrumentByte2(v);
                    DrawWaveLoopPosition();
                }
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0xFE) {
                    ++v;
                    SetInstrumentByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 5:
                SetInstrumentTransposeOff();
                break;
            case 7:
                RaiseInstrumentCommandRate();
                break;
            case 6:
                v = ReadCurrentInstrumentByte(0x0C) + 1;
                if (v != 0x80) {
                    WriteCurrentInstrumentByte(0x0C, v);
                    DrawWaveFinetune();
                }
                break;
            case 0x0D:
                NextInstrumentTableMode();
                break;
            case 0x0E:
                NextInstrumentTable();
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 2:
            case 3:
                DrawKitNameField(PreviousKit());
                break;
            case 4:
                v = ReadCurrentInstrumentByte(1) & 0x60;
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
                WriteCurrentInstrumentByte(1, v);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                v = ReadCurrentInstrumentByte(8);
                ++v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(8, v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(5);
                if (!(v & 64)) {
                    v |= 0x40;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(5, v);
                } else {
                    v = ReadCurrentInstrumentByte(2);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrumentByte(2, v);
                }
                break;
            case 0x0D:
                v = ReadCurrentInstrumentByte(5);
                if (!(v & 32)) {
                    v |= 0x20;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(5, v);
                } else {
                    v = ReadCurrentInstrumentByte(9);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrumentByte(9, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v < 0xFF)
                    ++v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case 0x0B:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if (v < 0xFF)
                    ++v;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurrentInstrumentByte(2) & 0xBF;
                WriteCurrentInstrumentByte(2, v);
                DrawString("  1");
                break;
            case 0x0F:
                v = ReadCurrentInstrumentByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    ++v;
                    WriteCurrentInstrumentByte(0x0A, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                NextVibratoShape();
                break;
            case 0x10:
                NextInstrumentTableMode();
                break;
            case 0x11:
                NextInstrumentTable();
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
                RaiseInstrumentFieldLow(1);
                break;
            case 3:
                RaiseInstrumentFieldLow(9);
                break;
            case 4:
                RaiseInstrumentFieldLow(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 6:
                RaiseInstrumentLength();
                break;
            case 8:
                if (!wButtonsSame)
                    NextVibratoShape();
                break;
            case 7:
                WriteCurrentInstrumentByte(2, 1);
                DrawString("SAFE");
                break;
            case 9:
                SetInstrumentTransposeOff();
                break;
            case 0x0A:
                RaiseInstrumentCommandRate();
                break;
            case 0x0B:
                NextInstrumentTableMode();
                break;
            case 0x0C:
                NextInstrumentTable();
            }
        }
        return 1;
    case 0x12:
        if (wInstrumentField == 1) {
            if (type)
                SetInstrumentType(type - 1);
            return 1;
        }
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
                LowerInstrumentFieldLow(1);
                break;
            case 3:
                LowerInstrumentFieldLow(9);
                break;
            case 4:
                LowerInstrumentFieldLow(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrumentByte(7, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case 6:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 2)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 1;
                }
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                LowerInstrumentLength();
                break;
            case 8:
                v = ReadCurrentInstrumentByte(4);
                if (v)
                    --v;
                WriteCurrentInstrumentByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                if (!wButtonsSame)
                    PreviousVibratoShape();
                break;
            case 0x0A:
                SetInstrumentTransposeOn();
                break;
            case 0x0D:
                LowerInstrumentCommandRate();
                break;
            case 0x0B:
                v = ReadCurrentInstrumentByte(2);
                --v;
                WriteCurrentInstrumentByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(0x0B) - 1;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(0x0B, v);
                break;
            case 0x0E:
                PreviousInstrumentTableMode();
                break;
            case 0x0F:
                PreviousInstrumentTable();
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                v = ReadCurrentInstrumentByte(1) & 0x60;
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
                WriteCurrentInstrumentByte(1, v);
                break;
            case 3:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 4:
                PreviousVibratoShape();
                break;
            case 9:
                v = ReadCurrentInstrumentByte(3);
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
                WriteCurrentInstrumentByte(3, v);
                break;
            case 8:
                v = ReadCurrentInstrumentByte(9);
                if (v) {
                    if (v > 4)
                        v = 0;
                    else
                        --v;
                    WriteCurrentInstrumentByte(9, v);
                    DrawString(PlayModeNames + v * 9);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPosition();
                    DrawWaveSynthField();
                }
                break;
            case 0x0B:
                v = GetWaveLength();
                if (v & 0x0F) {
                    --v;
                    SetInstrumentByte10(v);
                    DrawByte(v);
                    if (v < GetWaveLoopPosition())
                        SetInstrumentByte2(v);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveLoopPosition();
                }
                break;
            case 0x0C:
                v = GetWaveLoopPosition();
                if (v < GetWaveLength()) {
                    ++v;
                    SetInstrumentByte2(v);
                    DrawWaveLoopPosition();
                }
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v) {
                    --v;
                    SetInstrumentByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 5:
                SetInstrumentTransposeOn();
                break;
            case 7:
                LowerInstrumentCommandRate();
                break;
            case 6:
                v = ReadCurrentInstrumentByte(0x0C) - 1;
                if (v != 0x7F) {
                    WriteCurrentInstrumentByte(0x0C, v);
                    DrawWaveFinetune();
                }
                break;
            case 0x0D:
                PreviousInstrumentTableMode();
                break;
            case 0x0E:
                PreviousInstrumentTable();
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 2:
            case 3:
                DrawKitNameField(NextKit());
                break;
            case 4:
                v = ReadCurrentInstrumentByte(1) & 0x60;
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
                WriteCurrentInstrumentByte(1, v);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 7:
                v = ReadCurrentInstrumentByte(8);
                --v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(8, v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(2);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(2, v);
                } else {
                    v = ReadCurrentInstrumentByte(5);
                    v = (int)(char)v & (int)0xBF;
                    DrawString("OFF");
                    WriteCurrentInstrumentByte(5, v);
                }
                break;
            case 0x0D:
                v = ReadCurrentInstrumentByte(9);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(9, v);
                } else {
                    v = ReadCurrentInstrumentByte(5);
                    v = (int)(char)v & (int)0xDF;
                    DrawString("OFF");
                    WriteCurrentInstrumentByte(5, v);
                }
                break;
            case 8:
            case 9:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v)
                    --v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case 0x0B:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if (v)
                    --v;
                if (v) {
                    DrawStringTile20();
                    DrawHexByte(v);
                } else
                    DrawString("ALL");
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case 0x0E:
                v = ReadCurrentInstrumentByte(2) | 0x40;
                WriteCurrentInstrumentByte(2, v);
                DrawString("0.5");
                break;
            case 0x0F:
                v = ReadCurrentInstrumentByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    --v;
                    WriteCurrentInstrumentByte(0x0A, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                PreviousVibratoShape();
                break;
            case 0x10:
                PreviousInstrumentTableMode();
                break;
            case 0x11:
                PreviousInstrumentTable();
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
                LowerInstrumentFieldLow(1);
                break;
            case 3:
                LowerInstrumentFieldLow(9);
                break;
            case 4:
                LowerInstrumentFieldLow(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, v);
                break;
            case 6:
                LowerInstrumentLength();
                break;
            case 8:
                if (!wButtonsSame)
                    PreviousVibratoShape();
                break;
            case 7:
                WriteCurrentInstrumentByte(2, 0);
                DrawString("FREE");
                break;
            case 9:
                SetInstrumentTransposeOn();
                break;
            case 0x0A:
                LowerInstrumentCommandRate();
                break;
            case 0x0B:
                PreviousInstrumentTableMode();
                break;
            case 0x0C:
                PreviousInstrumentTable();
            }
        }
        return 1;
    case 0x14:
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
                RaiseInstrumentFieldHigh(1);
                break;
            case 3:
                RaiseInstrumentFieldHigh(9);
                break;
            case 4:
                RaiseInstrumentFieldHigh(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrumentByte(7, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case 6:
                v = ReadCurrentInstrumentByte(7);
                v |= 3;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, 3);
                break;
            case 7:
                RaiseInstrumentLengthBig();
                break;
            case 8:
                v = ReadCurrentInstrumentByte(4);
                if ((v & 0x70) != 0x70)
                    v += 0x10;
                WriteCurrentInstrumentByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                PreviousInstrumentPitchMode();
                break;
            case 0x0B:
                v = ReadCurrentInstrumentByte(2);
                v += 0x0C;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(2, v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(0x0B) + 0x10;
                WriteCurrentInstrumentByte(0x0B, v);
                DrawHexByte(v);
                break;
            case 0x0E:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case 0x0F:
                AddInstrumentTable16();
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                DrawStringIndexed(0x33);
                WriteCurrentInstrumentByte(1, 0x20);
                break;
            case 3:
                WriteCurrentInstrumentByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 4:
                PreviousInstrumentPitchMode();
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0xEF) {
                    v += 0x10;
                    SetInstrumentByte11(v);
                    DrawHexByte(v + 1);
                }
                break;
            case 9:
                if (WavePlayIsManual()) {
                    v = ReadCurrentInstrumentByte(3);
                    if (v < 0xF0) {
                        wVramCursor--;
                        v += 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrumentByte(3, v);
                    }
                }
                break;
            case 0x0B:
                v = 0x0F - GetWaveLength();
                SetInstrumentByte10(0x0F);
                DrawStringIndexed(0x46);
                v = GetWaveLoopPosition() + v;
                if (v & 0xF0)
                    v = 0x0F;
                SetInstrumentByte2(v);
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
                DrawWaveLoopPosition();
                break;
            case 0x0C:
                SetInstrumentByte2(0);
                DrawWaveLoopPosition();
                break;
            case 6:
                v = ReadCurrentInstrumentByte(0x0C) + 0x10;
                if ((v & 0xF0) == 0x80)
                    v = 0x7F;
                WriteCurrentInstrumentByte(0x0C, v);
                DrawWaveFinetune();
                break;
            case 0x0D:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case 0x0E:
                AddInstrumentTable16();
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 2:
            case 3: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = PreviousKit();
                DrawKitNameField(kit);
                break;
            }
            case 4:
                DrawStringIndexed(0x33);
                WriteCurrentInstrumentByte(1, 0x20);
                break;
            case 5:
                WriteCurrentInstrumentByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 7:
                v = ReadCurrentInstrumentByte(8) + 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case (int)0x0B:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurrentInstrumentByte(0x0A);
                if (v != 0xD3 || !wDefaultNotes) {
                    if (v > 0xC2 && v < 0xD3)
                        v = 0xD3;
                    else
                        v += 0x10;
                    WriteCurrentInstrumentByte(0x0A, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                PreviousInstrumentPitchMode();
                break;
            case 0x10:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case 0x11:
                AddInstrumentTable16();
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
                RaiseInstrumentFieldHigh(1);
                break;
            case 3:
                RaiseInstrumentFieldHigh(9);
                break;
            case 4:
                RaiseInstrumentFieldHigh(0x0A);
                break;
            case 5:
                WriteCurrentInstrumentByte(7, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case 6:
                RaiseInstrumentLengthBig();
                break;
            case 0x0B:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case 0x0C:
                AddInstrumentTable16();
            }
        }
        return 1;
    case 0x18:
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case 0:
            switch (wInstrumentField) {
            case 2:
                LowerInstrumentFieldHigh(1);
                break;
            case 3:
                LowerInstrumentFieldHigh(9);
                break;
            case 4:
                LowerInstrumentFieldHigh(0x0A);
                break;
            case 5:
                v = ReadCurrentInstrumentByte(7);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrumentByte(7, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case 6:
                v = ReadCurrentInstrumentByte(7) & 0xFC;
                WriteCurrentInstrumentByte(7, v);
                DrawFieldValue(0x0C, 0);
                break;
            case 7:
                LowerInstrumentLengthBig();
                break;
            case 8:
                v = ReadCurrentInstrumentByte(4);
                if (v & 0xF0)
                    v -= 0x10;
                WriteCurrentInstrumentByte(4, v);
                DrawHexByte(v);
                break;
            case 9:
                NextInstrumentPitchMode();
                break;
            case 0x0B:
                v = ReadCurrentInstrumentByte(2);
                v -= 0x0C;
                WriteCurrentInstrumentByte(2, v);
                DrawHexByte(v);
                break;
            case 0x0C:
                v = ReadCurrentInstrumentByte(0x0B) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(0x0B, v);
                break;
            case 0x0E:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case 0x0F:
                SubInstrumentTable16();
            }
            break;
        case 1:
            switch (wInstrumentField) {
            case 2:
                DrawStringIndexed(0x30);
                WriteCurrentInstrumentByte(1, 0);
                break;
            case 3:
                WriteCurrentInstrumentByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 4:
                NextInstrumentPitchMode();
                break;
            case 0x0B:
                SetInstrumentByte10(0);
                DrawStringIndexed(0x30);
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
            case 0x0C:
                SetInstrumentByte2(GetWaveLength());
                DrawWaveLoopPosition();
                break;
            case 0x0A:
                v = GetWaveSpeed();
                if (v < 0x10)
                    v = 0;
                else
                    v -= 0x10;
                SetInstrumentByte11(v);
                DrawHexByte(v + 1);
                break;
            case 9:
                if (WavePlayIsManual()) {
                    v = ReadCurrentInstrumentByte(3);
                    if (v >= 0x10) {
                        wVramCursor--;
                        v -= 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrumentByte(3, v);
                    }
                }
                break;
            case 6:
                v = ReadCurrentInstrumentByte(0x0C) - 0x10;
                if ((v & 0xF0) == 0x70)
                    v = 0x80;
                WriteCurrentInstrumentByte(0x0C, v);
                DrawWaveFinetune();
                break;
            case 0x0D:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case 0x0E:
                SubInstrumentTable16();
            }
            break;
        case 2:
            switch (wInstrumentField) {
            case 2:
            case 3: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = NextKit();
                DrawKitNameField(kit);
                break;
            }
            case 4:
                DrawStringIndexed(0x30);
                WriteCurrentInstrumentByte(1, 0);
                break;
            case 5:
                WriteCurrentInstrumentByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 7:
                v = ReadCurrentInstrumentByte(8) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(8, v);
                break;
            case 8:
            case (int)9:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case 0x0A:
            case (int)0x0B:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                if (v) {
                    DrawStringTile20();
                    DrawHexByte(v);
                } else
                    DrawString("ALL");
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case 0x0F:
                v = ReadCurrentInstrumentByte(0x0A);
                if (v != 0xD0 || !wDefaultNotes) {
                    if (v > 0xD0 && v < 0xE0)
                        v = 0xD0;
                    else
                        v -= 0x10;
                    WriteCurrentInstrumentByte(0x0A, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case 6:
                NextInstrumentPitchMode();
                break;
            case 0x10:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case 0x11:
                SubInstrumentTable16();
            }
            break;
        case 3:
            switch (wInstrumentField) {
            case 2:
                LowerInstrumentFieldHigh(1);
                break;
            case 3:
                LowerInstrumentFieldHigh(9);
                break;
            case 4:
                LowerInstrumentFieldHigh(0x0A);
                break;
            case 5:
                WriteCurrentInstrumentByte(7, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case 6:
                LowerInstrumentLengthBig();
                break;
            case 0x0B:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case 0x0C:
                SubInstrumentTable16();
            }
        }
        return 1;
    case 0x10:
        if (wAPressEnabled) {
            if (!wInstrumentField) {
                wNameEditIsInstrument = 1;
                FarCall(0x1B, FAR_FN(OpenNameEditor));
            } else if (wDrawStep++ == 1) {
                if (CheckInstrumentFieldOption(type)) {
                    SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
                    AllocInstrumentTable();
                } else if (type == 1 && wInstrumentField == 9 && !WavePlayIsManual()) {
                    AllocInstrumentSynth();
                    return 1;
                }
                FarCallArgument(5, ShowInstrumentFieldHelp, type);
            } else if (wDrawStep == 1 && CheckInstrumentFieldOption(type) && !IsInstrumentTableOn()) {
                SetInstrumentTableOn();
                SetVramCursor(InstrumentFieldX[type][wInstrumentField - 1], InstrumentFieldY[type][wInstrumentField - 1]);
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
                FarCall(0x1F, DrawInstrumentTableMode);
                wVramCursor++;
                hBgDigitPalette = wHighlightDigitPalette;
                hBgPalette = wHighlightPalette;
                FarCall(0x1F, DrawInstrumentTableField);
            }
        }
        return 1;
    case 0x21:
        NextUsedInstrument();
        return 1;
    case 0x22:
        PreviousUsedInstrument();
        return 1;
    case 0x24:
        wInstrument += 0x0F;
        if (wInstrument > 0x40)
            wInstrument = 0x40;
        NextUsedInstrument();
        return 1;
    case 0x28:
        PreviousInstrumentPage();
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        if (type == 1) {
            wSynth = ReadCurrentInstrumentByte(3);
            wSynthScreen = wSynth >> 4;
        }
        FarCall(5, MoveMapCursorUp);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorDown);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x41:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorRight);
        v = ReadCurrentInstrumentByte(6);
        if (v & 0x20)
            wTable = v & 0x1F;
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x50:
        if (!wSelectHeld)
            return 0;
        if (wInstrumentEditLatch)
            CloneSynthOrTable();
        else
            FarCall(1, FAR_FN(PasteInstrument));
        DrawInstrumentScreen();
        wSelectHeld = 0;
        return 1;
    case 0x70:
        wSelectHeld = 0;
        if (wInstrumentEditLatch) {
            CloneSynthOrTable();
            DrawInstrumentScreen();
        }
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case 0x60:
        if (!wSelectHeld)
            return 1;
        wSelectHeld = 0;
        FarCall(1, FAR_FN(CopyInstrument));
        wInstrumentEditLatch = 1;
        return 1;
    }
    return 0;
}
