/*
 * Small UI helpers -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE STAT_REG;         /* $FF41 */
extern UBYTE SpeedGlyphTiles[];             /* $01:$51EE */
extern UBYTE wIsCGB;                        /* $C402 */
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

UBYTE CheckJkMarker(void)
{
    if (ReadSongByte(4, (UBYTE *)0xA13E) != 'j')
        return 0;
    if (ReadSongByte(4, (UBYTE *)0xA13F) != 'k')
        return 0;
    return 1;
}

void UiLoadHwTables(void)
{
    memcpy(wNotePeriodLo, wIsCGB ? PitchCurveCgb : PitchCurve, 0xD8);
    memcpy(wNotePeriods, wIsCGB ? NoteFreqDeltaTable : NoteFreqTable, 0xD8);
}

void UiInitScreen(void)
{
    ApplyColorSet();
    ClearOamBuffer();
    UiLoadHwTables();
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
