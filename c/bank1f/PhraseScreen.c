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
void ReadPhraseCommandValue(void);                 /* $04:$4A8F, (phrase, row) */
void WritePhraseCommandValue(void);                /* $04:$4A30, (phrase, row, v) */
UBYTE ReadPhraseInstrument(UBYTE phrase, UBYTE row);  /* $04:$4918 */
extern UBYTE wDrawTextScratch;              /* $C9CD: ColorField's width, stored before the call */
extern UBYTE wPhraseDrawMode;               /* $C90C */
extern UBYTE wPhraseRowFlags[];             /* $C90D, [16] */
extern UBYTE PhraseCellX[];                 /* $1F:$64E1, [5]: the six phrase-cell layout rows */
extern UBYTE PhraseCellXAlternate[];              /* $64E6 */
extern UBYTE PhraseCellWidthAlternate[];          /* $64EB */
extern UBYTE PhraseCellWidth[];             /* $64F0 */
extern UBYTE PhraseCellPaletteAlternate[];            /* $64F5 */
extern UBYTE PhraseCellPalette[];               /* $64FA */
void UnhighlightPhraseCell(UBYTE column, UBYTE row);
UBYTE CanEnterSongColumn(UBYTE column);
void ErasePhraseCursor(void);
void WritePhraseInstrument(UBYTE phrase, UBYTE row, UBYTE instrument);
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

UBYTE CanEnterSongColumn(UBYTE column)
{
    UBYTE chain, row, phrase;
    void (*function)(void);

    if (column >= 4)
        DebugTrap();
    wMessageStringPointer = 0;
    if (wLiveMode && wChannelActive[column]) {
        chain = wChannelChain[column];
        if (chain & 0x80)
            return 0;
        phrase = wChannelPhrase[column];
        if (phrase & 0x80)
            return 0;
        row = wChannelChainRow[column];
        if (row & 0xF0)
            return 0;
        wSongRow = wChannelSongRow[column];
    } else {
        chain = ReadSequenceByte(column, wSongRow);
        if (chain & 0x80)
            return 0;
        row = wChainRow;
        function = FAR_FN(SoundGetChainPhrase);
        phrase = FarCallArgument2(2, (UWORD)function, chain, wChainRow);
        while (phrase == 0xFF && row)
            phrase = FarCallArgument2(2, (UWORD)function, chain, --row);
        if (phrase == 0xFF)
            return 0;
    }
    ErasePhraseCursor();
    wSongColumn = column;
    wChain = chain;
    wChainRow = row;
    wPhrase = phrase;
    FarCall(4, FAR_FN(DrawPhraseScreen));
    FarCall(0x1E, FAR_FN(DrawScreenColumnLabels));
    return 1;
}

UBYTE HasEnterableColumnRight(void)
{
    UBYTE column;

    for (column = wSongColumn + 1; column < 4; column++)
        if (CanEnterSongColumn(column))
            break;
}

UBYTE HasEnterableColumnLeft(void)
{
    UBYTE column;

    for (column = wSongColumn - 1; column < 4; column--)
        if (CanEnterSongColumn(column))
            break;
}

/* The phrase screen's cut/copy: every selected cell into the clipboard
   (the note byte split over columns 0 and 1 unless the row flag is $FF),
   cleared on a cut. A selection of the note column alone on channel 2
   takes the instrument column too, copied but never cut (cutInstr). The
   note accessors take only the row: FarCallArgument2/3 get one byte fewer. */
void CutCopyPhraseSelection(UBYTE cut)
{
    UBYTE column, row, t;
    UBYTE cutInstrument = cut;

    if (wClipboardType != SCREEN_PHRASE)
        return;
    ShowMessage(cutInstrument ? "CUT" : "COPY");
    wCursorMode = CURSOR_COPIED;
    wSelectionRight = wPhraseColumn;
    wSelectionBottom = wPhraseRow;
    SortBytePair(&wSelectionLeft, &wSelectionRight);
    SortBytePair(&wSelectionTop, &wSelectionBottom);
    if (wSelectionLeft == PHRASE_COLUMN_VALUE)
        wSelectionLeft = 3;
    if (wSelectionRight == PHRASE_COLUMN_COMMAND)
        wSelectionRight = 4;
    if (wSelectionRight == PHRASE_COLUMN_NOTE && wSongColumn == CHANNEL_WAV) {
        cutInstrument = 0;
        wSelectionRight = 2;
    }
    wSelectionRight++;
    wSelectionBottom++;
    for (row = wSelectionTop; row < wSelectionBottom; row++) {
        for (column = wSelectionLeft; column < wSelectionRight; column = column + 1) {
            wPhraseColumn = column;
            wPhraseRow = row;
            ErasePhraseCursor();
            switch (column) {
            case 0:
                if (wPhraseRowFlags[row] != 0xFF) {
                    t = FarCallArgument2P(4, FAR_FN(ReadPhraseNote), row);
                    WriteClipboardCell(0, row, t >> 4);
                    if (cut)
                        FarCallArgument3P(4, FAR_FN(WritePhraseNote), row, t & 0x0F);
                }
                if (wSelectionRight == PHRASE_COLUMN_NOTE && wSongColumn == CHANNEL_WAV) {
                    WriteClipboardCell(1, row, 0xFF);
                    cutInstrument = 0;
                    goto instrument;
                }
                break;
            case 1:
                if (wPhraseRowFlags[row] == 0xFF) {
                    WriteClipboardCell(0, row, 0);
                    WriteClipboardCell(1, row, FarCallArgument2P(4, FAR_FN(ReadPhraseNote), row));
                    if (cut)
                        FarCallArgument3P(4, FAR_FN(WritePhraseNote), row, 0);
                } else {
                    t = FarCallArgument2P(4, FAR_FN(ReadPhraseNote), row);
                    WriteClipboardCell(1, row, t & 0x0F);
                    if (cut)
                        FarCallArgument3P(4, FAR_FN(WritePhraseNote), row, t & 0xF0);
                }
                break;
            case 2:
            instrument:
                WriteClipboardCell(2, row, FarCallArgument2P(4, FAR_FN(ReadPhraseInstrument), wPhrase, row));
                if (cutInstrument)
                    FarCallArgument3P(4, FAR_FN(WritePhraseInstrument), wPhrase, row, 0xFF);
                break;
            case 3:
                WriteClipboardCell(3, row, FarCallArgument2P(4, FAR_FN(ReadPhraseCommand), wPhrase, row));
                if (cut)
                    FarCallArgument3P(4, FAR_FN(WritePhraseCommand), wPhrase, row, 0);
                break;
            case 4:
                WriteClipboardCell(4, row, FarCallArgument2P(4, FAR_FN(ReadPhraseCommandValue), wPhrase, row));
                if (cut)
                    FarCallArgument3P(4, FAR_FN(WritePhraseCommandValue), wPhrase, row, 0);
                break;
            }
        }
    }
    if (wSelectionRight == PHRASE_COLUMN_NOTE)
        wSelectionRight = 3;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
    wPhraseColumn = wSelectionLeft;
    wPhraseRow = wSelectionTop;
    FarCall(4, FAR_FN(DrawPhraseScreen));
}

void HighlightPhraseCell(UBYTE column, UBYTE row)
{
    char x;

    x = PhraseCellX[column];
    if (row & 0xF0)
        DebugTrap();
    if (wPhraseDrawMode)
        x += 3;
    if (x < 0)
        return;
    if (wPhraseRowFlags[row] == 0xFE || wPhraseRowFlags[row] == 0xFF)
        ColorField(x, row + 2, (wDrawTextScratch = PhraseCellWidth[column], wDrawTextScratch), hGameboyType ? 3 : 2);
    else
        ColorField(PhraseCellXAlternate[column], row + 2, (wDrawTextScratch = PhraseCellWidthAlternate[column], wDrawTextScratch), hGameboyType ? 3 : 2);
}

void DrawPhraseCursor(void)
{
    UBYTE row, column;

    row = wPhraseRow;
    column = wPhraseColumn;
    HighlightPhraseCell(column, row);
}

void UnhighlightPhraseCell(UBYTE column, UBYTE row)
{
    char x;
    UBYTE y;
    UBYTE palette;
    UBYTE w;

    x = PhraseCellX[column];
    y = row + 2;
    if (wPhraseDrawMode)
        x += 3;
    if (x < 0)
        return;
    if (wPhraseRow & 0xF0)
        DebugTrap();
    if (wPhraseRowFlags[row] == 0xFE || wPhraseRowFlags[row] == 0xFF) {
        w = PhraseCellWidth[column];
        palette = PhraseCellPalette[column];
    } else {
        x = PhraseCellXAlternate[column];
        w = PhraseCellWidthAlternate[column];
        palette = PhraseCellPaletteAlternate[column];
    }
    ColorField(x, y, w, palette);
}

void DrawPhraseSelection(void)
{
    UBYTE column, row, left, top, right, bottom;
    UBYTE cursorColumn;

    if (wClipboardType != SCREEN_PHRASE)
        return;
    cursorColumn = wPhraseColumn;               /* the ROM reads the column into c for the test only */
    if (wSelectionLeft < cursorColumn) {
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
    for (column = left ? left - 1 : 0; column < (right == 5 ? 5 : right + 1); column++) {
        for (row = top ? top - 1 : 0; row < (bottom == 0x10 ? 0x10 : bottom + 1); row = row + 1) {
            if (column < left || column >= right || row < top || row >= bottom)
                UnhighlightPhraseCell(column, row);
        }
    }
    /* ...and the rectangle itself drawn as the cursor cell */
    for (column = left; column < right; column++)
        for (row = top; row < bottom; row = row + 1)
            HighlightPhraseCell(column, row);
}

void ErasePhraseCursor(void)
{
    UBYTE row, column;

    row = wPhraseRow;
    column = wPhraseColumn;
    UnhighlightPhraseCell(column, row);
}
