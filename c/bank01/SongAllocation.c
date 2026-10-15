/*
 * Song data helpers (allocation bitmaps, copies) -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wInstrumentField;                   /* $C427 */
extern UBYTE wNameEditBuffer[];                /* $C431, [5] */
extern UBYTE wSelectionTop, wSelectionRight, wSelectionBottom; /* $C40E-$C410 */
extern UBYTE wSelectHeld;
extern UBYTE wColumnPhrase[];                  /* $C453, [4] */
extern UBYTE InstrumentTableFieldRow[];          /* $01:$532C, [4] */
void CopyTableData(UBYTE from, UBYTE to);
void ShowOutOfMemory(void);
void WriteInstrumentName(UBYTE instrument);
void CopyInstrumentName(UBYTE instrument);            /* UiHelpers.c */
void SendLinkClipboard(void);                   /* $1C */

UBYTE CheckSongInitializedMarker(void)
{
    if (ReadSongByte(0, SramInitializedMarker1) == 'r' && ReadSongByte(0, SramInitializedMarker1 + 1) == 'b')
        return 1;
    if (ReadSongByte(3, SramInitializedMarker3) == 'r' && ReadSongByte(3, SramInitializedMarker3 + 1) == 'b')
        return 1;
    if (ReadSongByte(1, SramInitializedMarker2) == 'r' && ReadSongByte(1, SramInitializedMarker2 + 1) == 'b')
        return 1;
    return 0;
}

void WriteSongInitializedMarker(void)
{
    WriteSongByte(0, SramInitializedMarker1, 'r');
    WriteSongByte(0, SramInitializedMarker1 + 1, 'b');
    WriteSongByte(1, SramInitializedMarker2, 'r');
    WriteSongByte(1, SramInitializedMarker2 + 1, 'b');
    WriteSongByte(3, SramInitializedMarker3, 'r');
    WriteSongByte(3, SramInitializedMarker3 + 1, 'b');
}

void MarkPhraseUsed(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAllocation + (phrase >> 3);
    phrase &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) | (1 << phrase));
}

void MarkChainUsed(UBYTE chain)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainAllocation + (chain >> 3);
    chain &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) | (1 << chain));
}

void FreePhrase(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAllocation + (phrase >> 3);
    phrase &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) & (0xFF - (1 << phrase)));
}

void FreeChain(UBYTE chain)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainAllocation + (chain >> 3);
    chain &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) & (0xFF - (1 << chain)));
}

UBYTE AllocateNextFreePhrase(void)
{
    UBYTE phrase;
    UBYTE *p;
    UBYTE start;

    start = wColumnPhrase[wSongColumn];
    if (start == 0xFF)
        start = 0;
    phrase = start;
    do {
        p = SramPhraseAllocation + (phrase >> 3);
        if (!(ReadSongByte(1, p) & (1 << (phrase & 7)))) {
            MarkPhraseUsed(phrase);
            if (phrase == 0xFF)
                DebugTrap();
            return phrase;
        }
        if (++phrase == 0xFF)
            phrase = 0;
    } while (phrase != start);
    FarCall(1, FAR_FN(ShowOutOfMemory));
    return 0xFF;
}

UBYTE IsChainUsed(UBYTE chain)
{
    UBYTE v, m;

    if (chain >= 0x80)
        DebugTrap();
    v = ReadSongByte(1, SramChainAllocation + (chain >> 3));
    m = 1 << (chain & 7);
    return v & m;
}

UBYTE IsPhraseUsed(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAllocation + (phrase >> 3);
    phrase &= 7;
    return ReadSongByte(1, p) & (1 << phrase);
}

/* $5253: copy the instrument ("COPIED INSTR"): its 16 bytes and its name
   into the clipboard's first two columns, as a 3 x 16 selection */

void CopyInstrument(void)
{
    UBYTE cell;

    ShowMessage("COPIED INSTR");
    wClipboardType = SCREEN_INSTRUMENT;
    wCursorMode = CURSOR_COPIED;
    for (cell = 0; cell < 16; cell++)
        WriteClipboardCell(0, cell, ReadCurrentInstrumentByte(cell));
    CopyInstrumentName(wInstrument);
    for (cell = 0; cell < 5; cell++)
        WriteClipboardCell(1, cell, wNameEditBuffer[cell]);
    wSelectionLeft = 0;
    wSelectionRight = 2;
    wSelectionTop = 0;
    wSelectionBottom = 16;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
}

void CopySong16(UBYTE bank, UWORD source, UWORD destination)
{
    UBYTE offset, v;

    for (offset = 0; offset < 0x10; offset++) {
        v = ReadSongByte(bank, source);
        WriteSongByte(bank, destination, v);
        source = source + 1;
        destination = destination + 1;
    }
}

void CopyTableData(UBYTE source, UBYTE destination)
{
    UWORD s, d;
    UWORD s1, s2, s3, s4, s5, s6;
    UWORD d1, d2, d3, d4, d5, d6;

    s = (UWORD)source << 4;
    d = (UWORD)destination << 4;
    s1 = (UWORD)SRAM_TABLE_COMMANDS1 + s;
    s2 = (UWORD)SRAM_TABLE_COMMAND_VALUES1 + s;
    s3 = (UWORD)SRAM_TABLE_COMMANDS2 + s;
    s4 = (UWORD)SRAM_TABLE_COMMAND_VALUES2 + s;
    s5 = (UWORD)SRAM_TABLE_TRANSPOSES + s;
    s6 = (UWORD)SRAM_TABLE_ENVELOPES + s;
    d1 = (UWORD)SRAM_TABLE_COMMANDS1 + d;
    d2 = (UWORD)SRAM_TABLE_COMMAND_VALUES1 + d;
    d3 = (UWORD)SRAM_TABLE_COMMANDS2 + d;
    d4 = (UWORD)SRAM_TABLE_COMMAND_VALUES2 + d;
    d5 = (UWORD)SRAM_TABLE_TRANSPOSES + d;
    d6 = (UWORD)SRAM_TABLE_ENVELOPES + d;
    CopySong16(1, s1, d1);
    CopySong16(1, s2, d2);
    CopySong16(1, s3, d3);
    CopySong16(1, s4, d4);
    CopySong16(1, s5, d5);
    CopySong16(0, s6, d6);
}

void CloneInstrumentTable(void)
{
    UBYTE v, t;

    if (wInstrumentField
            != InstrumentTableFieldRow[ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TYPE) & 3])
        return;
    v = ReadCurrentInstrumentByte(INSTRUMENT_BYTE_TABLE);
    if (!(v & 0x20))
        return;
    v &= 0x1F;
    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    ShowMessage("CLONED TABLE");
    CopyTableData(v, t);
    WriteInstrumentByte(wInstrument, INSTRUMENT_BYTE_TABLE, t | 0x20);
}

void PasteInstrument(void)
{
    UBYTE cell;

    if (wInstrument == 0x40)
        return;
    if (wCursorMode != CURSOR_COPIED)
        return;
    if (!wSelectHeld)
        return;
    if (wClipboardType != SCREEN_INSTRUMENT)
        return;
    for (cell = 0; cell < 16; cell++)
        WriteInstrumentByte(wInstrument, cell, ReadClipboardCell(0, cell));
    for (cell = 0; cell < 5; cell++)
        wNameEditBuffer[cell] = ReadClipboardCell(1, cell);
    WriteInstrumentName(wInstrument);
    if (hGameboyType)
        ClearBgTilemap();
    DrawScreenPaletteMap();
    FarCall(3, FAR_FN(DrawInstrumentScreen));
    ShowMessage("PASTED INSTR");
}
