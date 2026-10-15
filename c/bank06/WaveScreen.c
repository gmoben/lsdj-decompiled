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
extern UBYTE wWaveSelectionStart;           /* $C93B */
extern UBYTE wWaveSelectionEnd;             /* $C93C */
void DrawWaveColumn(UBYTE i);
void WriteWaveSample(UBYTE n, UBYTE v);
UBYTE ReadWaveSample(UBYTE position);
void InitializeWaveSprites(void);
void SetWaveSpriteColumns(UBYTE s, UBYTE x);
void GetWaveEditRange(UBYTE *a, UBYTE *b);
void DrawWaveRange(void);
void LockCurrentSynth(void);
void DrawWaveColumns(void);
void SetWaveCursorSprite(void);
extern UBYTE wSelectionTop;                 /* $C40E */
extern UBYTE wSelectionRight;               /* $C40F */
extern UBYTE wSelectionBottom;              /* $C410 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wWaveColumnEmpty[];            /* $C93D, [8] */
void WriteVramByteSafe(UBYTE *address, UBYTE v); /* $00:$17EC */
extern UBYTE wSynthScreen;                  /* $C441 */
extern UBYTE wSelectHeld;                   /* $C40A */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void ReverseWaveRange(void);
void CopyWaveSelection(void);
void PasteWaveSelection(void);
void SaveSelectionState(void);              /* $05:$743F */
void MoveMapCursorLeft(void);               /* $05:$4233 */
void MoveMapCursorDown(void);               /* $05:$4287 */
void SoundToggleSongPlayback(void);         /* $02:$78C4 */
void SoundTogglePlayback(void);             /* $02:$7C4C */
void SendLinkClipboard(void);               /* bank $1C */
extern char TxtCopy[];                      /* $06:$5467 "COPY" */

/* $48AB: the BG tilemap address of cell x,y: $9800 | (y*32 + x) */
UBYTE *TilemapAddress(UBYTE x, UBYTE y)
{
    UWORD a;

    a = y;
    a <<= 5;
    a += x;
    a |= BG_MAP(0, 0);
    return (UBYTE *)a;
}

/* $4901: the SRAM byte holding synth x's lock bit ("synths locked?"
   $BFC4-$BFC5): $BFC5 for 0-7, $BFC4 for 8-F */
UWORD SynthLockByteAddress(UBYTE x)
{
    UWORD p;

    p = (x & 8) ? 0xBFC4 : 0xBFC5;
    if (x >= 0x10)
        DebugTrap();
    return p;
}

/* $493F: mark the current synth (wSynth >> 4) locked: set its bit in the
   lock byte SynthLockByteAddress picks ($BFC4/$BFC5). m is a frame byte that
   only ever receives s (a volatile local nothing reads models it); r named
   so the OR reads it twice. */
void LockCurrentSynth(void)
{
    UBYTE s;
    volatile UBYTE m;
    UWORD p;
    UBYTE r;

    s = wSynth;
    s >>= 4;
    p = SynthLockByteAddress(s);
    r = ReadSongByte(1, p);
    m = s;
    r |= 1 << (s & 7);
    WriteSongByte(1, p, r);
}

/* $4992: sample (i+1)&31 of the edited synth wave frame (SRAM bank 3 $A000 +
   wSynth*16), inverted: $0F ^ nibble */
UBYTE ReadWaveSample(UBYTE position)
{
    UBYTE *p;
    UBYTE v;

    if (position >= 0x20)
        DebugTrap();
    position = (position + 1) & 0x1F;
    p = SramWaveFrames + ((UWORD)wSynth << 4);
    p += position >> 1;
    v = ReadSongByte(3, p);
    if (position & 1)
        v &= 0x0F;
    else
        v >>= 4;
    return 0x0F ^ v;
}

/* $4A0A: write sample pos+1 (inverted nibble) of the edited wave frame (SRAM
   bank 3 ) */
void WriteWaveSample(UBYTE position, UBYTE v)
{
    UBYTE *p;
    UBYTE b;

    if (position >= 0x20)
        DebugTrap();
    p = SramWaveFrames + ((UWORD)wSynth << 4);
    LockCurrentSynth();
    v ^= 0x0F;
    position = (position + 1) & 0x1F;
    p += position >> 1;
    b = ReadSongByte(3, (UWORD)p);
    if (position & 1) {
        b &= 0xF0;
        v = (v & 0x0F) | b;
    } else {
        b &= 0x0F;
        v <<= 4;
        v |= b;
    }
    WriteSongByte(3, (UWORD)p, v);
}

/* $4AB0: set the x of wave sprites 3..10 and 11..18 */
void SetWaveSpriteColumns(UBYTE y1, UBYTE y2)
{
    UBYTE sprite;

    for (sprite = 3; (UWORD)sprite != 11; sprite++)
        SetOamByte((sprite << 2) + 1, y1);
    for (; (UWORD)sprite != 19; sprite++)
        SetOamByte((sprite << 2) + 1, y2);
}

/* $4B16: place the wave cursor and selection-anchor sprites from
   wWaveCursor/wWaveSelectionStart (SetWaveSpriteColumns) */
void SetWaveCursorSprite(void)
{
    UBYTE x, s;

    x = (wWaveCursor << 2) + 0x18;
    s = (wWaveSelectionStart << 2) + 0x18;
    if (wCursorMode != CURSOR_SELECTING)
        s = x;
    SetWaveSpriteColumns(s, x);
}

/* $4B40: the wave screen's edit range into *start/*end (end exclusive): the
   cursor sample, or the selection from wWaveSelectionStart (wCursorMode 4) */
void GetWaveEditRange(UBYTE *a, UBYTE *b)
{
    *a = wWaveCursor;
    *b = wWaveCursor;
    if (wCursorMode == CURSOR_SELECTING) {
        if (wWaveSelectionStart < wWaveCursor)
            *a = wWaveSelectionStart;
        else
            *b = wWaveSelectionStart;
    }
    (*b)++;
}

/* $4BA7: init the 16 wave-sample sprites (3..18): y, tile 5, attr ; x via
   SetWaveSpriteColumns */
void InitializeWaveSprites(void)
{
    UBYTE y, offset;

    offset = 0x0C;
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOamByte(offset++, y);
        offset++;
        SetOamByte(offset++, 5);
        SetOamByte(offset++, 0x82);
    }
    for (y = 0x40; (UWORD)y != 0x80; y += 8) {
        SetOamByte(offset++, y);
        offset++;
        SetOamByte(offset++, 5);
        SetOamByte(offset++, 0x82);
    }
    SetWaveCursorSprite();
}

/* One column of the wave graph: samples 2*col and 2*col+1 as dots (one
   tile when they share a row, the half-tile pair otherwise; CGB and DMG
   tile numbers), the empty tile on the column's other rows, and the two
   values in hex below the graph. */
void DrawWaveColumn(UBYTE column)
{
    UBYTE s0, s1, level, j;
    UBYTE *p;
    char t;

    s0 = ReadWaveSample(column * 2);
    s1 = ReadWaveSample(column * 2 + 1);
    level = s0 >> 1;
    j = s1 >> 1;
    p = TilemapAddress(column + 2, 13 - level);
    FillMemory(wWaveColumnEmpty, 1, 8);
    wWaveColumnEmpty[level] = 0;
    wWaveColumnEmpty[j] = 0;
    if (level == j) {
        WriteVramByteSafe(p, (s0 & 1)
                          ? ((s1 & 1) ? (hGameboyType ? 0x54 : 0xDE) : (hGameboyType ? 0x55 : 0xDF))
                          : ((s1 & 1) ? (hGameboyType ? 0x56 : 0xE0) : (hGameboyType ? 0x57 : 0xE1)));
    } else {
        WriteVramByteSafe(p, (s0 & 1) ? (hGameboyType ? 0x50 : 0xDA) : (hGameboyType ? 0x53 : 0xDD));
        p = TilemapAddress(column + 2, 13 - j);
        WriteVramByteSafe(p, (s1 & 1) ? (hGameboyType ? 0x51 : 0xDB) : (hGameboyType ? 0x52 : 0xDC));
    }
    p = TilemapAddress(column + 2, 6);
    for (level = 7; ; level--) {
        if (wWaveColumnEmpty[level])
            WriteVramByteSafe(p, 3);
        if (!level)
            break;
        p += 0x20;
    }
    if (column & 2) {
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
    } else {
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
    }
    t = column;                        /* a char copy: the test and the x read b */
    if (t & 8)
        SetVramCursor(t * 2 - 14, 14);
    else
        SetVramCursor(column * 2 + 2, 5);
    DrawByte(ReadWaveSample(column * 2));
    DrawByte(ReadWaveSample(column * 2 + 1));
}

/* $4EAC: redraw the wave column under the cursor (DrawWaveColumn(wWaveCursor
   >> 1)) */
void DrawWaveCursorColumn(void)
{
    DrawWaveColumn(wWaveCursor >> 1);
}

/* $4EBD: redraw the wave columns of the edit range */
void DrawWaveRange(void)
{
    UBYTE start, end, t;

    GetWaveEditRange(&start, &end);
    t = start;
    start = t & 0xFE;
    for (; start < end; start += 2)
        DrawWaveColumn(start >> 1);
}

/* $4F02: draw all 16 wave columns */
void DrawWaveColumns(void)
{
    UBYTE column;

    for (column = 0; column < 0x10; column++)
        DrawWaveColumn(column);
}

/* $4F18: screen idx 8: draws the waveform; edits wave_frames (bank3 $A000)
   directly */
void DrawWaveScreen(void)
{
    InitializeWaveSprites();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    FarCall(1, FAR_FN(DrawTopBar));
    DrawWaveColumns();
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

/* $4FA9: A+UP/DOWN (help: FLIP SAMPLE): each sample in the edit range
   becomes F - v, then redraw */
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

/* $4FFB: UP/DOWN (help: CHANGE SAMPLE): add d (1 or -1) to each sample in
   the edit range, then redraw */
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

/* $504E: A+LEFT/RIGHT on the wave screen (DRAW LINE): copy the cursor's
   sample to the column `d` away (+1 or -1) and move there */
void DrawWaveLine(UBYTE d)
{
    UBYTE n, v;

    n = (wWaveCursor + d) & 0x1F;
    v = ReadWaveSample(wWaveCursor);
    WriteWaveSample(n, v);
    DrawWaveCursorColumn();
    wWaveCursor = n;
    DrawWaveCursorColumn();
    SetWaveCursorSprite();
}

/* $507F: wave-screen input dispatch (wButtons) */
UBYTE WaveScreenInput(void)
{
    switch (wButtons) {
    case J_SELECT_TAP:
        if (wCursorMode == CURSOR_SELECTING) {
            wWaveCursor = wWaveSelectionStart;
            FarCall(5, RestoreSelectionState);
            DrawWaveScreen();
            ShowMessage("CANCEL SELECT");
        }
        return 1;
    case J_RIGHT:
        if (!wButtonsSame || wWaveCursor != 0x1F) {
            wWaveCursor++;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            SetWaveCursorSprite();
        }
        return 1;
    case J_LEFT:
        if (!wButtonsSame || wWaveCursor) {
            wWaveCursor--;
            wWaveCursor = (char)wWaveCursor & 0x1F;
            SetWaveCursorSprite();
        }
        return 1;
    case J_UP:
        ChangeWaveRange(1);
        return 1;
    case J_DOWN:
        ChangeWaveRange(0xFF);
        return 1;
    case (J_A | J_LEFT):
        if (wCursorMode == CURSOR_SELECTING)
            ReverseWaveRange();
        else
            DrawWaveLine(0xFF);
        return 1;
    case (J_A | J_RIGHT):
        if (wCursorMode == CURSOR_SELECTING)
            ReverseWaveRange();
        else
            DrawWaveLine(1);
        return 1;
    case (J_A | J_UP):
    case (J_A | J_DOWN):
        FlipWaveRange();
        return 1;
    case (J_B | J_RIGHT):
        wSynth++;
        wMessageStringPointer = 0;
        DrawWaveScreen();
        return 1;
    case (J_B | J_LEFT):
        wSynth--;
        wMessageStringPointer = 0;
        DrawWaveScreen();
        return 1;
    case (J_B | J_UP):
        wSynth += 0x10;
        wMessageStringPointer = 0;
        DrawWaveScreen();
        return 1;
    case (J_B | J_DOWN):
        wSynth -= 0x10;
        wMessageStringPointer = 0;
        DrawWaveScreen();
        return 1;
    case (J_SELECT | J_START):
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            return 0;
        if (wSynth < 0xF0)
            wSynthScreen = wSynth >> 4;
        FarCall(5, MoveMapCursorLeft);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_DOWN):
        if (wButtonsSame)
            return 0;
        FarCall(5, MoveMapCursorDown);
        FarCall(7, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_B):
        if (!wSelectHeld)
            return 0;
        wSelectHeld = 0;
        switch (wCursorMode) {
        case CURSOR_NORMAL:
        case CURSOR_COPIED:
            FarCall(5, SaveSelectionState);
            wWaveSelectionStart = wWaveCursor;
            ShowMessage("SELECT");
            break;
        case CURSOR_SELECTING:
            wWaveSelectionStart = 0;
            wWaveCursor = 0x1F;
            break;
        }
        SetWaveCursorSprite();
        return 1;
    case J_B:
        if (wCursorMode != CURSOR_SELECTING)
            return 0;
        if (!wAPressEnabled)
            return 0;
        CopyWaveSelection();
        SetWaveCursorSprite();
        return 1;
    case (J_SELECT | J_A):
        if (wCursorMode != CURSOR_COPIED)
            return 0;
        if (wClipboardType != SCREEN_WAVE)
            return 0;
        if (!wSelectHeld)
            return 0;
        wSelectHeld = 0;
        PasteWaveSelection();
        return 1;
    }
    return 0;
}

/* $5398: wave-screen COPY: selection -> clipboard (clipType 8), shows "COPY" */
void CopyWaveSelection(void)
{
    UBYTE v;
    UBYTE position;

    if (wClipboardType != SCREEN_WAVE)
        DebugTrap();
    ShowMessage(TxtCopy);
    wWaveSelectionEnd = wWaveCursor;
    wWaveCursor = wWaveSelectionStart;
    SortBytePair(&wWaveSelectionStart, &wWaveSelectionEnd);
    wWaveSelectionEnd++;
    for (position = 0; position < 0x20; position += 2) {
        v = ReadWaveSample(position) << 4;
        v |= ReadWaveSample(position + 1);
        WriteClipboardCell(0, position >> 1, v);
    }
    WriteClipboardCell(0, 0x10, wWaveSelectionStart);
    WriteClipboardCell(0, 0x11, wWaveSelectionEnd);
    wSelectionLeft = 0;
    wSelectionRight = 1;
    wSelectionTop = 0;
    wSelectionBottom = 0x12;
    wCursorMode = CURSOR_COPIED;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
}

/* $546C: wave screen: paste the clipboard samples at the cursor */
void PasteWaveSelection(void)
{
    UBYTE position, v;

    wWaveSelectionStart = ReadClipboardCell(0, 0x10);
    wWaveSelectionEnd = ReadClipboardCell(0, 0x11);
    position = wWaveSelectionStart;
    while (position != wWaveSelectionEnd) {
        v = ReadClipboardCell(0, position >> 1);
        if (!(position & 1))
            v >>= 4;
        else
            v &= 0x0F;
        WriteWaveSample(wWaveCursor, v);
        position++;
        wWaveCursor++;
        wWaveCursor &= 0x1F;
    }
    ShowMessage("PASTE");
    DrawWaveColumns();
    SetWaveCursorSprite();
}
