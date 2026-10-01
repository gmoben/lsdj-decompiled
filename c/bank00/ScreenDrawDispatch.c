/*
 * The screen dispatchers -- LSDj 9.4.0, ROM bank $00:$24A8-$2BC3.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The editor index (wCurrentScreen: 1 phrase, 2 groove, 3 chain, 4 song,
 * 5 table, 6 instrument, 7 name editor, 8 wave, 9 project, 10 synth,
 * 12 word, 13 file, 14 help) selects each screen's draw, per-frame update
 * and input handler: page in the screen's ROM bank (HRAM shadow hCurrentBank
 * + the MBC5 register) and call it. SDCC compiles the dense switches to a
 * bounds check and a `jp hl` into a table of `jp`s; empty cases (0, 7, 11)
 * become their own `jp end` trampolines, in source order, which is how the
 * ROM has them, so the original spelled them out. Case bodies follow source
 * order too: ScreenUpdateDispatch lists the table screen (5) before chain (3)
 * and song (4), and HandleButtonEvent the SELECT case first.
 *
 * The callees live in other banks and are called directly (nonbanked, as
 * everything in LSDj); the link step resolves them from src/bankNN.sym.
 * RedrawScreen ($24DD) is asm: it ends by jumping into ScreenDrawDispatch.
 */

#include "lsdj.h"

extern UBYTE wChanActive[];                 /* $C0E0, [8] */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wPartialRedrawDone;            /* $C56D */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wFullRedrawTimer;              /* $CB7B */
extern UBYTE wAHeld;
extern UBYTE wBHeld;
extern UBYTE wSelectHeld;

void PhraseScreenUpdate(void);              /* bank $1F */
void GrooveScreenUpdate(void);              /* bank $1D */
void TableScreenUpdate(void);               /* bank 5 */
void ChainScreenUpdate(void);               /* bank 6 */
void SongScreenUpdate(void);                /* bank 7 */
void DrawShadowDigit(void);                 /* bank 6 */
void DrawSyncStatus(void);                  /* bank 6 */
UBYTE PhraseScreenInput(void);              /* the input handlers, by screen */
UBYTE GrooveScreenInput(void);
UBYTE ChainScreenInput(void);
UBYTE SongScreenInput(void);
UBYTE TableScreenInput(void);
UBYTE InstrScreenInput(void);
UBYTE NameEditorInput(void);
UBYTE WaveScreenInput(void);
UBYTE ProjectScreenInput(void);
UBYTE SynthScreenInput(void);
UBYTE WordEditorInput(void);
UBYTE FileScreenInput(void);
UBYTE HelpScreenInput(void);
UBYTE HandleInputCombos(void);

void ResetScreenSprites(void)
{
    if (wCurrentScreen != 7 && wCurrentScreen != 8 && wCurrentScreen != 9
            && wCurrentScreen != 0x0D && wCurrentScreen != 0x0E && wChanActive[2])
        PositionWavSprites();
    else
        ClearSprites012();
    ClearMarkerSprites();
}

void ScreenDrawDispatch(void)
{
    switch (wCurrentScreen) {
    case 0:
        break;
    case 1:
        SWITCH_ROM(0x04); DrawPhraseScreen();
        break;
    case 2:
        SWITCH_ROM(0x1D); DrawGrooveScreen();
        break;
    case 3:
        SWITCH_ROM(0x06); DrawChainScreen();
        break;
    case 4:
        SWITCH_ROM(0x07); DrawSongScreen();
        break;
    case 5:
        SWITCH_ROM(0x05); DrawTableScreen();
        break;
    case 6:
        SWITCH_ROM(0x03); DrawInstrumentScreen();
        break;
    case 7:
        break;
    case 8:
        SWITCH_ROM(0x06); DrawWaveScreen();
        break;
    case 9:
        SWITCH_ROM(0x1B); DrawProjectScreen();
        break;
    case 10:
        SWITCH_ROM(0x1D); DrawSynthScreen();
        break;
    case 11:
        break;
    case 12:
        SWITCH_ROM(0x06); DrawWordScreen();
        break;
    case 13:
        SWITCH_ROM(0x1C); DrawFileScreen();
        break;
    case 14:
        SWITCH_ROM(0x1B); DrawHelpScreen();
        break;
    }
}

void ScreenUpdateDispatch(void)
{
    if (wRedrawPending) {
        if (--wFullRedrawTimer) {
            if (!wPartialRedrawDone)
                return;
        }
        wFullRedrawTimer = 0x20;
    }
    wPartialRedrawDone = 0;
    switch (wCurrentScreen) {
    case 1:
        SWITCH_ROM(0x1F);
        PhraseScreenUpdate();
        break;
    case 2:
        SWITCH_ROM(0x1D);
        GrooveScreenUpdate();
        break;
    case 5:
        SWITCH_ROM(0x05);
        TableScreenUpdate();
        break;
    case 3:
        SWITCH_ROM(0x06);
        ChainScreenUpdate();
        break;
    case 4:
        SWITCH_ROM(0x07);
        SongScreenUpdate();
        break;
    }
    if (!wRedrawPending) {
        switch (wCurrentScreen) {
        case 7:
        case 8:
        case 9:
        case 0x0D:
        case 0x0E:
            break;
        default:
            SWITCH_ROM(0x06);
            DrawShadowDigit();
            DrawSyncStatus();
        }
    }
}

UBYTE DispatchScreenInput(void)
{
    switch (wCurrentScreen) {
    case 0:
        break;
    case 1:
        SWITCH_ROM(0x04);
        return PhraseScreenInput();
    case 2:
        SWITCH_ROM(0x1D);
        return GrooveScreenInput();
    case 3:
        SWITCH_ROM(0x06);
        return ChainScreenInput();
    case 4:
        SWITCH_ROM(0x07);
        return SongScreenInput();
    case 5:
        SWITCH_ROM(0x05);
        return TableScreenInput();
    case 6:
        SWITCH_ROM(0x03);
        return InstrScreenInput();
    case 7:
        SWITCH_ROM(0x1B);
        return NameEditorInput();
    case 8:
        SWITCH_ROM(0x06);
        return WaveScreenInput();
    case 9:
        SWITCH_ROM(0x1B);
        return ProjectScreenInput();
    case 10:
        SWITCH_ROM(0x1D);
        return SynthScreenInput();
    case 11:
        break;
    case 12:
        SWITCH_ROM(0x06);
        return WordEditorInput();
    case 13:
        SWITCH_ROM(0x1C);
        return FileScreenInput();
    case 14:
        SWITCH_ROM(0x1B);
        return HelpScreenInput();
    }
    return 0;
}

UBYTE HandleButtonEvent(void)
{
    UBYTE combo, input;

    switch (wButtons) {
    case 0x40:
        wSelectHeld = 1;
        break;
    case 0x10:
        wAHeld = 1;
        break;
    case 0x20:
        wBHeld = 1;
        break;
    }
    combo = HandleInputCombos();
    input = DispatchScreenInput();
    return combo | input;
}
