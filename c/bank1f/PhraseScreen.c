/*
 * The phrase screen's bank-$1F half -- LSDj 9.4.0, ROM bank $1F.
 *
 * Its update handler, the channel moves, cut/copy, and the cursor and
 * selection highlights.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wLiveStartColumn;                 /* $C571 */
extern UBYTE wChannelPhrase[];                 /* $C168, [4] */
extern UBYTE wChannelPhraseRow[];              /* $C16C, [4] */
extern UBYTE wPhraseColumn;                    /* $C415 */
extern UBYTE wSelectionTop;                       /* $C40E */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wSelectionRight;                     /* $C40F */
extern UBYTE wSelectionBottom;                    /* $C410 */
void SendLinkClipboard(void);                   /* $1C:$71CD */
void ReadPhraseNote(void);                  /* $04:$4968, (row) */
void WritePhraseNote(void);                 /* $04:$418F, (row, v) */
void ReadPhraseCommand(void);                    /* $04:$4AEA, (phrase, row) */
void WritePhraseCommand(void);                   /* $04:$49C5, (phrase, row, cmd) */
void ReadPhraseCommandVal(void);                 /* $04:$4A8F, (phrase, row) */
void WritePhraseCommandVal(void);                /* $04:$4A30, (phrase, row, v) */
UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row);  /* $04:$4918 */
extern UBYTE wDrawTextScratch;              /* $C9CD: ColorField's width, stored before the call */
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE PhraseCellX[];                 /* $1F:$64E1, [5]: the six phrase-cell layout rows */
extern UBYTE PhraseCellXAlt[];              /* $64E6 */
extern UBYTE PhraseCellWidthAlt[];          /* $64EB */
extern UBYTE PhraseCellWidth[];             /* $64F0 */
extern UBYTE PhraseCellPaletteAlt[];            /* $64F5 */
extern UBYTE PhraseCellPalette[];               /* $64FA */
void UnhighlightPhraseCell(UBYTE col, UBYTE row);
UBYTE CanEnterSongColumn(UBYTE col);
void ErasePhraseCursor(void);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instr);
extern UBYTE wLiveMode;                     /* $C528 */
extern UBYTE wChain;                        /* $C418 */
extern UBYTE wChainRow;                     /* $C41A */
extern UBYTE wChannelActive[];                 /* $C0E0, [4] */
extern UBYTE wChannelChain[];                  /* $C170, [4] */
extern UBYTE wChannelChainRow[];               /* $C17C, [4] */
extern UBYTE wChannelSongRow[];                /* $C200, [4] */
void DrawScreenColumnLabels(void);                   /* $1E:$4000 */

void PhraseScreenUpdate(void)
{
    if (wLiveStartColumn != 0xFF)
        return;
    if (wPlayMode && wPhrase == wChannelPhrase[wSongColumn])
        SetSpriteY(0, wChannelPhraseRow[wSongColumn]);
    else
        ClearSpriteY(0);
}

UBYTE CanEnterSongColumn(UBYTE col)
{
    UBYTE chain, row, phrase;
    void (*fn)(void);

    if (col >= 4)
        DebugTrap();
    wMessageStrPtr = 0;
    if (wLiveMode && wChannelActive[col]) {
        chain = wChannelChain[col];
        if (chain & 0x80)
            return 0;
        phrase = wChannelPhrase[col];
        if (phrase & 0x80)
            return 0;
        row = wChannelChainRow[col];
        if (row & 0xF0)
            return 0;
        wSongRow = wChannelSongRow[col];
    } else {
        chain = ReadSeqByte(col, wSongRow);
        if (chain & 0x80)
            return 0;
        row = wChainRow;
        fn = FAR_FN(SoundGetChainPhrase);
        phrase = FarCallArg2(2, (UWORD)fn, chain, wChainRow);
        while (phrase == 0xFF && row)
            phrase = FarCallArg2(2, (UWORD)fn, chain, --row);
        if (phrase == 0xFF)
            return 0;
    }
    ErasePhraseCursor();
    wSongColumn = col;
    wChain = chain;
    wChainRow = row;
    wPhrase = phrase;
    FarCall(4, FAR_FN(DrawPhraseScreen));
    FarCall(0x1E, FAR_FN(DrawScreenColumnLabels));
    return 1;
}

UBYTE HasEnterableColumnRight(void)
{
    UBYTE c;

    for (c = wSongColumn + 1; c < 4; c++)
        if (CanEnterSongColumn(c))
            break;
}

UBYTE HasEnterableColumnLeft(void)
{
    UBYTE c;

    for (c = wSongColumn - 1; c < 4; c--)
        if (CanEnterSongColumn(c))
            break;
}

/* The phrase screen's cut/copy: every selected cell into the clipboard
   (the note byte split over columns 0 and 1 unless the row flag is $FF),
   cleared on a cut. A selection of the note column alone on channel 2
   takes the instrument column too, copied but never cut (cutInstr). The
   note accessors take only the row: FarCallArg2/3 get one byte fewer. */
void CutCopyPhraseSelection(UBYTE cut)
{
    UBYTE x, y, t;
    UBYTE cutInstr = cut;

    if (wClipboardType != 1)
        return;
    ShowMessage(cutInstr ? "CUT" : "COPY");
    wCursorMode = 2;
    wSelectionRight = wPhraseColumn;
    wSelectionBottom = wPhraseRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    if (wSelectionLeft == 4)
        wSelectionLeft = 3;
    if (wSelectionRight == 3)
        wSelectionRight = 4;
    if (wSelectionRight == 1 && wSongColumn == 2) {
        cutInstr = 0;
        wSelectionRight = 2;
    }
    wSelectionRight++;
    wSelectionBottom++;
    for (y = wSelectionTop; y < wSelectionBottom; y++) {
        for (x = wSelectionLeft; x < wSelectionRight; x = x + 1) {
            wPhraseColumn = x;
            wPhraseRow = y;
            ErasePhraseCursor();
            switch (x) {
            case 0:
                if (wPhraseRowFlags[y] != 0xFF) {
                    t = FarCallArg2P(4, FAR_FN(ReadPhraseNote), y);
                    WriteClipboardCell(0, y, t >> 4);
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, t & 0x0F);
                }
                if (wSelectionRight == 1 && wSongColumn == 2) {
                    WriteClipboardCell(1, y, 0xFF);
                    cutInstr = 0;
                    goto instr;
                }
                break;
            case 1:
                if (wPhraseRowFlags[y] == 0xFF) {
                    WriteClipboardCell(0, y, 0);
                    WriteClipboardCell(1, y, FarCallArg2P(4, FAR_FN(ReadPhraseNote), y));
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, 0);
                } else {
                    t = FarCallArg2P(4, FAR_FN(ReadPhraseNote), y);
                    WriteClipboardCell(1, y, t & 0x0F);
                    if (cut)
                        FarCallArg3P(4, FAR_FN(WritePhraseNote), y, t & 0xF0);
                }
                break;
            case 2:
            instr:
                WriteClipboardCell(2, y, FarCallArg2P(4, FAR_FN(ReadPhraseInstrument), wPhrase, y));
                if (cutInstr)
                    FarCallArg3P(4, FAR_FN(WritePhraseInstrument), wPhrase, y, 0xFF);
                break;
            case 3:
                WriteClipboardCell(3, y, FarCallArg2P(4, FAR_FN(ReadPhraseCommand), wPhrase, y));
                if (cut)
                    FarCallArg3P(4, FAR_FN(WritePhraseCommand), wPhrase, y, 0);
                break;
            case 4:
                WriteClipboardCell(4, y, FarCallArg2P(4, FAR_FN(ReadPhraseCommandVal), wPhrase, y));
                if (cut)
                    FarCallArg3P(4, FAR_FN(WritePhraseCommandVal), wPhrase, y, 0);
                break;
            }
        }
    }
    if (wSelectionRight == 1)
        wSelectionRight = 3;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    wPhraseColumn = wSelectionLeft;
    wPhraseRow = wSelectionTop;
    FarCall(4, FAR_FN(DrawPhraseScreen));
}

void HighlightPhraseCell(UBYTE col, UBYTE row)
{
    char x;

    x = PhraseCellX[col];
    if (row & 0xF0)
        DebugTrap();
    if (wPhraseDrawMode)
        x += 3;
    if (x < 0)
        return;
    if (wPhraseRowFlags[row] == 0xFE || wPhraseRowFlags[row] == 0xFF)
        ColorField(x, row + 2, (wDrawTextScratch = PhraseCellWidth[col], wDrawTextScratch), hGameboyType ? 3 : 2);
    else
        ColorField(PhraseCellXAlt[col], row + 2, (wDrawTextScratch = PhraseCellWidthAlt[col], wDrawTextScratch), hGameboyType ? 3 : 2);
}

void DrawPhraseCursor(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseColumn;
    HighlightPhraseCell(col, row);
}

void UnhighlightPhraseCell(UBYTE col, UBYTE row)
{
    char x;
    UBYTE y;
    UBYTE pal;
    UBYTE w;

    x = PhraseCellX[col];
    y = row + 2;
    if (wPhraseDrawMode)
        x += 3;
    if (x < 0)
        return;
    if (wPhraseRow & 0xF0)
        DebugTrap();
    if (wPhraseRowFlags[row] == 0xFE || wPhraseRowFlags[row] == 0xFF) {
        w = PhraseCellWidth[col];
        pal = PhraseCellPalette[col];
    } else {
        x = PhraseCellXAlt[col];
        w = PhraseCellWidthAlt[col];
        pal = PhraseCellPaletteAlt[col];
    }
    ColorField(x, y, w, pal);
}

void DrawPhraseSelection(void)
{
    UBYTE x, y, left, top, right, bottom;
    UBYTE col;

    if (wClipboardType != 1)
        return;
    col = wPhraseColumn;               /* the ROM reads the column into c for the test only */
    if (wSelectionLeft < col) {
        left = wSelectionLeft;
        right = wPhraseColumn + 1;
    } else {
        right = wSelectionLeft + 1;
        left = wPhraseColumn;
    }
    if (wSelectionTop < wPhraseRow) {
        top = wSelectionTop;
        bottom = wPhraseRow + 1;
    } else {
        bottom = wSelectionTop + 1;
        top = wPhraseRow;
    }
    /* the one-cell ring around the rectangle back to its column palettes... */
    for (x = left ? left - 1 : 0; x < (right == 5 ? 5 : right + 1); x++) {
        for (y = top ? top - 1 : 0; y < (bottom == 0x10 ? 0x10 : bottom + 1); y = y + 1) {
            if (x < left || x >= right || y < top || y >= bottom)
                UnhighlightPhraseCell(x, y);
        }
    }
    /* ...and the rectangle itself drawn as the cursor cell */
    for (x = left; x < right; x++)
        for (y = top; y < bottom; y = y + 1)
            HighlightPhraseCell(x, y);
}

void ErasePhraseCursor(void)
{
    UBYTE row, col;

    row = wPhraseRow;
    col = wPhraseColumn;
    UnhighlightPhraseCell(col, row);
}
