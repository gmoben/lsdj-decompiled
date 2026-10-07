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
extern volatile sfr UBYTE hSoundCh;         /* $FF8B */
extern UBYTE wColorSet;
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wQuickMarking;             /* $C412 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wClipboardBuf[];               /* $C57A, [252] */
extern UBYTE wClipboardSave[];              /* $C7C6, [252] */
void ResetWorkState(void);                  /* bank 0 */
extern UBYTE wBootHwId;                     /* $CBD9 */
extern UBYTE wNameEditBuf[];                /* $C431, [5] + NUL */
extern UBYTE wPlayMinutes;                  /* $C552 */
extern UBYTE wPlaySeconds;                  /* $C553 */
extern UBYTE wPlayFrames;                   /* $C6A8 */
extern volatile UBYTE wProjDrawScratch;     /* $C9A7; volatile: the ROM re-reads it for the `++` */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wMsgNoPause;                  /* $C530 */
UBYTE CheckRomChecksum(void);               /* $00:$1FD5 */
UBYTE UiTestSramLo(void);
void SeedRandom16(UWORD seed);              /* bank 0 */
UBYTE Random16(void);                       /* bank 0, a 16-bit LCG; the SRAM test uses the low byte */
extern UBYTE wFileNameTmp[];
extern UWORD ColorSetPalettes[];            /* $01:$56A7, [sets][20] */
void set_bkg_palette(UBYTE first, UBYTE nb, UWORD *rgb);   /* GBDK */
void UiLoadObjPalette(UBYTE first, UBYTE nb, UWORD *rgb);
extern UBYTE wNotePeriodLo[];               /* $CD28 */
extern UBYTE wNotePeriods[];                /* $CF28 */
extern UBYTE NoteFreqTable[];               /* $01:$40FA, DMG */
extern UBYTE NoteFreqDeltaTable[];          /* $01:$41D2, CGB */
extern UBYTE PitchCurve[];                  /* $01:$42AA, DMG */
extern UBYTE PitchCurveCgb[];               /* $01:$4382, CGB */

void ApplyColorSet(void);
void ClearOamBuffer(void);
void UiLoadHwTables(void);
UBYTE TestSram(void);                       /* bank 0 */
UBYTE *GetScreenPalette(void);
void UiDrawScreenCgb(UWORD s);              /* asm */
void UiDrawScreenDmg(UBYTE *s);             /* asm */

void LoadSyncSetting(void);                 /* bank 0 */
void LoadFontTiles(void);              /* bank $1E */
void SetSyncSendRate(void);                 /* bank 7 */
void LoadFileListState(void);               /* bank $1C */
void FileUpgradeSong(void);                 /* bank $1C */
void DrawKbdInstrState(void);               /* bank 7 */
extern char *CommandHelpTable[];            /* $01:$776E */
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
extern char wTopBarStr[];                   /* $C555 */
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

extern UBYTE ScreenPalChain[];              /* $01:$57BF */
extern UBYTE ScreenPalFile[];               /* $01:$6B6F */
extern UBYTE ScreenPalGroove[];             /* $01:$5BF7 */
extern UBYTE ScreenPalInstrKit[];           /* $01:$62FF */
extern UBYTE ScreenPalInstrNoise[];         /* $01:$6467 */
extern UBYTE ScreenPalInstrPulse[];         /* $01:$602F */
extern UBYTE ScreenPalInstrWave[];          /* $01:$6197 */
extern UBYTE ScreenPalPhrase[];             /* $01:$5A8F */
extern UBYTE ScreenPalProject[];            /* $01:$5D5F */
extern UBYTE ScreenPalSong[];               /* $01:$5EC7 */
extern UBYTE ScreenPalSpeech[];             /* $01:$65CF */
extern UBYTE ScreenPalSynth[];              /* $01:$689F */
extern UBYTE ScreenPalTable[];              /* $01:$5927 */
extern UBYTE ScreenPalWave[];               /* $01:$6737 */
extern UBYTE ScreenPalWord[];               /* $01:$6A07 */
extern UBYTE TxtBlankString2[];             /* $01:$7498, 20 zero attribute bytes */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wCloneMode;                    /* $C531 */
extern UBYTE wColInstrument[];              /* $C468, [4] */
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
extern UWORD wTopBarLen;                    /* $C7AD, wTopBarDrawScratch as a word (aliased by match_function.py) */
extern UWORD wVramCursorW;                  /* $C573, wVramCursor as a plain word: its `< 0x9811` is a 32-bit compare (aliased by match_function.py) */

UBYTE UiTestSramLo(void)
{
    UBYTE i;
    UBYTE * volatile p;                     /* volatile: the ROM keeps p on the stack */
    char done;
    UBYTE v;
    char ok;

    SeedRandom16(0x1234);
    rRAMG = 0x0A;
    for (i = 0; i < 4; i++) {
        p = (UBYTE *)0xA000;
        rRAMB = i;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = Random16();
            *p = v;
            p = p + 1;
        }
    }
    SeedRandom16(0x1234);
    for (i = 0; i < 4; i++) {
        p = (UBYTE *)0xA000;
        rRAMB = i;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = *p;
            ok = v == Random16();
            if (!ok)
                return 0;
            p++;
        }
    }
    rRAMG = 0;
    return 1;
}

UBYTE UiTestSramHi(void)
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
    if (UiTestSramLo() && UiTestSramHi()) {
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

UBYTE CheckJkMarker(void)
{
    if (ReadSongByte(4, (UBYTE *)0xA13E) != 'j')
        return 0;
    if (ReadSongByte(4, (UBYTE *)0xA13F) != 'k')
        return 0;
    return 1;
}

void CopyInstrName(UBYTE instr)
{
    UBYTE *p;

    if (instr > 0x40)
        DebugTrap();
    if (instr == 0x40) {
        strcpy(wNameEditBuf, "SPCH");
        return;
    }
    p = (UBYTE *)0xBE7A + (UWORD)instr * 5;
    wNameEditBuf[0] = ReadSongByte(0, p++);
    wNameEditBuf[1] = ReadSongByte(0, p++);
    wNameEditBuf[2] = ReadSongByte(0, p++);
    wNameEditBuf[3] = ReadSongByte(0, p++);
    wNameEditBuf[4] = ReadSongByte(0, p);
    wNameEditBuf[5] = 0;
}

void WriteInstrName(UBYTE instr)
{
    UBYTE *p;

    if (instr >= 0x40)
        DebugTrap();
    p = (UBYTE *)0xBE7A + (UWORD)instr * 5;
    WriteSongByte(0, p++, wNameEditBuf[0]);
    WriteSongByte(0, p++, wNameEditBuf[1]);
    WriteSongByte(0, p++, wNameEditBuf[2]);
    WriteSongByte(0, p++, wNameEditBuf[3]);
    WriteSongByte(0, p, wNameEditBuf[4]);
}

void UiLoadHwTables(void)
{
    memcpy(wNotePeriodLo, wIsCGB ? PitchCurveCgb : PitchCurve, 0xD8);
    memcpy(wNotePeriods, wIsCGB ? NoteFreqDeltaTable : NoteFreqTable, 0xD8);
}

void ApplyColorSet(void)
{
    UWORD *pal;
    UBYTE v;

    if (wBootHwId == 0x11) {
        LCDC_REG &= 0x7F;
        pal = &ColorSetPalettes[wColorSet * 20];
        set_bkg_palette(0, 5, pal);
        UiLoadObjPalette(0, 2, pal);
        memcpy(wFileNameTmp, (UBYTE *)(pal + 5), 8);
        UiLoadObjPalette(2, 1, (UWORD *)wFileNameTmp);
        LCDC_REG |= 0x80;
    } else {
        v = (wColorSet & 1) ? 0x1B : 0xE4;
        BGP_REG = v;
        OBP0_REG = v;
    }
}

void UiInitScreen(void)
{
    ApplyColorSet();
    ClearOamBuffer();
    UiLoadHwTables();
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
        DrawFormat("%d:", (UWORD)wPlayMinutes);
        wProjDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
            wProjDrawScratch++;
        if (wPlaySeconds < 10)
            DrawStringIndexed('0');
        DrawFormat("%d", (UWORD)wPlaySeconds);
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
    else if ((v & 0x40) && ReadCurInstrByte(0) != 2)
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
        return ScreenPalPhrase;
    case 2:
        return ScreenPalGroove;
    case 3:
        return ScreenPalChain;
    case 4:
        return ScreenPalSong;
    case 5:
        return ScreenPalTable;
    case 6:
        if (wInstrument < 0x40) {
            switch (ReadCurInstrByte(0)) {
            case 0:
                return ScreenPalInstrPulse;
            case 1:
                return ScreenPalInstrWave;
            case 2:
                return ScreenPalInstrKit;
            case 3:
                return ScreenPalInstrNoise;
            }
        } else {
            return ScreenPalSpeech;
        }
        return 0;
    case 8:
        return ScreenPalWave;
    case 9:
        return ScreenPalProject;
    case 10:
        return ScreenPalSynth;
    case 11:
        return ScreenPalSpeech;
    case 12:
        return ScreenPalWord;
    case 13:
        return ScreenPalFile;
    }
    return 0;
}

void UiDrawTextHw(void)
{
    UBYTE *s;

    s = GetScreenPalette();
    if (!s) {
        VBK_REG = 1;
        ClearBgTilemap();
        VBK_REG = 0;
    } else {
        wMsgStrPtr = 0;
        wMsgTimer = 0;
        if (hGameboyType)
            UiDrawScreenCgb((UWORD)s);
        else
            UiDrawScreenDmg(s);
    }
}

void UiClearBgMap(void)
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
    if (wMsgStrPtr)
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
            DrawString(wTopBarStr);
        break;
    file:
        DrawString(TxtScreenFile);
        DrawString(wTopBarStr);
        i = 0;
        for (;;) {
            wTopBarLen = 12 - StringLength(wTopBarStr);
            if (i == wTopBarLen)
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

void LoadCommandHelp(UBYTE cmd)
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
        s = wSongCol == 2 ? TxtHelpFOffset : TxtHelpFTune;
        break;
    case 0x0F:
        switch (wSongCol) {
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
        s = wSongCol == 2 ? TxtHelpW : TxtHelpWShort;
        break;
    default:
        s = CommandHelpTable[cmd];
        break;
    }
    ShowMessage(s);
    wVramCursor = saved;
}

void ClearMessageBar(void)
{
    UBYTE i = 0;

    wMsgStrPtr = 0;
    wVramCursor = (UBYTE *)0x998F;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    do {
        DrawStringIndexed(wSampleNoteString[i]);
    } while (i++ != 4);
    wMsgTimer = 100;
    wMsgPanelShown = 1;
}

void UiUpdateMessage(void)
{
    UBYTE i;

    if (wSelectHeld && !wMsgNoPause)
        return;
    if (wMsgStrPtr) {
        if (!wMsgTimer) {
            hBgDigitPalette = wSelectDigitPal;
            hBgPalette = wSelectPal;
            wVramCursor = (UBYTE *)0x9800;
            for (i = 0; i < 20; i++)
                DrawStringIndexed(wMsgStrPtr[i]);
            wMsgTimer = 10;
            wMsgStrPtr += 20;
            if (!*wMsgStrPtr) {
                wMsgNoPause = 0;
                wMsgStrPtr = 0;
                wMsgTimer = 150;
            } else {
                wMsgStrPtr -= 19;
            }
        }
        wMsgTimer--;
    } else if (wMsgTimer) {
        if (!--wMsgTimer) {
            if (wMsgPanelShown) {
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

    left = wSelLeft;
    top = wSelTop;
    right = wSelRight;
    bottom = wSelBottom;
    ctype = wClipboardType;
    ctype1 = wQuickMarking;
    mode = wCursorMode;
    live = wLiveMode;
    if (keep)
        memcpy(wClipboardSave, wClipboardBuf, 0xFC);
    rRAMG = 0x0A;
    ResetWorkState();
    rRAMG = 0;
    if (keep) {
        memcpy(wClipboardBuf, wClipboardSave, 0xFC);
        wSelLeft = left;
        wSelRight = right;
        wSelTop = top;
        wSelBottom = bottom;
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
    LoadSyncSetting();
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
    hSoundCh = 0;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    wTempo = *(UBYTE *)0xBFB4;
    wTempoMode = *(UBYTE *)0xBFCC;
    wSongTranspose = *(UBYTE *)0xBFB5;
    memcpy(wColInstrument, (UBYTE *)0xBFC6, 4);
    i = 4;
    do {
        if (wColInstrument[i - 1] > 0x40 && wColInstrument[i - 1] != 0xFF)
            wColInstrument[i - 1] = 0;
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
        FarCall(7, FAR_FN(DrawKbdInstrState));
}
