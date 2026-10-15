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
void WriteInstrumentName(UBYTE instr);
void CopyInstrumentName(UBYTE instr);            /* UiHelpers.c */
void SendLinkClipboard(void);                   /* $1C */

UBYTE CheckSongInitializedMarker(void)
{
    if (ReadSongByte(0, SramInitFlag1) == 'r' && ReadSongByte(0, SramInitFlag1 + 1) == 'b')
        return 1;
    if (ReadSongByte(3, SramInitFlag3) == 'r' && ReadSongByte(3, SramInitFlag3 + 1) == 'b')
        return 1;
    if (ReadSongByte(1, SramInitFlag2) == 'r' && ReadSongByte(1, SramInitFlag2 + 1) == 'b')
        return 1;
    return 0;
}

void WriteSongInitializedMarker(void)
{
    WriteSongByte(0, SramInitFlag1, 'r');
    WriteSongByte(0, SramInitFlag1 + 1, 'b');
    WriteSongByte(1, SramInitFlag2, 'r');
    WriteSongByte(1, SramInitFlag2 + 1, 'b');
    WriteSongByte(3, SramInitFlag3, 'r');
    WriteSongByte(3, SramInitFlag3 + 1, 'b');
}

void MarkPhraseUsed(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAlloc + (phrase >> 3);
    phrase &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) | (1 << phrase));
}

void MarkChainUsed(UBYTE chain)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainAlloc + (chain >> 3);
    chain &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) | (1 << chain));
}

void FreePhrase(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAlloc + (phrase >> 3);
    phrase &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) & (0xFF - (1 << phrase)));
}

void FreeChain(UBYTE chain)
{
    UBYTE *p;

    if (chain >= 0x80)
        DebugTrap();
    p = SramChainAlloc + (chain >> 3);
    chain &= 7;
    WriteSongByte(1, p, ReadSongByte(1, p) & (0xFF - (1 << chain)));
}

UBYTE AllocNextFreePhrase(void)
{
    UBYTE i;
    UBYTE *p;
    UBYTE start;

    start = wColumnPhrase[wSongColumn];
    if (start == 0xFF)
        start = 0;
    i = start;
    do {
        p = SramPhraseAlloc + (i >> 3);
        if (!(ReadSongByte(1, p) & (1 << (i & 7)))) {
            MarkPhraseUsed(i);
            if (i == 0xFF)
                DebugTrap();
            return i;
        }
        if (++i == 0xFF)
            i = 0;
    } while (i != start);
    FarCall(1, FAR_FN(ShowOutOfMemory));
    return 0xFF;
}

UBYTE IsChainUsed(UBYTE chain)
{
    UBYTE v, m;

    if (chain >= 0x80)
        DebugTrap();
    v = ReadSongByte(1, SramChainAlloc + (chain >> 3));
    m = 1 << (chain & 7);
    return v & m;
}

UBYTE IsPhraseUsed(UBYTE phrase)
{
    UBYTE *p;

    if (phrase == 0xFF)
        DebugTrap();
    p = SramPhraseAlloc + (phrase >> 3);
    phrase &= 7;
    return ReadSongByte(1, p) & (1 << phrase);
}

/* $5253: copy the instrument ("COPIED INSTR"): its 16 bytes and its name
   into the clipboard's first two columns, as a 3 x 16 selection */

void CopyInstrument(void)
{
    UBYTE i;

    ShowMessage("COPIED INSTR");
    wClipboardType = 6;
    wCursorMode = 2;
    for (i = 0; i < 16; i++)
        WriteClipboardCell(0, i, ReadCurrentInstrumentByte(i));
    CopyInstrumentName(wInstrument);
    for (i = 0; i < 5; i++)
        WriteClipboardCell(1, i, wNameEditBuffer[i]);
    wSelectionLeft = 0;
    wSelectionRight = 2;
    wSelectionTop = 0;
    wSelectionBottom = 16;
    FarCall(0x1C, FAR_FN(SendLinkClipboard));
}

void CopySong16(UBYTE bank, UWORD src, UWORD dst)
{
    UBYTE i, v;

    for (i = 0; i < 0x10; i++) {
        v = ReadSongByte(bank, src);
        WriteSongByte(bank, dst, v);
        src = src + 1;
        dst = dst + 1;
    }
}

void CopyTableData(UBYTE src, UBYTE dst)
{
    UWORD s, d;
    UWORD s1, s2, s3, s4, s5, s6;
    UWORD d1, d2, d3, d4, d5, d6;

    s = (UWORD)src << 4;
    d = (UWORD)dst << 4;
    s1 = (UWORD)0xB680 + s;
    s2 = (UWORD)0xB880 + s;
    s3 = (UWORD)0xBA80 + s;
    s4 = (UWORD)0xBC80 + s;
    s5 = (UWORD)0xB480 + s;
    s6 = (UWORD)0xB690 + s;
    d1 = (UWORD)0xB680 + d;
    d2 = (UWORD)0xB880 + d;
    d3 = (UWORD)0xBA80 + d;
    d4 = (UWORD)0xBC80 + d;
    d5 = (UWORD)0xB480 + d;
    d6 = (UWORD)0xB690 + d;
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

    if (wInstrumentField != InstrumentTableFieldRow[ReadCurrentInstrumentByte(0) & 3])
        return;
    v = ReadCurrentInstrumentByte(6);
    if (!(v & 0x20))
        return;
    v &= 0x1F;
    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    ShowMessage("CLONED TABLE");
    CopyTableData(v, t);
    WriteInstrumentByte(wInstrument, 6, t | 0x20);
}

void PasteInstrument(void)
{
    UBYTE i;

    if (wInstrument == 0x40)
        return;
    if (wCursorMode != 2)
        return;
    if (!wSelectHeld)
        return;
    if (wClipboardType != 6)
        return;
    for (i = 0; i < 16; i++)
        WriteInstrumentByte(wInstrument, i, ReadClipboardCell(0, i));
    for (i = 0; i < 5; i++)
        wNameEditBuffer[i] = ReadClipboardCell(1, i);
    WriteInstrumentName(wInstrument);
    if (hGameboyType)
        ClearBgTilemap();
    DrawScreenPaletteMap();
    FarCall(3, FAR_FN(DrawInstrumentScreen));
    ShowMessage("PASTED INSTR");
}
