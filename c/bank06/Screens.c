/*
 * The chain, wave and word screens -- LSDj 9.4.0, ROM bank $06.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wWaveCursor;                   /* $C42B */
extern UBYTE wChanActive[];                 /* $C0E0, [4] */
extern UBYTE wChanChain[];                  /* $C170, [4] */
extern UBYTE wChanChainRow[];               /* $C17C, [4] */
extern UBYTE wChanSongRow[];                /* $C200, [4] */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainCol;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wWordClipCell0[];              /* $C935 */
extern UBYTE wWordEditRow;                  /* $C937 */
extern UBYTE wWordEditCol;                  /* $C938 */
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wWaveSelStart;                 /* $C93B */
extern UBYTE wWaveSelEnd;                   /* $C93C */
extern UBYTE wSyncRun;                      /* $CB6B */
extern UBYTE wSyncHandshake;                /* $CB6C */
extern UBYTE WordColX[];                  /* the word editor's two columns: x */
extern UBYTE WordColWidth[];                  /* width */
extern UBYTE ChainColX[];                  /* the chain screen's two columns: x */

void DrawWaveColumn(UBYTE i);
void ChainDrawWaveCell(void);
void ChainDrawWaveCellHw(void);
void DrawStringAtCursor(char *s);
void ChainDrawSyncStatus(void);
void ChainDrawLeadStatus(void);
UBYTE ChainSyncStatusFlag(void);
void WriteWaveSample(UBYTE n, UBYTE v);
UBYTE ReadWaveSample(UBYTE i);
void WaveInitSprites(void);
void WaveSetSpriteColumns(UBYTE s, UBYTE x);
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */
void ChainWriteCursorPair(UBYTE *a, UBYTE *b);
void ChainWriteWaveA(void);
void CopySynthParam(void);
void ChainDrawRow16(void);
void ChainSetCursorSprite(void);

void DrawAllChanStatus(void);               /* bank $1F */
extern char NoteNames[];                    /* $06:$76E4, [][3] */
extern char TxtKitNoteBad[];                /* $06:$7844 "???" */
extern char TxtKitNoteC5[];                 /* $06:$7830 "C 5" */
extern char TxtKitNoteC6[];                 /* $06:$7838 "C 6" */
extern char TxtKitNoteC7[];                 /* $06:$783C "C 7" */
extern char TxtKitNoteC8[];                 /* $06:$7840 "C 8" */
extern char TxtKitNoteF5[];                 /* $06:$7834 "F 5" */
extern char TxtNoNote[];                    /* $06:$782C "---" (this unit's literals follow NoteNames) */
extern char TxtNoteBad[];                   /* $06:$7848 "???" */
extern UBYTE wChanDispNote[];               /* $C41F, [4] */
extern UBYTE wChanDispNoteTimer[];          /* $C423, [4] */
extern UBYTE wChanModFlag[];                /* $C0A0, [4] */
extern UBYTE wChanModType[];                /* $C0A4, [4] */
extern UBYTE wKit1Bank;                     /* $C4F4 */
extern UBYTE wKit2Bank;                     /* $C4F5 */
extern UBYTE wNoteTranspose;                /* $C4D3 */
extern UBYTE wSndUpdateFlags;               /* $C8F5 */
extern UBYTE wTempoShadow;                  /* $C947 */

void DrawTileHexCell(UBYTE v);                /* c/bank06/DrawTileHexCell.c */
void DrawChainCell(UBYTE col, UBYTE v);
void DrawKitSampleNames(UBYTE ch, UBYTE note);
void ChainUpdateScope(void);

extern char TxtClonedPhrase[];              /* $06:$5899 "CLONED PHRASE" */
extern char TxtDashes2[];                   /* $06:$5A5E " --" */
extern UBYTE wColPhrase[];                  /* $C453, [4] */
void DrawChainPos(void);
void LoadChainRow(void);
void AllocNextFreePhrase(void);             /* bank 1 */
void WriteChainPhrase(void);                /* bank $1F */
void ShowOutOfMemory(void);                 /* bank 1 */
void ClonePhraseAtCursor(UBYTE src, UBYTE dst);

extern UBYTE wSelTop;                       /* $C40E */
extern UBYTE wSelRight;                     /* $C40F */
extern UBYTE wSelBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wWaveColEmpty[];               /* $C93D, [8] */
void WriteVramByteSafe(UBYTE *addr, UBYTE v);  /* $00:$17EC */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void ChainWriteWaveB(void);
void CopyWaveSelection(void);
void PasteWaveSelection(void);
void SaveSelectionState(void);              /* $05:$743F */
void MapMoveLeft(void);                     /* $05:$4233 */
void MapMoveDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);                 /* $02:$78C4 */
void TogglePlayback(void);                    /* $02:$7C4C */
void CopySelection(void);                   /* bank $1C */
extern char TxtCopy[];                      /* $06:$5467 "COPY" */

UBYTE ChainScaleValue(UBYTE v)
{
    UBYTE r;

    r = v / 15 + 0x19;
    if (r > 0x1A)
        r += 5;
    return r;
}

void DrawSampleNameCell(UBYTE v)
{
    UBYTE t, m;

    wWordClipCell0[0] = v;
    t = v;
    t--;
    m = t % 15 + 1;
    ReadKitSampleName(ChainScaleValue(t), m);
    DrawString(wSampleNoteString);
}

UBYTE ReadWordCell(void)
{
    UWORD p;

    p = (UWORD)SramWords + ((UWORD)wSpeechCursorRow << 5);
    p += (unsigned int)wWordEditCol;
    p += (unsigned int)(wWordEditRow << 1);
    return ReadSongByte(0, p);
}

void WriteWordCell(UBYTE v)
{
    UWORD p;

    p = (UWORD)SramWords + ((UWORD)wSpeechCursorRow << 5);
    p += (unsigned int)wWordEditCol;
    p += (unsigned int)(wWordEditRow << 1);
    WriteSongByte(0, p, v);
}

void ChainDrawWaveCell(void)
{
    ColorField(WordColX[wWordEditCol], wWordEditRow + 2, WordColWidth[wWordEditCol], wWordEditCol);
}

void ChainDrawWaveCellHw(void)
{
    ColorField(WordColX[wWordEditCol], wWordEditRow + 2, WordColWidth[wWordEditCol], hGameboyType ? 3 : 2);
}

void DrawWordScreen(void)
{
    UWORD p;
    UBYTE i;
    UBYTE v;
    UBYTE s, m;

    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = (UBYTE *)0x9800;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawString("WORD ");
    p = (UWORD)0xBDD0 + (UWORD)(wSpeechCursorRow * 3);
    i = 0;
    while (i < 3) {
        v = ReadSongByte(0, p);
        if (!v)
            break;
        DrawStringIndexed(v);
        p++;
        i++;
    }
    DrawTempoField();
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    p = (UWORD)0xB890 + ((UWORD)wSpeechCursorRow << 5);
    i = 0;
    while (i < 16) {
        GetTilemapAddr(2, i + 2);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        s = ReadSongByte(0, p++);
        if (s) {
            s--;
            m = s % 15 + 1;
            ReadKitSampleName(ChainScaleValue(s), m);
            DrawString(wSampleNoteString);
            DrawStringTile20();
        } else {
            DrawString("--- ");
        }
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
        DrawHexByte(ReadSongByte(0, p++));
        i++;
    }
    ChainDrawWaveCellHw();
}

void DrawWaveCellPair(void)
{
    if (wWordEditRow == 0x0F)
        return;
    ChainDrawWaveCell();
    wWordEditRow++;
    ChainDrawWaveCellHw();
}

UBYTE *TilemapAddr(UBYTE x, UBYTE y)
{
    UWORD a;

    a = y;
    a <<= 5;
    a += x;
    a |= 0x9800;
    return (UBYTE *)a;
}

UWORD ChainCalcParamAddr(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

UBYTE ReadWaveSample(UBYTE i)
{
    UBYTE *p;
    UBYTE v;

    if (i >= 0x20)
        DebugTrap();
    i = (i + 1) & 0x1F;
    p = SramWaveFrames + ((UWORD)wSynth << 4);
    p += i >> 1;
    v = ReadSongByte(3, p);
    if (i & 1)
        v &= 0x0F;
    else
        v >>= 4;
    return 0x0F ^ v;
}

void WriteWaveSample(UBYTE pos, UBYTE v)
{
    UBYTE *p;
    UBYTE b;

    if (pos >= 0x20)
        DebugTrap();
    p = SramWaveFrames + ((UWORD)wSynth << 4);
    CopySynthParam();
    v ^= 0x0F;
    pos = (pos + 1) & 0x1F;
    p += pos >> 1;
    b = ReadSongByte(3, (UWORD)p);
    if (pos & 1) {
        b &= 0xF0;
        v = (v & 0x0F) | b;
    } else {
        b &= 0x0F;
        v <<= 4;
        v |= b;
    }
    WriteSongByte(3, (UWORD)p, v);
}

void WaveSetSpriteColumns(UBYTE y1, UBYTE y2)
{
    UBYTE i;

    for (i = 3; (UWORD)i != 11; i++)
        SetOAMByte((i << 2) + 1, y1);
    for (; (UWORD)i != 19; i++)
        SetOAMByte((i << 2) + 1, y2);
}

void ChainSetCursorSprite(void)
{
    UBYTE x, s;

    x = (wWaveCursor << 2) + 0x18;
    s = (wWaveSelStart << 2) + 0x18;
    if (wCursorMode != 4)
        s = x;
    WaveSetSpriteColumns(s, x);
}

void ChainWriteCursorPair(UBYTE *a, UBYTE *b)
{
    *a = wWaveCursor;
    *b = wWaveCursor;
    if (wCursorMode == 4) {
        if (wWaveSelStart < wWaveCursor)
            *a = wWaveSelStart;
        else
            *b = wWaveSelStart;
    }
    (*b)++;
}

void WaveInitSprites(void)
{
    UBYTE y, i;

    i = 0x0C;
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOAMByte(i++, y);
        i++;
        SetOAMByte(i++, 5);
        SetOAMByte(i++, 0x82);
    }
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOAMByte(i++, y);
        i++;
        SetOAMByte(i++, 5);
        SetOAMByte(i++, 0x82);
    }
    ChainSetCursorSprite();
}

/* One column of the wave graph: samples 2*col and 2*col+1 as dots (one
   tile when they share a row, the half-tile pair otherwise; CGB and DMG
   tile numbers), the empty tile on the column's other rows, and the two
   values in hex below the graph. */
void DrawWaveColumn(UBYTE col)
{
    UBYTE s0, s1, i, j;
    UBYTE *p;
    char t;

    s0 = ReadWaveSample(col * 2);
    s1 = ReadWaveSample(col * 2 + 1);
    i = s0 >> 1;
    j = s1 >> 1;
    p = TilemapAddr(col + 2, 13 - i);
    MemSet(wWaveColEmpty, 1, 8);
    wWaveColEmpty[i] = 0;
    wWaveColEmpty[j] = 0;
    if (i == j) {
        WriteVramByteSafe(p, (s0 & 1)
                          ? ((s1 & 1) ? (hGameboyType ? 0x54 : 0xDE) : (hGameboyType ? 0x55 : 0xDF))
                          : ((s1 & 1) ? (hGameboyType ? 0x56 : 0xE0) : (hGameboyType ? 0x57 : 0xE1)));
    } else {
        WriteVramByteSafe(p, (s0 & 1) ? (hGameboyType ? 0x50 : 0xDA) : (hGameboyType ? 0x53 : 0xDD));
        p = TilemapAddr(col + 2, 13 - j);
        WriteVramByteSafe(p, (s1 & 1) ? (hGameboyType ? 0x51 : 0xDB) : (hGameboyType ? 0x52 : 0xDC));
    }
    p = TilemapAddr(col + 2, 6);
    for (i = 7; ; i--) {
        if (wWaveColEmpty[i])
            WriteVramByteSafe(p, 3);
        if (!i)
            break;
        p += 0x20;
    }
    if (col & 2) {
        hBgDigitPalette = wShadowDigitPal;
        hBgPalette = wShadowPal;
    } else {
        hBgDigitPalette = wSelectDigitPal;
        hBgPalette = wSelectPal;
    }
    t = col;                        /* a char copy: the test and the x read b */
    if (t & 8)
        GetTilemapAddr(t * 2 - 14, 14);
    else
        GetTilemapAddr(col * 2 + 2, 5);
    DrawByte(ReadWaveSample(col * 2));
    DrawByte(ReadWaveSample(col * 2 + 1));
}

void ChainDrawCursorByte(void)
{
    DrawWaveColumn(wWaveCursor >> 1);
}

void ChainWriteWaveA(void)
{
    UBYTE start, end, t;

    ChainWriteCursorPair(&start, &end);
    t = start;
    start = t & 0xFE;
    for (; start < end; start += 2)
        DrawWaveColumn(start >> 1);
}

void ChainDrawRow16(void)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        DrawWaveColumn(i);
}

void DrawWaveScreen(void)
{
    WaveInitSprites();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    FarCall(1, FAR_FN(DrawTopBar));
    ChainDrawRow16();
    FarCall(5, MapDrawTopRow);
}

void ChainWriteWaveC(void)
{
    UBYTE start, end, t;

    ChainWriteCursorPair(&start, &end);
    while (start < end) {
        WriteWaveSample(start, 0x0F - ReadWaveSample(start));
        t = start;
        start = t + 1;
    }
    ChainWriteWaveA();
}

void ChainWriteWaveD(UBYTE d)
{
    UBYTE start, end, t;

    ChainWriteCursorPair(&start, &end);
    while (start < end) {
        WriteWaveSample(start, ReadWaveSample(start) + d);
        t = start;
        start = t + 1;
    }
    ChainWriteWaveA();
}

void DrawWaveCursor(UBYTE d)
{
    UBYTE n, v;

    n = (wWaveCursor + d) & 0x1F;
    v = ReadWaveSample(wWaveCursor);
    WriteWaveSample(n, v);
    ChainDrawCursorByte();
    wWaveCursor = n;
    ChainDrawCursorByte();
    ChainSetCursorSprite();
}

UBYTE WaveScreenInput(void)
{
    switch (wButtons) {
    case 0x43:
        if (wCursorMode == 4) {
            wWaveCursor = wWaveSelStart;
            FarCall(5, RestoreSelectionState);
            DrawWaveScreen();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case 0x01:
        if (!wButtonsSame || wWaveCursor != 0x1F) {
            wWaveCursor++;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            ChainSetCursorSprite();
        }
        return 1;
    case 0x02:
        if (!wButtonsSame || wWaveCursor) {
            wWaveCursor--;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            ChainSetCursorSprite();
        }
        return 1;
    case 0x04:
        ChainWriteWaveD(1);
        return 1;
    case 0x08:
        ChainWriteWaveD(0xFF);
        return 1;
    case 0x12:
        if (wCursorMode == 4)
            ChainWriteWaveB();
        else
            DrawWaveCursor(0xFF);
        return 1;
    case 0x11:
        if (wCursorMode == 4)
            ChainWriteWaveB();
        else
            DrawWaveCursor(1);
        return 1;
    case 0x14:
    case 0x18:
        ChainWriteWaveC();
        return 1;
    case 0x21:
        wSynth++;
        wMsgStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x22:
        wSynth--;
        wMsgStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x24:
        wSynth += 0x10;
        wMsgStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x28:
        wSynth -= 0x10;
        wMsgStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    case 0x42:
        if (wButtonsSame)
            return 0;
        if (wSynth < 0xF0)
            wSynthScreen = wSynth >> 4;
        FarCall(5, MapMoveLeft);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    case 0x48:
        if (wButtonsSame)
            return 0;
        FarCall(5, MapMoveDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    case 0x60:
        if (!wSelectHeld)
            return 0;
        wSelectHeld = 0;
        switch (wCursorMode) {
        case 1:
        case 2:
            FarCall(5, SaveSelectionState);
            wWaveSelStart = wWaveCursor;
            ShowMessage("SELECT");
            break;
        case 4:
            wWaveSelStart = 0;
            wWaveCursor = 0x1F;
            break;
        }
        ChainSetCursorSprite();
        return 1;
    case 0x20:
        if (wCursorMode != 4)
            return 0;
        if (!wAPressEnabled)
            return 0;
        CopyWaveSelection();
        ChainSetCursorSprite();
        return 1;
    case 0x50:
        if (wCursorMode != 2)
            return 0;
        if (wClipboardType != 8)
            return 0;
        if (!wSelectHeld)
            return 0;
        wSelectHeld = 0;
        PasteWaveSelection();
        return 1;
    }
    return 0;
}

void CopyWaveSelection(void)
{
    UBYTE v;
    UBYTE i;

    if (wClipboardType != 8)
        DebugTrap();
    ShowMessage(TxtCopy);
    wWaveSelEnd = wWaveCursor;
    wWaveCursor = wWaveSelStart;
    SortBytePair(&wWaveSelStart, &wWaveSelEnd);
    wWaveSelEnd++;
    for (i = 0; i < 0x20; i += 2) {
        v = ReadWaveSample(i) << 4;
        v |= ReadWaveSample(i + 1);
        WriteClipboardCell(0, i >> 1, v);
    }
    WriteClipboardCell(0, 0x10, wWaveSelStart);
    WriteClipboardCell(0, 0x11, wWaveSelEnd);
    wSelLeft = 0;
    wSelRight = 1;
    wSelTop = 0;
    wSelBottom = 0x12;
    wCursorMode = 2;
    FarCall(0x1C, FAR_FN(CopySelection));
}

void PasteWaveSelection(void)
{
    UBYTE i, v;

    wWaveSelStart = ReadClipboardCell(0, 0x10);
    wWaveSelEnd = ReadClipboardCell(0, 0x11);
    i = wWaveSelStart;
    while (i != wWaveSelEnd) {
        v = ReadClipboardCell(0, i >> 1);
        if (!(i & 1))
            v >>= 4;
        else
            v &= 0x0F;
        WriteWaveSample(wWaveCursor, v);
        i++;
        wWaveCursor++;
        wWaveCursor &= 0x1F;
    }
    ShowMessage("PASTE");
    ChainDrawRow16();
    ChainSetCursorSprite();
}

UBYTE GetCursorChain(void)
{
    return ReadSeqByte(wSongCol, wSongRow);
}

UBYTE ChainLoadCursorPhrase(void)
{
    UBYTE row, ph;

    row = wChainRow;
    for (;;) {
        ph = FarCallArg2(2, (UWORD)SndGetChainPhrase, wChain, row);
        if (ph != 0xFF) {
            wPhrase = ph;
            wChainRow = row;
            return 1;
        }
        if (!row)
            return 0;
        row--;
    }
}

UBYTE ChainScreenEnter(UBYTE col)
{
    UBYTE chain, row;

    chain = wChain;
    row = wSongRow;
    if (col >= 4)
        DebugTrap();
    wMsgStrPtr = 0;
    if (wLiveMode && wChanActive[col]) {
        wChain = wChanChain[col];
        wSongRow = wChanSongRow[col];
    } else {
        wChain = ReadSeqByte(col, wSongRow);
        while (wChain == 0xFF && wSongRow)
            wChain = ReadSeqByte(col, --wSongRow);
    }
    if (wChain & 0x80) {
        wChain = chain;
        wSongRow = row;
        return 0;
    }
    wSongCol = col;
    DrawChainScreen();
    return 1;
}

void DrawChainPos(void)
{
    UBYTE r;

    wVramCursor = (UBYTE *)0x9851;
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    DrawString("CP");
    r = wChainRow;
    DrawByte(r);
}

void SetChainTranspose(UBYTE chain, UBYTE row, UBYTE v)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramChainTransposes + ((UWORD)chain << 4);
    WriteSongByte(1, p + row, v);
}

UBYTE ChainReadTrans(UBYTE chain, UBYTE step)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainTransposes + ((UWORD)chain << 4);
    return ReadSongByte(1, p + step);
}

void ClonePhraseAtCursor(UBYTE src, UBYTE dst)
{
    UBYTE *s1, *d1, *s2, *d2, *s3, *d3, *s4, *d4;
    UWORD soff, doff;
    UBYTE v;
    UBYTE i;

    soff = (UWORD)src << 4;
    doff = (UWORD)dst << 4;
    if (wCurrentScreen == 3)
        ShowMessage(TxtClonedPhrase);
    if (src == 0xFF)
        DebugTrap();
    if (dst == 0xFF)
        DebugTrap();
    s1 = (UBYTE *)0xA000 + soff;
    s2 = (UBYTE *)0xB000 + soff;
    s3 = (UBYTE *)0xA000 + soff;
    s4 = (UBYTE *)0xAFF0 + soff;
    d1 = (UBYTE *)0xA000 + doff;
    d2 = (UBYTE *)0xB000 + doff;
    d3 = (UBYTE *)0xA000 + doff;
    d4 = (UBYTE *)0xAFF0 + doff;
    for (i = 0; i < 16; i++) {
        v = ReadSongByte(0, s1++);
        WriteSongByte(0, d1++, v);
        v = ReadSongByte(3, s2++);
        WriteSongByte(3, d2++, v);
        v = ReadSongByte(2, s3++);
        WriteSongByte(2, d3++, v);
        v = ReadSongByte(2, s4++);
        WriteSongByte(2, d4++, v);
    }
}

void ChainDrawCellLo(void)
{
    UBYTE r, c;

    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    c = wChainCol;
    ColorField(ChainColX[wChainCol], wChainRow + 2, 2, c && hGameboyType);
}

void LoadChainRow(void)
{
    UBYTE *p, *q;

    p = SramChainPhrases + ((UWORD)wChain << 4);
    q = SramChainTransposes + ((UWORD)wChain << 4);
    ReadSongBytes(1, p, wSongReadBuf);
    ReadSongBytes(1, q, wUsedTableMap);
}

void DrawChainScreen(void)
{
    UBYTE y;
    UBYTE i, v, pal, r;

    if (wChain & 0x80)
        DebugTrap();
    SetSpriteX(0, 1);
    SetSpriteTile3(0);
    FarCall(1, FAR_FN(DrawTopBar));
    DrawTempoField();
    DrawChainPos();
    LoadChainRow();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x9841;
    for (i = 0;; i++) {
        v = wSongReadBuf[i];
        if (v == 0xFF) {
            DrawString(TxtDashes2);
        } else {
            DrawStringTile20();
            DrawHexByte(v);
        }
        if (i == 15)
            break;
        wVramCursor += 0x1D;
    }
    wVramCursor = (UBYTE *)0x9845;
    for (i = 0;; i++) {
        DrawHexByte(wUsedTableMap[i]);
        if (i == 15)
            break;
        wVramCursor += 0x1E;
    }
    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    pal = hGameboyType ? 3 : 2;
    y = wChainRow + 2;
    ColorField(ChainColX[wChainCol], y, 2, pal);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
}

void ChainScreenUpdate(void)
{
    if ((wPlayMode == 4 || wPlayMode == 3) && wChanActive[wSongCol]
            && wChain == wChanChain[wSongCol])
        SetSpriteY(0, wChanChainRow[wSongCol]);
    else
        ClearSpriteY(0);
}

void ChainDrawCellHw(void)
{
    ColorField(ChainColX[wChainCol], wChainRow + 2, 2, hGameboyType ? 3 : 2);
}

void ChainOpenPhrase(void)
{
    UBYTE ph;
    UBYTE n;

    ph = FarCallArg2(2, (UWORD)SndGetChainPhrase, wChain, (char)wChainRow);
    FarCall(5, 0x747D);                 /* RestoreSelectionState, bank 5, by address */
    if (ph == 0xFF)
        return;
    wColPhrase[wSongCol] = ph;
    n = FarCallByte(1, FAR_FN(AllocNextFreePhrase));
    if (n != 0xFF) {
        ClonePhraseAtCursor(ph, n);
        FarCallArg3(0x1F, (UWORD)WriteChainPhrase, wChain, (char)wChainRow, n);
        GetTilemapAddr(2, wChainRow + 2);
        hBgDigitPalette = wSelectDigitPal;
        hBgPalette = wSelectPal;
        DrawHexByte(n);
    } else {
        FarCall(1, FAR_FN(ShowOutOfMemory));
    }
}

void CutCopyChainSelection(UBYTE cut)
{
    UBYTE x, y;

    if (wClipboardType != 3)
        return;
    wCursorMode = 2;
    wSelRight = wChainCol;
    wSelBottom = wChainRow;
    SortBytePair(&wSelLeft, &wSelRight);
    SortBytePair(&wSelTop, &wSelBottom);
    wSelRight++;
    wSelBottom++;
    for (x = wSelLeft; x < wSelRight; x++) {
        for (y = wSelTop; y < wSelBottom; y = y + 1) {
            wChainCol = x;
            wChainRow = y;
            ChainDrawCellLo();
            switch (x) {
            case 0:
                WriteClipboardCell(0, y, FarCallArg2P(2, FAR_FN(SndGetChainPhrase), wChain, y));
                if (cut)
                    FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, y, 0xFF);
                break;
            case 1:
                WriteClipboardCell(1, y, ChainReadTrans(wChain, y));
                if (cut)
                    SetChainTranspose(wChain, y, 0);
                break;
            }
        }
    }
    FarCall(0x1C, FAR_FN(CopySelection));
    wChainCol = wSelLeft;
    wChainRow = wSelTop;
    ShowMessage(cut ? "CUT " : "COPY");
    DrawChainScreen();
}

void ChainPasteCells(void)
{
    UBYTE x, y;

    for (y = wSelTop; y < wSelBottom; y++) {
        for (x = wSelLeft; x < wSelRight; x = x + 1) {
            switch (x) {
            case 0:
                FarCallArg3P(0x1F, FAR_FN(WriteChainPhrase), wChain, wChainRow, ReadClipboardCell(0, y));
                break;
            case 1:
                SetChainTranspose(wChain, wChainRow, ReadClipboardCell(1, y));
                break;
            }
        }
        wChainRow++;
        wChainRow = (char)wChainRow & 0x0F;     /* read into c, like ChainOpenPhrase's (char)wChainRow */
    }
    if (wSelBottom - wSelTop > 1)
        ShowMessage("PASTE");
}

void DrawChainSelection(void)
{
    UBYTE x, y, left, top, right, bottom;
    UBYTE col;

    if (wClipboardType != 3)
        return;
    col = wChainCol;                /* into c for the test only, as DrawPhraseSelection */
    if (wSelLeft < col) {
        left = wSelLeft;
        right = wChainCol + 1;
    } else {
        right = wSelLeft + 1;
        left = wChainCol;
    }
    if (wSelTop < wChainRow) {
        top = wSelTop;
        bottom = wChainRow + 1;
    } else {
        bottom = wSelTop + 1;
        top = wChainRow;
    }
    /* both columns' cells around the rectangle back to their own palettes... */
    for (x = 0; x < 2; x++) {
        for (y = top ? top - 1 : 0; y < (bottom == 0x10 ? 0x10 : bottom + 1); y = y + 1) {
            if (x < left || x >= right || y < top || y >= bottom)
                ColorField(ChainColX[x], y + 2, 2, hGameboyType ? x : 0);
        }
    }
    /* ...and the rectangle highlighted */
    for (x = left; x < right; x++)
        for (y = top; y < bottom; y = y + 1)
            ColorField(ChainColX[x], y + 2, 2, hGameboyType ? 3 : 2);
}

void DrawKitSampleNames(UBYTE ch, UBYTE note)
{
    UBYTE c, n;

    GetTilemapAddr(17, ch + 6);
    if (wSndUpdateFlags && ch == 2) {
        wSampleNoteString[0] = 0;
        if (note & 0xF0)
            ReadKitSampleName(wKit1Bank, note >> 4);
        else
            ReadKitSampleName(wKit2Bank, note & 0x0F);
        c = wSampleNoteString[1];
        if (c == '-')
            wSampleNoteString[1] = ' ';
        c = wSampleNoteString[2];
        if (c == '-')
            wSampleNoteString[2] = ' ';
        DrawString(wSampleNoteString);
        return;
    }
    switch (ch) {
    case 3:
        n = *(UBYTE *)0xC2E7;               /* wNoiNote, read through the literal */
        n++;
        DrawChainCell(3, n);
        return;
    case 1:
        note += wNoteTranspose;
        break;
    }
    if (note <= 0x6C)
        DrawChainCell(ch, note);
}

void UpdateStatusPanel(void)
{
    UBYTE i;
    UBYTE n;
    UBYTE *p;
    UBYTE t;

    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    for (i = 0; i < 4; i++) {
        t = (wChanModFlag[i] & 0x80) && !wChanModType[i];
        if (t)
            continue;
        n = wChanDispNote[i];
        if (n) {
            DrawKitSampleNames(i, n);
            wChanDispNote[i] = 0;
            if (i == 2)
                PositionWavSprites();
            wChanDispNoteTimer[i] = 0x19;
        } else {
            p = &wChanDispNoteTimer[i];
            n = *p;
            if (n != 0) {
                if (n == 1) {
                    GetTilemapAddr(17, i + 6);
                    DrawString("   ");
                }
                *p = n - 1;
            }
        }
    }
    if (wTempo != wTempoShadow) {
        wTempoShadow = wTempo;
        DrawTempoField();
    }
    ChainUpdateScope();
}

void DrawStringAtCursor(char *s)
{
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = (UBYTE *)0x99B0;
    DrawString(s);
}

void ChainDrawSyncStatus(void)
{
    if (wSyncHandshake)
        DrawStringAtCursor("WAIT");
    else if (wSyncRun)
        DrawStringAtCursor("LEAD");
    else if (ChainSyncStatusFlag())
        DrawStringAtCursor("SYNC");
    else
        DrawStringAtCursor("    ");
}

void ChainDrawLeadStatus(void)
{
    if (ChainSyncStatusFlag())
        DrawStringAtCursor("SYNC");
    else if (wPlayMode)
        DrawStringAtCursor("WAIT");
    else
        DrawStringAtCursor("    ");
}

void DrawSyncStatus(void)
{
    switch (wSyncMode) {
    case 1:
        ChainDrawSyncStatus();
        break;
    case 3:
        ChainDrawLeadStatus();
        break;
    case 6:
        DrawStringAtCursor(wPlayMode ? "SYNC" : "    ");
        break;
    case 7:
        DrawStringAtCursor(wPlayMode ? "LEAD" : "    ");
        break;
    }
}

void DrawChainCell(UBYTE col, UBYTE v)
{
    char *p;
    UBYTE t;

    if (!v) {
        DrawString(TxtNoNote);
    } else if (col == 3) {
        t = v - 1;
        if (t < 0x3C) {
            DrawHexByte(t);
            DrawStringTile20();
        } else if ((UWORD)t < 0x78) {
            switch (t) {
            default:
                DrawTileHexCell(t);
                break;
            case 0x73:
                DrawString(TxtKitNoteC5);
                break;
            case 0x74:
                DrawString(TxtKitNoteF5);
                break;
            case 0x75:
                DrawString(TxtKitNoteC6);
                break;
            case 0x76:
                DrawString(TxtKitNoteC7);
                break;
            case 0x77:
                DrawString(TxtKitNoteC8);
                break;
            }
        } else {
            DrawString(TxtKitNoteBad);
        }
    } else if (v <= 0x6C) {
        p = NoteNames + (UWORD)v * 3;
        DrawStringIndexed(p[0]);
        DrawStringIndexed(p[1]);
        DrawStringIndexed(col == 2 ? p[2] - 1 : p[2]);
    } else {
        DrawString(TxtNoteBad);
    }
}
