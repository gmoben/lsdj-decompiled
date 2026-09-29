/*
 * The phrase screen -- LSDj 9.4.0, ROM bank $04.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wPhrase;                       /* $C414, the phrase being edited */
extern UBYTE wPhraseRow;                    /* $C416 */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE FxMnemonics[];                 /* $00:$0824, the command letters */

void FarCallArg2V(UBYTE bank, void (*fn)(void), UBYTE a, UBYTE b);   /* bank 0 */
void DrawChainCell(void);                   /* bank 6 */
void DrawPhraseCursor(void);                /* bank $1F */
void PastePhraseSelection(void);            /* bank $1F */
UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row);
UBYTE FindCursorInstr(void);

void DrawChainCellFar(UBYTE x)
{
    FarCallArg2V(6, DrawChainCell, wSongCol, x);
}

void DrawPhraseCursorFar(void)
{
    FarCall(0x1F, FAR_FN(DrawPhraseCursor));
}

void DrawPhraseCmdLetter(UBYTE cmd)
{
    UBYTE c, ok;

    ok = cmd <= 0x13;
    if (ok)
        c = FxMnemonics[cmd];
    else
        c = 0x3F;
    DrawStringIndexed(c);
}

UBYTE PhraseModeTile(void)
{
    return wSongCol == 3 ? 0x78 : 0x6C;
}

UBYTE ReadCursorInstr(void)
{
    UBYTE row;

    row = wPhraseRow;
    return ReadPhraseInstr(wPhrase, row);
}

UBYTE FindCursorInstr(void)
{
    UBYTE row, v;

    for (row = wPhraseRow; row != 0xFF; row--) {
        v = ReadPhraseInstr(wPhrase, row);
        if (v <= 0x40)
            return v;
    }
    return 0xFF;
}

UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    return ReadSongByte(3, p + (unsigned int)row);
}

void DrawEmptyNote(void)
{
    DrawString("---");
}

void PastePhraseSelectionFar(void)
{
    FarCall(0x1F, FAR_FN(PastePhraseSelection));
}

UBYTE PhraseCheckColFD(UBYTE i)
{
    UBYTE v;

    v = wPhraseRowFlags[i];
    return v > 0xFD;
}

void DrawNoteCell(void)
{
    DrawString(wSampleNoteString[0] ? wSampleNoteString : "OFF");
}

UBYTE CursorInstrIsKit(void)
{
    UBYTE v, r;

    v = FindCursorInstr();
    if (v < 0x40)
        r = ReadInstrByte(v, 0) == 2;
    else
        r = 0;
    return r;
}
