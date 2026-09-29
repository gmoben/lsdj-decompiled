/*
 * The file screen: slots, names, compression, sync -- LSDj 9.4.0, ROM bank $1C.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE wChanActive[];                 /* $C0E0, [8] */
extern UBYTE wFsCheckResult;                /* $C56B */
extern UBYTE wFileBlockCounts[];            /* $C959, [32] */
extern UBYTE wFileField;                    /* $C992 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wFileScroll;                   /* $C99B */
extern UBYTE wSyncRxByte;                   /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
extern UBYTE FileFieldX[];                  /* $1C:$5092 */
extern UBYTE FileFieldWidth[];              /* $1C:$5095 */

void SetInterruptEnable(UBYTE ie);          /* bank 0 */
UBYTE joypad(void);                         /* bank 0 */
void SendSyncHandshake(void);               /* bank 0 */
void FileClearChanState(void);

UBYTE FileSlotHalfBase(void)
{
    return wFsCheckResult ? 0x40 : 0xC0;
}

UBYTE GetFileBlockCount(UBYTE i)
{
    if (i >= 0x20)
        DebugTrap();
    return wFileBlockCounts[i];
}

void FileClearChanState(void)
{
    UBYTE i = 8;

    do {
        wChanActive[i - 1] = 0;
    } while (--i);
}

void FileStopSound(void)
{
    SetInterruptEnable(IE_REG & 0xFB);
    wPlayMode = 0;
    NR52_REG = 0;
    FileClearChanState();
}

void FileDrawField(UBYTE pal)
{
    DrawTextField(FileFieldX[wFileField], 2, FileFieldWidth[wFileField], pal);
}

void FileDrawFieldChecked(UBYTE pal)
{
    UBYTE r;

    r = wFileCursor - wFileScroll;
    if (r >= 0x10)
        DebugTrap();
    DrawTextField(3, wFileCursor + 4 - wFileScroll, 0x0E, pal);
}

UBYTE FileCheckNameLen(void)
{
    UBYTE d;

    d = wFileCursor - wFileScroll;
    return d > 0x0B;
}

void SyncTransferByte(UBYTE *out)
{
    while (wSyncState == 2 && joypad() == 0)
        ;
    *out = wSyncRxByte;
    SendSyncHandshake();
}
