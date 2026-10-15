/*
 * The file screen: slots, names, compression, sync -- LSDj 9.4.0, ROM bank $1C.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void CopyFileName(UBYTE f);
void MigrateTempoProperties(void);
UBYTE FileListSlots(void);
void FileValidateName(void);
extern volatile sfr UBYTE IE_REG;           /* $FFFF */
extern volatile sfr UBYTE NR52_REG;         /* $FF26 */
extern UBYTE wChannelActive[];                 /* $C0E0, [8] */
extern UBYTE wFilesystemCheckResult;                /* $C56B */
extern UBYTE wFileBlockCounts[];            /* $C959, [32] */
extern UBYTE wCompressByte;                     /* $C97F */
extern UBYTE wCompressBank;                     /* $C979 */
extern UWORD wCompressSource;                      /* $C97A, an SRAM address */
extern char wFileNameBuffer[];                 /* $C987, [8] */
extern UBYTE *wTopBarMarkPointer;               /* $C562 */
extern UBYTE wFileEraseArmed;               /* $C994 */
extern UBYTE wFilePromptActive;             /* $C995 */
extern UBYTE wFilePromptNo;                 /* $C996 */
extern void (*wBlockSinkPointer)(void);         /* $C984, FlushBlockToSram or the verify sink */
extern UBYTE wCompressProgress;                 /* $C981 */
extern UBYTE wFileFirstCharacter;                /* $C980 */
extern char wTopBarString[];                   /* $C555 */
extern UBYTE wFileListActive;               /* $C998 */
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
extern UBYTE FileFieldX[];                  /* $1C:$5092 */
extern UBYTE FileFieldWidth[];              /* $1C:$5095 */
void SetInterruptEnable(UBYTE interruptMask);          /* bank 0 */
void delay(UWORD d);                        /* bank 0 (GBDK) */
void DrawFileTextField(UBYTE x, UBYTE y, UBYTE w, UBYTE h);
UWORD FileCalculateCompressedSize(void);
void LoadFileListState(void);
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
void FreeFileBlocks(UBYTE f);
void EraseFileName(UBYTE f);
void FileClearChannelState(void);

/* $4778: the verify pass's panel ("VERIFY..."), then the slot read again
   through the verify sink: FileLoadSlotInfo's result */

UBYTE DrawVerifyPanel(UBYTE f)
{
    wVramCursor = 0x98E3;
    DrawString("VERIFY...    ");
    wBlockSinkPointer = VerifyBlockAgainstSram;
    return FileLoadSlotInfo(f, 1);
}

void LoadFileListState(void)
{
    UBYTE version;
    char modifier;
    UBYTE position;

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
    position = 0;
    if (wFileCursor > 0x1F)
        wFileCursor = 0;
    CopyFileName(wFileCursor);
    while (position < 8) {
        if (!wFileNameBuffer[position])
            break;
        wTopBarString[position] = wFileNameBuffer[position];
        position++;
    }
    wTopBarString[position++] = '.';
    version = ReadSongByte(4, (UWORD)0xA100 + wFileCursor);
    if (version & 0xF0) {
        wTopBarString[position] = (version >> 4) + 0x30;
        if ((version >> 4) > 9)
            wTopBarString[position] += 7;
        version &= 0x0F;
        if (version > 9)
            version += 7;
        position++;
        wTopBarString[position] = version + 0x30;
        position++;
        wTopBarMarkPointer = &wTopBarString[position];
        wTopBarString[position++] = modifier;
        wTopBarString[position] = 0;
    } else {
        wTopBarString[position] = version + 0x30;
        if (version > 9)
            wTopBarString[position] += 7;
        position++;
        wTopBarMarkPointer = &wTopBarString[position];
        wTopBarString[position++] = modifier;
        wTopBarString[position] = 0;
    }
    if (position >= 0x0D)
        DebugTrap();
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

void FileClearChannelState(void)
{
    UBYTE channel = 8;

    do {
        wChannelActive[channel - 1] = 0;
    } while (--channel);
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
    UBYTE column, row;

    for (column = x; column < x + w; column++) {
        for (row = y; row < y + h; row++) {
            wStringSource = IdentityCharacterMap;
            hBgPalette = hGameboyType ? 0x48 : 0xD2;
            ColorField(column, row, 1, 0);
            SetVramCursor(column, row);
            if (column == x) {
                if (row == y)
                    DrawStringIndexed(0);
                else if (row == y + h - 1)
                    DrawStringIndexed(3);
                else
                    DrawStringIndexed(7);
            } else if (column == x + w - 1) {
                if (row == y)
                    DrawStringIndexed(1);
                else if (row == y + h - 1)
                    DrawStringIndexed(2);
                else
                    DrawStringIndexed(5);
            } else {
                if (row == y)
                    DrawStringIndexed(4);
                else if (row == y + h - 1)
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

void DrawFileList(void)
{
    UBYTE row, slot, active, end, blocks;
    UBYTE version, c, padding;
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
            padding = 9 - StringLength(wFileNameBuffer);
            while (padding--)
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
    UBYTE position;

    FarCall(1, FAR_FN(DrawTopBar));
    for (position = StringLength(wTopBarString); position != 12; position++)
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

void SeekNextWritableSlot(void)
{
    UBYTE file;

    file = wFileCursor;
    while (++file < 0x20) {
        if (FileFindSlotEnd() + 1 < file)
            break;
        if (ReadSongByte(4, (UBYTE *)0xA140) == file || GetFileBlockCount(file) == 0) {
            wFileCursor = file;
            break;
        }
    }
    if (wFileCursor >= 0x20)
        DebugTrap();
}

void SeekPreviousWritableSlot(void)
{
    UBYTE file;

    file = wFileCursor;
    while (--file != 0xFF) {
        if (ReadSongByte(4, (UBYTE *)0xA140) == file || GetFileBlockCount(file) == 0) {
            wFileCursor = file;
            break;
        }
    }
    if (wFileCursor >= 0x20)
        DebugTrap();
}

UBYTE FileCursorBelowWindow(void)
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
                    if (FileCursorBelowWindow()) {
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
                SeekNextWritableSlot();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCursorBelowWindow()) {
                do {
                    wFileScroll++;
                } while (FileCursorBelowWindow());
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
                SeekPreviousWritableSlot();
            if (wFileCursor >= 0x20)
                DebugTrap();
            if (FileCursorBelowWindow()) {
                do {
                    wFileScroll--;
                } while (FileCursorBelowWindow());
                DrawFileList();
            }
            FileDrawFieldChecked(hGameboyType ? 3 : 2);
        }
        break;
    }
    return 1;
}
