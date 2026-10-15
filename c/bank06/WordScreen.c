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
extern UBYTE wWordCol;                  /* $C938 */
extern UBYTE WordColX[];                  /* the word editor's two columns: x */
extern UBYTE WordColWidth[];                  /* width */
void EraseWordCursor(void);
void DrawWordCursor(void);
void ReadKitSampleName(UBYTE kit, UBYTE sample);   /* bank 0 */
void DrawAllChanStatus(void);               /* bank $1F */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wAPressEnabled;                /* $C409 */
void MapMoveLeft(void);                     /* $05:$4233 */
void MapMoveUp(void);                       /* $05:$426A */
void MapMoveDown(void);                     /* $05:$4287 */
void ToggleSongPlayback(void);                 /* $02:$78C4 */
void TogglePlayback(void);                    /* $02:$7C4C */

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
    p += (unsigned int)wWordCol;
    p += (unsigned int)(wWordRow << 1);
    return ReadSongByte(0, p);
}

void WriteWordCell(UBYTE v)
{
    UWORD p;

    p = (UWORD)SramWords + ((UWORD)wSpeechRow << 5);
    p += (unsigned int)wWordCol;
    p += (unsigned int)(wWordRow << 1);
    WriteSongByte(0, p, v);
}

void EraseWordCursor(void)
{
    ColorField(WordColX[wWordCol], wWordRow + 2, WordColWidth[wWordCol], wWordCol);
}

void DrawWordCursor(void)
{
    ColorField(WordColX[wWordCol], wWordRow + 2, WordColWidth[wWordCol], hGameboyType ? 3 : 2);
}

void DrawWordScreen(void)
{
    UWORD p;
    UBYTE i;
    UBYTE v;
    UBYTE s, m;

    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = (UBYTE *)0x9800;
    hBgDigitPal = 4;
    hBgPal = 1;
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
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    p = (UWORD)0xB890 + ((UWORD)wSpeechRow << 5);
    i = 0;
    while (i < 16) {
        GetTilemapAddr(2, i + 2);
        hBgDigitPal = 4;
        hBgPal = 1;
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
        hBgDigitPal = wShadowDigitPal;
        hBgPal = wShadowPal;
        DrawHexByte(ReadSongByte(0, p++));
        i++;
    }
    DrawWordCursor();
}

void WordCursorDown(void)
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

    hBgDigitPal = wSelectDigitPal;
    hBgPal = wSelectPal;
    y = wWordRow + 2;
    GetTilemapAddr(wWordCol * 4 + 2, y);
    switch (wButtons) {
    case 0x04:
        if (wWordRow) {
            EraseWordCursor();
            wWordRow--;
            DrawWordCursor();
        }
        return 1;
    case 0x08:
        WordCursorDown();
        return 1;
    case 0x02:
        EraseWordCursor();
        wWordCol = 0;
        DrawWordCursor();
        return 1;
    case 0x01:
        EraseWordCursor();
        wWordCol = 1;
        DrawWordCursor();
        return 1;
    case 0x30:
        if (!wAPressEnabled)
            return 0;
        {
        UBYTE t;
        v = wWordCol;
        if (wClipboardType != 0x0C && wCursorMode == 4)
            DebugTrap();
        wClipboardType = 0x0C;
        wCursorMode = 2;
        wWordCol = 0;
        wWordClipCell0[0] = ReadWordCell();
        wClipboardBuf[0] = ReadWordCell();
        wWordCol = 1;
        wWordClipPtr = wWordClipCell0 + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        wWordClipPtr = wClipboardBuf + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        WriteWordCell(0);
        wWordCol = 0;
        WriteWordCell(0);
        wWordCol = v;
        DrawWordScreen();
        return 1;
        }
    case 0x50:
        if (wClipboardType != 0x0C)
            return 0;
        v = wWordCol;
        wWordCol = 0;
        WriteWordCell(wClipboardBuf[0]);
        wWordCol = 1;
        WriteWordCell(wClipboardBuf[1]);
        wWordCol = v;
        DrawWordScreen();
        WordCursorDown();
        return 1;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        col = wWordCol;
        if (!wWordCol) {
            if (!ReadWordCell() && wWordClipCell0[0]) {
                WriteWordCell(wWordClipCell0[0]);
                wWordCol = 1;
                WriteWordCell(wWordClipCell1[0]);
                GetTilemapAddr(2, y);
                hBgDigitPal = 4;
                hBgPal = 1;
                v = wWordClipCell0[0];
                v--;
                wWordClipPtrB = v % 15 + 1;
                ReadKitSampleName(AllophoneKit(v), wWordClipPtrB);
                DrawString(wSampleNoteString);
                DrawStringTile20();
                hBgDigitPal = wShadowDigitPal;
                hBgPal = wShadowPal;
                DrawHexByte(wWordClipCell1[0]);
                wWordCol = col;
                DrawWordCursor();
                return 1;
            }
            wWordCol = col;
        } else if (!ReadWordCell()) {
            WriteWordCell(wWordClipCell1[0]);
            GetTilemapAddr(6, y);
            hBgDigitPal = wSelectDigitPal;
            hBgPal = wSelectPal;
            DrawHexByte(wWordClipCell1[0]);
            return 1;
        }
        return 0;
    case 0x11:
        v = ReadWordCell();
        if (wWordCol) {
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
        if (wWordCol) {
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
        if (wWordCol) {
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
        if (wWordCol) {
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
        FarCall(5, MapMoveLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveUp);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveDown);
        FarCall(4, FAR_FN(RedrawScreen));
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
    }
    return 0;
}
