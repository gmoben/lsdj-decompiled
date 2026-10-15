/*
 * The file system -- LSDj 9.4.0, ROM bank $1C.
 *
 * SRAM's song files: the bank and format checks, the block allocation
 * table, the slots' names and block counts.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wFilesystemCheckResult;                /* $C56B */
extern UBYTE wFileBlockIndex;                 /* $C958 */
extern UBYTE wFileBlockCounts[];            /* $C959, [32] */
extern UBYTE *wFileBlockCountPointer;           /* $C990 */
extern UBYTE wCompressByte;                     /* $C97F */
extern UWORD wCompressDestination;                      /* $C97D, a working-song offset */
extern UBYTE wDecompressPosition;                    /* $C983 */
extern char wFileNameBuffer[];                 /* $C987, [8] */
extern UBYTE wFileNameTerm[];               /* $C98F, its NUL */
extern void (*wBlockSinkPointer)(void);         /* $C984, FlushBlockToSram or the verify sink */
extern UBYTE wCompressProgress;                 /* $C981 */
extern UBYTE wDecompressOk;                     /* $C982 */
extern UBYTE wBlockDestinationBank;                 /* $C97C */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wFileScroll;                   /* $C99B */
void LoadFileListState(void);
void FlushBlockToSram(void);                /* $1C:$743C */
void FileDecompressByte(void);              /* $1C:$749E */
void VerifyBlockAgainstSram(void);         /* $1C:$746E */
UBYTE FindFreeBlock(UBYTE *bank, UWORD *source);   /* $1C:$415A */
UBYTE FileLoadSlotInfo(UBYTE slot, UBYTE verify);
void CountFileBlocks(void);                 /* $1C:$48D4 */
UBYTE GetFileBlockCount(UBYTE i);
UBYTE FileFindSlotEnd(void);
UBYTE FileSlotHalfBase(void);
void LoadCompressedBlock(UBYTE v);
UBYTE SramHasFileBanks(void);
void FreeFileBlocks(UBYTE f);
void EraseFileName(UBYTE f);

UBYTE SramHasFileBanks(void)
{
    if (ReadSongByte(4, 0xBE78) != 'r')
        return 1;
    if (ReadSongByte(4, 0xBE79) != 'b')
        return 1;
    if (ReadSongByte(5, 0xBE80) != 'r')
        return 1;
    if (ReadSongByte(5, 0xBE81) != 'b')
        return 1;
    if (ReadSongByte(7, 0xBFF0) != 'r')
        return 1;
    if (ReadSongByte(7, 0xBFF1) != 'b')
        return 1;
    return 0;
}

UBYTE CheckFileMagicAll(void)
{
    if (!SramHasFileBanks())
        return 0;
    if (ReadSongByte(0x0C, 0xA13E) == 'j' && ReadSongByte(0x0C, 0xA13F) == 'k'
            && ReadSongByte(8, 0xBE78) == 'r' && ReadSongByte(8, 0xBE79) == 'b')
        return 1;
    return 0;
}

/* $415A: claim the first free block of the allocation table ($A141 on;
   to $A180 on the 64K-only SRAM format, wFilesystemCheckResult, else $A200) for
   the file being compressed: mark it with wCompressByte, return its bank and
   SRAM address through the pointers (range-checked). 0 when the table is
   full. */
UBYTE FindFreeBlock(UBYTE *bank, UWORD *source)
{
    UWORD p, end;

    p = 0xA141;
    end = wFilesystemCheckResult ? 0xA180 : 0xA200;
    if (wCompressByte >= 0x20)
        DebugTrap();
    wFileBlockIndex = 1;
    while ((p - end) & 0x8000) {
        if (ReadSongByte(4, p) > 0x1F) {
            WriteSongByte(4, p, wCompressByte);
            *bank = (wFileBlockIndex >> 4) + 4;
            *source = ((UWORD)(wFileBlockIndex & 0x0F) << 9) + (UWORD)0xA000;
            if (*bank < 4 || *bank >= 0x10)
                DebugTrap();
            if (*source < 0xA000 || *source >= 0xC000)
                DebugTrap();
            wVramCursor = 0x98EC;
            hBgDigitPalette = 4;
            hBgPalette = 1;
            DrawByte(++wCompressProgress);
            return 1;
        }
        wFileBlockIndex++;
        p++;
    }
    return 0;
}

UBYTE FileSlotHalfBase(void)
{
    return wFilesystemCheckResult ? 0x40 : 0xC0;
}

UBYTE FileScanSlotNames(void)
{
    wFileBlockIndex = 1;
    do {
        if (wCompressByte == ReadSongByte(4, (UBYTE *)0xA140 + (unsigned int)wFileBlockIndex)) {
            LoadCompressedBlock(wFileBlockIndex);
            return 1;
        }
        wFileBlockIndex++;
    } while (wFileBlockIndex < FileSlotHalfBase());
    return 0;
}

UBYTE GetFileBlockCount(UBYTE i)
{
    if (i >= 0x20)
        DebugTrap();
    return wFileBlockCounts[i];
}

void CopyFileName(UBYTE f)
{
    UBYTE *p;

    if (f >= 0x20)
        DebugTrap();
    if (GetFileBlockCount(f)) {
        p = (UBYTE *)0xA000 + (unsigned int)(UBYTE)(f << 3);
        _asm
            di
        _endasm;
        rRAMB = 4;
        rRAMG = 0x0A;
        memcpy((UBYTE *)wFileNameBuffer, p, 8);
        rRAMG = 0;
        _asm
            ei
        _endasm;
        wFileNameTerm[0] = 0;
    } else {
        strcpy(wFileNameBuffer, "[EMPTY]");
    }
}

void FormatFileSpace(void)
{
    UWORD address;

    WriteSongByte(4, 0xBE78, 0);
    for (address = 0xA000; address != 0xA100; address++)
        WriteSongByte(4, address, 0);
    for (address = 0xA140; address != 0xA200; address++)
        WriteSongByte(4, address, 0xFF);
    LoadFileListState();
    WriteSongByte(4, 0xA13E, 0x6A);
    WriteSongByte(4, 0xA13F, 0x6B);
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

/*
 * Count the blocks each file owns: clear the 32 counts, then walk the block
 * allocation table ($A141..$A1FF, bank 4) and bump the count of every file
 * number below 32 -- the increment through wFileBlockCountPointer, written as
 * two statements so the store re-reads the pointer global as the ROM does.
 * `while (p != 0xA200)` is the 32-bit compare (a UWORD against a constant
 * above 32767).
 *
 * lsdj-alloc(CountFileBlocks): NOSPILLAT=iTemp0:27 SPILLAT=iTemp0:28
 * -- at the count's read nothing is spillable (p keeps bc, the read spills
 * itself into the dead long's slot), and p is evicted at the `+ 1` that
 * follows (docs/toolchain.md, "The phantom register").
 */
void CountFileBlocks(void)
{
    UWORD address;
    UBYTE v;
    UBYTE t;

    address = 0xA141;
    FillMemory(wFileBlockCounts, 0, 0x20);
    while (address != 0xA200) {
        v = ReadSongByte(4, address++);
        if (v < 0x20) {
            wFileBlockCountPointer = &wFileBlockCounts[v];
            t = *wFileBlockCountPointer + 1;
            *wFileBlockCountPointer = t;
        }
    }
}

UBYTE FileListSlots(void)
{
    UWORD address;
    UBYTE n;

    address = 0xA141;
    n = 0;
    while (address != 0xA200) {
        if (ReadSongByte(4, address++) != 0xFF)
            n++;
    }
    return n;
}

void FreeFileBlocks(UBYTE f)
{
    UWORD p;

    p = 0xA141;
    if (f >= 0x20)
        DebugTrap();
    for (; p < 0xA200; p = p + 1) {
        if (f == ReadSongByte(4, p))
            WriteSongByte(4, p, 0xFF);
    }
}

void EraseFileName(UBYTE f)
{
    UWORD address;
    UBYTE v;

    address = (UWORD)0xA000 + (f << 3);
    while (address != (UWORD)0xA0F8) {
        WriteSongByte(4, address, ReadSongByte(4, address + 8));
        address++;
    }
    WriteSongByte(4, 0xA0F8, 0);
    address = (UWORD)0xA100 + f;
    while (address != (UWORD)0xA11F) {
        WriteSongByte(4, address, ReadSongByte(4, address + 1));
        address++;
    }
    for (address = 0xA141; address != 0xA200; address++) {
        v = ReadSongByte(4, address);
        if (v < 0x20 && f < v)
            WriteSongByte(4, address, v - 1);
    }
}

UBYTE FileLoadSlotInfo(UBYTE slot, UBYTE verify)
{
    UBYTE ok;

    wDecompressOk = 1;
    if (slot >= 0x20)
        DebugTrap();
    wCompressByte = slot;
    wCompressProgress = 1;
    if (FileScanSlotNames() == 0)
        return 0;
    wDecompressPosition = 0;
    wCompressDestination = 0xA000;
    wBlockDestinationBank = 0;
    wBlockSinkPointer = verify ? VerifyBlockAgainstSram : FlushBlockToSram;
    FileDecompressByte();
    ok = wDecompressOk && wBlockDestinationBank == 4;
    return ok && wCompressDestination == 0xA000 && !wDecompressPosition;
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
