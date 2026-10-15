/*
 * The link-cable clipboard -- LSDj 9.4.0, ROM bank $1C.
 *
 * A copy's clipboard sent to and received from a linked Game Boy.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE SC_REG;           /* $FF02 */
extern UBYTE wSyncTransmitByte;             /* $CBDC */
extern UBYTE wSyncReceiveByte;              /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
void SetInterruptEnable(UBYTE interruptMask); /* bank 0 */
UBYTE joypad(void);                         /* bank 0 */
void SendSyncHandshake(void);               /* bank 0 */
void SendSyncByte(void);                    /* bank 0 */
void delay(UWORD d);                        /* bank 0 (GBDK) */

extern UBYTE wSelectionTop;                 /* $C40E */
extern UBYTE wSelectionRight;               /* $C40F */
extern UBYTE wSelectionBottom;              /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wClipboardBuffer[][42];        /* $C57A, 6 columns of 42 cells */
extern UBYTE wSyncTransferActive;           /* $CB65 */
void SyncSetReceiveMode(void);              /* bank 0 */

/* $719C: send byte `v` over the link cable (SendSyncByte), asserting no
   transfer is under way, then wait 6 ticks (3 on DMG) */
void FileSendSyncByte(UBYTE v)
{
    UBYTE s;

    s = SC_REG & 0x81;
    if (s == 0x81)
        DebugTrap();
    wSyncTransmitByte = v;
    SendSyncByte();
    delay(hGameboyType ? 6 : 3);
}

/* After a copy on two linked Game Boys that are not playing (SYNC = LSDJ):
   send $FE, the clipboard type and the selection bounds, then the
   clipboard's cells column by column, for the other one to paste (the
   manual's "Clipboard Transfer"). */
void SendLinkClipboard(void)
{
    UBYTE column, row, ok;

    if (wCursorMode != CURSOR_COPIED)
        DebugTrap();
    if (wSelectionLeft >= wSelectionRight)
        DebugTrap();
    if (wSelectionTop >= wSelectionBottom)
        DebugTrap();
    if (wSelectionRight > 6)
        DebugTrap();
    if (wSelectionBottom > 0x2A)
        DebugTrap();
    if (wPlayMode || wSyncMode != SYNC_LSDJ)
        return;
    wSyncTransferActive = 1;
    SetInterruptEnable(0);
    FileSendSyncByte(0xFE);
    FileSendSyncByte(wClipboardType);
    FileSendSyncByte(wSelectionLeft);
    FileSendSyncByte(wSelectionTop);
    FileSendSyncByte(wSelectionRight);
    FileSendSyncByte(wSelectionBottom);
    for (column = wSelectionLeft; column < wSelectionRight; column++) {
        row = wSelectionTop;
        while (ok = row < wSelectionBottom) {
            FileSendSyncByte(wClipboardBuffer[column][row]);
            row = row + 1;
        }
    }
    SyncSetReceiveMode();
    wSyncTransferActive = 0;
}

/* $72EE: link-port byte exchange step (wSyncState, joypad,
   SendSyncHandshake) */
void SyncTransferByte(UBYTE *out)
{
    while (wSyncState == 2 && joypad() == 0)
        ;
    *out = wSyncReceiveByte;
    SendSyncHandshake();
}

/* ProcessSyncByte's $FE: receive a clipboard sent by SendLinkClipboard on the
   linked Game Boy -- its type, the selection bounds and, when they are
   sane, the cells (the column offset multiplied inside the loop and
   hoisted by SDCC, which gives the ROM's spills). */
void ReceiveLinkClipboard(void)
{
    UBYTE column, row, ok;

    wCursorMode = CURSOR_NORMAL;
    wSyncTransferActive = 1;
    SetInterruptEnable(8);
    SendSyncHandshake();
    SyncTransferByte(&wClipboardType);
    SyncTransferByte(&wSelectionLeft);
    SyncTransferByte(&wSelectionTop);
    SyncTransferByte(&wSelectionRight);
    SyncTransferByte(&wSelectionBottom);
    if (wSelectionLeft < wSelectionRight && wSelectionTop < wSelectionBottom && wSelectionRight <= 6 && wSelectionBottom <= 0x2A) {
        for (column = wSelectionLeft; column < wSelectionRight; column++) {
            row = wSelectionTop;
            while (ok = row < wSelectionBottom) {
                SyncTransferByte(&wClipboardBuffer[column][row]);
                row = row + 1;
            }
        }
        wCursorMode = CURSOR_COPIED;
    }
    SyncSetReceiveMode();
    wSyncTransferActive = 0;
}
