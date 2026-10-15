/*
 * Display setup -- LSDj 9.4.0, ROM bank $01.
 *
 * The CGB/DMG tables, the colour set and the screens' palette maps, the OAM
 * and BG map clears, a screen's setup.
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
extern UBYTE wBootHardwareId;                     /* $CBD9 */
extern UBYTE wFileNameScratch[];
extern UWORD ColorSetPalettes[];            /* $01:$56A7, [sets][20] */
void set_bkg_palette(UBYTE first, UBYTE nb, UWORD *rgb);   /* GBDK */
void LoadObjPalette(UBYTE first, UBYTE nb, UWORD *rgb);
extern UBYTE wNotePeriodLow[];               /* $CD28 */
extern UBYTE wNotePeriods[];                /* $CF28 */
extern UBYTE NoteFrequencyTable[];               /* $01:$40FA, DMG */
extern UBYTE NoteFrequencyTableCgb[];          /* $01:$41D2, CGB */
extern UBYTE PitchCurve[];                  /* $01:$42AA, DMG */
extern UBYTE PitchCurveCgb[];               /* $01:$4382, CGB */
void ApplyColorSet(void);
void ClearOamBuffer(void);
void LoadHardwareTables(void);
void WriteVramByteSafeP(UBYTE *address, UBYTE v);   /* WriteVramByteSafe with a pointer address (aliased) */
extern UBYTE wOamBuffer[];                  /* $DD00, the OAM shadow: 40 sprites x {y, x, tile, prop} */
extern volatile UBYTE wOamDirty;            /* $CBD1 */
UBYTE *GetScreenPalette(void);
void CopyScreenMapCgb(UWORD s);              /* asm */
void CopyScreenMapDmg(UBYTE *s);             /* asm */
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

void LoadHardwareTables(void)
{
    memcpy(wNotePeriodLow, wIsCgb ? PitchCurveCgb : PitchCurve, 0xD8);
    memcpy(wNotePeriods, wIsCgb ? NoteFrequencyTableCgb : NoteFrequencyTable, 0xD8);
}

/* $4B5E: clear the OAM shadow and the sprite tiles ($8000-$802F and
   $8050-$805F, the second half a 2-column pattern), place the three WAV
   display sprites and wait for the next OAM DMA before enabling sprites.
   p is a plain word that is the operand of every cast on it, so the cast
   copies spill themselves and p ends in its slot. */
void ClearOamBuffer(void)
{
    UWORD address;

    FillMemory(wOamBuffer, 0, 0xA0);
    wOamDirty = 1;
    for (address = 0x8000; address != 0x8030; address++)
        WriteVramByteSafeP((UBYTE *)address, 0);
    for (address = 0x8050; address != 0x8060; address++)
        WriteVramByteSafeP((UBYTE *)address, ((UBYTE)address & 2) ? 0x60 : 0);
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
    UWORD *palette;
    UBYTE v;

    if (wBootHardwareId == 0x11) {
        LCDC_REG &= 0x7F;
        palette = &ColorSetPalettes[wColorSet * 20];
        set_bkg_palette(0, 5, palette);
        LoadObjPalette(0, 2, palette);
        memcpy(wFileNameScratch, (UBYTE *)(palette + 5), 8);
        LoadObjPalette(2, 1, (UWORD *)wFileNameScratch);
        LCDC_REG |= 0x80;
    } else {
        v = (wColorSet & 1) ? 0x1B : 0xE4;
        BGP_REG = v;
        OBP0_REG = v;
    }
}

void InitializeScreen(void)
{
    ApplyColorSet();
    ClearOamBuffer();
    LoadHardwareTables();
}

UBYTE *GetScreenPalette(void)
{
    switch (wCurrentScreen) {
    case SCREEN_PHRASE:
        return ScreenPalettePhrase;
    case SCREEN_GROOVE:
        return ScreenPaletteGroove;
    case SCREEN_CHAIN:
        return ScreenPaletteChain;
    case SCREEN_SONG:
        return ScreenPaletteSong;
    case SCREEN_TABLE:
        return ScreenPaletteTable;
    case SCREEN_INSTRUMENT:
        if (wInstrument < 0x40) {
            switch (ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE)) {
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
    case SCREEN_WAVE:
        return ScreenPaletteWave;
    case SCREEN_PROJECT:
        return ScreenPaletteProject;
    case SCREEN_SYNTH:
        return ScreenPaletteSynth;
    case SCREEN_SPEECH:
        return ScreenPaletteSpeech;
    case SCREEN_WORD:
        return ScreenPaletteWord;
    case SCREEN_FILE:
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
    UBYTE *cell = (UBYTE *)BG_MAP(0, 0);

    if (hGameboyType) {
        VBK_REG = 1;
        while (cell != (UBYTE *)BG_MAP(0, 18)) {
            while (STAT_REG & 2)
                ;
            *cell++ = 0;
        }
        VBK_REG = 0;
    }
}
