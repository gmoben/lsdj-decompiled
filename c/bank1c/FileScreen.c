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
extern UBYTE wChannelActive[];                 /* $C0E0, [8] */
extern UBYTE wFilesystemCheckResult;                /* $C56B */
extern UBYTE wFileBlockIndex;                 /* $C958 */
extern UBYTE wFileBlockCounts[];            /* $C959, [32] */
extern UBYTE *wFileBlockCountPointer;           /* $C990 */
extern UBYTE wCompressByte;                     /* $C97F */
extern UBYTE wCompressBank;                     /* $C979 */
extern UWORD wCompressSource;                      /* $C97A, an SRAM address */
extern UWORD wCompressDestination;                      /* $C97D, a working-song offset */
extern UBYTE wDecompressPosition;                    /* $C983 */
extern UBYTE wWorkSongPage;                 /* $C986, ReadWorkingSongByte's cached page */
extern char wFileNameBuffer[];                 /* $C987, [8] */
extern UBYTE wFileNameTerm[];               /* $C98F, its NUL */
extern UBYTE wDecompressBuffer[];                  /* $DA00, [256] */
extern UBYTE DefaultWaveFrameB1C[];         /* $1C:$4047 */
extern UBYTE *wTopBarMarkPointer;               /* $C562 */
extern UBYTE wFileEraseArmed;               /* $C994 */
extern UBYTE wFilePromptActive;             /* $C995 */
extern UBYTE wFilePromptNo;                 /* $C996 */
extern void (*wBlockSinkPointer)(void);         /* $C984, FlushBlockToSram or the verify sink */
extern UBYTE wCompressProgress;                 /* $C981 */
extern UBYTE wFileFirstCharacter;                /* $C980 */
extern char wTopBarString[];                   /* $C555 */
extern UBYTE wFileListActive;               /* $C998 */
extern UBYTE wDecompressOk;                     /* $C982 */
extern UBYTE wBlockDestinationBank;                 /* $C97C */
extern UBYTE wNameEditBuffer[];                /* $C431, [5] */
extern UBYTE wFileSramProperties[];              /* $C99D, [4] */
extern UBYTE wFileLoadAlternateReturn;            /* $C997 */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wFileDrawIndex;                  /* $C993 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wFileListAction;               /* $C99A */
extern UBYTE wFileNameNavigationMode;              /* $C99C */
extern UBYTE wNameEditIsInstrument;              /* $C430 */
extern UBYTE wFileField;                    /* $C992 */
extern UBYTE wFileCursor;                   /* $C999 */
extern UBYTE wFileScroll;                   /* $C99B */
extern UBYTE wSyncTransmitByte;                   /* $CBDC */
extern UBYTE wSyncReceiveByte;                   /* $CBDD */
extern UBYTE wSyncState;                    /* $CBDE */
extern UBYTE FileFieldX[];                  /* $1C:$5092 */
extern UBYTE FileFieldWidth[];              /* $1C:$5095 */

void SetInterruptEnable(UBYTE interruptMask);          /* bank 0 */
UBYTE joypad(void);                         /* bank 0 */
void SendSyncHandshake(void);               /* bank 0 */
void SendSyncByte(void);                    /* bank 0 */
void delay(UWORD d);                        /* bank 0 (GBDK) */
void DrawFileTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
UWORD FileCalculateCompressedSize(void);
UBYTE ReadCompressedByte(void);
void FilePutDecompressByte(UBYTE v);
void EmitDefaultWave(void);
void EmitDefaultInstrument(void);
void LoadFileListState(void);
void FlushBlockToSram(void);                /* $1C:$743C */
void FileDecompressByte(void);              /* $1C:$749E */
void VerifyBlockAgainstSram(void);         /* $1C:$746E */
void DrawFileList(void);                    /* $1C:$55F1 */
int StringLength(const char *s);            /* bank00/GbdkLib.c, nonbanked */
UBYTE FindFreeBlock(UBYTE *bank, UWORD *source);   /* $1C:$415A */
UBYTE CompressSong(void);                   /* $1C:$441F */
UBYTE DrawVerifyPanel(UBYTE f);             /* $1C:$4778 */
UBYTE FileLoadSlotInfo(UBYTE slot, UBYTE verify);
UBYTE GetNextButtons(void);                 /* bank 0 */
void FormatSramSong(void);                     /* $01:$45D1 */
void LoadProjectSettings(void);             /* $01:$7C86 */
void CountFileBlocks(void);                 /* $1C:$48D4 */
void SoundToggleSongPlayback(void);                 /* $02:$78C4 */
void OpenNameEditor(void);                  /* $1B:$4E8A */
int strcmp(const char *a, const char *b);   /* bank 0's StringCompare */
UBYTE GetFileBlockCount(UBYTE i);
UBYTE FileFindSlotEnd(void);
UBYTE FileSlotHalfBase(void);
void LoadCompressedBlock(UBYTE v);
UBYTE FileClassifyToken(void);              /* asm */
UBYTE CheckFileMagic64(void);
void FreeFileBlocks(UBYTE f);
void EraseFileName(UBYTE f);
UBYTE ReadWorkingSongByte(UWORD address);      /* asm */
void WriteCompressedByte(UBYTE v);          /* asm, v also in a */
void FileCompressRun(UBYTE v);              /* asm, v in a */
void FileClearChannelState(void);

void FileWriteCompressedSram(void)
{
    if (!FileClassifyToken())
        DebugTrap();
    rRAMB = wCompressBank;
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    memcpy((UBYTE *)wCompressSource, wDecompressBuffer, 0x100);
    rRAMG = 0;
    _asm
        ei
    _endasm;
    wCompressSource += 0x100;
    wDecompressPosition = 0;
}

UBYTE CheckFileMagic64(void)
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
    if (!CheckFileMagic64())
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

UBYTE CompressIsDefaultWave(void)
{
    UBYTE i;

    if (wCompressDestination >= (UWORD)0x7FF1)
        return 0;
    for (i = 0; i < 0x10; i++) {
        if (ReadWorkingSongByte(wCompressDestination + i) != DefaultWaveFrameB1C[i])
            return 0;
    }
    return 1;
}

/*
 * SAVE's compressor: walks the working song with the current and next byte
 * and writes the block stream through WriteCompressedByte -- $C0 $C0 and
 * $E0 $E0 escape the two marker bytes, a byte repeated four times becomes an
 * RLE run (FileCompressRun), consecutive default wave frames an `$E0 $F0 n`
 * run. Near the end of a block's second page it links to the next free block
 * (`$E0 n`); 0 when none is left, 1 after the closing `$E0 $FF`.
 */
UBYTE CompressSong(void)
{
    UBYTE current;
    UBYTE next;
    UBYTE k;
    UBYTE bank;
    UWORD source;

    wCompressDestination = 0;
    wDecompressPosition = 0;
    wWorkSongPage = 0xFF;
    current = ReadSongByte(0, 0xA000);
    next = ReadSongByte(0, 0xA001);
    do {
        if (current == 0xC0) {
            WriteCompressedByte(0xC0);
            WriteCompressedByte(0xC0);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == 0x71 && next == 0x32 && CompressIsDefaultWave()) {
            k = 0;
            do {
                k = k + 1;
                wCompressDestination += 0x10;
            } while (CompressIsDefaultWave() && k != 0xFF);
            WriteCompressedByte(0xE0);
            WriteCompressedByte(0xF0);
            WriteCompressedByte(k);
            current = ReadWorkingSongByte(wCompressDestination);
            next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == 0xE0) {
            WriteCompressedByte(0xE0);
            WriteCompressedByte(0xE0);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        } else if (current == next && wCompressDestination < 0x7FFD
                   && next == ReadWorkingSongByte(wCompressDestination + 2)
                   && ReadWorkingSongByte(wCompressDestination + 2) == ReadWorkingSongByte(wCompressDestination + 3)) {
            FileCompressRun(current);
            if (wCompressDestination < 0x8000) {
                current = ReadWorkingSongByte(wCompressDestination);
                if (wCompressDestination < 0x7FFF)
                    next = ReadWorkingSongByte(wCompressDestination + 1);
            }
        } else {
            WriteCompressedByte(current);
            current = next;
            wCompressDestination++;
            if (wCompressDestination < 0x7FFF)
                next = ReadWorkingSongByte(wCompressDestination + 1);
        }
        if (wDecompressPosition > 0xFB && (wCompressSource & 0x100)) {
            WriteCompressedByte(0xE0);
            if (!FindFreeBlock(&bank, &source))
                return 0;
            WriteCompressedByte(wFileBlockIndex);
            if (wDecompressPosition)
                FileWriteCompressedSram();
            wCompressBank = bank;
            wCompressSource = source;
        }
    } while (!(wCompressDestination & 0x8000));
    WriteCompressedByte(0xE0);
    WriteCompressedByte(0xFF);
    if (wDecompressPosition)
        FileWriteCompressedSram();
    return 1;
}

UBYTE DecompressBlock(void)
{
    UBYTE b;

    switch (b = ReadCompressedByte()) {
    case 0xE0:
        FilePutDecompressByte(0xE0);
        break;
    case 0xF0:
        b = ReadCompressedByte();
        while (b--)
            EmitDefaultWave();
        break;
    case 0xF1:
        b = ReadCompressedByte();
        while (b--)
            EmitDefaultInstrument();
        break;
    case 0xFF:
        return 1;
    default:
        if (wBlockSinkPointer == FlushBlockToSram) {
            wVramCursor = 0x98EC;
            wCompressProgress++;
            DrawByte(wCompressProgress);
            DrawStringIndexed('/');
            DrawByte(wFileFirstCharacter);
        }
        LoadCompressedBlock(b);
    }
    return 0;
}

/* $4778: the verify pass's panel ("VERIFY..."), then the slot read again
   through the verify sink: FileLoadSlotInfo's result */

UBYTE DrawVerifyPanel(UBYTE f)
{
    wVramCursor = 0x98E3;
    DrawString("VERIFY...    ");
    wBlockSinkPointer = VerifyBlockAgainstSram;
    return FileLoadSlotInfo(f, 1);
}

void FormatFileSpace(void)
{
    UWORD p;

    WriteSongByte(4, 0xBE78, 0);
    for (p = 0xA000; p != 0xA100; p++)
        WriteSongByte(4, p, 0);
    for (p = 0xA140; p != 0xA200; p++)
        WriteSongByte(4, p, 0xFF);
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
    UWORD p;
    UBYTE v;
    UBYTE t;

    p = 0xA141;
    FillMemory(wFileBlockCounts, 0, 0x20);
    while (p != 0xA200) {
        v = ReadSongByte(4, p++);
        if (v < 0x20) {
            wFileBlockCountPointer = &wFileBlockCounts[v];
            t = *wFileBlockCountPointer + 1;
            *wFileBlockCountPointer = t;
        }
    }
}

UBYTE FileListSlots(void)
{
    UWORD p;
    UBYTE n;

    p = 0xA141;
    n = 0;
    while (p != 0xA200) {
        if (ReadSongByte(4, p++) != 0xFF)
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

void LoadFileListState(void)
{
    UBYTE version;
    char modifier;
    UBYTE i;

    modifier = ReadSongByte(1, 0xBFC1);
    wFileCursor = ReadSongByte(4, 0xA140);
    CountFileBlocks();
    if (wFileCursor == 0xFF) {
        wFileScroll = 0;
        wFileCursor = 0;
        strcpy(wTopBarString, "[EMPTY] ");
        wTopBarMarkPointer = wTopBarString + 7;
        return;
    }
    i = 0;
    if (wFileCursor > 0x1F)
        wFileCursor = 0;
    CopyFileName(wFileCursor);
    while (i < 8) {
        if (!wFileNameBuffer[i])
            break;
        wTopBarString[i] = wFileNameBuffer[i];
        i++;
    }
    wTopBarString[i++] = '.';
    version = ReadSongByte(4, (UWORD)0xA100 + wFileCursor);
    if (version & 0xF0) {
        wTopBarString[i] = (version >> 4) + 0x30;
        if ((version >> 4) > 9)
            wTopBarString[i] += 7;
        version &= 0x0F;
        if (version > 9)
            version += 7;
        i++;
        wTopBarString[i] = version + 0x30;
        i++;
        wTopBarMarkPointer = &wTopBarString[i];
        wTopBarString[i++] = modifier;
        wTopBarString[i] = 0;
    } else {
        wTopBarString[i] = version + 0x30;
        if (version > 9)
            wTopBarString[i] += 7;
        i++;
        wTopBarMarkPointer = &wTopBarString[i];
        wTopBarString[i++] = modifier;
        wTopBarString[i] = 0;
    }
    if (i >= 0x0D)
        DebugTrap();
}

void EraseFileName(UBYTE f)
{
    UWORD p;
    UBYTE v;

    p = (UWORD)0xA000 + (f << 3);
    while (p != (UWORD)0xA0F8) {
        WriteSongByte(4, p, ReadSongByte(4, p + 8));
        p++;
    }
    WriteSongByte(4, 0xA0F8, 0);
    p = (UWORD)0xA100 + f;
    while (p != (UWORD)0xA11F) {
        WriteSongByte(4, p, ReadSongByte(4, p + 1));
        p++;
    }
    for (p = 0xA141; p != 0xA200; p++) {
        v = ReadSongByte(4, p);
        if (v < 0x20 && f < v)
            WriteSongByte(4, p, v - 1);
    }
}

void EraseFile(UBYTE f)
{
    if (f >= 0x20)
        DebugTrap();
    FreeFileBlocks(f);
    EraseFileName(f);
    wCompressByte = ReadSongByte(4, 0xA140);
    if (wCompressByte != 0xFF) {
        if (wCompressByte == f)
            wCompressByte = 0xFF;
        else if (f < wCompressByte)
            wCompressByte--;
    }
    if (wCompressByte >= 0x20 && wCompressByte != 0xFF)
        DebugTrap();
    WriteSongByte(4, 0xA140, wCompressByte);
}

void MigrateTempoProperties(void)
{
    WriteSongByte(1, (UBYTE *)0xBFCB, ReadSongByte(1, (UBYTE *)0xBFB3));
    WriteSongByte(1, (UBYTE *)0xBFCA, ReadSongByte(1, (UBYTE *)0xBFB2));
}

UBYTE SaveFile(UBYTE f)
{
    UBYTE i;
    UWORD p;

    if (f >= 0x20)
        DebugTrap();
    p = (UWORD)0xA000 + (f << 3);
    for (i = 0; i < 8; i++)
        WriteSongByte(4, p++, 0x3F);
    wCompressByte = f;
    FreeFileBlocks(f);
    wCompressProgress = 0;
    i = FindFreeBlock(&wCompressBank, &wCompressSource);
    if (!i || !CompressSong()) {
        FreeFileBlocks(f);
        return 0;
    }
    if (!DrawVerifyPanel(f)) {
        FreeFileBlocks(f);
        return 2;
    }
    i = ReadSongByte(4, 0xA140);
    if (i != 0xFF)
        i = ReadSongByte(4, (UWORD)0xA100 + i);
    if (wCompressByte >= 0x20 && wCompressByte != 0xFF)
        DebugTrap();
    WriteSongByte(4, 0xA140, wCompressByte);
    WriteSongByte(4, (UWORD)0xA100 + wCompressByte, i + 1);
    p = (UWORD)0xA000 + (wCompressByte << 3);
    for (i = 0; i < 8; i++)
        WriteSongByte(4, p++, wNameEditBuffer[i]);
    WriteSongByte(1, 0xBFC1, 0x20);
    LoadFileListState();
    MigrateTempoProperties();
    return 1;
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

void FileClearChannelState(void)
{
    UBYTE i = 8;

    do {
        wChannelActive[i - 1] = 0;
    } while (--i);
}

void FileStopSound(void)
{
    SetInterruptEnable(IE_REG & 0xFB);
    wPlayMode = 0;
    NR52_REG = 0;
    FileClearChannelState();
}

void LoadFile(void)
{
    UBYTE keyRepeat, keyDelay, syncIn, syncOut, n;

    keyRepeat = ReadSongByte(1, 0xBFBB);
    keyDelay = ReadSongByte(1, 0xBFBA);
    syncIn = ReadSongByte(1, 0xBFBD);
    syncOut = ReadSongByte(1, 0xBFBF);
    FileStopSound();
    n = GetFileBlockCount(wFileCursor);
    wFileFirstCharacter = n;
    if (n) {
        DrawFileTextField(1, 6, 0x11, 3);
        wVramCursor = 0x98E2;
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("LOADING...1/");
        DrawByte(wFileFirstCharacter);
        wFileSramProperties[0] = ReadSongByte(1, 0xBFB6);
        wFileSramProperties[1] = ReadSongByte(1, 0xBFB7);
        wFileSramProperties[2] = ReadSongByte(1, 0xBFB8);
        wFileSramProperties[3] = ReadSongByte(1, 0xBFB9);
        if (!FileLoadSlotInfo(wFileCursor, 0)) {
            wVramCursor = 0x98EC;
            DrawString("FAIL!");
            while (GetNextButtons())
                ;
            while (!GetNextButtons())
                ;
            while (GetNextButtons())
                ;
            wRedrawPending = 0;
            FarCall(1, FAR_FN(RedrawScreen));
            return;
        }
        WriteSongByte(1, 0xBFB6, wFileSramProperties[0]);
        WriteSongByte(1, 0xBFB7, wFileSramProperties[1]);
        WriteSongByte(1, 0xBFB8, wFileSramProperties[2]);
        WriteSongByte(1, 0xBFB9, wFileSramProperties[3]);
    } else {
        FarCall(1, FAR_FN(FormatSramSong));
        WriteSongByte(4, (UWORD)0xA100 + wFileCursor, 0);
    }
    WriteSongByte(1, 0xBFBA, keyDelay);
    WriteSongByte(1, 0xBFBB, keyRepeat);
    WriteSongByte(1, 0xBFBD, syncIn);
    WriteSongByte(1, 0xBFBF, syncOut);
    if (wFileCursor >= 0x20 && wFileCursor != 0xFF)
        DebugTrap();
    WriteSongByte(4, 0xA140, wFileCursor);
    WriteSongByte(1, 0xBFC1, 0x20);
    LoadFileListState();
    MigrateTempoProperties();
    FarCallArgument(1, FAR_FN(LoadProjectSettings), 1);
    if (wFileLoadAlternateReturn)
        wCurrentScreen = 0x0D;
    FarCall(1, FAR_FN(RedrawScreen));
    wRedrawPending = 0;
}

/* A w x h frame at (x, y): corner/edge tiles through the identity map,
 * the interior cleared with tile 2 in palette 1. Inline bounds keep the
 * prologue free of `local := temp` copies, so SDCC 2.2.1's pre-loop dataflow
 * settles after two passes and the edge tests, `x + w - 1` and the ASCII
 * restore stay in the inner loop's preheader (an outer-loop invariant whose
 * definition has not reached that preheader is not hoisted further), where
 * the ROM has them. The `==` operands are written `i == x`: SDCC 2.2.1 swaps
 * them back.
 */
void DrawFileTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h)
{
    UBYTE i, j;

    for (i = x; i < x + w; i++) {
        for (j = y; j < y + h; j++) {
            wStringSource = IdentityCharacterMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            ColorField(i, j, 1, 0);
            SetVramCursor(i, j);
            if (i == x) {
                if (j == y)
                    DrawStringIndexed(0);
                else if (j == y + h - 1)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if (i == x + w - 1) {
                if (j == y)
                    DrawStringIndexed(1);
                else if (j == y + h - 1)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (j == y)
                    DrawStringIndexed(4);
                else if (j == y + h - 1)
                    DrawStringIndexed(6);
                else {
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawStringIndexed(2);
                }
                wStringSource = AsciiCharacterMap - 0x20;
            }
        }
    }
    wStringSource = AsciiCharacterMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
}

void FileDrawField(UBYTE palette)
{
    ColorField(FileFieldX[wFileField], 2, FileFieldWidth[wFileField], palette);
}

void FileDrawFieldChecked(UBYTE palette)
{
    UBYTE r;

    r = wFileCursor - wFileScroll;
    if (r >= 0x10)
        DebugTrap();
    ColorField(3, wFileCursor + 4 - wFileScroll, 0x0E, palette);
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

void DrawFileList(void)
{
    UBYTE row, slot, active, end, blocks;
    UBYTE version, c, n;
    char *empty = "[EMPTY]      0";

    row = 0;
    slot = wFileScroll;
    active = ReadSongByte(4, 0xA140);
    end = FileFindSlotEnd() + 1;
    CountFileBlocks();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = 0x9880;
    while (row < 12 && slot < 0x20) {
        blocks = wFileBlockCounts[slot];
        if (slot == end)
            break;
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawHexByte(slot);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawStringIndexed(slot == active ? '*' : ' ');
        if (blocks) {
            version = ReadSongByte(4, (UWORD)0xA100 + slot);
            CopyFileName(slot);
            DrawString(wFileNameBuffer);
            DrawStringIndexed('.');
            DrawByte(version);
            if ((version & 0xF0) == 0)
                DrawStringTile20();
            n = 9 - StringLength(wFileNameBuffer);
            while (n--)
                DrawStringTile20();
            if ((blocks & 0xF0) == 0)
                DrawStringTile20();
            DrawByte(blocks);
        } else {
            DrawString(empty);
        }
        wVramCursor += 0x0F;
        row++;
        slot++;
    }
    if (row < 12) {
        c = row + wFileScroll;
        if (c < 0x20) {
            SetVramCursor(0, row + 4);
            hBgDigitPalette = wShadedDigitPalette;
            hBgPalette = wShadedPalette;
            DrawHexByte(c);
            hBgDigitPalette = 4;
            hBgPalette = 1;
            if (active == c)
                DrawStringIndexed('*');
            else
                DrawStringTile20();
            DrawString(empty);
        }
    }
    wFileDrawIndex = row + wFileScroll;
    if (wFileDrawIndex == 0x20)
        wFileDrawIndex--;
}

void DrawFileScreen(void)
{
    UBYTE i;

    FarCall(1, FAR_FN(DrawTopBar));
    for (i = StringLength(wTopBarString); i != 12; i++)
        DrawStringTile20();
    hBgDigitPalette = wShadedDigitPalette;
    hBgPalette = wShadedPalette;
    wVramCursor = 0x9840;
    DrawString("LOAD");
    wVramCursor = 0x9845;
    DrawString("SAVE");
    wVramCursor = 0x984A;
    DrawString("ERASE");
    DrawFileList();
    if (wFileListActive)
        FileDrawFieldChecked(hGameboyType ? 3 : 2);
    else
        FileDrawField(hGameboyType ? 3 : 2);
    wVramCursor = 0x9A20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    printf("BLOCKS USED: %x/", (UWORD)FileListSlots());
    DrawString(wFilesystemCheckResult ? "3F" : "BF");
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

void FileCopyNameToBuffer(void)
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

UBYTE FileCheckNameLength(void)
{
    UBYTE d;

    d = wFileCursor - wFileScroll;
    return d > 0x0B;
}

const UBYTE FileNoAttributeTiles[] = {3, 3};    /* $1C:$5AD4: CGB attributes under the NO prompt */

UBYTE DrawBlocksUsed(void)
{
    UWORD n;

    n = FileCalculateCompressedSize();
    if (*wTopBarMarkPointer != 0x21)
        return 0;
    wFileEraseArmed = 0;
    DrawFileTextField(1, 5, 0x12, 6);
    wFilePromptActive = 1;
    wFilePromptNo = 1;
    wVramCursor = 0x98C2;
    if (n < 0x78)
        printf("LOSE %u MINUTES", n);
    else
        printf("LOSE %u HOURS", n / 60);
    wVramCursor = 0x98E2;
    DrawString("OF UNSAVED WORK?");
    wVramCursor = 0x9926;
    DrawString("YES");
    wVramCursor = 0x992B;
    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    DrawString("NO");
    if (hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0x0B, 9, 2, 1, FileNoAttributeTiles);
        VBK_REG = 0;
    }
    return 1;
}

UBYTE FileScreenInput(void)
{
    UBYTE i;

    wFileLoadAlternateReturn = 0;
    switch (wButtons) {
    case 0x80:
        if (!wButtonsSame)
            FarCall(2, FAR_FN(SoundToggleSongPlayback));
        break;
    case 0x50:
        wFileLoadAlternateReturn = 1;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        if (wFilePromptActive) {
            wFilePromptActive = 0;
            ColorField(6, 9, 6, 0);
            if (wFilePromptNo)
                goto cancel;
            if (!wFileEraseArmed) {
                LoadFile();
                return 1;
            }
            EraseFile(wFileCursor);
            DrawFileTextField(2, 7, 0x0F, 3);
            wVramCursor = 0x9903;
            DrawString("FILE ERASED!");
            delay(hGameboyType ? 2000 : 1000);
            FileDrawFieldChecked(0);
            wFileListActive = 0;
            FileValidateName();
            if (hGameboyType)
                ClearBgTilemap();
            else
                FarCall(1, FAR_FN(DrawScreenPaletteMap));
            DrawFileScreen();
        } else if (wFileListActive) {
            FileDrawFieldChecked(2);
            switch (wFileListAction) {
            case 0:
                if (!DrawBlocksUsed())
                    LoadFile();
                return 1;
            case 1:
                FileDrawFieldChecked(0);
                i = ReadSongByte(4, 0xA140);
                wNameEditBuffer[0] = 0;
                if (i != 0xFF) {
                    CopyFileName(i);
                    if (strcmp("[EMPTY]", wFileNameBuffer)) {
                        for (i = 0; i < 9; i++)
                            wNameEditBuffer[i] = wFileNameBuffer[i];
                    }
                }
                wNameEditIsInstrument = 0;
                wFileListActive = 0;
                FarCall(0x1B, FAR_FN(OpenNameEditor));
                return 1;
            case 2:
                if (GetFileBlockCount(wFileCursor)) {
                    wFileEraseArmed = 1;
                    hBgDigitPalette = 4;
                    hBgPalette = 1;
                    DrawFileTextField(5, 6, 9, 5);
                    wFilePromptActive = 1;
                    wFilePromptNo = 1;
                    wVramCursor = 0x98E6;
                    DrawString("ERASE?");
                    wVramCursor = 0x9926;
                    DrawString("YES");
                    wVramCursor = 0x992B;
                    DrawString("NO");
                    ColorField(0x0B, 9, 2, hGameboyType ? 3 : 2);
                    return 1;
                }
                return 0;
            default:
                if (hGameboyType)
                    ClearBgTilemap();
                wFileListActive = 0;
                DrawFileScreen();
                break;
            }
        } else {
            switch (wFileField) {
            case 0:
            case 1:
            case 2:
                wFileListAction = wFileField;
                wFileListActive = 1;
                wFileNameNavigationMode = wFileField == 1;
                if (wFileNameNavigationMode) {
                    wFileCursor = ReadSongByte(4, 0xA140);
                    if (wFileCursor == 0xFF)
                        wFileCursor = 0;
                    if (wFileCursor >= 0x20)
                        DebugTrap();
                    if (FileCheckNameLength()) {
                        FileValidateName();
                        DrawFileList();
                    }
                }
                FileDrawField(2);
                FileDrawFieldChecked(hGameboyType ? 3 : 2);
                break;
            }
        }
        break;
    case 0x20:
        if (!wAPressEnabled)
            return 0;
        if (wFilePromptActive) {
        cancel:
            wFilePromptActive = 0;
            if (hGameboyType)
                ClearBgTilemap();
            wFileListActive = 0;
            FarCall(1, FAR_FN(DrawScreenPaletteMap));
            DrawFileScreen();
        } else if (wFileListActive) {
            FileDrawFieldChecked(0);
            wFileListActive = 0;
            FileDrawField(hGameboyType ? 3 : 2);
        } else {
            wCurrentScreen = 9;
            FarCall(7, FAR_FN(RedrawScreen));
        }
        break;
    case 2:
        if (wButtonsSame)
            break;
        if (wFilePromptActive) {
            wFilePromptNo = 0;
            ColorField(6, 9, 3, hGameboyType ? 3 : 2);
            ColorField(0x0B, 9, 2, 0);
        } else if (!wFileListActive && wFileField) {
            FileDrawField(1);
            wFileField--;
            FileDrawField(hGameboyType ? 3 : 2);
        }
        break;
    case 1:
        if (wButtonsSame)
            break;
        if (wFilePromptActive) {
            wFilePromptNo = 1;
            ColorField(6, 9, 3, 0);
            ColorField(0x0B, 9, 2, hGameboyType ? 3 : 2);
        } else if (!wFileListActive && wFileField < 3 - 1) {
            FileDrawField(1);
            wFileField++;
            FileDrawField(hGameboyType ? 3 : 2);
        }
        break;
    case 8:
        if (!wFilePromptActive && wFileListActive && wFileCursor < wFileDrawIndex) {
            FileDrawFieldChecked(0);
            if (!wFileNameNavigationMode)
                wFileCursor++;
            else
                FileMatchName();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCheckNameLength()) {
                do {
                    wFileScroll++;
                } while (FileCheckNameLength());
                DrawFileList();
            }
            FileDrawFieldChecked(hGameboyType ? 3 : 2);
        }
        break;
    case 4:
        if (!wFilePromptActive && wFileListActive && wFileCursor) {
            FileDrawFieldChecked(0);
            if (!wFileNameNavigationMode)
                wFileCursor--;
            else
                FileCopyNameToBuffer();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCheckNameLength()) {
                do {
                    wFileScroll--;
                } while (FileCheckNameLength());
                DrawFileList();
            }
            FileDrawFieldChecked(hGameboyType ? 3 : 2);
        }
        break;
    }
    return 1;
}

void FileUpgradeRetrigRate(UBYTE *p)
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

extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wClipboardBuffer[][42];           /* $C57A, 6 columns of 42 cells */
extern UBYTE wSyncTransferActive;               /* $CB65 */
void SyncSetReceiveMode(void);                 /* bank 0 */

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
    UBYTE x, y, ok;

    if (wCursorMode != 2)
        DebugTrap();
    if (wSelectionLeft >= wSelectionRight)
        DebugTrap();
    if (wSelectionTop >= wSelectionBottom)
        DebugTrap();
    if (wSelectionRight > 6)
        DebugTrap();
    if (wSelectionBottom > 0x2A)
        DebugTrap();
    if (wPlayMode || wSyncMode != 1)
        return;
    wSyncTransferActive = 1;
    SetInterruptEnable(0);
    FileSendSyncByte(0xFE);
    FileSendSyncByte(wClipboardType);
    FileSendSyncByte(wSelectionLeft);
    FileSendSyncByte(wSelectionTop);
    FileSendSyncByte(wSelectionRight);
    FileSendSyncByte(wSelectionBottom);
    for (x = wSelectionLeft; x < wSelectionRight; x++) {
        y = wSelectionTop;
        while (ok = y < wSelectionBottom) {
            FileSendSyncByte(wClipboardBuffer[x][y]);
            y = y + 1;
        }
    }
    SyncSetReceiveMode();
    wSyncTransferActive = 0;
}

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
    UBYTE x, y, ok;

    wCursorMode = 1;
    wSyncTransferActive = 1;
    SetInterruptEnable(8);
    SendSyncHandshake();
    SyncTransferByte(&wClipboardType);
    SyncTransferByte(&wSelectionLeft);
    SyncTransferByte(&wSelectionTop);
    SyncTransferByte(&wSelectionRight);
    SyncTransferByte(&wSelectionBottom);
    if (wSelectionLeft < wSelectionRight && wSelectionTop < wSelectionBottom && wSelectionRight <= 6 && wSelectionBottom <= 0x2A) {
        for (x = wSelectionLeft; x < wSelectionRight; x++) {
            y = wSelectionTop;
            while (ok = y < wSelectionBottom) {
                SyncTransferByte(&wClipboardBuffer[x][y]);
                y = y + 1;
            }
        }
        wCursorMode = 2;
    }
    SyncSetReceiveMode();
    wSyncTransferActive = 0;
}
