/*
 * The screen dispatchers and the B combos -- LSDj 9.4.0, ROM bank $00:$24A8-$2BC3.
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
 * order too: DispatchScreenUpdate lists the table screen (5) before chain (3)
 * and song (4), and HandleButtonEvent the SELECT case first.
 *
 * The callees live in other banks and are called directly (nonbanked, as
 * everything in LSDj); the link step resolves them from src/bankNN.sym.
 * RedrawScreen ($24DD) is asm: it ends by jumping into DispatchScreenDraw.
 */

#include "lsdj.h"

extern UBYTE wChannelActive[];              /* $C0E0, [8] */
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
void UpdateStatusPanel(void);               /* bank 6 */
void DrawSyncStatus(void);                  /* bank 6 */
UBYTE PhraseScreenInput(void);              /* the input handlers, by screen */
UBYTE GrooveScreenInput(void);
UBYTE ChainScreenInput(void);
UBYTE SongScreenInput(void);
UBYTE TableScreenInput(void);
UBYTE InstrumentScreenInput(void);
UBYTE NameEditorInput(void);
UBYTE WaveScreenInput(void);
UBYTE ProjectScreenInput(void);
UBYTE SynthScreenInput(void);
UBYTE WordScreenInput(void);
UBYTE FileScreenInput(void);
UBYTE HelpScreenInput(void);
extern volatile sfr UBYTE hSoundChannel;    /* $FF8B */
extern UBYTE wChannelOutput[];              /* $C0A0, [4] */
extern UBYTE wChannelOutputOverride[];      /* $C0A4, [4] */
extern UBYTE wChannelOutputSoloSaved[];     /* $C0A8, [4] */
extern UBYTE wMuteSoloLatch;                /* $C56A */
extern UBYTE wKeyboardActionChannel;        /* $CB9A */
extern UBYTE wAPressEnabled;                /* $C409 */
void SoundWritePanning(UBYTE v);            /* bank 2 */
void DrawAllChannelStatus(void);               /* $271A: the B combos, ahead of the screen's own input (none on the name
   editor, wave, project, file and help screens). B+SELECT mutes the cursor's
   channel (or the keyboard's), B+LEFT/RIGHT on the song screen pans it to
   one side, B+START solos it; releasing B with the latch set puts the
   output back. Returns 1 when a combo took the buttons */

/* $271A: the B combos before the screen's input: B+SELECT mute, B+LEFT/RIGHT
   pan (song screen), B+START solo, B released restores; 1 when one took the
   buttons */
UBYTE HandleInputCombos(void)
{
    UBYTE saved;
    UBYTE changed, channel, v;

    if (wCurrentScreen == SCREEN_NAME_EDITOR || wCurrentScreen == SCREEN_WAVE
            || wCurrentScreen == SCREEN_PROJECT || wCurrentScreen == SCREEN_FILE
            || wCurrentScreen == SCREEN_HELP)
        return 0;
    if (wBHeld) {
        changed = 0;
        if (wButtons == (J_SELECT | J_B)) {
            wChannelOutputOverride[wSongColumn] = 0;
            changed = 1;
        } else if (wCurrentScreen == SCREEN_SONG) {
            switch (wButtons) {
            case (J_B | J_LEFT):
                wChannelOutputOverride[wSongColumn] = 1;
                changed = 1;
                break;
            case (J_B | J_RIGHT):
                wChannelOutputOverride[wSongColumn] = 2;
                changed = 1;
                break;
            }
        }
        if (changed) {
            hSoundChannel = !(wKeyboardActionChannel == 0xFF) ? wKeyboardActionChannel : wSongColumn;
            if (!(wChannelOutput[hSoundChannel] & 0x80)) {
                saved = wChannelOutput[hSoundChannel];
                SWITCH_ROM(2);
                SoundWritePanning(wChannelOutputOverride[hSoundChannel]);
                wChannelOutput[hSoundChannel] = saved | 0x80;
                SWITCH_ROM(0x1F);
                DrawAllChannelStatus();
                wMuteSoloLatch = 1;
            }
            return 1;
        }
    }
    if (wBHeld && wButtons == (J_START | J_B)) {
        SWITCH_ROM(2);
        for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++) {
            if (wSongColumn == hSoundChannel)
                continue;
            saved = wChannelOutput[hSoundChannel];
            if (wChannelOutputOverride[hSoundChannel]) {
                wChannelOutputSoloSaved[hSoundChannel] = wChannelOutputOverride[hSoundChannel];
                wChannelOutputOverride[hSoundChannel] = 0;
            }
            SoundWritePanning(0);
            wChannelOutput[hSoundChannel] = saved | 0x80;
        }
        hSoundChannel = wSongColumn;
        wChannelOutput[wSongColumn] &= 3;
        SoundWritePanning(wChannelOutput[wSongColumn]);
        SWITCH_ROM(0x1F);
        DrawAllChannelStatus();
        wMuteSoloLatch = 2;
        return 1;
    }
    if (wCursorMode == CURSOR_SELECTING)
        return 0;
    if (wButtons == J_B && wMuteSoloLatch) {
        if (wMuteSoloLatch == 1 || wAPressEnabled) {
            channel = !(wKeyboardActionChannel == 0xFF) ? wKeyboardActionChannel : wSongColumn;
            wChannelOutput[channel] &= 3;
            wChannelOutputOverride[channel] = 0;
            wChannelOutputSoloSaved[channel] = 0;
            hSoundChannel = channel;
            SWITCH_ROM(2);
            SoundWritePanning(wChannelOutput[hSoundChannel]);
        } else {
            for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++) {
                if (wSongColumn == hSoundChannel)
                    continue;
                v = wChannelOutputSoloSaved[hSoundChannel];
                if (v)
                    wChannelOutputOverride[hSoundChannel] = v;
                else
                    wChannelOutput[hSoundChannel] &= 3;
                SWITCH_ROM(2);
                SoundWritePanning(wChannelOutput[hSoundChannel]);
            }
        }
        SWITCH_ROM(0x1F);
        DrawAllChannelStatus();
    }
    return 0;
}

/* bank $1F */

void ResetScreenSprites(void)
{
    if (wCurrentScreen != SCREEN_NAME_EDITOR && wCurrentScreen != SCREEN_WAVE
            && wCurrentScreen != SCREEN_PROJECT && wCurrentScreen != SCREEN_FILE
            && wCurrentScreen != SCREEN_HELP && wChannelActive[2])
        PositionWavSprites();
    else
        ClearSprites012();
    ClearMarkerSprites();
}

/* $254F: jp table ScreenDrawJumps indexed by wCurrentScreen ($C401):
   per-screen draw */
void DispatchScreenDraw(void)
{
    switch (wCurrentScreen) {
    case SCREEN_NONE:
        break;
    case SCREEN_PHRASE:
        SWITCH_ROM(0x04); DrawPhraseScreen();
        break;
    case SCREEN_GROOVE:
        SWITCH_ROM(0x1D); DrawGrooveScreen();
        break;
    case SCREEN_CHAIN:
        SWITCH_ROM(0x06); DrawChainScreen();
        break;
    case SCREEN_SONG:
        SWITCH_ROM(0x07); DrawSongScreen();
        break;
    case SCREEN_TABLE:
        SWITCH_ROM(0x05); DrawTableScreen();
        break;
    case SCREEN_INSTRUMENT:
        SWITCH_ROM(0x03); DrawInstrumentScreen();
        break;
    case SCREEN_NAME_EDITOR:
        break;
    case SCREEN_WAVE:
        SWITCH_ROM(0x06); DrawWaveScreen();
        break;
    case SCREEN_PROJECT:
        SWITCH_ROM(0x1B); DrawProjectScreen();
        break;
    case SCREEN_SYNTH:
        SWITCH_ROM(0x1D); DrawSynthScreen();
        break;
    case SCREEN_SPEECH:
        break;
    case SCREEN_WORD:
        SWITCH_ROM(0x06); DrawWordScreen();
        break;
    case SCREEN_FILE:
        SWITCH_ROM(0x1C); DrawFileScreen();
        break;
    case SCREEN_HELP:
        SWITCH_ROM(0x1B); DrawHelpScreen();
        break;
    }
}

/* $264B: per-frame per-screen update for screens 1-5 (table $268D) */
void DispatchScreenUpdate(void)
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
    case SCREEN_PHRASE:
        SWITCH_ROM(0x1F);
        PhraseScreenUpdate();
        break;
    case SCREEN_GROOVE:
        SWITCH_ROM(0x1D);
        GrooveScreenUpdate();
        break;
    case SCREEN_TABLE:
        SWITCH_ROM(0x05);
        TableScreenUpdate();
        break;
    case SCREEN_CHAIN:
        SWITCH_ROM(0x06);
        ChainScreenUpdate();
        break;
    case SCREEN_SONG:
        SWITCH_ROM(0x07);
        SongScreenUpdate();
        break;
    }
    if (!wRedrawPending) {
        switch (wCurrentScreen) {
        case SCREEN_NAME_EDITOR:
        case SCREEN_WAVE:
        case SCREEN_PROJECT:
        case SCREEN_FILE:
        case SCREEN_HELP:
            break;
        default:
            SWITCH_ROM(0x06);
            UpdateStatusPanel();
            DrawSyncStatus();
        }
    }
}

/* $2A71: jump table by wCurrentScreen (0-$0E, table $2A86) -> the current
   screen's input handler in its bank */
UBYTE DispatchScreenInput(void)
{
    switch (wCurrentScreen) {
    case SCREEN_NONE:
        break;
    case SCREEN_PHRASE:
        SWITCH_ROM(0x04);
        return PhraseScreenInput();
    case SCREEN_GROOVE:
        SWITCH_ROM(0x1D);
        return GrooveScreenInput();
    case SCREEN_CHAIN:
        SWITCH_ROM(0x06);
        return ChainScreenInput();
    case SCREEN_SONG:
        SWITCH_ROM(0x07);
        return SongScreenInput();
    case SCREEN_TABLE:
        SWITCH_ROM(0x05);
        return TableScreenInput();
    case SCREEN_INSTRUMENT:
        SWITCH_ROM(0x03);
        return InstrumentScreenInput();
    case SCREEN_NAME_EDITOR:
        SWITCH_ROM(0x1B);
        return NameEditorInput();
    case SCREEN_WAVE:
        SWITCH_ROM(0x06);
        return WaveScreenInput();
    case SCREEN_PROJECT:
        SWITCH_ROM(0x1B);
        return ProjectScreenInput();
    case SCREEN_SYNTH:
        SWITCH_ROM(0x1D);
        return SynthScreenInput();
    case SCREEN_SPEECH:
        break;
    case SCREEN_WORD:
        SWITCH_ROM(0x06);
        return WordScreenInput();
    case SCREEN_FILE:
        SWITCH_ROM(0x1C);
        return FileScreenInput();
    case SCREEN_HELP:
        SWITCH_ROM(0x1B);
        return HelpScreenInput();
    }
    return 0;
}

/* $2B8D: process one button event: latch A/B/Select held flags
   (wAHeld/wBHeld/wSelectHeld) from wButtons, then HandleInputCombos +
   DispatchScreenInput, OR their results */
UBYTE HandleButtonEvent(void)
{
    UBYTE combo, input;

    switch (wButtons) {
    case J_SELECT:
        wSelectHeld = 1;
        break;
    case J_A:
        wAHeld = 1;
        break;
    case J_B:
        wBHeld = 1;
        break;
    }
    combo = HandleInputCombos();
    input = DispatchScreenInput();
    return combo | input;
}
