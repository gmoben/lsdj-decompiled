/*
 * The file screen: slots, names, compression, sync -- LSDj 9.4.0, ROM bank $1C.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern volatile sfr UBYTE SC_REG;           /* $FF02 */
extern UBYTE wChanActive[];                 /* $C0E0, [8] */
extern UBYTE wFsCheckResult;                /* $C56B */
extern UBYTE wFileBlockIdx;                 /* $C958 */
extern UBYTE wFileBlockCounts[];            /* $C959, [32] */
extern UBYTE wCompByte;                     /* $C97F */
extern UBYTE wCompBank;                     /* $C979 */
extern UWORD wCompSrc;                      /* $C97A, an SRAM address */
extern UWORD wCompDst;                      /* $C97D, a working-song offset */
extern UBYTE wDecompPos;                    /* $C983 */
extern UBYTE wDecompBuf[];                  /* $DA00, [256] */
extern UBYTE DefaultWaveFrameB1C[];         /* $1C:$4047 */
extern UBYTE wFileField;                    /* $C992 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wFileScroll;                   /* $C99B */
extern UBYTE wSyncTxByte;                   /* $CBDC */
extern UBYTE wSyncRxByte;                   /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
extern UBYTE FileFieldX[];                  /* $1C:$5092 */
extern UBYTE FileFieldWidth[];              /* $1C:$5095 */

void SetInterruptEnable(UBYTE ie);          /* bank 0 */
UBYTE joypad(void);                         /* bank 0 */
void SendSyncHandshake(void);               /* bank 0 */
void SendSyncByte(void);                    /* bank 0 */
void delay(UWORD d);                        /* bank 0 (GBDK) */
UBYTE GetFileBlockCount(UBYTE i);
UBYTE FileFindSlotEnd(void);
UBYTE FileSlotHalfBase(void);
void WriteDecompByte(UBYTE v);
UBYTE FileClassifyToken(void);              /* asm */
UBYTE ReadWorkingSongByte(UWORD addr);      /* asm */
void FileClearChanState(void);

void FileWriteCompressedSram(void)
{
    if (!FileClassifyToken())
        DebugTrap();
    rRAMB = wCompBank;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    memcpy((UBYTE *)wCompSrc, wDecompBuf, 0x100);
    rRAMG = 0;
    _asm
        ei
    _endasm;
    wCompSrc += 0x100;
    wDecompPos = 0;
}

UBYTE FileSlotHalfBase(void)
{
    return wFsCheckResult ? 0x40 : 0xC0;
}

UBYTE FileScanSlotNames(void)
{
    wFileBlockIdx = 1;
    do {
        if (wCompByte == ReadSongByte(4, (UBYTE *)0xA140 + (unsigned int)wFileBlockIdx)) {
            WriteDecompByte(wFileBlockIdx);
            return 1;
        }
        wFileBlockIdx++;
    } while (wFileBlockIdx < FileSlotHalfBase());
    return 0;
}

UBYTE GetFileBlockCount(UBYTE i)
{
    if (i >= 0x20)
        DebugTrap();
    return wFileBlockCounts[i];
}

UBYTE FileReadCompressedBlock(void)
{
    UBYTE i;

    if (wCompDst >= (UWORD)0x7FF1)
        return 0;
    for (i = 0; i < 0x10; i++) {
        if (ReadWorkingSongByte(wCompDst + i) != DefaultWaveFrameB1C[i])
            return 0;
    }
    return 1;
}

UBYTE FileFindSlotEnd(void)
{
    UWORD p;
    UBYTE m, v;

    p = 0xA141;
    m = 0xFF;
    for (; p < 0xA200; p = p + 1) {
        v = ReadSongByte(4, p);
        if (v < 0x20) {
            if (m == 0xFF)
                m = v;
            else if (m < v)
                m = v;
        }
    }
    return m;
}

void MigrateTempoProps(void)
{
    WriteSongByte(1, (UBYTE *)0xBFCB, ReadSongByte(1, (UBYTE *)0xBFB3));
    WriteSongByte(1, (UBYTE *)0xBFCA, ReadSongByte(1, (UBYTE *)0xBFB2));
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

void FileValidateName(void)
{
    UBYTE e, d;

    e = FileFindSlotEnd();
    if (e < wFileCursor)
        wFileCursor = e;
    d = wFileCursor - wFileScroll;
    if (d > 0x0A) {
        wFileScroll = wFileCursor;
        while ((d = wFileCursor - wFileScroll) < 0x0A && wFileScroll)
            wFileScroll--;
    }
    if (wFileCursor == 0x1F)
        wFileScroll = 0x14;
    if (wFileCursor >= 0x20)
        DebugTrap();
}

void FileMatchName(void)
{
    UBYTE i;

    i = wFileCursor;
    while (++i < 0x20) {
        if (FileFindSlotEnd() + 1 < i)
            break;
        if (ReadSongByte(4, (UBYTE *)0xA140) == i || GetFileBlockCount(i) == 0) {
            wFileCursor = i;
            break;
        }
    }
    if (wFileCursor >= 0x20)
        DebugTrap();
}

void FileCopyNameToBuf(void)
{
    UBYTE i;

    i = wFileCursor;
    while (--i != 0xFF) {
        if (ReadSongByte(4, (UBYTE *)0xA140) == i || GetFileBlockCount(i) == 0) {
            wFileCursor = i;
            break;
        }
    }
    if (wFileCursor >= 0x20)
        DebugTrap();
}

UBYTE FileCheckNameLen(void)
{
    UBYTE d;

    d = wFileCursor - wFileScroll;
    return d > 0x0B;
}

void FileCheckByte6973(UBYTE *p)
{
    UBYTE v;

    v = *p;
    if ((v & 0xF0) != 0x80) {
        *p = v & 0xF0;
        v++;
        v &= 0x0F;
        *p |= v;
    }
}

void FileSendSyncByte(UBYTE v)
{
    UBYTE s;

    s = SC_REG & 0x81;
    if (s == 0x81)
        DebugTrap();
    wSyncTxByte = v;
    SendSyncByte();
    delay(hGameboyType ? 6 : 3);
}

void SyncTransferByte(UBYTE *out)
{
    while (wSyncState == 2 && joypad() == 0)
        ;
    *out = wSyncRxByte;
    SendSyncHandshake();
}
