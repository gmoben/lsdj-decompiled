/*
 * Keyboard, link queue and sync rate -- LSDj 9.4.0, ROM bank $07:$5DD8-$73FF.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * SetSyncSendRate multiplies by 0x800 through a variable (`r = 0x800;
 * r *= ...`): a literal operand of `*` is moved to the right and a power of
 * two there becomes a shift, while the ROM calls MulU16 with 0x800 as the
 * left operand. Its table read goes through the same UWORD (one stack slot
 * in the ROM; a separate pointer local takes a second one). KeyboardPollScancode's
 * empty `case 0x27: break;` comes first, which is what inverts the ROM's
 * last compare (`jp nz,default; jp end`).
 */

#include "lsdj.h"

extern UBYTE wLinkQueueCount;               /* $CB7D, bytes queued */
extern UBYTE wLinkQueueHead;                /* $CB80, read index */
extern UBYTE wLinkQueueBuffer[];            /* $CB81, [16] ring */
extern UBYTE wKeyboardTranspose;            /* $CB91 */
extern UWORD wSyncSendRate;                 /* $C954 */
extern UWORD TempoTimerTable[];             /* $07:$5E49, [256] */
extern UWORD TempoTimerTableSgb[];          /* $07:$6049, [256] */

void delay(UWORD d);                        /* GBDK */
void SendSyncHandshake(void);               /* bank 0 */

extern UBYTE wKeyboardChannelState[];       /* $CB96, [4] per-channel keyboard-active state */
extern UBYTE wKeyboardArmed;                /* $CB94 */
extern UBYTE wKeyboardInstrument;           /* $CB93 */
extern UBYTE wKeyboardPulseChannels;        /* $CB7F */
extern char wNameEditBuffer[];              /* $C431, [5] */
extern char TxtKeyboardCalibration[];       /* $07:$7171 */
extern char TxtTapDownArrowKey[];           /* $07:$7186 */
extern char TxtOnPs2Keyboard[];             /* $07:$7199 */
extern char TxtManyTimes[];                 /* $07:$71AA */
extern char TxtFail[];                      /* $07:$71B5 */
extern char TxtOk[];                        /* $07:$71BB */
void KeyboardDrainQueue(void);
UBYTE HexDigitCharacter(UBYTE v);
void CopyInstrumentName(UBYTE instrument);  /* bank 1 */
void ClearMessageBar(void);                 /* bank 1 */
void ClearBgMapAndAttributes(void);         /* bank 1 */

extern UBYTE wKeyboardHeldNote;             /* $CB7E */
extern UBYTE wKeyboardNoteQueued;           /* $CB92 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C407 */
extern UBYTE KeyboardNoteMap[];             /* $07:$662A */
extern UBYTE KeyboardFunctionMap[];         /* $07:$66AA */
UBYTE PhraseRowIsSentinel(UBYTE row);       /* bank 4 */

extern volatile sfr UBYTE hSoundChannel;    /* $FF8B */
extern volatile sfr UBYTE hCommandValue;    /* $FF8F */
extern UBYTE wKeyboardCommandAccumulator;   /* $CBA1 */
extern UBYTE wKeyboardCommandModifier;      /* $CB9F */
extern UBYTE wKeyboardSustain;              /* $CBA0 */
extern UBYTE wKeyboardInstrument2;          /* $CB95 */
extern UBYTE wKeyboardActionChannel;        /* $CB9A */
extern UBYTE wKeyboardChannelTrigger[];     /* $CB9B, [4] */
extern UBYTE wChannelOutput[];              /* $C0A0, [4] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
void DrawKeyboardInstrumentName(void);
void DrawKeyboardOctave(void);
UBYTE ProcessKeyboardNote(UBYTE scancode);
void Command_A(void);                       /* bank 2 */
void FarCallArgument0(UBYTE bank, void (*function)(void)); /* FarCallArgument called without its byte (hCommandValue set by hand; aliased) */

/* $5DD8: wSyncSendRate (interrupts off) from the tempo: $800 x (4 -
   wTempoMode) for the sync speeds, else TempoTimerTable[wTempo - 40]
   (TempoTimerTableSgb on a Super Game Boy) */
void SetSyncSendRate(void)
{
    UWORD r;

    if (wTempoMode) {
        r = 0x800;
        r *= 4 - wTempoMode;
    } else {
        r = (UWORD)(wIsSgb ? TempoTimerTableSgb : TempoTimerTable);
        r += (UWORD)(wTempo - 40) << 1;
        r = *(UWORD *)r;
    }
    _asm di _endasm;
    wSyncSendRate = r;
    _asm ei _endasm;
}

/* $6543: keyboard state cell (arrows by wKeyboardPulseChannels) + instrument
   hex (wKeyboardInstrument) at row 12 */
void DrawKeyboardInstrumentState(void)
{
    UBYTE * volatile pu2;
    UBYTE * volatile wav;
    UBYTE t;

    wKeyboardChannelState[0] = 0;
    pu2 = &wKeyboardChannelState[1];
    *pu2 = 0;
    wav = &wKeyboardChannelState[2];
    *wav = 1;
    wKeyboardChannelState[3] = 0;
    wKeyboardArmed = 0;
    if (wKeyboardInstrument < 0x40) {
        t = ReadInstrumentByte(wKeyboardInstrument, INSTRUMENT_BYTE_TYPE);
        switch (t) {
        case 0:
            *wav = 0;
            switch (wKeyboardPulseChannels) {
            case 0:
                wKeyboardChannelState[0] = 1;
                *pu2 = 0;
                break;
            case 1:
                wKeyboardChannelState[0] = 0;
                *pu2 = 1;
                break;
            case 2:
                wKeyboardChannelState[0] = 1;
                *pu2 = 1;
                break;
            }
            break;
        case 2:
            wKeyboardArmed = 1;
            break;
        case 3:
            *wav = 0;
            break;
        }
    }
    wVramCursor = (UBYTE *)BG_MAP(17, 12);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('I');
    DrawHexByte(wKeyboardInstrument);
}

/* $672A: the hex digit for `d` (0-15) */
UBYTE HexDigitCharacter(UBYTE d)
{
    return "0123456789ABCDEF"[d];
}

/* $674D: DrawKeyboardInstrumentState + instrument name via
   CopyInstrumentName -> wSampleNoteString */
void DrawKeyboardInstrumentName(void)
{
    UBYTE letter;

    DrawKeyboardInstrumentState();
    FarCallArgument(1, FAR_FN(CopyInstrumentName), wKeyboardInstrument);
    if (wNameEditBuffer[0]) {
        for (letter = 0; letter < 5; letter++)
            wSampleNoteString[letter] = wNameEditBuffer[letter] ? wNameEditBuffer[letter] : ' ';
    } else {
        wSampleNoteString[0] = 'I';
        wSampleNoteString[1] = HexDigitCharacter(wKeyboardInstrument >> 4);
        wSampleNoteString[2] = HexDigitCharacter(wKeyboardInstrument & 0x0F);
        wSampleNoteString[3] = ' ';
        wSampleNoteString[4] = ' ';
    }
    FarCall(1, FAR_FN(ClearMessageBar));
}

/* $6812: draw the keyboard octave range ("O3-4".."O7-8") from
   wKeyboardTranspose */
void DrawKeyboardOctave(void)
{
    wVramCursor = (UBYTE *)BG_MAP(16, 13);
    hBgDigitPalette = 4;
    hBgPalette = 1;
    switch (wKeyboardTranspose) {
    case 0x2D:
        DrawString("O7-8");
        break;
    case 0x21:
        DrawString("O6-7");
        break;
    case 0x15:
        DrawString("O5-6");
        break;
    case 0x09:
        DrawString("O4-5");
        break;
    case 0xFD:
        DrawString("O3-4");
        break;
    }
    wMessageStringPointer = 0;
    wMessageTimer = 100;
}

/* $68A2: drain the link-port ring buffer (wLinkQueueBuffer+, head
   wLinkQueueHead): keyboard scancodes / sync bytes */
UBYTE ProcessLinkQueue(void)
{
    UBYTE modifier;
    UBYTE key;
    UBYTE t;
    UBYTE u;
    UBYTE code;

    code = wLinkQueueBuffer[wLinkQueueHead];
    modifier = wKeyboardCommandAccumulator + wKeyboardCommandModifier;
    key = code | wKeyboardSustain;
    if (code & 0x80)
        modifier |= 4;
    wLinkQueueHead++;
    wLinkQueueHead &= 0x0F;
    switch (code) {
    case 0x03:
        wKeyboardCommandModifier = 1;
        return 0xFF;
    case 0x07:
        if (!wKeyboardSustain) {
            wKeyboardSustain = 0x80;
            return 0xFF;
        }
        break;
    case 0x14:
        wKeyboardCommandAccumulator = wKeyboardSustain ? 0 : 2;
        wKeyboardSustain = 0;
        return 0xFF;
    }
    wKeyboardSustain = 0;
    wKeyboardCommandModifier = 0;
    if (modifier & 4) {
        if (key <= 0xA0 && key >= 0x80) {
            wKeyboardInstrument = key + 0x80;
            DrawKeyboardInstrumentName();
        } else {
        switch (key) {
        case 0xB3:
            wKeyboardTranspose = 0xFD;
            DrawKeyboardOctave();
            return 0xFF;
        case 0xB4:
            wKeyboardTranspose = 0x09;
            DrawKeyboardOctave();
            return 0xFF;
        case 0xB5:
            wKeyboardTranspose = 0x15;
            DrawKeyboardOctave();
            return 0xFF;
        case 0xB6:
            wKeyboardTranspose = 0x21;
            DrawKeyboardOctave();
            return 0xFF;
        case 0xB7:
            wKeyboardTranspose = 0x2D;
            DrawKeyboardOctave();
            return 0xFF;
        }
        wVramCursor = (UBYTE *)BG_MAP(15, 13);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        switch (key) {
        case 0xA1:
            wKeyboardChannelState[0] = 1;
            wKeyboardChannelState[1] = 0;
            DrawString("PU1  ");
            break;
        case 0xA2:
            wKeyboardChannelState[0] = 0;
            wKeyboardChannelState[1] = 1;
            DrawString("PU  2");
            break;
        case 0xA3:
            wKeyboardChannelState[0] = 1;
            wKeyboardChannelState[1] = 1;
            DrawString("PU1+2");
            break;
        }
        wMessageStringPointer = 0;
        wMessageTimer = 0x64;
        }
        return 0xFF;
    }
    switch (key) {
    case 0x5F:
        wBHeld = 1;
        return 0x24;
    case 0x2F:
        wBHeld = 1;
        return 0x28;
    case 0x2D:
        if (modifier & 2) {
            wSelectHeld = 1;
            return 0xC0;
        }
        return 0x80;
    case 0x6B:
        if (!(modifier & 1)) {
            wSelectHeld = 1;
            return 0x42;
        }
        return 0x02;
    case 0x17:
        if (!(modifier & 1)) {
            wSelectHeld = 1;
            return 0x41;
        }
        return 0x01;
    case 0x57:
        if (!(modifier & 1)) {
            wSelectHeld = 1;
            return 0x44;
        }
        return 0x04;
    case 0x27:
        if (!(modifier & 1)) {
            wSelectHeld = 1;
            return 0x48;
        }
        return 0x08;
    case 0x10:
        if (wKeyboardInstrument) {
            wKeyboardNoteQueued = 0;
            wKeyboardInstrument--;
            DrawKeyboardInstrumentName();
        }
        return 0xFF;
    case 0x18:
        if (wKeyboardInstrument != 0x40) {
            wKeyboardNoteQueued = 0;
            wKeyboardInstrument++;
            DrawKeyboardInstrumentName();
        }
        return 0xFF;
    case 0x60:
        if (wKeyboardInstrument2) {
            wVramCursor = (UBYTE *)BG_MAP(17, 12);
            hBgDigitPalette = 4;
            hBgPalette = 1;
            DrawStringIndexed('A');
            wKeyboardInstrument2--;
            DrawHexByte(wKeyboardInstrument2);
        }
        return 0xFF;
    case 0x68:
        if (wKeyboardInstrument2 != 0x40) {
            wVramCursor = (UBYTE *)BG_MAP(17, 12);
            hBgDigitPalette = 4;
            hBgPalette = 1;
            DrawStringIndexed('A');
            wKeyboardInstrument2++;
            DrawHexByte(wKeyboardInstrument2);
        }
        return 0xFF;
    case 0x4A:
        _asm di _endasm;
        for (hSoundChannel = 0; hSoundChannel < 4; hSoundChannel++) {
            if (wKeyboardChannelState[hSoundChannel]) {
                wKeyboardChannelTrigger[hSoundChannel] = wKeyboardInstrument2 | 0x80;
                hCommandValue = wKeyboardInstrument2;
                FarCallArgument0(2, FAR_FN(Command_A));
            }
        }
        _asm ei _endasm;
        return 0xFF;
    case 0xCA:
        wKeyboardChannelTrigger[0] = 0;
        wKeyboardChannelTrigger[1] = 0;
        wKeyboardChannelTrigger[2] = 0;
        wKeyboardChannelTrigger[3] = 0;
        return 0xFF;
    case 0x50:
        if (wKeyboardTranspose != 0xFD) {
            wKeyboardTranspose += 0xF4;
            DrawKeyboardOctave();
        }
        return 0xFF;
    case 0x30:
        if (wKeyboardTranspose != 0x2D) {
            wKeyboardTranspose += 0x0C;
            DrawKeyboardOctave();
        }
        return 0xFF;
    case 0x28:
        wKeyboardNoteQueued = 0;
        wVramCursor = (UBYTE *)BG_MAP(15, 13);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        t = wKeyboardChannelState[0];
        if (t) {
            u = wKeyboardChannelState[1];
            if (u) {
                wKeyboardChannelState[1] = 0;
                DrawString("PU1  ");
                goto message;
            }
        }
        if (t) {
            u = wKeyboardChannelState[1];
            if (!u) {
                wKeyboardChannelState[0] = 0;
                wKeyboardChannelState[1] = 1;
                DrawString("PU  2");
                goto message;
            }
        }
        if (!t && wKeyboardChannelState[1]) {
            wKeyboardChannelState[0] = 1;
            DrawString("PU1+2");
        }
message:
        wMessageStringPointer = 0;
        wMessageTimer = 0x64;
        return 0xFF;
    case 0xC0:
        if (!(modifier & 2))
            goto note;
    case 0x40:
        wKeyboardActionChannel = 0;
        if (wChannelOutput[0] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0xC8:
        if (!(modifier & 2))
            goto note;
    case 0x48:
        wKeyboardActionChannel = 1;
        if (wChannelOutput[1] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0x8F:
        if (!(modifier & 2))
            goto note;
    case 0x0F:
        wKeyboardActionChannel = 2;
        if (wChannelOutput[2] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0xF0:
        if (!(modifier & 2))
            goto note;
    case 0x70:
        wKeyboardActionChannel = 3;
        if (wChannelOutput[3] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0x07:
        return 0x0D;
    case 0xC7:
        return 0x0C;
    case 0x9B:
        return 0x0E;
    case 0xCB:
        return 0x0F;
    }
note:
    return ProcessKeyboardNote(key);
}

/* $6E50: PS/2 keyboard note -> play/preview on the channel
   (wKeyboardHeldNote; screen/col aware) */
UBYTE ProcessKeyboardNote(UBYTE scancode)
{
    UBYTE note;
    UBYTE armed;
    char isPhrase;

    if (!(scancode & 0x80)) {
        isPhrase = 0;
        if (wPlayMode && !wKeyboardHeldNote) {
            armed = wKeyboardArmed;
        } else if (wCurrentScreen == SCREEN_PHRASE) {
            isPhrase = 1;
            armed = wSongColumn == CHANNEL_NOI
                    || !FarCallArgumentByte(4, (UWORD)PhraseRowIsSentinel, wPhraseRow);
        } else {
            return 0;
        }
        if (armed) {
            note = KeyboardFunctionMap[scancode];
            if (!note)
                return 0;
        } else {
            note = KeyboardNoteMap[scancode];
            if (!note)
                return 0;
            note += wKeyboardTranspose;
            if (0x6C < note)
                return 0;
        }
        if (isPhrase) {
            if (wKeyboardHeldNote != note) {
                wKeyboardHeldNote = note;
                wAPressEnabled = 1;
                wButtons = 0;
            }
        } else if (armed) {
            wKeyboardNoteQueued &= (note & 0x0F) ? 0xF0 : 0x0F;
            wKeyboardNoteQueued |= note;
        } else {
            wKeyboardNoteQueued = note;
        }
    } else if (wCurrentScreen == SCREEN_PHRASE) {
        scancode &= 0x7F;
        armed = wSongColumn == CHANNEL_NOI
                || !FarCallArgumentByte(4, (UWORD)PhraseRowIsSentinel, wPhraseRow);
        if (armed) {
            note = KeyboardFunctionMap[scancode];
        } else {
            note = KeyboardNoteMap[scancode];
            note += wKeyboardTranspose;
        }
        if (note == wKeyboardHeldNote)
            wKeyboardHeldNote = 0;
    }
    return wKeyboardHeldNote ? 0x10 : 0;
}

/* $7012: wait for a queued link byte and pop it from the wLinkQueueBuffer
   ring (index wLinkQueueHead, 16 entries) */
UBYTE LinkDequeueByte(void)
{
    UBYTE v;

    while (!wLinkQueueCount)
        ;
    wLinkQueueCount--;
    v = wLinkQueueBuffer[wLinkQueueHead++];
    wLinkQueueHead &= 0x0F;
    return v;
}

/* $7033: delay(10), drop every queued link byte, then SendSyncHandshake */
void KeyboardDrainQueue(void)
{
    delay(10);
    while (wLinkQueueCount)
        LinkDequeueByte();
    SendSyncHandshake();
}

/* $704E: drain the queue, then expect 20 key records (3, [7,] $27) from the
   keyboard: 1 if all arrive (the calibration taps) */
UBYTE KeyboardPollScancode(void)
{
    UBYTE attempt, b;

    KeyboardDrainQueue();
    for (attempt = 0; attempt != 20; attempt++) {
        b = LinkDequeueByte();
        if (b != 3)
            return 0;
        b = LinkDequeueByte();
        switch (b) {
        case 0x27:
            break;
        case 7:
            b = LinkDequeueByte();
            if (b != 0x27)
                return 0;
            break;
        default:
            return 0;
        }
    }
    return 1;
}

/* $7097: PS/2 "KEYBOARD CALIBRATION" screen (tap down-arrow prompt) */
void DrawCalibrationScreen(void)
{
    UBYTE scancode;

    FarCall(1, ClearBgMapAndAttributes);
    ClearBgTilemap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)BG_MAP(0, 0);
    DrawString(TxtKeyboardCalibration);
    wVramCursor = (UBYTE *)BG_MAP(0, 2);
    DrawString(TxtTapDownArrowKey);
    wVramCursor = (UBYTE *)BG_MAP(0, 3);
    DrawString(TxtOnPs2Keyboard);
    wVramCursor = (UBYTE *)BG_MAP(0, 4);
    DrawString(TxtManyTimes);
    wVramCursor = (UBYTE *)BG_MAP(0, 5);
    wSyncMode2 = 4;
    for (;;) {
        scancode = KeyboardPollScancode();
        if (scancode)
            break;
        DrawStringIndexed('.');
        wSyncMode2 += 4;
        if (!wSyncMode2) {
            DrawString(TxtFail);
            delay(1000);
            KeyboardDrainQueue();
            return;
        }
    }
    for (;;) {
        if (wSyncMode2 == 1)
            break;
        wSyncMode2--;
        DrawStringIndexed('.');
        scancode = KeyboardPollScancode();
        if (!scancode) {
            wSyncMode2 += 10;
            break;
        }
    }
    DrawString(TxtOk);
    delay(1000);
    KeyboardDrainQueue();
}
