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

void LoadKitSampleInfo(UBYTE i);
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
    DrawTextField(WordColX[wWordEditCol], wWordEditRow + 2, WordColWidth[wWordEditCol], wWordEditCol);
}

void ChainDrawWaveCellHw(void)
{
    DrawTextField(WordColX[wWordEditCol], wWordEditRow + 2, WordColWidth[wWordEditCol], hGameboyType ? 3 : 2);
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

void ChainDrawCursorByte(void)
{
    LoadKitSampleInfo(wWaveCursor >> 1);
}

void ChainWriteWaveA(void)
{
    UBYTE start, end, t;

    ChainWriteCursorPair(&start, &end);
    t = start;
    start = t & 0xFE;
    for (; start < end; start += 2)
        LoadKitSampleInfo(start >> 1);
}

void ChainDrawRow16(void)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        LoadKitSampleInfo(i);
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

void PasteWaveSelection(void)
{
    UBYTE i, v;

    wWaveSelStart = ReadClipboardCell(0, 0x10);
    wWaveSelEnd = ReadClipboardCell(0, 0x11);
    i = wWaveSelStart;
    while (i != wWaveSelEnd) {
        v = ReadClipboardCell(0, i >> 1);
        if (!(i \& 1))
            v >>= 4;
        else
            v \&= 0x0F;
        WriteWaveSample(wWaveCursor, v);
        i++;
        wWaveCursor++;
        wWaveCursor \&= 0x1F;
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

void ChainDrawCellLo(void)
{
    UBYTE r, c;

    r = wChainRow;
    if (r & 0xF0)
        DebugTrap();
    c = wChainCol;
    DrawTextField(ChainColX[wChainCol], wChainRow + 2, 2, c && hGameboyType);
}

void LoadChainRow(void)
{
    UBYTE *p, *q;

    p = SramChainPhrases + ((UWORD)wChain << 4);
    q = SramChainTransposes + ((UWORD)wChain << 4);
    ReadSongBytes(1, p, wSongReadBuf);
    ReadSongBytes(1, q, wUsedTableMap);
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
    DrawTextField(ChainColX[wChainCol], wChainRow + 2, 2, hGameboyType ? 3 : 2);
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
