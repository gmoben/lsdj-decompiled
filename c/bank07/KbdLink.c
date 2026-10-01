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
extern UBYTE wKbdInstrument;                /* $CB93 */
extern UBYTE wKbdHeldNote_p1;               /* $CB7F */
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
    if (wKbdInstrument < 0x40) {
        t = ReadInstrByte(wKbdInstrument, 0);
        switch (t) {
        case 0:
            *wav = 0;
            switch (wKbdHeldNote_p1) {
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
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawStringIndexed('I');
    DrawHexByte(wKbdInstrument);
}

UBYTE SongHexDigit(UBYTE d)
{
    return "0123456789ABCDEF"[d];
}

void DrawKbdInstrName(void)
{
    UBYTE i;

    DrawKbdInstrState();
    FarCallArg(1, FAR_FN(CopyInstrName), wKbdInstrument);
    if (wNameEditBuf[0]) {
        for (i = 0; i < 5; i++)
            wSampleNoteString[i] = wNameEditBuf[i] ? wNameEditBuf[i] : ' ';
    } else {
        wSampleNoteString[0] = 'I';
        wSampleNoteString[1] = SongHexDigit(wKbdInstrument >> 4);
        wSampleNoteString[2] = SongHexDigit(wKbdInstrument & 0x0F);
        wSampleNoteString[3] = ' ';
        wSampleNoteString[4] = ' ';
    }
    FarCall(1, FAR_FN(ClearMessageBar));
}

void DrawKbdOctave(void)
{
    wVramCursor = (UBYTE *)0x99B0;
    hBgDigitPalette = 4;
    hBgPalette = 1;
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
    wMsgStrPtr = 0;
    wMsgTimer = 100;
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

void DrawKeyboardCalibScreen(void)
{
    UBYTE k;

    FarCall(1, UiClearBgMap);
    ClearBgTilemap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
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
