/*
 * ScreenDrawDispatch -- LSDj 9.4.0, ROM bank $00:$254F-$264A (252 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank00/ScreenDrawDispatch.c --target ScreenDrawDispatch
 *
 * The editor index: page in the screen's ROM bank (HRAM shadow hCurrentBank +
 * the MBC5 register) and call its draw routine. SDCC compiles the switch to
 * its bounds check (`xor a; ld a,#14; sbc a,(hl); jp c,end`) and a `jp hl`
 * into ScreenDrawTable, 15 `jp`s at 3 bytes each; the empty cases 0, 7 and
 * 11 each become a `jp end` trampoline emitted in source order, which is how
 * the ROM has them (so the original spelled those cases out).
 *
 * The callees live in other banks and are called directly (nonbanked, as
 * everything in LSDj); the link step resolves them from src/bankNN.sym.
 */

#ifdef __clang__  /* editor tooling only: SDCC 2.2.1 keyword (never set by SDCC) */
#define sfr
#endif

typedef unsigned char UBYTE;

extern volatile sfr UBYTE hCurrentBank;     /* $FF8E */
extern UBYTE wCurrentScreen;                /* $C401 */
extern UBYTE rROMB0;                        /* $2000, the MBC5 ROM bank register */

/* GBDK's SWITCH_ROM_MBC1 is `*(UBYTE *)0x2000 = n`, which SDCC compiles through
 * de (`ld de,#0x2000; ld a,#n; ld (de),a`); the ROM has the named-variable form
 * (`ld hl,#0x2000; ld (hl),#n`), so LSDj wrote the register as a variable. It
 * also keeps the HRAM shadow the far-call helpers rely on. */
#define SWITCH_ROM(n) hCurrentBank = (n); rROMB0 = (n)

void DrawPhraseScreen(void);        /* $04:$4CE5 */
void DrawGrooveScreen(void);        /* $1D:$41DA */
void DrawChainScreen(void);         /* $06:$5947 */
void DrawSongScreen(void);          /* $07:$4574 */
void DrawTableScreen(void);         /* $05:$4762 */
void DrawInstrumentScreen(void);    /* $03:$4DE7 */
void DrawWaveScreen(void);          /* $06:$4F18 */
void DrawProjectScreen(void);       /* $1B:$5C17 */
void DrawSynthScreen(void);         /* $1D:$68AB */
void DrawWordScreen(void);          /* $06:$41A2 */
void DrawFileScreen(void);          /* $1C:$57BB */
void DrawHelpScreen(void);          /* $1B:$4A62 */

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
