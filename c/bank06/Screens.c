/*
 * The chain, wave and word screens -- LSDj 9.4.0, ROM bank $06.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wChain;                        /* $C418, the chain being edited */
extern UBYTE wWaveCursor;                   /* $C42B */
extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wChainCol;                     /* $C419 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wWordClipCell0[];              /* $C935 */
extern UBYTE wWordEditRow;                  /* $C937 */
extern UBYTE wWordEditCol;                  /* $C938 */
extern UBYTE wWaveSelStart;                 /* $C93B */
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
void AccessWaveFrame(UBYTE n, UBYTE v);
UBYTE ChainReadPhraseNote(UBYTE i);
void ChainDrawWaveform(void);
void ChainScanCmd(UBYTE s, UBYTE x);
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */

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

UWORD ChainCalcParamAddr(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

void ChainSetCursorSprite(void)
{
    UBYTE x, s;

    x = (wWaveCursor << 2) + 0x18;
    s = (wWaveSelStart << 2) + 0x18;
    if (wCursorMode != 4)
        s = x;
    ChainScanCmd(s, x);
}

void ChainDrawCursorByte(void)
{
    LoadKitSampleInfo(wWaveCursor >> 1);
}

void ChainDrawRow16(void)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        LoadKitSampleInfo(i);
}

void DrawWaveScreen(void)
{
    ChainDrawWaveform();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    FarCall(1, FAR_FN(DrawTopBar));
    ChainDrawRow16();
    FarCall(5, MapDrawTopRow);
}

void DrawWaveCursor(UBYTE d)
{
    UBYTE n, v;

    n = (wWaveCursor + d) & 0x1F;
    v = ChainReadPhraseNote(wWaveCursor);
    AccessWaveFrame(n, v);
    ChainDrawCursorByte();
    wWaveCursor = n;
    ChainDrawCursorByte();
    ChainSetCursorSprite();
}

UBYTE GetCursorChain(void)
{
    return ReadSeqByte(wSongCol, wSongRow);
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
