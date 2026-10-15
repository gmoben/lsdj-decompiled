/*
 * The wave screen -- LSDj 9.4.0, ROM bank $06.
 *
 * The wave graph and its columns, the cursor and selection sprites, the
 * range edits (reverse, flip, change, line), copy and paste, the synth
 * lock.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wWaveCursor;                   /* $C42B */
extern UBYTE wSynth;                        /* $C42A */
extern UBYTE wWaveSelectionStart;                 /* $C93B */
extern UBYTE wWaveSelectionEnd;                   /* $C93C */
void DrawWaveCol(UBYTE i);
void WriteWaveSample(UBYTE n, UBYTE v);
UBYTE ReadWaveSample(UBYTE i);
void InitWaveSprites(void);
void SetWaveSpriteCols(UBYTE s, UBYTE x);
void GetWaveEditRange(UBYTE *a, UBYTE *b);
void DrawWaveRange(void);
void LockCurSynth(void);
void DrawWaveCols(void);
void SetWaveCursorSprite(void);
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wWaveColEmpty[];               /* $C93D, [8] */
void WriteVramByteSafe(UBYTE *addr, UBYTE v);  /* $00:$17EC */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void ReverseWaveRange(void);
void CopyWaveSelection(void);
void PasteWaveSelection(void);
void SaveSelectionState(void);              /* $05:$743F */
void MoveMapCursorLeft(void);                     /* $05:$4233 */
void MoveMapCursorDown(void);                     /* $05:$4287 */
void SndToggleSongPlayback(void);                 /* $02:$78C4 */
void SndTogglePlayback(void);                    /* $02:$7C4C */
void SendLinkClipboard(void);                   /* bank $1C */
extern char TxtCopy[];                      /* $06:$5467 "COPY" */

UBYTE *TilemapAddr(UBYTE x, UBYTE y)
{
    UWORD a;

    a = y;
    a <<= 5;
    a += x;
    a |= 0x9800;
    return (UBYTE *)a;
}

UWORD SynthLockByteAddr(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

/* $493F: mark the current synth (wSynth >> 4) locked: set its bit in the
   lock byte SynthLockByteAddr picks ($BFC4/$BFC5). m is a frame byte that
   only ever receives s (a volatile local nothing reads models it); r named
   so the OR reads it twice. */
void LockCurSynth(void)
{
    UBYTE s;
    volatile UBYTE m;
    UWORD p;
    UBYTE r;

    s = wSynth;
    s >>= 4;
    p = SynthLockByteAddr(s);
    r = ReadSongByte(1, p);
    m = s;
    r |= 1 << (s & 7);
    WriteSongByte(1, p, r);
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
    LockCurSynth();
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

void SetWaveSpriteCols(UBYTE y1, UBYTE y2)
{
    UBYTE i;

    for (i = 3; (UWORD)i != 11; i++)
        SetOamByte((i << 2) + 1, y1);
    for (; (UWORD)i != 19; i++)
        SetOamByte((i << 2) + 1, y2);
}

void SetWaveCursorSprite(void)
{
    UBYTE x, s;

    x = (wWaveCursor << 2) + 0x18;
    s = (wWaveSelectionStart << 2) + 0x18;
    if (wCursorMode != 4)
        s = x;
    SetWaveSpriteCols(s, x);
}

void GetWaveEditRange(UBYTE *a, UBYTE *b)
{
    *a = wWaveCursor;
    *b = wWaveCursor;
    if (wCursorMode == 4) {
        if (wWaveSelectionStart < wWaveCursor)
            *a = wWaveSelectionStart;
        else
            *b = wWaveSelectionStart;
    }
    (*b)++;
}

void InitWaveSprites(void)
{
    UBYTE y, i;

    i = 0x0C;
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOamByte(i++, y);
        i++;
        SetOamByte(i++, 5);
        SetOamByte(i++, 0x82);
    }
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOamByte(i++, y);
        i++;
        SetOamByte(i++, 5);
        SetOamByte(i++, 0x82);
    }
    SetWaveCursorSprite();
}

/* One column of the wave graph: samples 2*col and 2*col+1 as dots (one
   tile when they share a row, the half-tile pair otherwise; CGB and DMG
   tile numbers), the empty tile on the column's other rows, and the two
   values in hex below the graph. */
void DrawWaveCol(UBYTE col)
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
        hBgDigitPal = wShadedDigitPal;
        hBgPal = wShadedPal;
    } else {
        hBgDigitPal = wHighlightDigitPal;
        hBgPal = wHighlightPal;
    }
    t = col;                        /* a char copy: the test and the x read b */
    if (t & 8)
        SetVramCursor(t * 2 - 14, 14);
    else
        SetVramCursor(col * 2 + 2, 5);
    DrawByte(ReadWaveSample(col * 2));
    DrawByte(ReadWaveSample(col * 2 + 1));
}

void DrawWaveCursorCol(void)
{
    DrawWaveCol(wWaveCursor >> 1);
}

void DrawWaveRange(void)
{
    UBYTE start, end, t;

    GetWaveEditRange(&start, &end);
    t = start;
    start = t & 0xFE;
    for (; start < end; start += 2)
        DrawWaveCol(start >> 1);
}

void DrawWaveCols(void)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        DrawWaveCol(i);
}

void DrawWaveScreen(void)
{
    InitWaveSprites();
    hBgDigitPal = 4;
    hBgPal = 1;
    FarCall(1, FAR_FN(DrawTopBar));
    DrawWaveCols();
    FarCall(5, DrawMapTopRow);
}

/* Reverse the samples of the range: swap them from both ends inwards. The
   second read is inline and only the first is named: the ROM spills the
   first result and keeps the second in b (named, the second is pushed
   from e and the first stays in b). */
void ReverseWaveRange(void)
{
    UBYTE start, end, v;

    GetWaveEditRange(&start, &end);
    while (start < --end) {
        v = ReadWaveSample(start);
        WriteWaveSample(start, ReadWaveSample(end));
        WriteWaveSample(end, v);
        ++start;
    }
    DrawWaveRange();
}

void FlipWaveRange(void)
{
    UBYTE start, end, t;

    GetWaveEditRange(&start, &end);
    while (start < end) {
        WriteWaveSample(start, 0x0F - ReadWaveSample(start));
        t = start;
        start = t + 1;
    }
    DrawWaveRange();
}

void ChangeWaveRange(UBYTE d)
{
    UBYTE start, end, t;

    GetWaveEditRange(&start, &end);
    while (start < end) {
        WriteWaveSample(start, ReadWaveSample(start) + d);
        t = start;
        start = t + 1;
    }
    DrawWaveRange();
}

void DrawWaveLine(UBYTE d)
{
    UBYTE n, v;

    n = (wWaveCursor + d) & 0x1F;
    v = ReadWaveSample(wWaveCursor);
    WriteWaveSample(n, v);
    DrawWaveCursorCol();
    wWaveCursor = n;
    DrawWaveCursorCol();
    SetWaveCursorSprite();
}

UBYTE WaveScreenInput(void)
{
    switch (wButtons) {
    case 0x43:
        if (wCursorMode == 4) {
            wWaveCursor = wWaveSelectionStart;
            FarCall(5, RestoreSelectionState);
            DrawWaveScreen();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case 0x01:
        if (!wButtonsSame || wWaveCursor != 0x1F) {
            wWaveCursor++;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            SetWaveCursorSprite();
        }
        return 1;
    case 0x02:
        if (!wButtonsSame || wWaveCursor) {
            wWaveCursor--;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            SetWaveCursorSprite();
        }
        return 1;
    case 0x04:
        ChangeWaveRange(1);
        return 1;
    case 0x08:
        ChangeWaveRange(0xFF);
        return 1;
    case 0x12:
        if (wCursorMode == 4)
            ReverseWaveRange();
        else
            DrawWaveLine(0xFF);
        return 1;
    case 0x11:
        if (wCursorMode == 4)
            ReverseWaveRange();
        else
            DrawWaveLine(1);
        return 1;
    case 0x14:
    case 0x18:
        FlipWaveRange();
        return 1;
    case 0x21:
        wSynth++;
        wMessageStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x22:
        wSynth--;
        wMessageStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x24:
        wSynth += 0x10;
        wMessageStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0x28:
        wSynth -= 0x10;
        wMessageStrPtr = 0;
        DrawWaveScreen();
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SndToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SndTogglePlayback));
        return 1;
    case 0x42:
        if (wButtonsSame)
            return 0;
        if (wSynth < 0xF0)
            wSynthScreen = wSynth >> 4;
        FarCall(5, MoveMapCursorLeft);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    case 0x48:
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
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
            wWaveSelectionStart = wWaveCursor;
            ShowMessage("SELECT");
            break;
        case 4:
            wWaveSelectionStart = 0;
            wWaveCursor = 0x1F;
            break;
        }
        SetWaveCursorSprite();
        return 1;
    case 0x20:
        if (wCursorMode != 4)
            return 0;
        if (!wAPressEnabled)
            return 0;
        CopyWaveSelection();
        SetWaveCursorSprite();
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
    wWaveSelectionEnd = wWaveCursor;
    wWaveCursor = wWaveSelectionStart;
    SortBytePair(&wWaveSelectionStart, &wWaveSelectionEnd);
    wWaveSelectionEnd++;
    for (i = 0; i < 0x20; i += 2) {
        v = ReadWaveSample(i) << 4;
        v |= ReadWaveSample(i + 1);
        WriteClipboardCell(0, i >> 1, v);
    }
    WriteClipboardCell(0, 0x10, wWaveSelectionStart);
    WriteClipboardCell(0, 0x11, wWaveSelectionEnd);
    wSelectionLeft = 0;
    wSelectionRight = 1;
    wSelectionTop = 0;
    wSelectionBottom = 0x12;
    wCursorMode = 2;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
}

void PasteWaveSelection(void)
{
    UBYTE i, v;

    wWaveSelectionStart = ReadClipboardCell(0, 0x10);
    wWaveSelectionEnd = ReadClipboardCell(0, 0x11);
    i = wWaveSelectionStart;
    while (i != wWaveSelectionEnd) {
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
    DrawWaveCols();
    SetWaveCursorSprite();
}
