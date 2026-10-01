/*
 * Song data helpers (allocation bitmaps, copies) -- LSDj 9.4.0, ROM bank $01.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wInstrField;                   /* $C427 */
extern UBYTE wColPhrase[];                  /* $C453, [4] */
extern UBYTE InstrTableFieldRow[];          /* $01:$532C, [4] */
void CopyTableData(UBYTE from, UBYTE to);
void ShowOutOfMemory(void);

UBYTE CheckSongInitFlag(void)
{
    if (ReadSongByte(0, SramInitFlag1) == 'r' && ReadSongByte(0, SramInitFlag1 + 1) == 'b')
        return 1;
    if (ReadSongByte(3, SramInitFlag3) == 'r' && ReadSongByte(3, SramInitFlag3 + 1) == 'b')
        return 1;
    if (ReadSongByte(1, SramInitFlag2) == 'r' && ReadSongByte(1, SramInitFlag2 + 1) == 'b')
        return 1;
    return 0;
}

void WriteSongInitFlag(void)
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

    start = wColPhrase[wSongCol];
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

void CloneInstrTable(void)
{
    UBYTE v, t;

    if (wInstrField != InstrTableFieldRow[ReadCurInstrByte(0) \& 3])
        return;
    v = ReadCurInstrByte(6);
    if (!(v & 0x20))
        return;
    v &= 0x1F;
    t = FarCallByte(0x1F, FAR_FN(FindFreeTable));
    if (t == 0xFF)
        return;
    ShowMessage("CLONED TABLE");
    CopyTableData(v, t);
    WriteInstrByte(wInstrument, 6, t | 0x20);
}
