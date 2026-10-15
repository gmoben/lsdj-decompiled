/*
 * Bank 0's shared C helpers -- LSDj 9.4.0, ROM bank $00.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * The kit helpers page a kit's ROM bank in with SWITCH_ROM. rROMB0 has to
 * be volatile: with no call between the two switches in KitExists, SDCC
 * otherwise drops the first store as dead. KitExists reads the kit header
 * as one word (`*(UWORD *)0x4000 == 0x4060`), which SDCC loads into the
 * reversed b/c pair. ShowMessage casts its argument: the ROM copies the
 * pointer through bc before passing it on, as a conversion does.
 */

#include "lsdj.h"

extern UBYTE wClipboardBuffer[];               /* $C57A, [6][42] */
extern int wSynthShapeBuffer[];                /* $C9F4, [32] */

UBYTE KitIndexToBank(UBYTE kit);            /* hand-written asm */

UBYTE ReadClipboardCell(UBYTE column, UBYTE row)
{
    UBYTE *p;

    if (column >= 6)
        DebugTrap(); 
   if (row >= 42)
        DebugTrap();
    p = wClipboardBuffer + column * 42;
    return p[row];
}

void WriteClipboardCell(UBYTE column, UBYTE row, UBYTE v)
{
    UBYTE *p;

    if (column >= 6)
        DebugTrap();
    if (row >= 42)
        DebugTrap();
    p = wClipboardBuffer + column * 42;
    p[row] = v;
}

void LoadSynthShape(char *source)
{
    UBYTE saved, position;
    char v;

    saved = hCurrentBank;
    SWITCH_ROM(0x1E);
    for (position = 0; position < 0x20; position++) {
        v = *source;
        if (v & 0x80) {
            wSynthShapeBuffer[position] = v;
            wSynthShapeBuffer[position] |= 0xFF00;
        } else {
            wSynthShapeBuffer[position] = v;
        }
        source++;
    }
    SWITCH_ROM(saved);
}

UBYTE KitExists(UBYTE kit)
{
    UBYTE saved, bank, ok;

    saved = hCurrentBank;
    bank = KitIndexToBank(kit);
    if (bank > 0x3F)
        return 0;
    SWITCH_ROM(bank);
    ok = (*(UWORD *)0x4000 == 0x4060);
    SWITCH_ROM(saved);
    return ok;
}

void ReadKitName(UBYTE kit)
{
    UBYTE saved;

    saved = hCurrentBank;
    SWITCH_ROM(KitIndexToBank(kit));
    memcpy(wSampleNoteString, (UBYTE *)0x4052, 6);
    wSampleNoteString[6] = 0;
    SWITCH_ROM(saved);
}

void SortBytePair(UBYTE *a, UBYTE *b)
{
    UBYTE x, y;

    x = *a;
    y = *b;
    if (y < x) {
        *a = y;
        *b = x;
    }
}

void ShowMessage(char *text)
{
    strcpy(wStringBuffer, (UBYTE *)text);
    FarCall(1, DrawMessageBar);
}
