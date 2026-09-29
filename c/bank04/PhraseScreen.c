/*
 * The phrase screen -- LSDj 9.4.0, ROM bank $04.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern char wPhraseCol;                     /* $C415, a plain char */
extern UBYTE wPhraseColLast[];              /* $C907, [4] */
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wInstrIsKit;                   /* $C90B */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE PhraseColX0[];                 /* $04:$429E */
extern UBYTE PhraseColX1[];                 /* $04:$42A3 */

void DrawChainCell(void);                   /* bank 6 */
void DrawPhraseCursor(void);                /* bank $1F */
void PastePhraseSelection(void);            /* bank $1F */
void CopyInstrName(void);                   /* bank 1 */
void WriteInstrName(void);                  /* bank 1 */
UBYTE ReadPhraseInstr(UBYTE phrase, UBYTE row);
extern UBYTE wAPressEnabled;                /* $C409 */
UBYTE PhraseCursorOnEditCell(void);
void CloneInstrAtCursor(void);
UBYTE CloneTableAtCursor(void);
UBYTE PhraseReadFx(UBYTE phrase, UBYTE row);
UBYTE GetScreenScratchCell2(UBYTE a, UBYTE b);   /* bank 0 */
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

void PhraseResolveCursorNote(UBYTE v)
{
    UBYTE instr;

    wPhraseColLast[wSongCol] = v;
    if (wSongCol == 3 && wCursorMode != 4) {
        instr = FindCursorInstr();
        if (instr < 0x40 && ReadInstrByte(instr, 0) == 3)
            WriteInstrByte(instr, 4, v);
    }
}

void WritePhraseNote(UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == 0xFF)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    WriteSongByte(0, (UBYTE *)p, v);
}

void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (instr > 0x40 && instr != 0xFF)
        DebugTrap();
    p = SramPhraseInstruments + ((UWORD)phrase << 4);
    WriteSongByte(3, p + row, instr);
}

void PhraseDrawCursorGlyph(UBYTE row)
{
    UBYTE y;
    UBYTE *tbl;

    y = row + 2;
    tbl = wPhraseDrawMode ? PhraseColX1 : PhraseColX0;
    GetTilemapAddr(tbl[wPhraseCol], y);
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

UBYTE PhraseReadNote(UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseNotes + (unsigned int)row);
    if (row >= 0x10)
        DebugTrap();
    if (wPhrase == 0xFF)
        DebugTrap();
    p += (UWORD)wPhrase << 4;
    return ReadSongByte(0, (UBYTE *)p);
}

void WritePhraseFx(UBYTE phrase, UBYTE row, UBYTE cmd)
{
    UWORD p;

    p = (UWORD)(SramPhraseFx + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    if (cmd > 0x13)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, cmd);
}

void WritePhraseFxVal(UBYTE phrase, UBYTE row, UBYTE v)
{
    UWORD p;

    p = (UWORD)(SramPhraseFxVal + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    WriteSongByte(2, (UBYTE *)p, v);
}

UBYTE PhraseReadFxVal(UBYTE phrase, UBYTE row)
{
    UWORD p;

    p = (UWORD)(SramPhraseFxVal + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    return ReadSongByte(2, (UBYTE *)p);
}

UBYTE PhraseReadFx(UBYTE phrase, UBYTE row)
{
    UWORD p;
    UBYTE v, ok;

    p = (UWORD)(SramPhraseFx + (unsigned int)row);
    if (phrase == 0xFF)
        DebugTrap();
    if (row >= 0x10)
        DebugTrap();
    p += (UWORD)phrase << 4;
    v = ReadSongByte(2, (UBYTE *)p);
    ok = v <= 0x13;
    if (!ok)
        v = 0;
    return v;
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

UBYTE PhraseCheckEditContext(UBYTE x)
{
    if (wSongCol != 2)
        return 0;
    if (wPhraseCol > 1)
        return 0;
    if (wSelLeft > 1)
        return 0;
    if (!CursorInstrIsKit())
        return 0;
    if (wPhraseCol == 0)
        return wSelLeft == 1;
    return GetScreenScratchCell2(1, x) == 0xFF;
}

void CloneInstrument(UBYTE src, UBYTE dst)
{
    UBYTE i;

    for (i = 0; i < 0x10; i++)
        WriteInstrByte(dst, i, ReadInstrByte(src, i));
    FarCallArg(1, (UWORD)CopyInstrName, src);
    FarCallArg(1, (UWORD)WriteInstrName, dst);
}

UBYTE PhraseCloneTableCmd(void)
{
    UBYTE row;

    if (wAPressEnabled || PhraseCursorOnEditCell())
        return 0;
    if (wPhraseCol == 2) {
        CloneInstrAtCursor();
        return 1;
    }
    if (wPhraseCol == 4) {
        row = wPhraseRow;
        if (PhraseReadFx(wPhrase, row) == 1) {
            if (CloneTableAtCursor())
                ShowMessage("CLONED TABLE");
            return 1;
        }
    }
    return 0;
}

UBYTE PrelistenEditedCell(void)
{
    UBYTE i;

    if (wSongCol != 2)
        return 0;
    i = ReadCursorInstr();
    if (i > 0x40)
        return wInstrIsKit;
    if (i == 0x40)
        return 0;
    return ReadInstrByte(i, 0) == 2;
}
