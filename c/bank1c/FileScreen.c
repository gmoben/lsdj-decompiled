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
extern UBYTE wWorkSongPage;                 /* $C986, ReadWorkingSongByte's cached page */
extern char wFileNameBuf[];                 /* $C987, [8] */
extern UBYTE wFileNameTerm[];               /* $C98F, its NUL */
extern UBYTE wDecompBuf[];                  /* $DA00, [256] */
extern UBYTE DefaultWaveFrameB1C[];         /* $1C:$4047 */
extern UBYTE *wTopBarMarkPtr;               /* $C562 */
extern UBYTE wFileEraseArmed;               /* $C994 */
extern UBYTE wFilePromptActive;             /* $C995 */
extern UBYTE wFilePromptNo;                 /* $C996 */
extern void (*wBlockSinkPtr)(void);         /* $C984, FlushBlockToSram or the verify sink */
extern UBYTE wCompProgress;                 /* $C981 */
extern UBYTE wFileFirstChar;                /* $C980 */
extern char wTopBarStr[];                   /* $C555 */
extern UBYTE wFileListActive;               /* $C998 */
extern UBYTE wDecompOk;                     /* $C982 */
extern UBYTE wBlockDstBank;                 /* $C97C */
extern UBYTE wNameEditBuf[];                /* $C431, [5] */
extern UBYTE wFileSramProps[];              /* $C99D, [4] */
extern UBYTE wFileLoadAltReturn;            /* $C997 */
extern UBYTE wRedrawPending;                /* $C56C */
extern UBYTE wFileDrawIdx;                  /* $C993 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wFileListAction;               /* $C99A */
extern UBYTE wFileNameNavMode;              /* $C99C */
extern UBYTE wNameEditIsInstr;              /* $C430 */
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
void DrawFileTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
UWORD FileCalcCompressedSize(void);
UBYTE ReadCompressedByte(void);
void FilePutDecompByte(UBYTE v);
void EmitDefaultWave(void);
void EmitDefaultInstrument(void);
void LoadFileListState(void);
void FlushBlockToSram(void);                /* $1C:$743C */
void FileDecompressByte(void);              /* $1C:$749E */
void VerifyBlockAgainstSram(void);         /* $1C:$746E */
void DrawFileList(void);                    /* $1C:$55F1 */
int StringLength(const char *s);            /* bank00/GbdkLib.c, nonbanked */
UBYTE FindFreeBlock(UBYTE *bank, UWORD *src);   /* $1C:$415A */
UBYTE CompressSong(void);                   /* $1C:$441F */
UBYTE DrawVerifyPanel(UBYTE f);             /* $1C:$4778 */
UBYTE GetNextButtons(void);                 /* bank 0 */
void UiClearSram(void);                     /* $01:$45D1 */
void LoadProjectSettings(void);             /* $01:$7C86 */
void CountFileBlocks(void);                 /* $1C:$48D4 */
void ToggleSongPlayback(void);                 /* $02:$78C4 */
void OpenNameEditor(void);                  /* $1B:$4E8A */
int strcmp(const char *a, const char *b);   /* bank 0's StrCompare */
UBYTE GetFileBlockCount(UBYTE i);
UBYTE FileFindSlotEnd(void);
UBYTE FileSlotHalfBase(void);
void LoadCompressedBlock(UBYTE v);
UBYTE FileClassifyToken(void);              /* asm */
UBYTE CheckFileMagic64(void);
void FreeFileBlocks(UBYTE f);
void EraseFileName(UBYTE f);
UBYTE ReadWorkingSongByte(UWORD addr);      /* asm */
void WriteCompressedByte(UBYTE v);          /* asm, v also in a */
void FileCompressRun(UBYTE v);              /* asm, v in a */
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

UBYTE FileSlotHalfBase(void)
{
    return wFsCheckResult ? 0x40 : 0xC0;
}

UBYTE FileScanSlotNames(void)
{
    wFileBlockIdx = 1;
    do {
        if (wCompByte == ReadSongByte(4, (UBYTE *)0xA140 + (unsigned int)wFileBlockIdx)) {
            LoadCompressedBlock(wFileBlockIdx);
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
        memcpy((UBYTE *)wFileNameBuf, p, 8);
        rRAMG = 0;
        _asm
            ei
        _endasm;
        wFileNameTerm[0] = 0;
    } else {
        strcpy(wFileNameBuf, "[EMPTY]");
    }
}

UBYTE CompressIsDefaultWave(void)
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
    UBYTE cur;
    UBYTE next;
    UBYTE k;
    UBYTE bank;
    UWORD src;

    wCompDst = 0;
    wDecompPos = 0;
    wWorkSongPage = 0xFF;
    cur = ReadSongByte(0, 0xA000);
    next = ReadSongByte(0, 0xA001);
    do {
        if (cur == 0xC0) {
            WriteCompressedByte(0xC0);
            WriteCompressedByte(0xC0);
            cur = next;
            wCompDst++;
            if (wCompDst < 0x7FFF)
                next = ReadWorkingSongByte(wCompDst + 1);
        } else if (cur == 0x71 && next == 0x32 && CompressIsDefaultWave()) {
            k = 0;
            do {
                k = k + 1;
                wCompDst += 0x10;
            } while (CompressIsDefaultWave() && k != 0xFF);
            WriteCompressedByte(0xE0);
            WriteCompressedByte(0xF0);
            WriteCompressedByte(k);
            cur = ReadWorkingSongByte(wCompDst);
            next = ReadWorkingSongByte(wCompDst + 1);
        } else if (cur == 0xE0) {
            WriteCompressedByte(0xE0);
            WriteCompressedByte(0xE0);
            cur = next;
            wCompDst++;
            if (wCompDst < 0x7FFF)
                next = ReadWorkingSongByte(wCompDst + 1);
        } else if (cur == next && wCompDst < 0x7FFD
                   && next == ReadWorkingSongByte(wCompDst + 2)
                   && ReadWorkingSongByte(wCompDst + 2) == ReadWorkingSongByte(wCompDst + 3)) {
            FileCompressRun(cur);
            if (wCompDst < 0x8000) {
                cur = ReadWorkingSongByte(wCompDst);
                if (wCompDst < 0x7FFF)
                    next = ReadWorkingSongByte(wCompDst + 1);
            }
        } else {
            WriteCompressedByte(cur);
            cur = next;
            wCompDst++;
            if (wCompDst < 0x7FFF)
                next = ReadWorkingSongByte(wCompDst + 1);
        }
        if (wDecompPos > 0xFB && (wCompSrc & 0x100)) {
            WriteCompressedByte(0xE0);
            if (!FindFreeBlock(&bank, &src))
                return 0;
            WriteCompressedByte(wFileBlockIdx);
            if (wDecompPos)
                FileWriteCompressedSram();
            wCompBank = bank;
            wCompSrc = src;
        }
    } while (!(wCompDst & 0x8000));
    WriteCompressedByte(0xE0);
    WriteCompressedByte(0xFF);
    if (wDecompPos)
        FileWriteCompressedSram();
    return 1;
}

UBYTE DecompressBlock(void)
{
    UBYTE b;

    switch (b = ReadCompressedByte()) {
    case 0xE0:
        FilePutDecompByte(0xE0);
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
        if (wBlockSinkPtr == FlushBlockToSram) {
            wVramCursor = 0x98EC;
            wCompProgress++;
            DrawByte(wCompProgress);
            DrawStringIndexed('/');
            DrawByte(wFileFirstChar);
        }
        LoadCompressedBlock(b);
    }
    return 0;
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
    UBYTE ver;
    char mod;
    UBYTE i;

    mod = ReadSongByte(1, 0xBFC1);
    wFileCursor = ReadSongByte(4, 0xA140);
    CountFileBlocks();
    if (wFileCursor == 0xFF) {
        wFileScroll = 0;
        wFileCursor = 0;
        strcpy(wTopBarStr, "[EMPTY] ");
        wTopBarMarkPtr = wTopBarStr + 7;
        return;
    }
    i = 0;
    if (wFileCursor > 0x1F)
        wFileCursor = 0;
    CopyFileName(wFileCursor);
    while (i < 8) {
        if (!wFileNameBuf[i])
            break;
        wTopBarStr[i] = wFileNameBuf[i];
        i++;
    }
    wTopBarStr[i++] = '.';
    ver = ReadSongByte(4, (UWORD)0xA100 + wFileCursor);
    if (ver & 0xF0) {
        wTopBarStr[i] = (ver >> 4) + 0x30;
        if ((ver >> 4) > 9)
            wTopBarStr[i] += 7;
        ver &= 0x0F;
        if (ver > 9)
            ver += 7;
        i++;
        wTopBarStr[i] = ver + 0x30;
        i++;
        wTopBarMarkPtr = &wTopBarStr[i];
        wTopBarStr[i++] = mod;
        wTopBarStr[i] = 0;
    } else {
        wTopBarStr[i] = ver + 0x30;
        if (ver > 9)
            wTopBarStr[i] += 7;
        i++;
        wTopBarMarkPtr = &wTopBarStr[i];
        wTopBarStr[i++] = mod;
        wTopBarStr[i] = 0;
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
    wCompByte = ReadSongByte(4, 0xA140);
    if (wCompByte != 0xFF) {
        if (wCompByte == f)
            wCompByte = 0xFF;
        else if (f < wCompByte)
            wCompByte--;
    }
    if (wCompByte >= 0x20 && wCompByte != 0xFF)
        DebugTrap();
    WriteSongByte(4, 0xA140, wCompByte);
}

void MigrateTempoProps(void)
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
    wCompByte = f;
    FreeFileBlocks(f);
    wCompProgress = 0;
    i = FindFreeBlock(&wCompBank, &wCompSrc);
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
    if (wCompByte >= 0x20 && wCompByte != 0xFF)
        DebugTrap();
    WriteSongByte(4, 0xA140, wCompByte);
    WriteSongByte(4, (UWORD)0xA100 + wCompByte, i + 1);
    p = (UWORD)0xA000 + (wCompByte << 3);
    for (i = 0; i < 8; i++)
        WriteSongByte(4, p++, wNameEditBuf[i]);
    WriteSongByte(1, 0xBFC1, 0x20);
    LoadFileListState();
    MigrateTempoProps();
    return 1;
}

UBYTE FileLoadSlotInfo(UBYTE slot, UBYTE verify)
{
    UBYTE ok;

    wDecompOk = 1;
    if (slot >= 0x20)
        DebugTrap();
    wCompByte = slot;
    wCompProgress = 1;
    if (FileScanSlotNames() == 0)
        return 0;
    wDecompPos = 0;
    wCompDst = 0xA000;
    wBlockDstBank = 0;
    wBlockSinkPtr = verify ? VerifyBlockAgainstSram : FlushBlockToSram;
    FileDecompressByte();
    ok = wDecompOk && wBlockDstBank == 4;
    return ok && wCompDst == 0xA000 && !wDecompPos;
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

void LoadFile(void)
{
    UBYTE keyRepeat, keyDelay, syncIn, syncOut, n;

    keyRepeat = ReadSongByte(1, 0xBFBB);
    keyDelay = ReadSongByte(1, 0xBFBA);
    syncIn = ReadSongByte(1, 0xBFBD);
    syncOut = ReadSongByte(1, 0xBFBF);
    FileStopSound();
    n = GetFileBlockCount(wFileCursor);
    wFileFirstChar = n;
    if (n) {
        DrawFileTextField(1, 6, 0x11, 3);
        wVramCursor = 0x98E2;
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawString("LOADING...1/");
        DrawByte(wFileFirstChar);
        wFileSramProps[0] = ReadSongByte(1, 0xBFB6);
        wFileSramProps[1] = ReadSongByte(1, 0xBFB7);
        wFileSramProps[2] = ReadSongByte(1, 0xBFB8);
        wFileSramProps[3] = ReadSongByte(1, 0xBFB9);
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
        WriteSongByte(1, 0xBFB6, wFileSramProps[0]);
        WriteSongByte(1, 0xBFB7, wFileSramProps[1]);
        WriteSongByte(1, 0xBFB8, wFileSramProps[2]);
        WriteSongByte(1, 0xBFB9, wFileSramProps[3]);
    } else {
        FarCall(1, FAR_FN(UiClearSram));
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
    MigrateTempoProps();
    FarCallArg(1, FAR_FN(LoadProjectSettings), 1);
    if (wFileLoadAltReturn)
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
            wStringSrc = IdentityCharMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            DrawTextField(i, j, 1, 0);
            GetTilemapAddr(i, j);
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
                wStringSrc = AsciiCharMap - 0x20;
            }
        }
    }
    wStringSrc = AsciiCharMap - 0x20;
    hBgDigitPalette = 4;
    hBgPalette = 1;
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

void DrawFileList(void)
{
    UBYTE row, slot, active, end, blocks;
    UBYTE ver, c, n;
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
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawHexByte(slot);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawStringIndexed(slot == active ? '*' : ' ');
        if (blocks) {
            ver = ReadSongByte(4, (UWORD)0xA100 + slot);
            CopyFileName(slot);
            DrawString(wFileNameBuf);
            DrawStringIndexed('.');
            DrawByte(ver);
            if ((ver & 0xF0) == 0)
                DrawStringTile20();
            n = 9 - StringLength(wFileNameBuf);
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
            GetTilemapAddr(0, row + 4);
            hBgDigitPalette = wShadowDigitPal;
            hBgPalette = wShadowPal;
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
    wFileDrawIdx = row + wFileScroll;
    if (wFileDrawIdx == 0x20)
        wFileDrawIdx--;
}

void DrawFileScreen(void)
{
    UBYTE i;

    FarCall(1, FAR_FN(DrawTopBar));
    for (i = StringLength(wTopBarStr); i != 12; i++)
        DrawStringTile20();
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
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
    DrawFormat("BLOCKS USED: %x/", (UWORD)FileListSlots());
    DrawString(wFsCheckResult ? "3F" : "BF");
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

const UBYTE FileNoAttrTiles[] = {3, 3};    /* $1C:$5AD4: CGB attributes under the NO prompt */

UBYTE DrawBlocksUsed(void)
{
    UWORD n;

    n = FileCalcCompressedSize();
    if (*wTopBarMarkPtr != 0x21)
        return 0;
    wFileEraseArmed = 0;
    DrawFileTextField(1, 5, 0x12, 6);
    wFilePromptActive = 1;
    wFilePromptNo = 1;
    wVramCursor = 0x98C2;
    if (n < 0x78)
        DrawFormat("LOSE %u MINUTES", n);
    else
        DrawFormat("LOSE %u HOURS", n / 60);
    wVramCursor = 0x98E2;
    DrawString("OF UNSAVED WORK?");
    wVramCursor = 0x9926;
    DrawString("YES");
    wVramCursor = 0x992B;
    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    DrawString("NO");
    if (hGameboyType) {
        VBK_REG = 1;
        set_bkg_tiles(0x0B, 9, 2, 1, FileNoAttrTiles);
        VBK_REG = 0;
    }
    return 1;
}

UBYTE FileScreenInput(void)
{
    UBYTE i;

    wFileLoadAltReturn = 0;
    switch (wButtons) {
    case 0x80:
        if (!wButtonsSame)
            FarCall(2, FAR_FN(ToggleSongPlayback));
        break;
    case 0x50:
        wFileLoadAltReturn = 1;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        if (wFilePromptActive) {
            wFilePromptActive = 0;
            DrawTextField(6, 9, 6, 0);
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
                FarCall(1, FAR_FN(UiDrawTextHw));
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
                wNameEditBuf[0] = 0;
                if (i != 0xFF) {
                    CopyFileName(i);
                    if (strcmp("[EMPTY]", wFileNameBuf)) {
                        for (i = 0; i < 9; i++)
                            wNameEditBuf[i] = wFileNameBuf[i];
                    }
                }
                wNameEditIsInstr = 0;
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
                    DrawTextField(0x0B, 9, 2, hGameboyType ? 3 : 2);
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
                wFileNameNavMode = wFileField == 1;
                if (wFileNameNavMode) {
                    wFileCursor = ReadSongByte(4, 0xA140);
                    if (wFileCursor == 0xFF)
                        wFileCursor = 0;
                    if (wFileCursor >= 0x20)
                        DebugTrap();
                    if (FileCheckNameLen()) {
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
            FarCall(1, FAR_FN(UiDrawTextHw));
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
            DrawTextField(6, 9, 3, hGameboyType ? 3 : 2);
            DrawTextField(0x0B, 9, 2, 0);
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
            DrawTextField(6, 9, 3, 0);
            DrawTextField(0x0B, 9, 2, hGameboyType ? 3 : 2);
        } else if (!wFileListActive && wFileField < 3 - 1) {
            FileDrawField(1);
            wFileField++;
            FileDrawField(hGameboyType ? 3 : 2);
        }
        break;
    case 8:
        if (!wFilePromptActive && wFileListActive && wFileCursor < wFileDrawIdx) {
            FileDrawFieldChecked(0);
            if (!wFileNameNavMode)
                wFileCursor++;
            else
                FileMatchName();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCheckNameLen()) {
                do {
                    wFileScroll++;
                } while (FileCheckNameLen());
                DrawFileList();
            }
            FileDrawFieldChecked(hGameboyType ? 3 : 2);
        }
        break;
    case 4:
        if (!wFilePromptActive && wFileListActive && wFileCursor) {
            FileDrawFieldChecked(0);
            if (!wFileNameNavMode)
                wFileCursor--;
            else
                FileCopyNameToBuf();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCheckNameLen()) {
                do {
                    wFileScroll--;
                } while (FileCheckNameLen());
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
