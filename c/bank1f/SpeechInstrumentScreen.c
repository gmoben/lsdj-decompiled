/*
 * The speech instrument screen -- LSDj 9.4.0, ROM bank $1F.
 *
 * The word list, its cursor and its input.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void DrawAllChannelStatus(void);
extern UBYTE wNameEditBuffer[];                /* $C431, [5] */
extern UBYTE wNameEditIsInstrument;              /* $C430 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wButtons;                      /* $C575 */
extern UBYTE wClipboardType;                /* $C411 */
UBYTE SpeechCursorY(void);
void OpenNameEditor(void);                  /* bank $1B */
void MoveMapCursorLeft(void);                     /* bank 5 */
void MoveMapCursorRight(void);                    /* bank 5 */
void MoveMapCursorUp(void);                       /* bank 5 */
void MoveMapCursorDown(void);                     /* bank 5 */
void PreviousInstrumentPage(void);             /* bank 3 */
void SoundToggleSongPlayback(void);                 /* bank 2 */
void SoundTogglePlayback(void);                    /* bank 2 */
void DrawInstrumentScreenFrame(void);                /* $03:$45A7 */

void DrawWordRow(UBYTE n)
{
    UBYTE *p;
    UBYTE letter, c, w;

    w = n;
    w--;
    if (w < 14) {
        p = SramWordNames + (unsigned int)w * 3;
        for (letter = 0; letter < 3; letter++) {
            c = ReadSongByte(0, p++);
            if (!c)
                c = ' ';
            DrawStringIndexed(c);
        }
    } else
        DrawString("---");
}

void RedrawScreenFar1F(void)
{
    FarCall(4, FAR_FN(RedrawScreen));
}

UBYTE SpeechCursorY(void)
{
    return (UWORD)wSpeechRow + 4;
}

void EraseSpeechCursor(void)
{
    ColorField(0, SpeechCursorY(), 3, !(wSpeechRow & 1));
}

void DrawSpeechCursor(void)
{
    ColorField(0, SpeechCursorY(), 3, hGameboyType ? 3 : 2);
}

void DrawSpeechInstrumentScreen(void)
{
    UBYTE *p;
    UBYTE row, letter, c;

    wCurrentScreen = SCREEN_SPEECH;
    FarCall(1, FAR_FN(DrawScreenPaletteMap));
    FarCall(0x1F, FAR_FN(DrawAllChannelStatus));
    wCurrentScreen = SCREEN_INSTRUMENT;
    FarCall(3, FAR_FN(DrawInstrumentScreenFrame));
    hBgDigitPalette = 4;
    hBgPalette = 1;
    wVramCursor = BG_MAP(0, 2);
    DrawString("TYPE   SPEECH");
    p = SramWordNames;
    row = 4;
    while (row < 4 + 14) {
        SetVramCursor(0, row);
        if (row & 1) {
            hBgDigitPalette = wShadedDigitPalette;
            hBgPalette = wShadedPalette;
        } else {
            hBgDigitPalette = 4;
            hBgPalette = 1;
        }
        letter = 0;
        while (letter < 3) {
            c = ReadSongByte(0, p);
            if (!c)
                break;
            DrawStringIndexed(c);
            p = p + 1;
            letter = letter + 1;
        }
        while (letter < 3) {
            DrawStringIndexed(' ');
            p = p + 1;
            letter = letter + 1;
        }
        row++;
    }
    DrawSpeechCursor();
}

/*
 * The word (speech) screen's input: A opens the name editor on the word
 * under the cursor, up/down move the cursor, select+direction leaves through
 * the screen map, A+B / select+B cut or copy the word (its 16 allophone
 * pairs and 3-letter name; a cut leaves the name "W-n"), select+A pastes it,
 * B+left / B+down step the instrument back. 1 when the key was taken.
 */
UBYTE SpeechInstrumentInput(void)
{
    UWORD p;
    UBYTE cell;
    UBYTE v;

    switch (wButtons) {
    case J_A:
        if (!wAPressEnabled)
            return 0;
        p = (UWORD)SRAM_WORD_NAMES + wSpeechRow * 3;
        wNameEditBuffer[0] = ReadSongByte(0, p++);
        wNameEditBuffer[1] = ReadSongByte(0, p++);
        wNameEditBuffer[2] = ReadSongByte(0, p);
        wNameEditBuffer[3] = 0;
        wNameEditBuffer[4] = 0;
        wNameEditIsInstrument = 0;
        FarCall(0x1B, FAR_FN(OpenNameEditor));
        return 1;
    case J_UP:
        if (!wSpeechRow)
            return 0;
        EraseSpeechCursor();
        wSpeechRow--;
        DrawSpeechCursor();
        return 1;
    case J_DOWN:
        if (wSpeechRow == 0x0D)
            return 0;
        EraseSpeechCursor();
        wSpeechRow++;
        DrawSpeechCursor();
        return 1;
    case (J_SELECT | J_DOWN):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorDown);
        RedrawScreenFar1F();
        return 1;
    case (J_SELECT | J_UP):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorUp);
        RedrawScreenFar1F();
        return 1;
    case (J_SELECT | J_RIGHT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorRight);
        RedrawScreenFar1F();
        return 1;
    case (J_SELECT | J_LEFT):
        if (wButtonsSame)
            break;
        FarCall(5, MoveMapCursorLeft);
        RedrawScreenFar1F();
        return 1;
    case (J_B | J_A):
    case (J_SELECT | J_B):
        if (!wAPressEnabled)
            return 0;
        if (wButtons == (J_SELECT | J_B))
            ShowMessage("COPIED WORD");
        else
            ShowMessage("CUT WORD");
        wClipboardType = SCREEN_SPEECH;
        wCursorMode = 2;
        p = ((UWORD)wSpeechRow << 5) + (UWORD)SRAM_WORDS;
        for (cell = 0; cell < 0x10; cell++) {
            WriteClipboardCell(0, cell, ReadSongByte(0, p));
            if (wButtons == (J_B | J_A))
                WriteSongByte(0, p, 0);
            p++;
            WriteClipboardCell(1, cell, ReadSongByte(0, p));
            if (wButtons == (J_B | J_A))
                WriteSongByte(0, p, 0);
            p++;
        }
        p = (UWORD)wSpeechRow * 3 + (UWORD)SRAM_WORD_NAMES;
        for (cell = 0; cell < 3; cell++) {
            WriteClipboardCell(2, cell, ReadSongByte(0, p));
            p++;
        }
        if (wButtons == (J_B | J_A)) {
            SetVramCursor(0, SpeechCursorY());
            hBgDigitPalette = wHighlightDigitPalette;
            hBgPalette = wHighlightPalette;
            p -= 3;
            WriteSongByte(0, p++, 'W');
            DrawStringIndexed('W');
            WriteSongByte(0, p++, '-');
            DrawStringIndexed('-');
            cell = wSpeechRow;
            cell += wSpeechRow < 10 ? '0' : '7';
            WriteSongByte(0, p, cell);
            DrawStringIndexed((char)cell);
        }
        return 1;
    case (J_SELECT | J_A):
        if (!wAPressEnabled)
            return 0;
        if (wClipboardType != SCREEN_SPEECH)
            return 0;
        ShowMessage("PASTED WORD");
        p = ((UWORD)wSpeechRow << 5) + (UWORD)SRAM_WORDS;
        for (cell = 0; cell < 0x10; cell++) {
            if (p >= (UWORD)0xBDD0)
                DebugTrap();
            WriteSongByte(0, p++, ReadClipboardCell(0, cell));
            if (p >= (UWORD)0xBDD0)
                DebugTrap();
            WriteSongByte(0, p++, ReadClipboardCell(1, cell));
        }
        p = (UWORD)wSpeechRow * 3 + (UWORD)SRAM_WORD_NAMES;
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        SetVramCursor(0, SpeechCursorY());
        for (cell = 0; cell < 3; cell++) {
            if (p >= (UWORD)0xBE78)
                DebugTrap();
            v = ReadClipboardCell(2, cell);
            WriteSongByte(0, p++, v);
            if (v)
                DrawStringIndexed((char)v);
            else
                DrawStringTile20();
        }
        return 1;
    case (J_B | J_LEFT):
        FarCall(3, PreviousUsedInstrument);
        return 1;
    case (J_B | J_DOWN):
        FarCall(3, PreviousInstrumentPage);
        return 1;
    case (J_SELECT | J_START):
        if (!wButtonsSame)
            FarCall(2, FAR_FN(SoundToggleSongPlayback));
        return 1;
    case J_START:
        if (!wButtonsSame && !wBHeld)
            FarCall(2, FAR_FN(SoundTogglePlayback));
        return 1;
    }
    return 0;
}
