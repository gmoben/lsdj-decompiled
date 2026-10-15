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

extern UBYTE wWordClipboardCell0[];              /* $C935 */
extern UBYTE wWordClipboardCell1[];              /* $C936 */
extern UBYTE *wWordClipboardPointer;                 /* $C939 */
extern UBYTE wWordClipboardPointerB;                 /* $C939 as a byte (aliased by match_function.py) */
extern UBYTE wClipboardBuffer[];               /* $C57A */
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

    wWordClipboardCell0[0] = v;
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
    UBYTE row;
    UBYTE v;
    UBYTE s, m;

    FarCall(1, FAR_FN(DrawTopBar));
    wVramCursor = (UBYTE *)0x9800;
    hBgDigitPalette = 4;
    hBgPalette = 1;
    DrawString("WORD ");
    p = (UWORD)0xBDD0 + (UWORD)(wSpeechRow * 3);
    row = 0;
    while (row < 3) {
        v = ReadSongByte(0, p);
        if (!v)
            break;
        DrawStringIndexed(v);
        p++;
        row++;
    }
    DrawTempoField();
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    p = (UWORD)0xB890 + ((UWORD)wSpeechRow << 5);
    row = 0;
    while (row < 16) {
        SetVramCursor(2, row + 2);
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
        row++;
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
    UBYTE column;
    UBYTE y;

    hBgDigitPalette = wHighlightDigitPalette;
    hBgPalette = wHighlightPalette;
    y = wWordRow + 2;
    SetVramCursor(wWordColumn * 4 + 2, y);
    switch (wButtons) {
    case J_UP:
        if (wWordRow) {
            EraseWordCursor();
            wWordRow--;
            DrawWordCursor();
        }
        return 1;
    case J_DOWN:
        MoveWordCursorDown();
        return 1;
    case J_LEFT:
        EraseWordCursor();
        wWordColumn = 0;
        DrawWordCursor();
        return 1;
    case J_RIGHT:
        EraseWordCursor();
        wWordColumn = 1;
        DrawWordCursor();
        return 1;
    case (J_B | J_A):
        if (!wAPressEnabled)
            return 0;
        {
        UBYTE t;
        v = wWordColumn;
        if (wClipboardType != SCREEN_WORD && wCursorMode == 4)
            DebugTrap();
        wClipboardType = SCREEN_WORD;
        wCursorMode = 2;
        wWordColumn = 0;
        wWordClipboardCell0[0] = ReadWordCell();
        wClipboardBuffer[0] = ReadWordCell();
        wWordColumn = 1;
        wWordClipboardPointer = wWordClipboardCell0 + 1;
        t = ReadWordCell();
        *wWordClipboardPointer = t;
        wWordClipboardPointer = wClipboardBuffer + 1;
        t = ReadWordCell();
        *wWordClipboardPointer = t;
        WriteWordCell(0);
        wWordColumn = 0;
        WriteWordCell(0);
        wWordColumn = v;
        DrawWordScreen();
        return 1;
        }
    case (J_SELECT | J_A):
        if (wClipboardType != SCREEN_WORD)
            return 0;
        v = wWordColumn;
        wWordColumn = 0;
        WriteWordCell(wClipboardBuffer[0]);
        wWordColumn = 1;
        WriteWordCell(wClipboardBuffer[1]);
        wWordColumn = v;
        DrawWordScreen();
        MoveWordCursorDown();
        return 1;
    case J_A:
        if (!wAPressEnabled)
            return 0;
        column = wWordColumn;
        if (!wWordColumn) {
            if (!ReadWordCell() && wWordClipboardCell0[0]) {
                WriteWordCell(wWordClipboardCell0[0]);
                wWordColumn = 1;
                WriteWordCell(wWordClipboardCell1[0]);
                SetVramCursor(2, y);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                v = wWordClipboardCell0[0];
                v--;
                wWordClipboardPointerB = v % 15 + 1;
                ReadKitSampleName(AllophoneKit(v), wWordClipboardPointerB);
                DrawString(wSampleNoteString);
                DrawStringTile20();
                hBgDigitPalette = wShadedDigitPalette;
                hBgPalette = wShadedPalette;
                DrawHexByte(wWordClipboardCell1[0]);
                wWordColumn = column;
                DrawWordCursor();
                return 1;
            }
            wWordColumn = column;
        } else if (!ReadWordCell()) {
            WriteWordCell(wWordClipboardCell1[0]);
            SetVramCursor(6, y);
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            DrawHexByte(wWordClipboardCell1[0]);
            return 1;
        }
        return 0;
    case (J_A | J_RIGHT):
        v = ReadWordCell();
        if (wWordColumn) {
            if (v < 0xFF)
                v++;
            DrawHexByte(v);
            wWordClipboardCell1[0] = v;
        } else if (v != 0x3B) {
            ReadKitSampleName(AllophoneKit(v), v % 15 + 1);
            v++;
            wWordClipboardCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case (J_A | J_LEFT):
        v = ReadWordCell();
        if (wWordColumn) {
            if (v)
                v--;
            DrawHexByte(v);
            wWordClipboardCell1[0] = v;
        } else if (v > 1) {
            v -= 2;
            ReadKitSampleName(AllophoneKit(v), v % 15 + 1);
            v++;
            wWordClipboardCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case (J_A | J_UP):
        v = ReadWordCell() + 0x10;
        if (wWordColumn) {
            if (v < 0x10)
                return 0;
            DrawHexByte(v);
            wWordClipboardCell1[0] = v;
        } else {
            if (v > 0x3B)
                v = 0x3B;
            DrawAllophoneName(v);
        }
        WriteWordCell(v);
        return 1;
    case (J_A | J_DOWN):
        v = ReadWordCell() - 0x10;
        if (wWordColumn) {
            if (v >= 0xF0)
                return 0;
            DrawHexByte(v);
            wWordClipboardCell1[0] = v;
        } else {
            if (v > 0x80)
                v = 1;
            DrawAllophoneName(v);
        }
        WriteWordCell(v);
        return 1;
    case (J_B | J_LEFT):
        if (wSpeechRow) {
            wSpeechRow--;
            DrawWordScreen();
        }
        return 1;
    case (J_B | J_RIGHT):
        if ((UWORD)wSpeechRow < 13) {
            wSpeechRow++;
            DrawWordScreen();
        }
        return 1;
    case (J_B | J_DOWN):
        wSpeechRow -= 12;
        if (wSpeechRow > 0x80)
            wSpeechRow = 0;
        DrawWordScreen();
        return 1;
    case (J_B | J_UP):
        wSpeechRow += 12;
        if (wSpeechRow >= 14)
            wSpeechRow = 13;
        DrawWordScreen();
        return 1;
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_UP):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case (J_SELECT | J_DOWN):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorDown);
        FarCall(4, FAR_FN(RedrawScreen));
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
    }
    return 0;
}
