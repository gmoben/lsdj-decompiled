/*
 * The top bar -- LSDj 9.4.0, ROM bank $01.
 *
 * The top status bar: the timed message or the screen title, a command's
 * help line.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wMessageNoPause;                  /* $C530 */
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
UBYTE SramHasFileBanks(void);               /* $1C:$4057 */
void DrawChannelHeader(void);               /* bank 0 */
extern UBYTE TxtBlankString2[];             /* $01:$7498, 20 zero attribute bytes */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wGroove;                       /* $C417 */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wTable;                        /* $C41B */
extern UWORD wTopBarLength;                    /* $C7AD, wTopBarDrawScratch as a word (aliased by match_function.py) */
extern UWORD wVramCursorW;                  /* $C573, wVramCursor as a plain word: its `< 0x9811` is a 32-bit compare (aliased by match_function.py) */

void DrawTopBar(void)
{
    UBYTE i;
    UBYTE isFile;

    wVramCursorW = BG_MAP(0, 0);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    if (wMessageStringPointer)
        return;
    isFile = wCurrentScreen == SCREEN_FILE;
    if (!isFile && wCurrentScreen != SCREEN_PROJECT && hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0, 0, 20, 1, TxtBlankString2);
        VBK_REG = 0;
    }
    wVramCursorW = BG_MAP(0, 0);
    switch (wCurrentScreen) {
    default:
        if (!isFile)
            break;
        goto file;
    case SCREEN_SONG:
        if (wLiveMode)
            wTopBarDrawScratch = TxtScreenLive;
        else
            wTopBarDrawScratch = TxtScreenSong;
        DrawString(wTopBarDrawScratch);
        break;
    case SCREEN_CHAIN:
        DrawString(TxtScreenChain);
        DrawHexByte(wChain);
        break;
    case SCREEN_PHRASE:
        DrawString(TxtScreenPhrase);
        DrawHexByte(wPhrase);
        break;
    case SCREEN_TABLE:
        DrawString(TxtScreenTable);
        DrawHexByte(wTable);
        break;
    case SCREEN_PROJECT:
        DrawString(TxtScreenProject);
        if (FarCallByte(0x1C, FAR_FN(SramHasFileBanks)))
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
    case SCREEN_INSTRUMENT:
        DrawString(TxtScreenInstr);
        DrawHexByte(wInstrument);
        break;
    case SCREEN_WAVE:
        DrawString(TxtScreenWave);
        DrawHexByte(wSynth);
        break;
    case SCREEN_SYNTH:
        DrawString(TxtScreenSynth);
        DrawByte(wSynthScreen);
        break;
    case SCREEN_GROOVE:
        DrawString(TxtScreenGroove);
        DrawHexByte(wGroove);
        break;
    }
    while (wVramCursorW < BG_MAP(17, 0))
        DrawStringIndexed(' ');
    if (wCurrentScreen != SCREEN_PROJECT && !isFile)
        DrawChannelHeader();
}

void ShowCommandHelp(UBYTE command)
{
    UBYTE *saved;
    UBYTE *s;

    saved = wVramCursor;
    if (!command)
        return;
    if (command > 0x13)
        DebugTrap();
    switch (command) {
    case 2:
        s = wCurrentScreen == SCREEN_PHRASE ? TxtHelpBNote : TxtHelpBHop;
        break;
    case 8:
        s = wCurrentScreen == SCREEN_PHRASE ? TxtHelpHPhrase : TxtHelpHTable;
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
        s = CommandHelpPointers[command];
        break;
    }
    ShowMessage(s);
    wVramCursor = saved;
}

void ClearMessageBar(void)
{
    UBYTE letter = 0;

    wMessageStringPointer = 0;
    wVramCursor = (UBYTE *)BG_MAP(15, 12);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    do {
        DrawStringIndexed(wSampleNoteString[letter]);
    } while (letter++ != 4);
    wMessageTimer = 100;
    wMessagePanelShown = 1;
}

void UpdateMessage(void)
{
    UBYTE letter;

    if (wSelectHeld && !wMessageNoPause)
        return;
    if (wMessageStringPointer) {
        if (!wMessageTimer) {
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            wVramCursor = (UBYTE *)BG_MAP(0, 0);
            for (letter = 0; letter < 20; letter++)
                DrawStringIndexed(wMessageStringPointer[letter]);
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
                wVramCursor = (UBYTE *)BG_MAP(15, 12);
                DrawString("     ");
            } else {
                DrawTopBar();
            }
        }
    }
}
