/*
 * The word screen -- LSDj 9.4.0, ROM bank $06.
 *
 * Screen 12: a word's allophones and their lengths, the allophone kit and
 * names, the cursor and the input.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wWordClipCell0[];              /* $C935 */
extern UBYTE wWordClipCell1[];              /* $C936 */
extern UBYTE *wWordClipPtr;                 /* $C939 */
extern UBYTE wWordClipPtrB;                 /* $C939 as a byte (aliased by match_function.py) */
extern UBYTE wClipboardBuf[];               /* $C57A */
extern UBYTE wWordRow;                  /* $C937 */
extern UBYTE wWordColumn;                  /* $C938 */
extern UBYTE WordColumnX[];                  /* the word editor's two columns: x */
extern UBYTE WordColumnWidth[];                  /* width */
void EraseWordCursor(void);
void DrawWordCursor(void);
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */
void DrawAllChannelStatus(void);               /* bank $1F */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void MoveMapCursorLeft(void);                     /* $05:$4233 */
void MoveMapCursorUp(void);                       /* $05:$426A */
void MoveMapCursorDown(void);                     /* $05:$4287 */
void SoundToggleSongPlayback(void);                 /* $02:$78C4 */
void SoundTogglePlayback(void);                    /* $02:$7C4C */

UBYTE AllophoneKit(UBYTE v)
{
    UBYTE r;

    r = v / 15 + 0x19;
    if (r > 0x1A)
        r += 5;
    return r;
}

void DrawAllophoneName(UBYTE v)
{
    UBYTE t, m;

    wWordClipCell0[0] = v;
    t = v;
    t--;
    m = t % 15 + 1;
    ReadKitSampleName(AllophoneKit(t), m);
    DrawString(wSampleNoteString);
}

UBYTE ReadWordCell(void)
{
    UWORD p;

    p = (UWORD)SramWords + ((UWORD)wSpeechRow << 5);
    p += (unsigned int)wWordColumn;
    p += (unsigned int)(wWordRow << 1);
    return ReadSongByte(0, p);
}

void WriteWordCell(UBYTE v)
{
    UWORD p;

    p = (UWORD)SramWords + ((UWORD)wSpeechRow << 5);
    p += (unsigned int)wWordColumn;
    p += (unsigned int)(wWordRow << 1);
    WriteSongByte(0, p, v);
}

void EraseWordCursor(void)
{
    ColorField(WordColumnX[wWordColumn], wWordRow + 2, WordColumnWidth[wWordColumn], wWordColumn);
}

void DrawWordCursor(void)
{
    ColorField(WordColumnX[wWordColumn], wWordRow + 2, WordColumnWidth[wWordColumn], hGameboyType ? 3 : 2);
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
    p = (UWORD)0xBDD0 + (UWORD)(wSpeechRow * 3);
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
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    p = (UWORD)0xB890 + ((UWORD)wSpeechRow << 5);
    i = 0;
    while (i < 16) {
        SetVramCursor(2, i + 2);
        hBgDigitPalette = 4;
        hBgPalette = 1;
        s = ReadSongByte(0, p++);
        if (s) {
            s--;
            m = s % 15 + 1;
            ReadKitSampleName(AllophoneKit(s), m);
            DrawString(wSampleNoteString);
            DrawStringTile20();
        } else {
            DrawString("--- ");
        }
        hBgDigitPalette = wShadedDigitPalette;
        hBgPalette = wShadedPalette;
        DrawHexByte(ReadSongByte(0, p++));
        i++;
    }
    DrawWordCursor();
}

void MoveWordCursorDown(void)
{
    if (wWordRow == 0x0F)
        return;
    EraseWordCursor();
    wWordRow++;
    DrawWordCursor();
}

/* The word (speech) editor's input: cursor moves, the allophone/length
 * cut (0x30) and paste (0x50) through the 4-byte word clipboard, A
 * (0x10) to re-enter the clipboard's cells, +/- edits, page moves, the map
 * moves, play. The `.bRight` check is a 16-bit compare of wSpeechRow.
 *
 * lsdj-alloc(WordScreenInput): BUSYC=1-93
 * -- register c held from the first instruction through the cut's second
 * ReadWordCell result (the first two results take b, y and v never hold a
 * register) and released before the third (docs/toolchain.md, "The phantom
 * register"). */
UBYTE WordScreenInput(void)
{
    UBYTE v;
    UBYTE col;
    UBYTE y;

    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    y = wWordRow + 2;
    SetVramCursor(wWordColumn * 4 + 2, y);
    switch (wButtons) {
    case 0x04:
        if (wWordRow) {
            EraseWordCursor();
            wWordRow--;
            DrawWordCursor();
        }
        return 1;
    case 0x08:
        MoveWordCursorDown();
        return 1;
    case 0x02:
        EraseWordCursor();
        wWordColumn = 0;
        DrawWordCursor();
        return 1;
    case 0x01:
        EraseWordCursor();
        wWordColumn = 1;
        DrawWordCursor();
        return 1;
    case 0x30:
        if (!wAPressEnabled)
            return 0;
        {
        UBYTE t;
        v = wWordColumn;
        if (wClipboardType != 0x0C && wCursorMode == 4)
            DebugTrap();
        wClipboardType = 0x0C;
        wCursorMode = 2;
        wWordColumn = 0;
        wWordClipCell0[0] = ReadWordCell();
        wClipboardBuf[0] = ReadWordCell();
        wWordColumn = 1;
        wWordClipPtr = wWordClipCell0 + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        wWordClipPtr = wClipboardBuf + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        WriteWordCell(0);
        wWordColumn = 0;
        WriteWordCell(0);
        wWordColumn = v;
        DrawWordScreen();
        return 1;
        }
    case 0x50:
        if (wClipboardType != 0x0C)
            return 0;
        v = wWordColumn;
        wWordColumn = 0;
        WriteWordCell(wClipboardBuf[0]);
        wWordColumn = 1;
        WriteWordCell(wClipboardBuf[1]);
        wWordColumn = v;
        DrawWordScreen();
        MoveWordCursorDown();
        return 1;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        col = wWordColumn;
        if (!wWordColumn) {
            if (!ReadWordCell() && wWordClipCell0[0]) {
                WriteWordCell(wWordClipCell0[0]);
                wWordColumn = 1;
                WriteWordCell(wWordClipCell1[0]);
                SetVramCursor(2, y);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                v = wWordClipCell0[0];
                v--;
                wWordClipPtrB = v % 15 + 1;
                ReadKitSampleName(AllophoneKit(v), wWordClipPtrB);
                DrawString(wSampleNoteString);
                DrawStringTile20();
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
                DrawHexByte(wWordClipCell1[0]);
                wWordColumn = col;
                DrawWordCursor();
                return 1;
            }
            wWordColumn = col;
        } else if (!ReadWordCell()) {
            WriteWordCell(wWordClipCell1[0]);
            SetVramCursor(6, y);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            DrawHexByte(wWordClipCell1[0]);
            return 1;
        }
        return 0;
    case 0x11:
        v = ReadWordCell();
        if (wWordColumn) {
            if (v < 0xFF)
                v++;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else if (v != 0x3B) {
            ReadKitSampleName(AllophoneKit(v), v % 15 + 1);
            v++;
            wWordClipCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case 0x12:
        v = ReadWordCell();
        if (wWordColumn) {
            if (v)
                v--;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else if (v > 1) {
            v -= 2;
            ReadKitSampleName(AllophoneKit(v), v % 15 + 1);
            v++;
            wWordClipCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case 0x14:
        v = ReadWordCell() + 0x10;
        if (wWordColumn) {
            if (v < 0x10)
                return 0;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else {
            if (v > 0x3B)
                v = 0x3B;
            DrawAllophoneName(v);
        }
        WriteWordCell(v);
        return 1;
    case 0x18:
        v = ReadWordCell() - 0x10;
        if (wWordColumn) {
            if (v >= 0xF0)
                return 0;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else {
            if (v > 0x80)
                v = 1;
            DrawAllophoneName(v);
        }
        WriteWordCell(v);
        return 1;
    case 0x22:
        if (wSpeechRow) {
            wSpeechRow--;
            DrawWordScreen();
        }
        return 1;
    case 0x21:
        if ((UWORD)wSpeechRow < 13) {
            wSpeechRow++;
            DrawWordScreen();
        }
        return 1;
    case 0x28:
        wSpeechRow -= 12;
        if (wSpeechRow > 0x80)
            wSpeechRow = 0;
        DrawWordScreen();
        return 1;
    case 0x24:
        wSpeechRow += 12;
        if (wSpeechRow >= 14)
            wSpeechRow = 13;
        DrawWordScreen();
        return 1;
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorDown);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    }
    return 0;
}
