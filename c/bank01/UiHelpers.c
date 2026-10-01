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
extern UBYTE wColorSet;
extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wClipboardType_p1;             /* $C412 */
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
extern UBYTE wColorSet_p1;                  /* $C530 */
UBYTE CheckRomChecksum(void);               /* $00:$1FD5 */
UBYTE UiTestSramLo(void);
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

    if (wSelectHeld && !wColorSet_p1)
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
                wColorSet_p1 = 0;
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
    ctype1 = wClipboardType_p1;
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
        wClipboardType_p1 = ctype1;
        wCursorMode = mode;
        wLiveMode = live;
    }
}
