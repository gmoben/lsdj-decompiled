/*
 * Small UI helpers -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
extern volatile sfr UBYTE LCDC_REG;         /* $FF40 */
extern volatile sfr UBYTE BGP_REG;          /* $FF47 */
extern volatile sfr UBYTE OBP0_REG;         /* $FF48 */
extern volatile sfr UBYTE NR50_REG;         /* $FF24 */
extern volatile sfr UBYTE NR51_REG;         /* $FF25 */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE hSoundChannel;         /* $FF8B */
extern UBYTE wColorSet;
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wQuickMarking;             /* $C412 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wClipboardBuffer[];               /* $C57A, [252] */
extern UBYTE wClipboardSave[];              /* $C7C6, [252] */
void ResetWorkState(void);                  /* bank 0 */
extern UBYTE wBootHardwareId;                     /* $CBD9 */
extern UBYTE wNameEditBuffer[];                /* $C431, [5] + NUL */
extern UBYTE wPlayMinutes;                  /* $C552 */
extern UBYTE wPlaySeconds;                  /* $C553 */
extern UBYTE wPlayFrames;                   /* $C6A8 */
extern volatile UBYTE wProjectDrawScratch;     /* $C9A7; volatile: the ROM re-reads it for the `++` */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wMessageNoPause;                  /* $C530 */
UBYTE CheckRomChecksum(void);               /* $00:$1FD5 */
UBYTE TestSramLo(void);
void initrand(UWORD seed);              /* bank 0 */
UBYTE randw(void);                       /* bank 0, a 16-bit LCG; the SRAM test uses the low byte */
extern UBYTE wFileNameScratch[];
extern UWORD ColorSetPalettes[];            /* $01:$56A7, [sets][20] */
void set_bkg_palette(UBYTE first, UBYTE nb, UWORD *rgb);   /* GBDK */
void LoadObjPalette(UBYTE first, UBYTE nb, UWORD *rgb);
extern UBYTE wNotePeriodLo[];               /* $CD28 */
extern UBYTE wNotePeriods[];                /* $CF28 */
extern UBYTE NoteFrequencyTable[];               /* $01:$40FA, DMG */
extern UBYTE NoteFrequencyDeltaTable[];          /* $01:$41D2, CGB */
extern UBYTE PitchCurve[];                  /* $01:$42AA, DMG */
extern UBYTE PitchCurveCgb[];               /* $01:$4382, CGB */

void ApplyColorSet(void);
void ClearOamBuffer(void);
void LoadHardwareTables(void);
void ClearSramWindow(void);                 /* $01:$7DE9, asm: zero the SRAM window */
void ClearBookmarks(void);                  /* $07:$643D */
extern UBYTE DefaultWaveFrameB1[];          /* $01:$40EA, [16] */
void WriteVramByteSafeP(UBYTE *addr, UBYTE v);   /* WriteVramByteSafe with a pointer address (aliased) */
extern UBYTE wOamBuffer[];                  /* $DD00, the OAM shadow: 40 sprites x {y, x, tile, prop} */
extern volatile UBYTE wOamDirty;            /* $CBD1 */

typedef struct { UBYTE y, x, tile, prop; } OamEntry;
#define OamShadow ((OamEntry *)0xDD00)      /* the OAM shadow as GBDK's sprite records */
UBYTE TestSram(void);                       /* bank 0 */
UBYTE *GetScreenPalette(void);
void CopyScreenMapCgb(UWORD s);              /* asm */
void CopyScreenMapDmg(UBYTE *s);             /* asm */

void SyncLoadSetting(void);                 /* bank 0 */
void LoadFontTiles(void);              /* bank $1E */
void SetSyncSendRate(void);                 /* bank 7 */
void LoadFileListState(void);               /* bank $1C */
void FileUpgradeSong(void);                 /* bank $1C */
void DrawKeyboardInstrumentState(void);               /* bank 7 */
extern char *CommandHelpPointers[];            /* $01:$776E */
extern char TxtHelpBNote[];                 /* $01:$6FDB "B: MAYBE PLAY NOTE; ..." */
extern char TxtHelpBHop[];                  /* $01:$7016 "B: MAYBE HOP; ..." */
extern char TxtHelpFTune[];                 /* $01:$7123 "F: FINETUNE ..." */
extern char TxtHelpFOffset[];               /* $01:$7143 "F: KIT/SYNTH OFFSET; ..." */
extern char TxtHelpHPhrase[];               /* $01:$718D "H0X: HOP TO NEXT PHRASE; ..." */
extern char TxtHelpHTable[];                /* $01:$71EF "H: HOP TO STEP; ..." */
extern char TxtHelpWShort[];                /* $01:$7411 "W: WAVE" */
extern char TxtHelpW[];                     /* $01:$7419 "W: WAVE [SPEED,LENGTH] 0=NO CHANGE" */
extern char TxtHelpSKit[];                  /* $01:$7796 "S: MOVE KIT LOOP POINTS; ..." */
extern char TxtHelpSNoise[];                /* $01:$77DF "S: ADD TO NOISE SHAPE; ..." */
extern char TxtHelpSSweep[];                /* $01:$780D "S: SET PU1 SWEEP" */
extern char wTopBarString[];                   /* $C555 */
extern char *wTopBarDrawScratch;            /* $C7AD, 16-bit scratch */
extern char TxtScreenLive[];                /* $01, "LIVE" */
extern char TxtScreenSong[];                /* $01, "SONG" */
extern char TxtScreenChain[];               /* $01, "CHAIN " */
extern char TxtScreenPhrase[];              /* $01, "PHRASE " */
extern char TxtScreenTable[];               /* $01, "TABLE " */
extern char TxtScreenProject[];             /* $01, "PROJECT " */
extern char TxtScreenFile[];                /* $01, "FILE " */
extern char TxtScreenInstr[];               /* $01, "INSTR " */
extern char TxtScreenWave[];                /* $01, "WAVE " */
extern char TxtScreenSynth[];               /* $01, "SYNTH " */
extern char TxtScreenGroove[];              /* $01, "GROOVE " */
int StringLength(const char *s);            /* bank 0 */
UBYTE CheckFileMagic64(void);               /* $1C:$4057 */
void DrawChannelHeader(void);               /* bank 0 */

extern UBYTE ScreenPaletteChain[];              /* $01:$57BF */
extern UBYTE ScreenPaletteFile[];               /* $01:$6B6F */
extern UBYTE ScreenPaletteGroove[];             /* $01:$5BF7 */
extern UBYTE ScreenPaletteInstrumentKit[];           /* $01:$62FF */
extern UBYTE ScreenPaletteInstrumentNoise[];         /* $01:$6467 */
extern UBYTE ScreenPaletteInstrumentPulse[];         /* $01:$602F */
extern UBYTE ScreenPaletteInstrumentWave[];          /* $01:$6197 */
extern UBYTE ScreenPalettePhrase[];             /* $01:$5A8F */
extern UBYTE ScreenPaletteProject[];            /* $01:$5D5F */
extern UBYTE ScreenPaletteSong[];               /* $01:$5EC7 */
extern UBYTE ScreenPaletteSpeech[];             /* $01:$65CF */
extern UBYTE ScreenPaletteSynth[];              /* $01:$689F */
extern UBYTE ScreenPaletteTable[];              /* $01:$5927 */
extern UBYTE ScreenPaletteWave[];               /* $01:$6737 */
extern UBYTE ScreenPaletteWord[];               /* $01:$6A07 */
extern UBYTE TxtBlankString2[];             /* $01:$7498, 20 zero attribute bytes */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wColumnInstrument[];              /* $C468, [4] */
extern UBYTE wFont;                         /* $C52E */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wKeyDelay;                     /* $C52B */
extern UBYTE wKeyDelayTimer;                /* $C406 */
extern UBYTE wKeyRepeat;                    /* $C52C */
extern UBYTE wKeyRepeatTimer;               /* $C407 */
extern UBYTE wSongTranspose;                /* $C444 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wTable;                        /* $C41B */
extern UWORD wTopBarLength;                    /* $C7AD, wTopBarDrawScratch as a word (aliased by match_function.py) */
extern UWORD wVramCursorW;                  /* $C573, wVramCursor as a plain word: its `< 0x9811` is a 32-bit compare (aliased by match_function.py) */

UBYTE TestSramLo(void)
{
    UBYTE i;
    UBYTE * volatile p;                     /* volatile: the ROM keeps p on the stack */
    char done;
    UBYTE v;
    char ok;

    initrand(0x1234);
    rRAMG = 0x0A;
    for (i = 0; i < 4; i++) {
        p = (UBYTE *)0xA000;
        rRAMB = i;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = randw();
            *p = v;
            p = p + 1;
        }
    }
    initrand(0x1234);
    for (i = 0; i < 4; i++) {
        p = (UBYTE *)0xA000;
        rRAMB = i;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = *p;
            ok = v == randw();
            if (!ok)
                return 0;
            p++;
        }
    }
    rRAMG = 0;
    return 1;
}

UBYTE TestSramHi(void)
{
    UBYTE i;

    rRAMG = 0x0A;
    for (i = 4; i < 0x10; i++) {
        rRAMB = i;
        if (TestSram())
            return 0;
    }
    rRAMG = 0;
    return 1;
}

void DrawCartTest(void)
{
    wVramCursor = (UBYTE *)0x9820;
    DrawString("CARTRIDGE TEST");
    wVramCursor = (UBYTE *)0x9860;
    DrawString("ROM...");
    if (FarCallByte(0, FAR_FN(CheckRomChecksum))) {
        DrawString("OK!");
    } else {
        DrawString("FAIL!");
        wVramCursor = (UBYTE *)0x9880;
        DrawString("TRY CLEANING PINS");
        wVramCursor = (UBYTE *)0x98A0;
        DrawString("OR FLASH AGAIN.");
        for (;;)
            ;
    }
    wVramCursor = (UBYTE *)0x9880;
    DrawString("SRAM...");
    if (TestSramLo() && TestSramHi()) {
        DrawString("OK!");
    } else {
        DrawString("FAIL!");
        wVramCursor = (UBYTE *)0x98A0;
        DrawString("TRY CLEANING PINS");
        wVramCursor = (UBYTE *)0x98C0;
        DrawString("OR REPLACE BATTERY.");
        for (;;)
            ;
    }
}

/* $45D1: format the SRAM song: zero every bank, default the grooves (6/6),
   the sequence, the word names "W-0".."W-x" (digits then letters), the
   instrument names, the synth parameters, the wave frames and the
   properties, with the 'rb' markers and $BFFF = $16. p is a plain
   pointer: it owns bc in the allocator's books and is the operand of
   its own copies and reads, which therefore spill themselves (the word
   loop's `*p += ...`); s2 is a volatile byte so the third saved
   property byte is stored while the other two keep b and c. */
void FormatSramSong(void)
{
    UBYTE *p;
    UBYTE i;
    volatile UBYTE s2;
    UBYTE s0, s1;

    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    ClearSramWindow();
    FarCall(7, FAR_FN(ClearBookmarks));
    *(UBYTE *)0xBE78 = 'r';
    *(UBYTE *)0xBE79 = 'b';
    for (p = (UBYTE *)0xB090; p != (UBYTE *)0xB290; ) {
        *p = 6;
        p++;
        *p = 6;
        p += 0x0F;
    }
    FillMemory((UBYTE *)0xB290, 0xFF, 0x400);
    p = (UBYTE *)0xBDD0;
    i = 0;
    do {
        *p = 'W';
        p++;
        *p = '-';
        p++;
        *p = i;
        *p += i < 10 ? '0' : '7';
        p++;
        i++;
    } while (p != (UBYTE *)0xBDFA);
    rRAMB = 1;
    s0 = *(UBYTE *)0xBFB6;
    s1 = *(UBYTE *)0xBFB7;
    s2 = *(UBYTE *)0xBFB8;
    ClearSramWindow();
    FillMemory((UBYTE *)0xBFC6, 0xFF, 4);
    *(UBYTE *)0xBFB6 = s0;
    *(UBYTE *)0xBFB7 = s1;
    *(UBYTE *)0xBFB8 = s2;
    *(UBYTE *)0xBFC3 = 1;
    rRAMB = 3;
    ClearSramWindow();
    FillMemory((UBYTE *)0xB000, 0xFF, 0xFF0);
    rRAMB = 2;
    ClearSramWindow();
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    *(UBYTE *)0xBE80 = 'r';
    *(UBYTE *)0xBE81 = 'b';
    *(UBYTE *)0xBFB4 = 0x80;
    *(UBYTE *)0xBFBA = 7;
    *(UBYTE *)0xBFBB = 2;
    wColorSet = 0;
    FillMemory((UBYTE *)0xA080, 0xFF, 0x800);
    for (p = (UBYTE *)0xBEB2; p != (UBYTE *)0xBFB2; p += 0x10) {
        p[5] = 0x10;
        p[9] = 0x10;
        p[6] = 0xFF;
        p[10] = 0xFF;
    }
    rRAMB = 3;
    *(UBYTE *)0xBFF0 = 'r';
    *(UBYTE *)0xBFF1 = 'b';
    for (p = (UBYTE *)0xA000; p < (UBYTE *)0xB000; p += 0x10)
        memcpy((void *)p, DefaultWaveFrameB1, 0x10);
    *(UBYTE *)0xBFFF = 0x16;
    _asm
        ei
    _endasm;
    rRAMG = 0;
}

UBYTE CheckJkMarker(void)
{
    if (ReadSongByte(4, (UBYTE *)0xA13E) != 'j')
        return 0;
    if (ReadSongByte(4, (UBYTE *)0xA13F) != 'k')
        return 0;
    return 1;
}

void CopyInstrumentName(UBYTE instr)
{
    UBYTE *p;

    if (instr > 0x40)
        DebugTrap();
    if (instr == 0x40) {
        strcpy(wNameEditBuffer, "SPCH");
        return;
    }
    p = (UBYTE *)0xBE7A + (UWORD)instr * 5;
    wNameEditBuffer[0] = ReadSongByte(0, p++);
    wNameEditBuffer[1] = ReadSongByte(0, p++);
    wNameEditBuffer[2] = ReadSongByte(0, p++);
    wNameEditBuffer[3] = ReadSongByte(0, p++);
    wNameEditBuffer[4] = ReadSongByte(0, p);
    wNameEditBuffer[5] = 0;
}

void WriteInstrumentName(UBYTE instr)
{
    UBYTE *p;

    if (instr >= 0x40)
        DebugTrap();
    p = (UBYTE *)0xBE7A + (UWORD)instr * 5;
    WriteSongByte(0, p++, wNameEditBuffer[0]);
    WriteSongByte(0, p++, wNameEditBuffer[1]);
    WriteSongByte(0, p++, wNameEditBuffer[2]);
    WriteSongByte(0, p++, wNameEditBuffer[3]);
    WriteSongByte(0, p, wNameEditBuffer[4]);
}

void LoadHardwareTables(void)
{
    memcpy(wNotePeriodLo, wIsCgb ? PitchCurveCgb : PitchCurve, 0xD8);
    memcpy(wNotePeriods, wIsCgb ? NoteFrequencyDeltaTable : NoteFrequencyTable, 0xD8);
}

/* $4B5E: clear the OAM shadow and the sprite tiles ($8000-$802F and
   $8050-$805F, the second half a 2-column pattern), place the three WAV
   display sprites and wait for the next OAM DMA before enabling sprites.
   p is a plain word that is the operand of every cast on it, so the cast
   copies spill themselves and p ends in its slot. */
void ClearOamBuffer(void)
{
    UWORD p;

    FillMemory(wOamBuffer, 0, 0xA0);
    wOamDirty = 1;
    for (p = 0x8000; p != 0x8030; p++)
        WriteVramByteSafeP((UBYTE *)p, 0);
    for (p = 0x8050; p != 0x8060; p++)
        WriteVramByteSafeP((UBYTE *)p, ((UBYTE)p & 2) ? 0x60 : 0);
    OamShadow[0].x = 0x90;
    OamShadow[1].x = 0x98;
    OamShadow[1].tile = 1;
    OamShadow[2].x = 0xA0;
    OamShadow[2].tile = 2;
    while (wOamDirty)
        ;
    LCDC_REG |= 2;
}

void ApplyColorSet(void)
{
    UWORD *pal;
    UBYTE v;

    if (wBootHardwareId == 0x11) {
        LCDC_REG &= 0x7F;
        pal = &ColorSetPalettes[wColorSet * 20];
        set_bkg_palette(0, 5, pal);
        LoadObjPalette(0, 2, pal);
        memcpy(wFileNameScratch, (UBYTE *)(pal + 5), 8);
        LoadObjPalette(2, 1, (UWORD *)wFileNameScratch);
        LCDC_REG |= 0x80;
    } else {
        v = (wColorSet & 1) ? 0x1B : 0xE4;
        BGP_REG = v;
        OBP0_REG = v;
    }
}

void InitScreen(void)
{
    ApplyColorSet();
    ClearOamBuffer();
    LoadHardwareTables();
}

void UpdatePlayClock(void)
{
    if (!wPlayMode) {
        wPlayFrames = 0;
        return;
    }
    wPlayFrames++;
    if (wPlayFrames != 60)
        return;
    wPlaySeconds++;
    if (wPlaySeconds == 60) {
        wPlaySeconds = 0;
        wPlayMinutes++;
    }
    wPlayFrames = 0;
    if ((wPlaySeconds & 3) == 0)
        wPlayFrames = 1;
    if (wPlayMode && wCurrentScreen == 9) {
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor = 0x9A05;
        printf("%d:", (UWORD)wPlayMinutes);
        wProjectDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
            wProjectDrawScratch++;
        if (wPlaySeconds < 10)
            DrawStringIndexed('0');
        printf("%d", (UWORD)wPlaySeconds);
    }
}

void UpdateWorkClock(void)
{
    UBYTE v;

    v = ReadSongByte(1, (UBYTE *)0xBFB3);
    v++;
    if (v == 60) {
        WriteSongByte(1, (UBYTE *)0xBFB3, 0);
        v = ReadSongByte(1, (UBYTE *)0xBFB2);
        v++;
        WriteSongByte(1, (UBYTE *)0xBFB2, v);
    } else {
        WriteSongByte(1, (UBYTE *)0xBFB3, v);
    }
    v = ReadSongByte(1, (UBYTE *)0xBFB8);
    v++;
    if (v == 60) {
        WriteSongByte(1, (UBYTE *)0xBFB8, 0);
        v = ReadSongByte(1, (UBYTE *)0xBFB7);
        v++;
        if (v == 24) {
            WriteSongByte(1, (UBYTE *)0xBFB7, 0);
            v = ReadSongByte(1, (UBYTE *)0xBFB6);
            v++;
            WriteSongByte(1, (UBYTE *)0xBFB6, v);
        } else {
            WriteSongByte(1, (UBYTE *)0xBFB7, v);
        }
    } else {
        WriteSongByte(1, (UBYTE *)0xBFB8, v);
    }
}

/*
 * The speed icon's tiles are file-scope const data, defined here: SDCC puts
 * const data in the constant segment and flushes that after the next
 * function it compiles, ahead of the function's own string literals, which
 * is where the ROM has the table ($01:$51EE, between DrawSpeedModeName's
 * code and its strings).
 */
const UBYTE SpeedGlyphTiles[] = { 0x6B, 0x6C, 0x69, 0x6A, 0x6D, 0x6E };

void DrawSpeedModeName(UBYTE v)
{
    if (v & 0x80)
        DrawString("STEP");
    else if (v & 0x10)
        DrawString("TICK");
    else if ((v & 0x40) && ReadCurrentInstrumentByte(0) != 2)
        DrawString("DRUM");
    else
        DrawString("FAST");
    DrawSpeedGlyph(v);
}

void DrawSpeedGlyph(UBYTE v)
{
    char g;

    g = SpeedGlyphTiles[v & 7];
    DrawStringIndexed(g);
    DrawStringIndexed(g);
}

void DrawOnOffName(UBYTE v)
{
    DrawString((v & 0x20) ? "OFF" : " ON");
}

UBYTE *GetScreenPalette(void)
{
    switch (wCurrentScreen) {
    case 1:
        return ScreenPalettePhrase;
    case 2:
        return ScreenPaletteGroove;
    case 3:
        return ScreenPaletteChain;
    case 4:
        return ScreenPaletteSong;
    case 5:
        return ScreenPaletteTable;
    case 6:
        if (wInstrument < 0x40) {
            switch (ReadCurrentInstrumentByte(0)) {
            case 0:
                return ScreenPaletteInstrumentPulse;
            case 1:
                return ScreenPaletteInstrumentWave;
            case 2:
                return ScreenPaletteInstrumentKit;
            case 3:
                return ScreenPaletteInstrumentNoise;
            }
        } else {
            return ScreenPaletteSpeech;
        }
        return 0;
    case 8:
        return ScreenPaletteWave;
    case 9:
        return ScreenPaletteProject;
    case 10:
        return ScreenPaletteSynth;
    case 11:
        return ScreenPaletteSpeech;
    case 12:
        return ScreenPaletteWord;
    case 13:
        return ScreenPaletteFile;
    }
    return 0;
}

void DrawScreenPaletteMap(void)
{
    UBYTE *s;

    s = GetScreenPalette();
    if (!s) {
        VBK_REG = 1;
        ClearBgTilemap();
        VBK_REG = 0;
    } else {
        wMessageStringPointer = 0;
        wMessageTimer = 0;
        if (hGameboyType)
            CopyScreenMapCgb((UWORD)s);
        else
            CopyScreenMapDmg(s);
    }
}

void ClearBgMapAndAttributes(void)
{
    UBYTE *p = (UBYTE *)0x9800;

    if (hGameboyType) {
        VBK_REG = 1;
        while (p != (UBYTE *)0x9A40) {
            while (STAT_REG & 2)
                ;
            *p++ = 0;
        }
        VBK_REG = 0;
    }
}

void DrawTopBar(void)
{
    UBYTE i;
    UBYTE isFile;

    wVramCursorW = 0x9800;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    if (wMessageStringPointer)
        return;
    isFile = wCurrentScreen == 13;
    if (!isFile && wCurrentScreen != 9 && hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0, 0, 20, 1, TxtBlankString2);
        VBK_REG = 0;
    }
    wVramCursorW = 0x9800;
    switch (wCurrentScreen) {
    default:
        if (!isFile)
            break;
        goto file;
    case 4:
        if (wLiveMode)
            wTopBarDrawScratch = TxtScreenLive;
        else
            wTopBarDrawScratch = TxtScreenSong;
        DrawString(wTopBarDrawScratch);
        break;
    case 3:
        DrawString(TxtScreenChain);
        DrawHexByte(wChain);
        break;
    case 1:
        DrawString(TxtScreenPhrase);
        DrawHexByte(wPhrase);
        break;
    case 5:
        DrawString(TxtScreenTable);
        DrawHexByte(wTable);
        break;
    case 9:
        DrawString(TxtScreenProject);
        if (FarCallByte(0x1C, FAR_FN(CheckFileMagic64)))
            DrawString(wTopBarString);
        break;
    file:
        DrawString(TxtScreenFile);
        DrawString(wTopBarString);
        i = 0;
        for (;;) {
            wTopBarLength = 12 - StringLength(wTopBarString);
            if (i == wTopBarLength)
                break;
            DrawStringTile20();
            i++;
        }
        break;
    case 6:
        DrawString(TxtScreenInstr);
        DrawHexByte(wInstrument);
        break;
    case 8:
        DrawString(TxtScreenWave);
        DrawHexByte(wSynth);
        break;
    case 10:
        DrawString(TxtScreenSynth);
        DrawByte(wSynthScreen);
        break;
    case 2:
        DrawString(TxtScreenGroove);
        DrawHexByte(wGroove);
        break;
    }
    while (wVramCursorW < 0x9811)
        DrawStringIndexed(' ');
    if (wCurrentScreen != 9 && !isFile)
        DrawChannelHeader();
}

void ShowCommandHelp(UBYTE cmd)
{
    UBYTE *saved;
    UBYTE *s;

    saved = wVramCursor;
    if (!cmd)
        return;
    if (cmd > 0x13)
        DebugTrap();
    switch (cmd) {
    case 2:
        s = wCurrentScreen == 1 ? TxtHelpBNote : TxtHelpBHop;
        break;
    case 8:
        s = wCurrentScreen == 1 ? TxtHelpHPhrase : TxtHelpHTable;
        break;
    case 6:
        s = wSongColumn == 2 ? TxtHelpFOffset : TxtHelpFTune;
        break;
    case 0x0F:
        switch (wSongColumn) {
        case 2:
            s = TxtHelpSKit;
            break;
        case 3:
            s = TxtHelpSNoise;
            break;
        default:
            s = TxtHelpSSweep;
            break;
        }
        break;
    case 0x12:
        s = wSongColumn == 2 ? TxtHelpW : TxtHelpWShort;
        break;
    default:
        s = CommandHelpPointers[cmd];
        break;
    }
    ShowMessage(s);
    wVramCursor = saved;
}

void ClearMessageBar(void)
{
    UBYTE i = 0;

    wMessageStringPointer = 0;
    wVramCursor = (UBYTE *)0x998F;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    do {
        DrawStringIndexed(wSampleNoteString[i]);
    } while (i++ != 4);
    wMessageTimer = 100;
    wMessagePanelShown = 1;
}

void UpdateMessage(void)
{
    UBYTE i;

    if (wSelectHeld && !wMessageNoPause)
        return;
    if (wMessageStringPointer) {
        if (!wMessageTimer) {
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            wVramCursor = (UBYTE *)0x9800;
            for (i = 0; i < 20; i++)
                DrawStringIndexed(wMessageStringPointer[i]);
            wMessageTimer = 10;
            wMessageStringPointer += 20;
            if (!*wMessageStringPointer) {
                wMessageNoPause = 0;
                wMessageStringPointer = 0;
                wMessageTimer = 150;
            } else {
                wMessageStringPointer -= 19;
            }
        }
        wMessageTimer--;
    } else if (wMessageTimer) {
        if (!--wMessageTimer) {
            if (wMessagePanelShown) {
                hBgDigitPalette = 4;
                hBgPalette = 1;
                wVramCursor = (UBYTE *)0x998F;
                DrawString("     ");
            } else {
                DrawTopBar();
            }
        }
    }
}

void ResetEditorState(UBYTE keep)
{
    UBYTE top, right, bottom, ctype, ctype1, mode, left, live;

    left = wSelectionLeft;
    top = wSelectionTop;
    right = wSelectionRight;
    bottom = wSelectionBottom;
    ctype = wClipboardType;
    ctype1 = wQuickMarking;
    mode = wCursorMode;
    live = wLiveMode;
    if (keep)
        memcpy(wClipboardSave, wClipboardBuffer, 0xFC);
    rRAMG = 0x0A;
    ResetWorkState();
    rRAMG = 0;
    if (keep) {
        memcpy(wClipboardBuffer, wClipboardSave, 0xFC);
        wSelectionLeft = left;
        wSelectionRight = right;
        wSelectionTop = top;
        wSelectionBottom = bottom;
        wClipboardType = ctype;
        wQuickMarking = ctype1;
        wCursorMode = mode;
        wLiveMode = live;
    }
}

void LoadProjectSettings(UBYTE keep)
{
    UBYTE i;

    ResetEditorState(keep);
    NR52_REG = 0;
    NR51_REG = 0;
    NR50_REG = 0;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    wFont = *(UBYTE *)0xBFBC;
    if (wFont > 2) {
        wFont = 0;
        *(UBYTE *)0xBFBC = 0;
    }
    SyncLoadSetting();
    rRAMG = 0x0A;
    rRAMB = 1;
    wCloneMode = *(UBYTE *)0xBFC0;
    wColorSet = *(UBYTE *)0xBFBE;
    if (!hGameboyType && wColorSet > 3)
        wColorSet = 0;
    rRAMG = 0;
    FarCall(0x1E, FAR_FN(LoadFontTiles));
    hBgDigitPalette = 4;
    hBgPalette = 1;
    ApplyColorSet();
    hSoundChannel = 0;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    wTempo = *(UBYTE *)0xBFB4;
    wTempoMode = *(UBYTE *)0xBFCC;
    wSongTranspose = *(UBYTE *)0xBFB5;
    memcpy(wColumnInstrument, (UBYTE *)0xBFC6, 4);
    i = 4;
    do {
        if (wColumnInstrument[i - 1] > 0x40 && wColumnInstrument[i - 1] != 0xFF)
            wColumnInstrument[i - 1] = 0;
    } while (--i);
    wKeyDelay = *(UBYTE *)0xBFBA & 0x0F;
    wKeyRepeat = *(UBYTE *)0xBFBB & 0x0F;
    rRAMG = 0;
    FarCall(7, FAR_FN(SetSyncSendRate));
    wKeyDelayTimer = wKeyDelay;
    wKeyRepeatTimer = wKeyRepeat;
    FarCall(0x1C, FAR_FN(LoadFileListState));
    FarCall(0x1C, FAR_FN(FileUpgradeSong));
    if (wSyncMode == 5)
        FarCall(7, FAR_FN(DrawKeyboardInstrumentState));
}


/* $794B: the crash entry: interrupts off, SRAM locked, then the stack dump
   and the crash screen */

void SaveCrashStack(void);                  /* asm: saves sp and 20 stack bytes, then DrawCrashScreen */

void CrashHandler(void)
{
    _asm di _endasm;
    rRAMG = 0;
    SaveCrashStack();
}

/* $7955: the crash screen, reached from SaveCrashStack: "DONT PANIC!", the
   ROM bank, the stack pointer and 10 words from the stack, the play mode,
   the screen, the hardware, the sync mode and whether an assertion fired
   (AST), then the version; then it hangs */

extern volatile sfr UBYTE SCY_REG;          /* $FF42 */
extern volatile sfr UBYTE SCX_REG;          /* $FF43 */
extern UWORD wCrashSP;                      /* $C7C4 */
extern UBYTE wCrashStack[];                 /* $C7B0, [20] */
extern UBYTE wBootAgbId;                    /* $CBDA */
extern UBYTE wDebugTrapped;                 /* $C7AF */
extern char TxtVersion[];                   /* $01:$6F8E, "V9.4.0  (c) ..." */
void DebugNop(void);                        /* bank 0 */

void DrawCrashScreen(void)
{
    int i;

    ClearBgTilemap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    LCDC_REG &= ~2;                 /* sprites off */
    wVramCursor = (UBYTE *)0x9800;
    DrawString("DONT PANIC!");
    wVramCursor = (UBYTE *)0x9840;
    DrawString("PLEASE MAIL THIS");
    wVramCursor = (UBYTE *)0x9860;
    DrawString("DATA TO INFO AT");
    wVramCursor = (UBYTE *)0x9880;
    DrawString("LITTLESOUNDDJ.COM");
    wVramCursor = (UBYTE *)0x98C1;
    DrawString("ROM:");
    DrawHexByte(hCurrentBank);
    wVramCursor = (UBYTE *)0x9907;
    DrawString("STK:");
    DrawHexByte(wCrashSP >> 8);
    DrawHexByte(wCrashSP);
    wVramCursor = (UBYTE *)0x9967;
    DrawString("PLY:");
    DrawByte(wPlayMode);
    wVramCursor = (UBYTE *)0x9987;
    DrawString("CVW:");
    DrawByte(wCurrentScreen);
    if (hGameboyType) {
        wVramCursor = (UBYTE *)0x99A7;
        DrawString("CGB");
    }
    if (wBootAgbId) {
        wVramCursor = (UBYTE *)0x99C7;
        DrawString("GBA");
    }
    wVramCursor = (UBYTE *)0x9A07;
    DrawString("SYN:");
    DrawByte(wSyncMode);
    if (wDebugTrapped) {
        wVramCursor = (UBYTE *)0x9A27;
        DrawString("AST");
    }
    for (i = 0; i < 10; i++) {
        SetVramCursor(1, i + 8);
        DrawHexByte(wCrashStack[(i << 1) + 1]);
        DrawHexByte(wCrashStack[i << 1]);
    }
    wVramCursor = (UBYTE *)0x98C9;
    DrawStringIndexed(TxtVersion[0]);
    DrawStringIndexed(TxtVersion[1]);
    DrawStringIndexed(TxtVersion[2]);
    DrawStringIndexed(TxtVersion[3]);
    DrawStringIndexed(TxtVersion[4]);
    DrawStringIndexed(TxtVersion[5]);
    SCY_REG = 0;
    SCX_REG = 0;
    DebugNop();
    ClearBgMapAndAttributes();
    while (1)
        ;
}
