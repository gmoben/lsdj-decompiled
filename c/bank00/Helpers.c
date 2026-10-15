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

extern UBYTE wClipboardBuffer[];            /* $C57A, [6][42] */
extern int wSynthShapeBuffer[];             /* $C9F4, [32] */

UBYTE KitIndexToBank(UBYTE kit);            /* hand-written asm */

/* $07DA: return wClipboardBuffer[col*42 + row] (asserts col < 6, row < 42) */
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

/* $0908: wClipboardBuffer[col*42 + row] = v (asserts col < 6, row < 42) */
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

/* $0B54: copy the 32 signed seed bytes at `source` (in bank $1E) into
   wSynthShapeBuffer as words, sign-extended */
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

/* $0CFE: (kit index) returns 1 if a kit is present at KitIndexToBank(index):
   switch to that bank and check [$4000:01] == $6040 */
UBYTE KitExists(UBYTE kit)
{
    UBYTE saved, bank, ok;

    saved = hCurrentBank;
    bank = KitIndexToBank(kit);
    if (bank > 0x3F)
        return 0;
    SWITCH_ROM(bank);
    ok = (*(UWORD *)KIT_SAMPLE_BOUNDARIES == KIT_SAMPLES);
    SWITCH_ROM(saved);
    return ok;
}

/* $0D4C: copy the 6-char kit name (kit bank $4052, via KitIndexToBank) into
   wSampleNoteString */
void ReadKitName(UBYTE kit)
{
    UBYTE saved;

    saved = hCurrentBank;
    SWITCH_ROM(KitIndexToBank(kit));
    memcpy(wSampleNoteString, (UBYTE *)KIT_NAME, 6);
    wSampleNoteString[6] = 0;
    SWITCH_ROM(saved);
}

/* $0D8B: order the bytes at two pointers: swap them so *a <= *b */
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

/* $0DBC: show `text` in the message bar: copied into wStringBuffer for
   DrawMessageBar */
void ShowMessage(char *text)
{
    strcpy(wStringBuffer, (UBYTE *)text);
    FarCall(1, DrawMessageBar);
}
