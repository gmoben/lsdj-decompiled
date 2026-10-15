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
 * in the ROM; a separate pointer local takes a second one). KbdPollScancode's
 * empty `case 0x27: break;` comes first, which is what inverts the ROM's
 * last compare (`jp nz,default; jp end`).
 */

#include "lsdj.h"

extern UBYTE wLinkQueueCount;                 /* $CB7D, bytes queued */
extern UBYTE wLinkQueueHead;                /* $CB80, read index */
extern UBYTE wLinkQueueBuf[];               /* $CB81, [16] ring */
extern UBYTE wKbdTranspose;                 /* $CB91 */
extern UWORD wSyncSendRate;                 /* $C954 */
extern UWORD TempoTimerTableDmg[];          /* $07:$5E49, [256] */
extern UWORD TempoTimerTableCgb[];          /* $07:$6049, [256] */

void delay(UWORD d);                        /* GBDK */
void SendSyncHandshake(void);               /* bank 0 */

extern UBYTE wKbdChanState[];               /* $CB96, [4] per-channel keyboard-active state */
extern UBYTE wKbdArmed;                     /* $CB94 */
extern UBYTE wKbdInstr;                /* $CB93 */
extern UBYTE wKbdPulseChans;               /* $CB7F */
extern char wNameEditBuf[];                 /* $C431, [5] */
extern char TxtKeyboardCalibration[];       /* $07:$7171 */
extern char TxtTapDownArrowKey[];           /* $07:$7186 */
extern char TxtOnPs2Keyboard[];             /* $07:$7199 */
extern char TxtManyTimes[];                 /* $07:$71AA */
extern char TxtFail[];                      /* $07:$71B5 */
extern char TxtOk[];                        /* $07:$71BB */
void KbdDrainQueue(void);
UBYTE SongHexDigit(UBYTE v);
void CopyInstrName(UBYTE instr);            /* bank 1 */
void ClearMessageBar(void);                 /* bank 1 */
void UiClearBgMap(void);                    /* bank 1 */

extern UBYTE wKbdHeldNote;                  /* $CB7E */
extern UBYTE wKbdNoteQueued;                /* $CB92 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wButtons;                      /* $C407 */
extern UBYTE KbdNoteMap[];             /* $07:$662A */
extern UBYTE KbdFnMap[];               /* $07:$66AA */
UBYTE PhraseCheckColFD(UBYTE row);          /* bank 4 */

extern volatile sfr UBYTE hSoundCh;         /* $FF8B */
extern volatile sfr UBYTE hFxVal;         /* $FF8F */
extern UBYTE wKbdCmdAccum;                  /* $CBA1 */
extern UBYTE wKbdCmdModifier;               /* $CB9F */
extern UBYTE wKbdSustain;                   /* $CBA0 */
extern UBYTE wKbdInstrument2;               /* $CB95 */
extern UBYTE wKbdActionCh;                  /* $CB9A */
extern UBYTE wKbdChanTrigger[];             /* $CB9B, [4] */
extern UBYTE wChanOutput[];                /* $C0A0, [4] */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
void DrawKbdInstrName(void);
void DrawKbdOctave(void);
UBYTE ProcessKbdNote(UBYTE sc);
void FxCmd_A(void);                         /* bank 2 */
void FarCallArg0(UBYTE bank, void (*fn)(void));   /* FarCallArg called without its byte (hFxVal set by hand; aliased) */

void SetSyncSendRate(void)
{
    UWORD r;

    if (wTempoMode) {
        r = 0x800;
        r *= 4 - wTempoMode;
    } else {
        r = (UWORD)(wIsCGB ? TempoTimerTableCgb : TempoTimerTableDmg);
        r += (UWORD)(wTempo - 40) << 1;
        r = *(UWORD *)r;
    }
    _asm di _endasm;
    wSyncSendRate = r;
    _asm ei _endasm;
}

void DrawKbdInstrState(void)
{
    UBYTE * volatile pu2;
    UBYTE * volatile wav;
    UBYTE t;

    wKbdChanState[0] = 0;
    pu2 = &wKbdChanState[1];
    *pu2 = 0;
    wav = &wKbdChanState[2];
    *wav = 1;
    wKbdChanState[3] = 0;
    wKbdArmed = 0;
    if (wKbdInstr < 0x40) {
        t = ReadInstrByte(wKbdInstr, 0);
        switch (t) {
        case 0:
            *wav = 0;
            switch (wKbdPulseChans) {
            case 0:
                wKbdChanState[0] = 1;
                *pu2 = 0;
                break;
            case 1:
                wKbdChanState[0] = 0;
                *pu2 = 1;
                break;
            case 2:
                wKbdChanState[0] = 1;
                *pu2 = 1;
                break;
            }
            break;
        case 2:
            wKbdArmed = 1;
            break;
        case 3:
            *wav = 0;
            break;
        }
    }
    wVramCursor = (UBYTE *)0x9991;
    hBgDigitPal = 4;
    hBgPal = 1;
    DrawStringIndexed('I');
    DrawHexByte(wKbdInstr);
}

UBYTE SongHexDigit(UBYTE d)
{
    return "0123456789ABCDEF"[d];
}

void DrawKbdInstrName(void)
{
    UBYTE i;

    DrawKbdInstrState();
    FarCallArg(1, FAR_FN(CopyInstrName), wKbdInstr);
    if (wNameEditBuf[0]) {
        for (i = 0; i < 5; i++)
            wSampleNoteString[i] = wNameEditBuf[i] ? wNameEditBuf[i] : ' ';
    } else {
        wSampleNoteString[0] = 'I';
        wSampleNoteString[1] = SongHexDigit(wKbdInstr >> 4);
        wSampleNoteString[2] = SongHexDigit(wKbdInstr & 0x0F);
        wSampleNoteString[3] = ' ';
        wSampleNoteString[4] = ' ';
    }
    FarCall(1, FAR_FN(ClearMessageBar));
}

void DrawKbdOctave(void)
{
    wVramCursor = (UBYTE *)0x99B0;
    hBgDigitPal = 4;
    hBgPal = 1;
    switch (wKbdTranspose) {
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
    wMessageStrPtr = 0;
    wMessageTimer = 100;
}

UBYTE ProcessLinkQueue(void)
{
    UBYTE mod;
    UBYTE key;
    UBYTE t;
    UBYTE u;
    UBYTE code;

    code = wLinkQueueBuf[wLinkQueueHead];
    mod = wKbdCmdAccum + wKbdCmdModifier;
    key = code | wKbdSustain;
    if (code & 0x80)
        mod |= 4;
    wLinkQueueHead++;
    wLinkQueueHead &= 0x0F;
    switch (code) {
    case 0x03:
        wKbdCmdModifier = 1;
        return 0xFF;
    case 0x07:
        if (!wKbdSustain) {
            wKbdSustain = 0x80;
            return 0xFF;
        }
        break;
    case 0x14:
        wKbdCmdAccum = wKbdSustain ? 0 : 2;
        wKbdSustain = 0;
        return 0xFF;
    }
    wKbdSustain = 0;
    wKbdCmdModifier = 0;
    if (mod & 4) {
        if (key <= 0xA0 && key >= 0x80) {
            wKbdInstr = key + 0x80;
            DrawKbdInstrName();
        } else {
        switch (key) {
        case 0xB3:
            wKbdTranspose = 0xFD;
            DrawKbdOctave();
            return 0xFF;
        case 0xB4:
            wKbdTranspose = 0x09;
            DrawKbdOctave();
            return 0xFF;
        case 0xB5:
            wKbdTranspose = 0x15;
            DrawKbdOctave();
            return 0xFF;
        case 0xB6:
            wKbdTranspose = 0x21;
            DrawKbdOctave();
            return 0xFF;
        case 0xB7:
            wKbdTranspose = 0x2D;
            DrawKbdOctave();
            return 0xFF;
        }
        wVramCursor = (UBYTE *)0x99AF;
        hBgDigitPal = 4;
        hBgPal = 1;
        switch (key) {
        case 0xA1:
            wKbdChanState[0] = 1;
            wKbdChanState[1] = 0;
            DrawString("PU1  ");
            break;
        case 0xA2:
            wKbdChanState[0] = 0;
            wKbdChanState[1] = 1;
            DrawString("PU  2");
            break;
        case 0xA3:
            wKbdChanState[0] = 1;
            wKbdChanState[1] = 1;
            DrawString("PU1+2");
            break;
        }
        wMessageStrPtr = 0;
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
        if (mod & 2) {
            wSelectHeld = 1;
            return 0xC0;
        }
        return 0x80;
    case 0x6B:
        if (!(mod & 1)) {
            wSelectHeld = 1;
            return 0x42;
        }
        return 0x02;
    case 0x17:
        if (!(mod & 1)) {
            wSelectHeld = 1;
            return 0x41;
        }
        return 0x01;
    case 0x57:
        if (!(mod & 1)) {
            wSelectHeld = 1;
            return 0x44;
        }
        return 0x04;
    case 0x27:
        if (!(mod & 1)) {
            wSelectHeld = 1;
            return 0x48;
        }
        return 0x08;
    case 0x10:
        if (wKbdInstr) {
            wKbdNoteQueued = 0;
            wKbdInstr--;
            DrawKbdInstrName();
        }
        return 0xFF;
    case 0x18:
        if (wKbdInstr != 0x40) {
            wKbdNoteQueued = 0;
            wKbdInstr++;
            DrawKbdInstrName();
        }
        return 0xFF;
    case 0x60:
        if (wKbdInstrument2) {
            wVramCursor = (UBYTE *)0x9991;
            hBgDigitPal = 4;
            hBgPal = 1;
            DrawStringIndexed('A');
            wKbdInstrument2--;
            DrawHexByte(wKbdInstrument2);
        }
        return 0xFF;
    case 0x68:
        if (wKbdInstrument2 != 0x40) {
            wVramCursor = (UBYTE *)0x9991;
            hBgDigitPal = 4;
            hBgPal = 1;
            DrawStringIndexed('A');
            wKbdInstrument2++;
            DrawHexByte(wKbdInstrument2);
        }
        return 0xFF;
    case 0x4A:
        _asm di _endasm;
        for (hSoundCh = 0; hSoundCh < 4; hSoundCh++) {
            if (wKbdChanState[hSoundCh]) {
                wKbdChanTrigger[hSoundCh] = wKbdInstrument2 | 0x80;
                hFxVal = wKbdInstrument2;
                FarCallArg0(2, FAR_FN(FxCmd_A));
            }
        }
        _asm ei _endasm;
        return 0xFF;
    case 0xCA:
        wKbdChanTrigger[0] = 0;
        wKbdChanTrigger[1] = 0;
        wKbdChanTrigger[2] = 0;
        wKbdChanTrigger[3] = 0;
        return 0xFF;
    case 0x50:
        if (wKbdTranspose != 0xFD) {
            wKbdTranspose += 0xF4;
            DrawKbdOctave();
        }
        return 0xFF;
    case 0x30:
        if (wKbdTranspose != 0x2D) {
            wKbdTranspose += 0x0C;
            DrawKbdOctave();
        }
        return 0xFF;
    case 0x28:
        wKbdNoteQueued = 0;
        wVramCursor = (UBYTE *)0x99AF;
        hBgDigitPal = 4;
        hBgPal = 1;
        t = wKbdChanState[0];
        if (t) {
            u = wKbdChanState[1];
            if (u) {
                wKbdChanState[1] = 0;
                DrawString("PU1  ");
                goto msg;
            }
        }
        if (t) {
            u = wKbdChanState[1];
            if (!u) {
                wKbdChanState[0] = 0;
                wKbdChanState[1] = 1;
                DrawString("PU  2");
                goto msg;
            }
        }
        if (!t && wKbdChanState[1]) {
            wKbdChanState[0] = 1;
            DrawString("PU1+2");
        }
msg:
        wMessageStrPtr = 0;
        wMessageTimer = 0x64;
        return 0xFF;
    case 0xC0:
        if (!(mod & 2))
            goto note;
    case 0x40:
        wKbdActionCh = 0;
        if (wChanOutput[0] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0xC8:
        if (!(mod & 2))
            goto note;
    case 0x48:
        wKbdActionCh = 1;
        if (wChanOutput[1] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0x8F:
        if (!(mod & 2))
            goto note;
    case 0x0F:
        wKbdActionCh = 2;
        if (wChanOutput[2] & 0x80)
            return 0x20;
        wBHeld = 1;
        return 0x60;
    case 0xF0:
        if (!(mod & 2))
            goto note;
    case 0x70:
        wKbdActionCh = 3;
        if (wChanOutput[3] & 0x80)
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
    return ProcessKbdNote(key);
}

UBYTE ProcessKbdNote(UBYTE sc)
{
    UBYTE note;
    UBYTE armed;
    char isPhrase;

    if (!(sc & 0x80)) {
        isPhrase = 0;
        if (wPlayMode && !wKbdHeldNote) {
            armed = wKbdArmed;
        } else if (wCurScreen == 1) {
            isPhrase = 1;
            armed = wSongCol == 3 || !FarCallArgByte(4, (UWORD)PhraseCheckColFD, wPhraseRow);
        } else {
            return 0;
        }
        if (armed) {
            note = KbdFnMap[sc];
            if (!note)
                return 0;
        } else {
            note = KbdNoteMap[sc];
            if (!note)
                return 0;
            note += wKbdTranspose;
            if (0x6C < note)
                return 0;
        }
        if (isPhrase) {
            if (wKbdHeldNote != note) {
                wKbdHeldNote = note;
                wAPressEnabled = 1;
                wButtons = 0;
            }
        } else if (armed) {
            wKbdNoteQueued &= (note & 0x0F) ? 0xF0 : 0x0F;
            wKbdNoteQueued |= note;
        } else {
            wKbdNoteQueued = note;
        }
    } else if (wCurScreen == 1) {
        sc &= 0x7F;
        armed = wSongCol == 3 || !FarCallArgByte(4, (UWORD)PhraseCheckColFD, wPhraseRow);
        if (armed) {
            note = KbdFnMap[sc];
        } else {
            note = KbdNoteMap[sc];
            note += wKbdTranspose;
        }
        if (note == wKbdHeldNote)
            wKbdHeldNote = 0;
    }
    return wKbdHeldNote ? 0x10 : 0;
}

UBYTE LinkDequeueByte(void)
{
    UBYTE v;

    while (!wLinkQueueCount)
        ;
    wLinkQueueCount--;
    v = wLinkQueueBuf[wLinkQueueHead++];
    wLinkQueueHead &= 0x0F;
    return v;
}

void KbdDrainQueue(void)
{
    delay(10);
    while (wLinkQueueCount)
        LinkDequeueByte();
    SendSyncHandshake();
}

UBYTE KbdPollScancode(void)
{
    UBYTE i, b;

    KbdDrainQueue();
    for (i = 0; i != 20; i++) {
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

void DrawKbdCalibScreen(void)
{
    UBYTE k;

    FarCall(1, UiClearBgMap);
    ClearBgTilemap();
    hBgDigitPal = 4;
    hBgPal = 1;
    wVramCursor = (UBYTE *)0x9800;
    DrawString(TxtKeyboardCalibration);
    wVramCursor = (UBYTE *)0x9840;
    DrawString(TxtTapDownArrowKey);
    wVramCursor = (UBYTE *)0x9860;
    DrawString(TxtOnPs2Keyboard);
    wVramCursor = (UBYTE *)0x9880;
    DrawString(TxtManyTimes);
    wVramCursor = (UBYTE *)0x98A0;
    wSyncMode2 = 4;
    for (;;) {
        k = KbdPollScancode();
        if (k)
            break;
        DrawStringIndexed('.');
        wSyncMode2 += 4;
        if (!wSyncMode2) {
            DrawString(TxtFail);
            delay(1000);
            KbdDrainQueue();
            return;
        }
    }
    for (;;) {
        if (wSyncMode2 == 1)
            break;
        wSyncMode2--;
        DrawStringIndexed('.');
        k = KbdPollScancode();
        if (!k) {
            wSyncMode2 += 10;
            break;
        }
    }
    DrawString(TxtOk);
    delay(1000);
    KbdDrainQueue();
}
