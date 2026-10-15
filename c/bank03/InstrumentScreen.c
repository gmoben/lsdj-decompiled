/*
 * The instrument screen -- LSDj 9.4.0, ROM bank $03 (its field accessors,
 * cursor steppers and small draw helpers, in ROM order).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The screen edits the current instrument (wInstrument), a 16-byte record
 * of the working song read and written through ReadCurrentInstrumentByte /
 * WriteCurrentInstrumentByte (bank 0, c/bank00/InstrumentByte.c); constants/song.inc
 * has the per-type field layout (byte 5 = pitch flags, bit 5 transpose;
 * byte 6 = table on (bit 5) + table number (bits 4-0)).
 */

#define SONG_BYTE_PTR               /* this bank's ReadSongByte takes a pointer (CloneSynth) */
#include "lsdj.h"

extern UBYTE DefaultWaveFrameB3[];          /* $03:$4015, 16 bytes */
extern char TxtUnlim[];                     /* $03:$4025 "UNLIM" */
extern UBYTE InstrumentFieldX[][18];        /* $03:$402B, [type][field] cursor X */
extern UBYTE InstrumentFieldY[][18];        /* $03:$406E */
extern UBYTE InstrumentFieldW[][18];        /* $03:$40B1 */
extern UBYTE InstrumentFieldCounts[];       /* $03:$40F4, [4] fields per instrument type */
extern UBYTE wDefaultNotes;                 /* $C904 */
extern UBYTE wAHeld;                        /* $C40C */
extern UBYTE wNameEditIsInstrument;         /* $C430 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wInstrumentColumn;             /* $C428 */
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
void ShowInstrumentFieldHelp(void);         /* $05:$69BD */
void PasteInstrument(void);                 /* $01:$5503 */
void CopyInstrument(void);                  /* $01:$5253 */
void SpeechInstrumentInput(void);           /* $1F:$724D */
void MoveMapCursorLeft(void);               /* $05:$4233 */
void MoveMapCursorRight(void);              /* $05:$4253 */
void MoveMapCursorUp(void);                 /* $05:$426A */
void MoveMapCursorDown(void);               /* $05:$4287 */
void SoundToggleSongPlayback(void);         /* $02:$78C4 */
void SoundTogglePlayback(void);             /* $02:$7C4C */
void WriteCurrentInstrumentByteC(UBYTE field, char value); /* WriteCurrentInstrumentByte taking a char (aliased) */
void FarCallArgumentWC(UBYTE bank, UWORD function, char argument); /* FarCallArgument with a word target and a char argument (aliased) */
void FarCallArgumentW(UBYTE bank, UWORD function, UBYTE argument); /* FarCallArgument with a word target (aliased) */
void FarCallW(UBYTE bank, UWORD function);  /* FarCall with a word target (aliased) */
extern UBYTE wInstrumentFieldCount;         /* $C905 */
extern UBYTE wNameEditBuffer[];             /* $C431 */
extern char InstrumentTypeNames[];          /* $03:$4FC4, [4][6] */
void DrawSpeechInstrumentScreen(void);      /* $1F:$7108 */
void DrawAllChannelStatus(void);            /* $1F:$7072 */
void DrawPulseInstrument(void);             /* bank $1F */
void DrawWaveInstrument(void);
void DrawKitInstrument(void);
void DrawNoiseInstrument(void);
void CopyInstrumentName(void);              /* bank 1 */
UBYTE MulU8(UBYTE a, UBYTE b);

/* $4000: write `value` to byte `field` of the current instrument
   (wInstrument) */
void WriteCurrentInstrumentByte(UBYTE field, UBYTE value)
{
    WriteInstrumentByte(wInstrument, field, value);
}

/* $40F8: the wave PLAY mode: byte 9 (MANUAL, ONCE, LOOP, PINGPONG, RESYNC =
   PlayModeNames) */
UBYTE GetWavePlayMode(void)
{
    return ReadCurrentInstrumentByte(WAVE_BYTE_PLAY);
}

/* $4105: 1 if the wave PLAY mode is MANUAL (SPEED/LENGTH/LOOP POS then show
   dashes) */
UBYTE WavePlayIsManual(void)
{
    return GetWavePlayMode() == 0;
}

/* $4118: instrument byte 6 bit 5 (table on) as 0/1 */
UBYTE IsInstrumentTableOn(void)
{
    UBYTE r;

    if (ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE) & 0x20)
        r = 1;
    else
        r = 0;
    return r;
}

/* $4133: set instrument byte 6 bit 5 (table on) */
void SetInstrumentTableOn(void)
{
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE,
                               ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE) | 0x20);
}

/* $414F: return 1 if option-table[c] (ROM InstrumentFieldCounts) minus 1 ==
   current field wInstrumentField, else 0 */
UBYTE CheckInstrumentFieldOption(UBYTE type)
{
    return wInstrumentField == InstrumentFieldCounts[type] - 1;
}

/* $4170: draw the cursor highlight on field wInstrumentField (ColorField
   from InstrumentFieldX/Y/W[type][field], palette 3 CGB / 2) */
void DrawInstrumentCursor(void)
{
    UBYTE type;

    type = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) & 3;
    if (type == INSTRUMENT_WAVE && WavePlayIsManual() && wInstrumentField == WAVE_FIELD_WAVE) {
        ColorField(0x0B, 0x0C, 2, hGameboyType ? 3 : 2);
        return;
    }
    ColorField(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField],
                  InstrumentFieldW[type][wInstrumentField], hGameboyType ? 3 : 2);
}

/* $4257: write instrument byte 5 (pitch flags) and redraw its transpose
   ON/OFF (bank 1 DrawOnOffName) */
void SetInstrumentPitchByte(UBYTE v)
{
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
    FarCallArgument(1, FAR_FN(DrawOnOffName), v);
}

/* $427B: set instrument byte 5 bit 5 (transpose) via SetInstrumentPitchByte */
void SetInstrumentTransposeOn(void)
{
    SetInstrumentPitchByte(ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) | 0x20);
}

/* $4293: clear instrument byte 5 bit 5 (transpose) via
   SetInstrumentPitchByte */
void SetInstrumentTransposeOff(void)
{
    SetInstrumentPitchByte(ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) & 0xDF);
}

/* $42AB: 1 if synth n's 256-byte wave page (SRAM bank 3) differs from
   DefaultWaveFrameB3 repeated */
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

/* $42FF: first synth (0-15) whose waves are still default; else 'OUT OF
   SYNTHS!' message and $FF */
UBYTE FindFreeSynth(void)
{
    UBYTE synth;

    for (synth = 0; synth < 16; synth++) {
        if (!SynthWavesModified(synth))
            return synth;
    }
    strcpy(wStringBuffer, "OUT OF SYNTHS! CLEAN INSTR DATA IN PROJECT SCREEN");
    FarCall(1, DrawMessageBar);
    return 0xFF;
}

/* $436D: give the instrument the first free table (FarCallByte bank $1F
   FindFreeTable), switched on; SDCC bug clears byte 6's other bits */
void AllocateInstrumentTable(void)
{
    UBYTE t, v;

    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    t |= 0x20;
    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE, t | (v & 0xD0));
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
}

/* $43C0: A twice on a wave instrument's SYNTH field (PLAY not MANUAL): give
   it a free synth (FindFreeSynth), drawn and stored in byte 3 bits 7-4 */
void AllocateInstrumentSynth(void)
{
    UBYTE v;

    v = FindFreeSynth();
    if (v == 0xFF)
        return;
    SetVramCursor(InstrumentFieldX[1][9], InstrumentFieldY[1][9]);   /* WAVE: the synth field */
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    DrawByte(v);
    WriteCurrentInstrumentByte(WAVE_BYTE_WAVE, v << 4);
}

/* $441A: clone the current instrument's synth into a free synth slot (params
   + 256 wave bytes), then point the instrument at it */
void CloneSynth(void)
{
    UBYTE source, destination, offset;
    UWORD sourceAddress, destinationAddress;
    UBYTE v, w;

    source = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE) >> 4;
    destination = FindFreeSynth();
    if (destination == 0xFF)
        return;
    for (offset = 0; offset < 0x10; offset++) {
        v = ReadSongByte(1, SramSynthParameters + (UWORD)(source << 4) + (UWORD)offset);
        WriteSongByte(1, (UWORD)(destination << 4) + (UWORD)SRAM_SYNTH_PARAMETERS + (UWORD)offset,
                              v);
    }
    offset = 0;
    sourceAddress = source;
    sourceAddress <<= 8;
    sourceAddress |= 0xA000;
    destinationAddress = destination;
    destinationAddress <<= 8;
    destinationAddress |= 0xA000;
    do {
        w = ReadSongByte(3, (UBYTE *)sourceAddress++);
        WriteSongByte(3, destinationAddress++, w);
    } while (++offset);
    WriteCurrentInstrumentByte(WAVE_BYTE_WAVE,
            (ReadCurrentInstrumentByte(WAVE_BYTE_WAVE) & 0x0F) | (destination << 4));
    ShowMessage("CLONED SYNTH");
}

/* $45A7: shared instrument-screen frame draw (map row, tempo field, marker
   sprites) */
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
            if (ReadSongByte(1, SramInstrumentAllocation + (UWORD)wInstrument))
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
        if (wInstrument > INSTRUMENT_SPEECH)
            wInstrument = 0;
        wInstrument++;
        PreviousUsedInstrument();
    }
}

/* $4638: step wInstrument on to the next allocated instrument (stops at $40,
   speech), then redraw */
void NextUsedInstrument(void)
{
    wMessageStringPointer = 0;
    while (wInstrument != INSTRUMENT_SPEECH) {
        wInstrument++;
        if (ReadSongByte(1, SramInstrumentAllocation + (UWORD)wInstrument))
            break;
    }
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    DrawInstrumentScreen();
}

/* $4679: erase the cursor highlight on field wInstrumentField (palette 1) */
void EraseInstrumentCursor(void)
{
    UBYTE type, x, y, r;

    type = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) & 3;
    if (type == INSTRUMENT_WAVE && WavePlayIsManual() && wInstrumentField == WAVE_FIELD_WAVE) {
        ColorField(0x0B, 0x0C, 2, 1);
        return;
    }
    x = InstrumentFieldX[type][wInstrumentField];
    r = InstrumentFieldW[type][wInstrumentField] + x;
    y = InstrumentFieldY[type][wInstrumentField];
    ColorField(x, y, r - x, 1);
}

/* $4741: A+RIGHT on CMD/RATE: byte 8 one up (to F), redrawn */
void RaiseInstrumentCommandRate(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_COMMAND_RATE);
    if (v < 0x0F) {
        v++;
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_COMMAND_RATE, v);
        DrawByte(v);
    }
}

/* $4769: A+LEFT on CMD/RATE: byte 8 one down (to 0), redrawn */
void LowerInstrumentCommandRate(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_COMMAND_RATE);
    if (v) {
        v--;
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_COMMAND_RATE, v);
        DrawByte(v);
    }
}

/* $4790: bit 3 of the current instrument's flags byte; nothing calls it */
UBYTE GetInstrumentByte5Flag8(void)
{
    return ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) & 0x08;
}

/* $47A2: table mode down: clear byte 5 bit 3 (table step) if set, else table
   off; redraw */
void PreviousInstrumentTableMode(void)
{
    if (wButtonsSame)
        return;
    if (ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) & 0x08)
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS,
                                   ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) & 0xF7);
    else
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE,
                                   ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE) & 0xDF);
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableMode));
    wVramCursor++;
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
}

/* $482E: table mode up: table on, then set byte 5 bit 3 (table step); redraw */
void NextInstrumentTableMode(void)
{
    if (wButtonsSame)
        return;
    if (IsInstrumentTableOn())
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS,
                                   ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) | 0x08);
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

/* $489D: when the current instrument's table is on, its next table (up to
   $1F), and redraw the field */
void NextInstrumentTable(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
    if (v & 0x20) {
        t = v & 0x1F;
        if (t == 0x1F)
            return;
        v++;
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE, v);
        FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
    }
}

/* $48D6: A+LEFT on the TABLE field: the previous table number; at 0 (a fresh
   press) the mode steps back twice and the cursor leaves the field */
void PreviousInstrumentTable(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
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
            WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE, v);
            FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
        }
    }
}

/* $4924: table on, fresh press: set table-number bit 4 (+16) */
void AddInstrumentTable16(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
    if (v & 0x20) {
        if (!wButtonsSame) {
            v |= 0x10;
            WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE, v);
            FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
        }
    }
}

/* $495C: table on: clear table-number bit 4 (-16) */
void SubInstrumentTable16(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
    if (v & 0x20) {
        v &= 0xEF;
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE, v);
        FarCall(0x1F, FAR_FN(DrawInstrumentTableField));
    }
}

/* $498B: draw the wave LENGTH at $99CC ("-" when PLAY is MANUAL) */
void DrawWaveLength(void)
{
    wVramCursor = (UBYTE *)BG_MAP(12, 14);
    if (WavePlayIsManual())
        DrawStringIndexed('-');
    else
        DrawByte(GetWaveLength());
}

/* $49B7: draw the wave SPEED at $99AB ("--" when PLAY is MANUAL) */
void DrawWaveSpeed(void)
{
    wVramCursor = (UBYTE *)BG_MAP(11, 13);
    if (WavePlayIsManual())
        DrawString("--");                   /* the literal lands right after the function ($49E4) */
    else
        DrawHexByte(GetWaveSpeed() + 1);
}

/* $49E7: the wave LOOP POS as stored: F ^ byte 2 bits 3-0 */
UBYTE GetWaveLoopPosition(void)
{
    return 0x0F ^ (ReadCurrentInstrumentByte(WAVE_BYTE_LOOP_POSITION) & 0x0F);
}

/* $49FD: draw the wave LOOP POS at $99EC (LOOP, PINGPONG and RESYNC only;
   length less loop position, 0 if negative) */
void DrawWaveLoopPosition(void)
{
    UBYTE v, d;

    wVramCursor = (UBYTE *)BG_MAP(12, 15);
    v = GetWavePlayMode();
    if (v == 2 || v == 3 || v == 4) {
        d = GetWaveLength() - GetWaveLoopPosition();
        if (d & 0xF0)
            d = 0;
        DrawByte(d);
    } else
        DrawStringIndexed('-');
}

/* $4A44: draw the kit DIST mode: $d0-$d3 -> " HARD"/" SOFT"/" FOLD"/" WRAP" */
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

/* $4ACF: the wave SPEED: byte 11 + 3 */
UBYTE GetWaveSpeed(void)
{
    return ReadCurrentInstrumentByte(WAVE_BYTE_SPEED) + 3;
}

/* $4AE0: set the wave instrument's SPEED: byte 11 is the speed - 3
   (GetWaveSpeed) */
void SetWaveSpeed(UBYTE v)
{
    WriteCurrentInstrumentByte(WAVE_BYTE_SPEED, v - 3);
}

/* $4AF4: the wave LENGTH as shown: F - byte 10 bits 3-0 */
UBYTE GetWaveLength(void)
{
    return 0x0F - (ReadCurrentInstrumentByte(WAVE_BYTE_LENGTH) & 0x0F);
}

/* $4B0A: set the wave instrument's LENGTH: byte 10 holds $0F - length
   (GetWaveLength) */
void SetWaveLength(UBYTE v)
{
    if (v >= 0x10)
        DebugTrap();
    WriteCurrentInstrumentByte(WAVE_BYTE_LENGTH, 0x0F - v);
}

/* $4B29: set the wave instrument's LOOP POS: byte 2's low nibble, inverted
   (GetWaveLoopPosition) */
void SetWaveLoopPosition(UBYTE v)
{
    WriteCurrentInstrumentByte(WAVE_BYTE_LOOP_POSITION,
            0x0F ^ ((ReadCurrentInstrumentByte(WAVE_BYTE_LOOP_POSITION) & 0xF0) | v));
}

/* $4B4C: the PITCH field's vibrato shape: instrument byte 5 bits 2-0
   (sine/saw/square << 1, bit 0 = up) */
UBYTE GetVibratoShape(void)
{
    return ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS) & 0x07;
}

/* $4B5E: store the vibrato shape in instrument byte 5 bits 2-0 (v: sin/saw/
   sqr << 1 | up) and redraw the PITCH field. A signed v: the OR reads it
   through a cast that sits between the mask and the OR, which keeps the
   masked byte in c; the callees take a char for the same reason. */
void SetVibratoShape(char v)
{
    char t;

    t = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
    t &= 0xF8;
    v |= t;
    WriteCurrentInstrumentByteC(INSTRUMENT_BYTE_FLAGS, v);
    FarCallArgumentWC(1, ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) == INSTRUMENT_NOISE
                             ? (UWORD)DrawSpeedGlyph : (UWORD)DrawSpeedModeName, v);
}

/* $4BAC: A+RIGHT on PITCH: next vibrato shape (triangle, saw, square down,
   then up) */
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

/* $4BCB: A+LEFT on PITCH: previous vibrato shape */
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

/* $4BE9: read instrument byte 0 = type (0=PULSE 1=WAVE 2=KIT 3=NOISE) */
UBYTE GetInstrumentType3(void)
{
    return ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) - 2;
}

/* $4BF9: pitch mode up: byte 5 bits 4/6/7 (bit 7 = top), redraw via
   DrawSpeedModeName */
void NextInstrumentPitchMode(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
    if (v & 0x80)
        return;
    if (v & 0x10)
        v ^= 0x90;
    else if ((v & 0x40) && GetInstrumentType3())
        v ^= 0x40;
    else
        v |= 0x10;
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
}

/* $4C64: pitch mode down: byte 5 bits 4/6/7, redraw via DrawSpeedModeName */
void PreviousInstrumentPitchMode(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
    if (v & 0x80)
        v ^= 0x90;
    else if (v & 0x10) {
        if (GetInstrumentType3())
            v &= 0xAF;
        else
            v ^= 0x10;
    } else if (GetInstrumentType3())
        v |= 0x40;
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
    FarCallArgument(1, FAR_FN(DrawSpeedModeName), v);
}

/* $4CD9: draw the wave FINETUNE (byte 12) signed at $990A */
void DrawWaveFinetune(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(WAVE_BYTE_FINETUNE);
    wVramCursor = (UBYTE *)BG_MAP(10, 8);
    if (v & 0x80) {
        DrawStringIndexed('-');
        DrawHexByte(-v);
    } else {
        DrawStringTile20();
        DrawHexByte(v);
    }
}

/* $4D15: draw the WAVE/SYNTH field: 'WAVE ' + byte 3 (hex), or 'SYNTH' +
   byte 3 >> 4 */
void DrawWaveSynthField(void)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(0, 12);
    if (WavePlayIsManual()) {
        DrawString("WAVE ");
        wVramCursor = (UBYTE *)BG_MAP(11, 12);
        ColorField(0x0B, 0x0C, 1, 1);
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawHexByte(ReadCurrentInstrumentByte(WAVE_BYTE_WAVE));
    } else {
        DrawString("SYNTH");
        wVramCursor = (UBYTE *)BG_MAP(11, 12);
        ColorField(0x0B, 0x0C, 1, 0);
        DrawStringIndexed(' ');
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawByte(ReadCurrentInstrumentByte(WAVE_BYTE_WAVE) >> 4);
    }
}

/* $4DD7: after an envelope edit: UpdateInstrumentEnvelope (bank $1B) */
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
    UWORD function;

    if (wInstrument > INSTRUMENT_SPEECH)
        DebugTrap();
    if (wInstrument == INSTRUMENT_SPEECH) {
        FarCall(0x1F, FAR_FN(DrawSpeechInstrumentScreen));
        return;
    }
    FarCallArgumentW(0x1F, 0x492A, wInstrument);    /* ClaimInstrumentSlot */
    type = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE);
    if (type >= 4) {
        WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE, INSTRUMENT_PULSE);
        type = INSTRUMENT_PULSE;
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
    case INSTRUMENT_PULSE:
        function = (UWORD)DrawPulseInstrument;
        break;
    case INSTRUMENT_WAVE:
        function = (UWORD)DrawWaveInstrument;
        break;
    case INSTRUMENT_NOISE:
        function = (UWORD)DrawNoiseInstrument;
        break;
    case INSTRUMENT_KIT:
        function = (UWORD)DrawKitInstrument;
        break;
    }
    FarCallW(0x1F, function);
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

/* $4FDC: draw kit number (hex) + 6-char kit name (ReadKitName ->
   wSampleNoteString) */
void DrawKitNameField(UBYTE kit)
{
    ReadKitName(kit);
    DrawHexByte(kit + 1);
    DrawStringTile20();
    DrawString(wSampleNoteString);
}

/* $5002: write kit# into the field's low 6 bits (top flag bits preserved via
   $c0 mask) */
void WriteKitNumber(UBYTE byte, UBYTE kit)
{
    WORD t;

    t = (BYTE)ReadCurrentInstrumentByte(byte) & (WORD)0xC0;
    WriteCurrentInstrumentByte(byte, t | kit);
}

/* $503A: step to the next existing kit bank (KitExists scan), WriteKitNumber */
UBYTE NextKit(void)
{
    UBYTE byte, kit, original;

    byte = (wInstrumentField == KIT_FIELD_KIT1) ? 2 : 9;   /* the kit's instrument byte */
    kit = ReadCurrentInstrumentByte(byte) & 0x3F;
    original = kit;
    do {
        if (!kit)
            return original;
        kit--;
    } while (!KitExists(kit));
    WriteKitNumber(byte, kit);
    return kit;
}

/* $508C: step to the previous existing kit (bound $3f), WriteKitNumber */
UBYTE PreviousKit(void)
{
    UBYTE byte, kit, original;

    byte = (wInstrumentField == KIT_FIELD_KIT1) ? 2 : 9;   /* the kit's instrument byte */
    kit = ReadCurrentInstrumentByte(byte) & 0x3F;
    original = kit;
    do {
        if (kit == 0x3F)
            return original;
        kit++;
    } while (!KitExists(kit));
    WriteKitNumber(byte, kit);
    return kit;
}

/* $50DF: point either kit field of the current instrument (bytes 2 and 9)
   that names a missing kit at kit 0 */
void EnsureKitExists(void)
{
    UBYTE kit, field;

    for (kit = 0; kit < 2; kit++) {
        field = kit ? 2 : 9;
        if (!KitExists(ReadCurrentInstrumentByte(field) & 0x3F))
            WriteKitNumber(field, 0);
    }
}

/* $5128: write instrument byte 0 = type (0=PULSE 1=WAVE 2=KIT 3=NOISE) */
void SetInstrumentType(UBYTE type)
{
    UBYTE t, n;

    if (wButtonsSame)
        return;
    t = type;
    n = InstrumentFieldCounts[t];
    if (wInstrumentField >= n)
        wInstrumentField = n - 1;
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE, t);
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    if (t == 2)
        EnsureKitExists();
    DrawInstrumentScreen();
}

/* $5181: current field byte: +1 on the lo nibble (clamp $0f), redraw +
   InstrumentEnvelopeChanged */
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

/* $51BC: current field byte: -1 on the lo nibble (clamp 0) */
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

/* $51F4: current field byte: +$10 on the hi nibble (clamp $f0) */
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

/* $5231: current field byte: -$10 on the hi nibble */
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

/* $526B: A+RIGHT on LENGTH (byte 3, bit 6 = limited, shown as 3F - bits
   5-0): one longer, past 3F to UNLIM */
void RaiseInstrumentLength(void)
{
    UBYTE v;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH);
    if (v & 0x40) {
        v--;
        if (v == 0x3F)
            v = 0;
    }
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

/* $52BF: A+LEFT on LENGTH: one shorter, from UNLIM to 3F */
void LowerInstrumentLength(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH);
    if (!(v & 0x40)) {
        v &= 0x80;
        v += 0x40;
    } else {
        t = v & 0x3F;
        if (t != 0x3F)
            v++;
    }
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

/* $531C: A+UP on LENGTH: 16 longer, past 3F to UNLIM */
void RaiseInstrumentLengthBig(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH);
    if (v & 0x40) {
        t = v & 0x3F;
        if (t > 0x0F)
            v -= 0x10;
        else
            v = 0;
    }
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

/* $537A: A+DOWN on LENGTH: 16 shorter, from UNLIM to 30 */
void LowerInstrumentLengthBig(void)
{
    UBYTE v, t;

    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH);
    if (!(v & 0x40))
        v = 0x4F;
    else {
        t = v & 0x3F;
        if (t < 0x30)
            v += 0x10;
        else
            v |= 0x3F;
    }
    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_LENGTH, v);
    if (v & 0x40) {
        DrawString("   ");
        v = 0x3F - v;
        v &= 0x3F;
        DrawHexByte(v);
    } else
        DrawString(TxtUnlim);
}

/* $53DE: clone what the instrument screen's cursor names: the softsynth on a
   WAVE instrument's synth field, else the instrument's table
   (CloneInstrumentTable) */
void CloneSynthOrTable(void)
{
    UBYTE type;

    type = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) & 3;
    if (type == INSTRUMENT_WAVE && wInstrumentField == WAVE_FIELD_WAVE && !WavePlayIsManual())
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

    if (wInstrument == INSTRUMENT_SPEECH)
        return FarCallByte(0x1F, FAR_FN(SpeechInstrumentInput));
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    type = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE);
    if (wAPressEnabled)
        wInstrumentEditLatch = 0;
    if (wButtons != J_A)
        wDrawStep = 0;
    if (wButtons & J_A)
        WriteSongByte(1, (UWORD)wInstrument + (UWORD)SRAM_INSTRUMENT_ALLOCATION, 1);
    switch (wButtons) {
    case J_UP:
        if (!wInstrumentField)
            break;
        EraseInstrumentCursor();
        wInstrumentField--;
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
            case PULSE_FIELD_ENVELOPE2:
                wInstrumentField = 1;
                break;
            case PULSE_FIELD_ENVELOPE3:
                wInstrumentField -= 2;
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                wInstrumentField--;
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                wInstrumentField--;
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                wInstrumentField--;
            case WAVE_FIELD_LOOP_POSITION:
                switch (GetWavePlayMode()) {
                case 1:
                    wInstrumentField--;
                    break;
                case 0:
                    wInstrumentField -= 3;
                }
            }
            break;
        case INSTRUMENT_KIT:
            if (wInstrumentField == KIT_FIELD_TABLE_ADVANCE)
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
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
            case NOISE_FIELD_ENVELOPE2:
                wInstrumentField = 1;
                break;
            case NOISE_FIELD_ENVELOPE3:
                wInstrumentField -= 2;
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                wInstrumentField--;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case J_DOWN:
        EraseInstrumentCursor();
        if (InstrumentFieldCounts[type] - wInstrumentField - 1) {
            if (type == INSTRUMENT_WAVE)
                switch (wInstrumentField) {
                case INSTRUMENT_FIELD_TYPE:
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
            case INSTRUMENT_PULSE:
                switch (wInstrumentField) {
                case PULSE_FIELD_ENVELOPE2:
                case PULSE_FIELD_ENVELOPE3:
                    wInstrumentField = 5;
                    break;
                case PULSE_FIELD_TABLE_ADVANCE:
                    wInstrumentField++;
                }
                break;
            case INSTRUMENT_KIT:
                switch (wInstrumentField) {
                case KIT_FIELD_OUTPUT:
                case (int)KIT_FIELD_OFFSET2:
                case KIT_FIELD_LENGTH1:
                case (int)KIT_FIELD_LENGTH2:
                case KIT_FIELD_LOOP1:
                case (int)KIT_FIELD_LOOP2:
                case KIT_FIELD_TABLE_ADVANCE:
                    wInstrumentField++;
                    break;
                case KIT_FIELD_VOLUME:
                case KIT_FIELD_OFFSET1:
                    if (wInstrumentColumn)
                        wInstrumentField++;
                }
                break;
            case INSTRUMENT_NOISE:
                switch (wInstrumentField) {
                case NOISE_FIELD_ENVELOPE2:
                case NOISE_FIELD_ENVELOPE3:
                    wInstrumentField = 5;
                    break;
                case NOISE_FIELD_TABLE_ADVANCE:
                    wInstrumentField++;
                }
            }
        }
        DrawInstrumentCursor();
        return 1;
    case J_LEFT:
        EraseInstrumentCursor();
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE2:
            case PULSE_FIELD_ENVELOPE3:
            case PULSE_FIELD_TABLE:
                wInstrumentField--;
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_OUTPUT:
            case WAVE_FIELD_TABLE:
                wInstrumentField--;
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_OUTPUT:
            case (int)KIT_FIELD_OFFSET2:
            case (int)KIT_FIELD_LENGTH2:
            case (int)KIT_FIELD_LOOP2:
            case KIT_FIELD_TABLE:
                wInstrumentField--;
                wInstrumentColumn = 0;
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE2:
            case NOISE_FIELD_ENVELOPE3:
            case NOISE_FIELD_TABLE:
                wInstrumentField--;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case J_RIGHT:
        EraseInstrumentCursor();
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasSecond))
                    wInstrumentField++;
                break;
            case PULSE_FIELD_ENVELOPE2:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasThird))
                    wInstrumentField++;
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                wInstrumentField++;
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                wInstrumentField++;
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                wInstrumentField++;
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_VOLUME:
            case KIT_FIELD_OFFSET1:
            case KIT_FIELD_LENGTH1:
            case KIT_FIELD_LOOP1:
            case KIT_FIELD_TABLE_ADVANCE:
                wInstrumentField++;
                wInstrumentColumn = 1;
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasSecond))
                    wInstrumentField++;
                break;
            case NOISE_FIELD_ENVELOPE2:
                if (FarCallByteU(0x1F, InstrumentEnvelopeHasThird))
                    wInstrumentField++;
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                wInstrumentField++;
            }
        }
        DrawInstrumentCursor();
        return 1;
    case (J_A | J_RIGHT):
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        if (wInstrumentField == INSTRUMENT_FIELD_TYPE) {
            if (type < 3)
                SetInstrumentType(type + 1);
            return 1;
        }
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
                RaiseInstrumentFieldLow(1);
                break;
            case PULSE_FIELD_ENVELOPE2:
                RaiseInstrumentFieldLow(9);
                break;
            case PULSE_FIELD_ENVELOPE3:
                RaiseInstrumentFieldLow(0x0A);
                break;
            case PULSE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case PULSE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 1)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 2;
                }
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case PULSE_FIELD_LENGTH:
                RaiseInstrumentLength();
                break;
            case PULSE_FIELD_SWEEP:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_SWEEP);
                if (v < 0x7F)
                    ++v;
                WriteCurrentInstrumentByte(PULSE_BYTE_SWEEP, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_PITCH:
                if (!wButtonsSame)
                    NextVibratoShape();
                break;
            case PULSE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOff();
                break;
            case PULSE_FIELD_COMMAND_RATE:
                RaiseInstrumentCommandRate();
                break;
            case PULSE_FIELD_PU2_TRANSPOSE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE);
                ++v;
                WriteCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_FINETUNE) + 1;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(PULSE_BYTE_FINETUNE, v);
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                break;
            case PULSE_FIELD_TABLE:
                NextInstrumentTable();
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_VOLUME) & 0x60;
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
                WriteCurrentInstrumentByte(WAVE_BYTE_VOLUME, v);
                break;
            case WAVE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case WAVE_FIELD_PITCH:
                NextVibratoShape();
                break;
            case WAVE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE);
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
                WriteCurrentInstrumentByte(WAVE_BYTE_WAVE, v);
                break;
            case WAVE_FIELD_PLAY:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_PLAY);
                if (v != 4) {
                    if (v > 4)
                        v = 0;
                    else
                        ++v;
                    WriteCurrentInstrumentByte(WAVE_BYTE_PLAY, v);
                    DrawString(PlayModeNames + v * 9);
                    WriteCurrentInstrumentByte(WAVE_BYTE_WAVE,
                                               ReadCurrentInstrumentByte(WAVE_BYTE_WAVE) & 0xF0);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPosition();
                    DrawWaveSynthField();
                }
                break;
            case WAVE_FIELD_LENGTH:
                v = GetWaveLength();
                if (v != 0x0F) {
                    ++v;
                    SetWaveLength(v);
                    DrawByte(v);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveLoopPosition();
                }
                break;
            case WAVE_FIELD_LOOP_POSITION:
                v = GetWaveLoopPosition();
                if (v & 0x0F) {
                    --v;
                    SetWaveLoopPosition(v);
                    DrawWaveLoopPosition();
                }
                break;
            case WAVE_FIELD_SPEED:
                v = GetWaveSpeed();
                if (v < 0xFE) {
                    ++v;
                    SetWaveSpeed(v);
                    DrawHexByte(v + 1);
                }
                break;
            case WAVE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOff();
                break;
            case WAVE_FIELD_COMMAND_RATE:
                RaiseInstrumentCommandRate();
                break;
            case WAVE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_FINETUNE) + 1;
                if (v != 0x80) {
                    WriteCurrentInstrumentByte(WAVE_BYTE_FINETUNE, v);
                    DrawWaveFinetune();
                }
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                break;
            case WAVE_FIELD_TABLE:
                NextInstrumentTable();
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_KIT1:
            case KIT_FIELD_KIT2:
                DrawKitNameField(PreviousKit());
                break;
            case KIT_FIELD_VOLUME:
                v = ReadCurrentInstrumentByte(KIT_BYTE_VOLUME) & 0x60;
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
                WriteCurrentInstrumentByte(KIT_BYTE_VOLUME, v);
                break;
            case KIT_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case KIT_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(KIT_BYTE_FINETUNE);
                ++v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(KIT_BYTE_FINETUNE, v);
                break;
            case KIT_FIELD_LOOP1:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
                if (!(v & 64)) {
                    v |= 0x40;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
                } else {
                    v = ReadCurrentInstrumentByte(KIT_BYTE_KIT1);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrumentByte(KIT_BYTE_KIT1, v);
                }
                break;
            case KIT_FIELD_LOOP2:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
                if (!(v & 32)) {
                    v |= 0x20;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
                } else {
                    v = ReadCurrentInstrumentByte(KIT_BYTE_KIT2);
                    v |= 0x80;
                    DrawString("ATK");
                    WriteCurrentInstrumentByte(KIT_BYTE_KIT2, v);
                }
                break;
            case KIT_FIELD_OFFSET1:
            case KIT_FIELD_OFFSET2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v < 0xFF)
                    ++v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case KIT_FIELD_LENGTH1:
            case KIT_FIELD_LENGTH2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if (v < 0xFF)
                    ++v;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case KIT_FIELD_SPEED:
                v = ReadCurrentInstrumentByte(KIT_BYTE_KIT1) & 0xBF;
                WriteCurrentInstrumentByte(KIT_BYTE_KIT1, v);
                DrawString("  1");
                break;
            case KIT_FIELD_CLIP:
                v = ReadCurrentInstrumentByte(KIT_BYTE_CLIP);
                if (v != 0xD3 || !wDefaultNotes) {
                    ++v;
                    WriteCurrentInstrumentByte(KIT_BYTE_CLIP, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case KIT_FIELD_PITCH:
                NextVibratoShape();
                break;
            case KIT_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                break;
            case KIT_FIELD_TABLE:
                NextInstrumentTable();
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
                RaiseInstrumentFieldLow(1);
                break;
            case NOISE_FIELD_ENVELOPE2:
                RaiseInstrumentFieldLow(9);
                break;
            case NOISE_FIELD_ENVELOPE3:
                RaiseInstrumentFieldLow(0x0A);
                break;
            case NOISE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 1)
                    v = 3;
                else
                    v = 2;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case NOISE_FIELD_LENGTH:
                RaiseInstrumentLength();
                break;
            case NOISE_FIELD_VIBRATO_SHAPE:
                if (!wButtonsSame)
                    NextVibratoShape();
                break;
            case NOISE_FIELD_PITCH:
                WriteCurrentInstrumentByte(NOISE_BYTE_PITCH, 1);
                DrawString("SAFE");
                break;
            case NOISE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOff();
                break;
            case NOISE_FIELD_COMMAND_RATE:
                RaiseInstrumentCommandRate();
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                break;
            case NOISE_FIELD_TABLE:
                NextInstrumentTable();
            }
        }
        return 1;
    case (J_A | J_LEFT):
        if (wInstrumentField == INSTRUMENT_FIELD_TYPE) {
            if (type)
                SetInstrumentType(type - 1);
            return 1;
        }
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
                LowerInstrumentFieldLow(1);
                break;
            case PULSE_FIELD_ENVELOPE2:
                LowerInstrumentFieldLow(9);
                break;
            case PULSE_FIELD_ENVELOPE3:
                LowerInstrumentFieldLow(0x0A);
                break;
            case PULSE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case PULSE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 2)
                    v |= 3;
                else {
                    v &= 0xFC;
                    v |= 1;
                }
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case PULSE_FIELD_LENGTH:
                LowerInstrumentLength();
                break;
            case PULSE_FIELD_SWEEP:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_SWEEP);
                if (v)
                    --v;
                WriteCurrentInstrumentByte(PULSE_BYTE_SWEEP, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_PITCH:
                if (!wButtonsSame)
                    PreviousVibratoShape();
                break;
            case PULSE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOn();
                break;
            case PULSE_FIELD_COMMAND_RATE:
                LowerInstrumentCommandRate();
                break;
            case PULSE_FIELD_PU2_TRANSPOSE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE);
                --v;
                WriteCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_FINETUNE) - 1;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(PULSE_BYTE_FINETUNE, v);
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                break;
            case PULSE_FIELD_TABLE:
                PreviousInstrumentTable();
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_VOLUME) & 0x60;
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
                WriteCurrentInstrumentByte(WAVE_BYTE_VOLUME, v);
                break;
            case WAVE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case WAVE_FIELD_PITCH:
                PreviousVibratoShape();
                break;
            case WAVE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE);
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
                WriteCurrentInstrumentByte(WAVE_BYTE_WAVE, v);
                break;
            case WAVE_FIELD_PLAY:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_PLAY);
                if (v) {
                    if (v > 4)
                        v = 0;
                    else
                        --v;
                    WriteCurrentInstrumentByte(WAVE_BYTE_PLAY, v);
                    DrawString(PlayModeNames + v * 9);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveSpeed();
                    DrawWaveLength();
                    DrawWaveLoopPosition();
                    DrawWaveSynthField();
                }
                break;
            case WAVE_FIELD_LENGTH:
                v = GetWaveLength();
                if (v & 0x0F) {
                    --v;
                    SetWaveLength(v);
                    DrawByte(v);
                    if (v < GetWaveLoopPosition())
                        SetWaveLoopPosition(v);
                    hBgDigitPalette = wShadedDigitPalette;
                    hBgPalette = wShadedPalette;
                    DrawWaveLoopPosition();
                }
                break;
            case WAVE_FIELD_LOOP_POSITION:
                v = GetWaveLoopPosition();
                if (v < GetWaveLength()) {
                    ++v;
                    SetWaveLoopPosition(v);
                    DrawWaveLoopPosition();
                }
                break;
            case WAVE_FIELD_SPEED:
                v = GetWaveSpeed();
                if (v) {
                    --v;
                    SetWaveSpeed(v);
                    DrawHexByte(v + 1);
                }
                break;
            case WAVE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOn();
                break;
            case WAVE_FIELD_COMMAND_RATE:
                LowerInstrumentCommandRate();
                break;
            case WAVE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_FINETUNE) - 1;
                if (v != 0x7F) {
                    WriteCurrentInstrumentByte(WAVE_BYTE_FINETUNE, v);
                    DrawWaveFinetune();
                }
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                break;
            case WAVE_FIELD_TABLE:
                PreviousInstrumentTable();
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_KIT1:
            case KIT_FIELD_KIT2:
                DrawKitNameField(NextKit());
                break;
            case KIT_FIELD_VOLUME:
                v = ReadCurrentInstrumentByte(KIT_BYTE_VOLUME) & 0x60;
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
                WriteCurrentInstrumentByte(KIT_BYTE_VOLUME, v);
                break;
            case KIT_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case KIT_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(KIT_BYTE_FINETUNE);
                --v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(KIT_BYTE_FINETUNE, v);
                break;
            case KIT_FIELD_LOOP1:
                v = ReadCurrentInstrumentByte(KIT_BYTE_KIT1);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(KIT_BYTE_KIT1, v);
                } else {
                    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
                    v = (int)(char)v & (int)0xBF;
                    DrawString("OFF");
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
                }
                break;
            case KIT_FIELD_LOOP2:
                v = ReadCurrentInstrumentByte(KIT_BYTE_KIT2);
                if (v & 128) {
                    v &= 0x7F;
                    DrawString(" ON");
                    WriteCurrentInstrumentByte(KIT_BYTE_KIT2, v);
                } else {
                    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS);
                    v = (int)(char)v & (int)0xDF;
                    DrawString("OFF");
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_FLAGS, v);
                }
                break;
            case KIT_FIELD_OFFSET1:
            case KIT_FIELD_OFFSET2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v)
                    --v;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case KIT_FIELD_LENGTH1:
            case KIT_FIELD_LENGTH2:
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
            case KIT_FIELD_SPEED:
                v = ReadCurrentInstrumentByte(KIT_BYTE_KIT1) | 0x40;
                WriteCurrentInstrumentByte(KIT_BYTE_KIT1, v);
                DrawString("0.5");
                break;
            case KIT_FIELD_CLIP:
                v = ReadCurrentInstrumentByte(KIT_BYTE_CLIP);
                if (v != 0xD0 || !wDefaultNotes) {
                    --v;
                    WriteCurrentInstrumentByte(KIT_BYTE_CLIP, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case KIT_FIELD_PITCH:
                PreviousVibratoShape();
                break;
            case KIT_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                break;
            case KIT_FIELD_TABLE:
                PreviousInstrumentTable();
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
                LowerInstrumentFieldLow(1);
                break;
            case NOISE_FIELD_ENVELOPE2:
                LowerInstrumentFieldLow(9);
                break;
            case NOISE_FIELD_ENVELOPE3:
                LowerInstrumentFieldLow(0x0A);
                break;
            case NOISE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 3) == 2)
                    v = 3;
                else
                    v = 1;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, v);
                break;
            case NOISE_FIELD_LENGTH:
                LowerInstrumentLength();
                break;
            case NOISE_FIELD_VIBRATO_SHAPE:
                if (!wButtonsSame)
                    PreviousVibratoShape();
                break;
            case NOISE_FIELD_PITCH:
                WriteCurrentInstrumentByte(NOISE_BYTE_PITCH, 0);
                DrawString("FREE");
                break;
            case NOISE_FIELD_TRANSPOSE:
                SetInstrumentTransposeOn();
                break;
            case NOISE_FIELD_COMMAND_RATE:
                LowerInstrumentCommandRate();
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                break;
            case NOISE_FIELD_TABLE:
                PreviousInstrumentTable();
            }
        }
        return 1;
    case (J_A | J_UP):
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
                RaiseInstrumentFieldHigh(1);
                break;
            case PULSE_FIELD_ENVELOPE2:
                RaiseInstrumentFieldHigh(9);
                break;
            case PULSE_FIELD_ENVELOPE3:
                RaiseInstrumentFieldHigh(0x0A);
                break;
            case PULSE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if ((v & 0xC0) != 0xC0) {
                    v += 0x40;
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                    DrawPulseDuty(v >> 6);
                }
                break;
            case PULSE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                v |= 3;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, 3);
                break;
            case PULSE_FIELD_LENGTH:
                RaiseInstrumentLengthBig();
                break;
            case PULSE_FIELD_SWEEP:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_SWEEP);
                if ((v & 0x70) != 0x70)
                    v += 0x10;
                WriteCurrentInstrumentByte(PULSE_BYTE_SWEEP, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_PITCH:
                PreviousInstrumentPitchMode();
                break;
            case PULSE_FIELD_PU2_TRANSPOSE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE);
                v += 0x0C;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE, v);
                break;
            case PULSE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_FINETUNE) + 0x10;
                WriteCurrentInstrumentByte(PULSE_BYTE_FINETUNE, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case PULSE_FIELD_TABLE:
                AddInstrumentTable16();
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                DrawStringIndexed(0x33);
                WriteCurrentInstrumentByte(WAVE_BYTE_VOLUME, 0x20);
                break;
            case WAVE_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case WAVE_FIELD_PITCH:
                PreviousInstrumentPitchMode();
                break;
            case WAVE_FIELD_SPEED:
                v = GetWaveSpeed();
                if (v < 0xEF) {
                    v += 0x10;
                    SetWaveSpeed(v);
                    DrawHexByte(v + 1);
                }
                break;
            case WAVE_FIELD_WAVE:
                if (WavePlayIsManual()) {
                    v = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE);
                    if (v < 0xF0) {
                        wVramCursor--;
                        v += 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrumentByte(WAVE_BYTE_WAVE, v);
                    }
                }
                break;
            case WAVE_FIELD_LENGTH:
                v = 0x0F - GetWaveLength();
                SetWaveLength(0x0F);
                DrawStringIndexed(0x46);
                v = GetWaveLoopPosition() + v;
                if (v & 0xF0)
                    v = 0x0F;
                SetWaveLoopPosition(v);
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
                DrawWaveLoopPosition();
                break;
            case WAVE_FIELD_LOOP_POSITION:
                SetWaveLoopPosition(0);
                DrawWaveLoopPosition();
                break;
            case WAVE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_FINETUNE) + 0x10;
                if ((v & 0xF0) == 0x80)
                    v = 0x7F;
                WriteCurrentInstrumentByte(WAVE_BYTE_FINETUNE, v);
                DrawWaveFinetune();
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case WAVE_FIELD_TABLE:
                AddInstrumentTable16();
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_KIT1:
            case KIT_FIELD_KIT2: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = PreviousKit();
                DrawKitNameField(kit);
                break;
            }
            case KIT_FIELD_VOLUME:
                DrawStringIndexed(0x33);
                WriteCurrentInstrumentByte(KIT_BYTE_VOLUME, 0x20);
                break;
            case KIT_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case KIT_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(KIT_BYTE_FINETUNE) + 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(KIT_BYTE_FINETUNE, v);
                break;
            case KIT_FIELD_OFFSET1:
            case (int)KIT_FIELD_OFFSET2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case KIT_FIELD_LENGTH1:
            case (int)KIT_FIELD_LENGTH2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3);
                if ((v & 0xF0) != 0xF0)
                    v += 0x10;
                else
                    v = 0xFF;
                DrawStringTile20();
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0B : 3, v);
                break;
            case KIT_FIELD_CLIP:
                v = ReadCurrentInstrumentByte(KIT_BYTE_CLIP);
                if (v != 0xD3 || !wDefaultNotes) {
                    if (v > 0xC2 && v < 0xD3)
                        v = 0xD3;
                    else
                        v += 0x10;
                    WriteCurrentInstrumentByte(KIT_BYTE_CLIP, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case KIT_FIELD_PITCH:
                PreviousInstrumentPitchMode();
                break;
            case KIT_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case KIT_FIELD_TABLE:
                AddInstrumentTable16();
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
                RaiseInstrumentFieldHigh(1);
                break;
            case NOISE_FIELD_ENVELOPE2:
                RaiseInstrumentFieldHigh(9);
                break;
            case NOISE_FIELD_ENVELOPE3:
                RaiseInstrumentFieldHigh(0x0A);
                break;
            case NOISE_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 3);
                DrawFieldValue(0x0C, 3);
                break;
            case NOISE_FIELD_LENGTH:
                RaiseInstrumentLengthBig();
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                NextInstrumentTableMode();
                wVramCursor -= 7;
                NextInstrumentTableMode();
                break;
            case NOISE_FIELD_TABLE:
                AddInstrumentTable16();
            }
        }
        return 1;
    case (J_A | J_DOWN):
        SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1:
                LowerInstrumentFieldHigh(1);
                break;
            case PULSE_FIELD_ENVELOPE2:
                LowerInstrumentFieldHigh(9);
                break;
            case PULSE_FIELD_ENVELOPE3:
                LowerInstrumentFieldHigh(0x0A);
                break;
            case PULSE_FIELD_WAVE:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT);
                if (v & 0xC0) {
                    v -= 0x40;
                    WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                    DrawPulseDuty((v & 0xC0) >> 6);
                }
                break;
            case PULSE_FIELD_OUTPUT:
                v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT) & 0xFC;
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, v);
                DrawFieldValue(0x0C, 0);
                break;
            case PULSE_FIELD_LENGTH:
                LowerInstrumentLengthBig();
                break;
            case PULSE_FIELD_SWEEP:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_SWEEP);
                if (v & 0xF0)
                    v -= 0x10;
                WriteCurrentInstrumentByte(PULSE_BYTE_SWEEP, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_PITCH:
                NextInstrumentPitchMode();
                break;
            case PULSE_FIELD_PU2_TRANSPOSE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE);
                v -= 0x0C;
                WriteCurrentInstrumentByte(PULSE_BYTE_PU2_TRANSPOSE, v);
                DrawHexByte(v);
                break;
            case PULSE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(PULSE_BYTE_FINETUNE) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(PULSE_BYTE_FINETUNE, v);
                break;
            case PULSE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case PULSE_FIELD_TABLE:
                SubInstrumentTable16();
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME:
                DrawStringIndexed(0x30);
                WriteCurrentInstrumentByte(WAVE_BYTE_VOLUME, 0);
                break;
            case WAVE_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case WAVE_FIELD_PITCH:
                NextInstrumentPitchMode();
                break;
            case WAVE_FIELD_LENGTH:
                SetWaveLength(0);
                DrawStringIndexed(0x30);
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
            case WAVE_FIELD_LOOP_POSITION:
                SetWaveLoopPosition(GetWaveLength());
                DrawWaveLoopPosition();
                break;
            case WAVE_FIELD_SPEED:
                v = GetWaveSpeed();
                if (v < 0x10)
                    v = 0;
                else
                    v -= 0x10;
                SetWaveSpeed(v);
                DrawHexByte(v + 1);
                break;
            case WAVE_FIELD_WAVE:
                if (WavePlayIsManual()) {
                    v = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE);
                    if (v >= 0x10) {
                        wVramCursor--;
                        v -= 0x10;
                        DrawHexByte(v);
                        WriteCurrentInstrumentByte(WAVE_BYTE_WAVE, v);
                    }
                }
                break;
            case WAVE_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(WAVE_BYTE_FINETUNE) - 0x10;
                if ((v & 0xF0) == 0x70)
                    v = 0x80;
                WriteCurrentInstrumentByte(WAVE_BYTE_FINETUNE, v);
                DrawWaveFinetune();
                break;
            case WAVE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case WAVE_FIELD_TABLE:
                SubInstrumentTable16();
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_KIT1:
            case KIT_FIELD_KIT2: {
                UBYTE kit;

                for (v = 0x10; v; --v)
                    kit = NextKit();
                DrawKitNameField(kit);
                break;
            }
            case KIT_FIELD_VOLUME:
                DrawStringIndexed(0x30);
                WriteCurrentInstrumentByte(KIT_BYTE_VOLUME, 0);
                break;
            case KIT_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case KIT_FIELD_FINETUNE:
                v = ReadCurrentInstrumentByte(KIT_BYTE_FINETUNE) - 0x10;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(KIT_BYTE_FINETUNE, v);
                break;
            case KIT_FIELD_OFFSET1:
            case (int)KIT_FIELD_OFFSET2:
                v = ReadCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C);
                if (v & 0xF0)
                    v -= 0x10;
                else
                    v = 0;
                DrawHexByte(v);
                WriteCurrentInstrumentByte(wInstrumentColumn ? 0x0D : 0x0C, v);
                break;
            case KIT_FIELD_LENGTH1:
            case (int)KIT_FIELD_LENGTH2:
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
            case KIT_FIELD_CLIP:
                v = ReadCurrentInstrumentByte(KIT_BYTE_CLIP);
                if (v != 0xD0 || !wDefaultNotes) {
                    if (v > 0xD0 && v < 0xE0)
                        v = 0xD0;
                    else
                        v -= 0x10;
                    WriteCurrentInstrumentByte(KIT_BYTE_CLIP, v);
                    DrawKitDistortionName(v);
                    wDefaultNotes = 3;
                } else if (wAHeld) {
                    wDefaultNotes--;
                    wAHeld = 0;
                }
                break;
            case KIT_FIELD_PITCH:
                NextInstrumentPitchMode();
                break;
            case KIT_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case KIT_FIELD_TABLE:
                SubInstrumentTable16();
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1:
                LowerInstrumentFieldHigh(1);
                break;
            case NOISE_FIELD_ENVELOPE2:
                LowerInstrumentFieldHigh(9);
                break;
            case NOISE_FIELD_ENVELOPE3:
                LowerInstrumentFieldHigh(0x0A);
                break;
            case NOISE_FIELD_OUTPUT:
                WriteCurrentInstrumentByte(INSTRUMENT_BYTE_OUTPUT, 0);
                DrawFieldValue(0x0C, 0);
                break;
            case NOISE_FIELD_LENGTH:
                LowerInstrumentLengthBig();
                break;
            case NOISE_FIELD_TABLE_ADVANCE:
                PreviousInstrumentTableMode();
                wVramCursor -= 7;
                PreviousInstrumentTableMode();
                break;
            case NOISE_FIELD_TABLE:
                SubInstrumentTable16();
            }
        }
        return 1;
    case J_A:
        if (wAPressEnabled) {
            if (!wInstrumentField) {
                wNameEditIsInstrument = 1;
                FarCall(0x1B, FAR_FN(OpenNameEditor));
            } else if (wDrawStep++ == 1) {
                if (CheckInstrumentFieldOption(type)) {
                    SetVramCursor(InstrumentFieldX[type][wInstrumentField], InstrumentFieldY[type][wInstrumentField]);
                    AllocateInstrumentTable();
                } else if (type == INSTRUMENT_WAVE && wInstrumentField == WAVE_FIELD_WAVE
                        && !WavePlayIsManual()) {
                    AllocateInstrumentSynth();
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
    case (J_B | J_RIGHT):
        NextUsedInstrument();
        return 1;
    case (J_B | J_LEFT):
        PreviousUsedInstrument();
        return 1;
    case (J_B | J_UP):
        wInstrument += 0x0F;
        if (wInstrument > INSTRUMENT_SPEECH)
            wInstrument = 0x40;
        NextUsedInstrument();
        return 1;
    case (J_B | J_DOWN):
        PreviousInstrumentPage();
        return 1;
    case (J_SELECT | J_UP):
        if (wButtonsSame)
            break;
        if (type == INSTRUMENT_WAVE) {
            wSynth = ReadCurrentInstrumentByte(WAVE_BYTE_WAVE);
            wSynthScreen = wSynth >> 4;
        }
        FarCall(5, MoveMapCursorUp);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case (J_SELECT | J_DOWN):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorDown);
        FarCall(4, FAR_FN(RedrawScreen));
        break;
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_RIGHT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorRight);
        v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
        if (v & 0x20)
            wTable = v & 0x1F;
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_A):
        if (!wSelectHeld)
            return 0;
        if (wInstrumentEditLatch)
            CloneSynthOrTable();
        else
            FarCall(1, FAR_FN(PasteInstrument));
        DrawInstrumentScreen();
        wSelectHeld = 0;
        return 1;
    case (J_SELECT | J_B | J_A):
        wSelectHeld = 0;
        if (wInstrumentEditLatch) {
            CloneSynthOrTable();
            DrawInstrumentScreen();
        }
        return 1;
    case (J_SELECT | J_START):
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case (J_SELECT | J_B):
        if (!wSelectHeld)
            return 1;
        wSelectHeld = 0;
        FarCall(1, FAR_FN(CopyInstrument));
        wInstrumentEditLatch = 1;
        return 1;
    }
    return 0;
}
