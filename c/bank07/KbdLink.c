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

UBYTE SongHexDigit(UBYTE d)
{
    return "0123456789ABCDEF"[d];
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
