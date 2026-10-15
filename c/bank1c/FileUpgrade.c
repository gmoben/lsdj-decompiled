/*
 * Song-format upgrades -- LSDj 9.4.0, ROM bank $1C: loading an older .sav
 * steps its working song through FileUpgradeV0..V17b, each patching the
 * instruments of one or more types through a per-instrument callback
 * (FileForEachInstrumentType sets wFileInstrumentIndex and calls it).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wFileInstrumentIndex;                 /* $C9A1, the instrument being upgraded */
extern UBYTE FileUpgradeRemapTable[];       /* $1C:$68F1, [8] */
extern UBYTE NoiseNoteRemapTable[];         /* $1C:$6E3C, [112] */

void FileForEachInstrumentType(UBYTE type, void (*function)());
UBYTE FileReadInstrumentByte(UBYTE field);
void FileWriteInstrumentByte(UBYTE field, UBYTE value);
UBYTE FileUpgradeRemapBits(UBYTE v);
UBYTE FileUpgradeClampNibble(UBYTE v);
void FileUpgradeV0Kit(void);
void FileUpgradeV1Kit(void);
void FileUpgradeV3Pulse(void);
void FileUpgradeV3Kit(void);
void FileUpgradeV5Wave(void);
void FileUpgradeV6Wave(void);
void FileUpgradeV8Wave(void);
void FileUpgradeV9Wave(void);
void FileUpgradeV10Pulse(void);
void FileUpgradeV12Pulse(void);
void FileUpgradeV14Pulse(void);
void FileUpgradeV15Wave(void);
void FileUpgradeV17bPulse(void);
void FileUpgradeRetrigRate(UBYTE *p);          /* $1C:$6973 */
void FileUpgradeClearRange(void);           /* $1C:$65B9 */
void FileUpgradeV2(void);
void ReadSongBytesW(UBYTE bank, UWORD source, UBYTE *destination);  /* ReadSongBytes taking a word address (aliased by match_function.py) */
char ReadInstrumentByteS(UBYTE instrument, UBYTE field);  /* ReadInstrumentByte returning char (aliased): the cast to UBYTE keeps the call result in its own slot */

UWORD FileCalculateCompressedSize(void)
{
    UWORD n;

    n = ReadSongByte(1, 0xBFB2);
    n -= ReadSongByte(1, 0xBFCA);
    n *= 0x3C;
    n += ReadSongByte(1, 0xBFB3);
    n -= ReadSongByte(1, 0xBFCB);
    return n;
}

UBYTE FileReadInstrumentByte(UBYTE field)
{
    return ReadInstrumentByte(wFileInstrumentIndex, field);
}

void FileWriteInstrumentByte(UBYTE field, UBYTE value)
{
    WriteInstrumentByte(wFileInstrumentIndex, field, value);
}

void FileForEachInstrumentType(UBYTE type, void (*function)())
{
    for (wFileInstrumentIndex = 0; wFileInstrumentIndex < 0x40; wFileInstrumentIndex++) {
        if (FileReadInstrumentByte(0) == type)
            (*function)();
    }
}

/* $6134: the save-format 0 kit instrument: move the "loop" bit (bit 5) of
   both kit-number bytes (2 and 9) into bits 6/5 of byte 5 and shift the
   kit numbers up by 8. `a &= ~(UWORD)0x20` is the author's spelling: the
   16-bit mask zero-extends the byte into a spilt word and narrows the
   16-bit AND back to the byte -- the first through b, the second handed
   its registers by the dying word -- and the dead high-byte load the
   narrowing leaves is dropped by the peephole (dead_stores). */
void FileUpgradeV0Kit(void)
{
    UBYTE a, f, b, la, lb;

    a = FileReadInstrumentByte(2);
    f = FileReadInstrumentByte(5);
    b = FileReadInstrumentByte(9);
    la = a & 0x20;
    lb = b & 0x20;
    a &= ~(UWORD)0x20;
    b &= ~(UWORD)0x20;
    a += 8;
    b += 8;
    FileWriteInstrumentByte(2, a);
    FileWriteInstrumentByte(9, b);
    if (la)
        f |= 0x40;
    if (lb)
        f |= 0x20;
    FileWriteInstrumentByte(5, f);
}

void FileUpgradeV0(void)
{
    FileForEachInstrumentType(2, FileUpgradeV0Kit);
}

void FileUpgradeV1Kit(void)
{
    UBYTE a, b;

    a = FileReadInstrumentByte(2);
    b = FileReadInstrumentByte(9);
    a -= 8;
    b -= 8;
    FileWriteInstrumentByte(2, a);
    FileWriteInstrumentByte(9, b);
}

void FileUpgradeV1(void)
{
    FileForEachInstrumentType(2, FileUpgradeV1Kit);
}

/* Version 2 to 3: every table envelope byte ($B690-$B88F) whose low
   nibble is neither 0 nor F gains one. */
void FileUpgradeV2(void)
{
    UWORD address;
    UBYTE v, low;

    for (address = 0xB690; address != (UWORD)0xB890; address++) {
        v = ReadSongByte(0, address);
        low = v & 0x0F;
        if (low && low != 0x0F)
            WriteSongByte(0, address, v + 1);
    }
}

/* Version 3 to 4, pulse and wave instruments: byte 5's bits 2-1 move on.
   2 sets bit 4; 4 clears them and sets bit 4; 6 leaves 2 in them and sets
   bit 4. The `(char)` reads are the ROM's `ld b,c; ld a,b` copies, and the
   `& 0xF9` stays in a for the `|` (peep.py's chained_bitwise). */
void FileUpgradeV3Pulse(void)
{
    UBYTE v;

    v = FileReadInstrumentByte(5);
    switch ((char)v & 6) {
    case 2:
        v |= 0x10;
        break;
    case 4:
        v = ((char)v & 0xF9) | 0x10;
        break;
    case 6:
        v = ((char)v & 0xF9) | 0x14;
        break;
    }
    FileWriteInstrumentByte(5, v);
}

/* The same for kits: 2 sets bit 4, 4 sets bit 7, then bits 2-1 are cleared
   and bit 0 set. */
void FileUpgradeV3Kit(void)
{
    UBYTE v;

    v = FileReadInstrumentByte(5);
    switch ((char)v & 6) {
    case 2:
        v |= 0x10;
        break;
    case 4:
        v |= 0x80;
        break;
    }
    v = ((char)v & 0xF9) | 1;
    FileWriteInstrumentByte(5, v);
}

void FileUpgradeV3(void)
{
    FileForEachInstrumentType(0, FileUpgradeV3Pulse);
    FileForEachInstrumentType(1, FileUpgradeV3Pulse);
    FileForEachInstrumentType(2, FileUpgradeV3Kit);
}

void FileUpgradeV4(void)
{
    UWORD address;
    UBYTE v;

    for (address = 0xBEB4; address != (UWORD)0xBFB4; address += 0x10) {
        v = ReadSongByte(1, address);
        v |= v << 4;
        WriteSongByte(1, address, v);
    }
}

void FileUpgradeV5Wave(void)
{
    UBYTE v;

    v = FileReadInstrumentByte(14);
    FileWriteInstrumentByte(10, v >> 4);
    FileWriteInstrumentByte(11, v & 0x0F);
}

void FileUpgradeV5(void)
{
    FileForEachInstrumentType(1, FileUpgradeV5Wave);
}

void FileUpgradeV6Wave(void)
{
    UBYTE v;

    v = FileReadInstrumentByte(10);
    FileWriteInstrumentByte(10, 0x0F - v);
    v = FileReadInstrumentByte(11);
    FileWriteInstrumentByte(11, v - 3);
}

void FileUpgradeV6(void)
{
    FileForEachInstrumentType(1, FileUpgradeV6Wave);
}

void FileUpgradeV7(void)
{
    UBYTE *command;
    UBYTE v;

    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (command = (UBYTE *)0xB680; command != (UBYTE *)0xB880; command++) {
        v = *command;
        if (v > 1)
            *command = v + 1;
    }
    for (command = (UBYTE *)0xBA80; command != (UBYTE *)0xBC80; command++)
        if (*command > 1)
            (*command)++;
    rRAMB = 2;
    for (command = (UBYTE *)0xA000; command != (UBYTE *)0xAFF0; command++)
        if (*command > 1)
            (*command)++;
    rRAMG = 0;
    _asm ei _endasm;
}

void FileUpgradeV8Wave(void)
{
    FileWriteInstrumentByte(2, 0x0F ^ FileReadInstrumentByte(2));
}

void FileUpgradeV8(void)
{
    FileForEachInstrumentType(1, FileUpgradeV8Wave);
}

void FileUpgradeV9Wave(void)
{
    FileWriteInstrumentByte(9, (FileReadInstrumentByte(9) + 1) & 3);
}

void FileUpgradeV9(void)
{
    FileForEachInstrumentType(1, FileUpgradeV9Wave);
}

void FileUpgradeV10Pulse(void)
{
    UBYTE v;

    v = FileReadInstrumentByte(1) & 0x0F;
    if (v > 8)
        v = 0xF0;
    else
        v = 0;
    FileWriteInstrumentByte(9, v);
}

void FileUpgradeV10(void)
{
    FileForEachInstrumentType(0, FileUpgradeV10Pulse);
    FileForEachInstrumentType(3, FileUpgradeV10Pulse);
}

UBYTE FileUpgradeReadByte(UBYTE i)
{
    UBYTE v;

    v = ReadSongByte(0, SramSequence + 3 + ((UWORD)i << 2));   /* row i, channel 3 */
    return (v & 0x80) ? 0xFF : v;
}

/*
 * v11 -> v12, second half: the chain transposes were semitones; now they
 * are a note offset in the high nibble. Mark in $DA00 the chains the
 * sequence uses (FileUpgradeReadByte: row by row, channel 3), then rescale
 * each used chain's 16 transposes through bank 6's ReadChainTranspose /
 * SetChainTranspose by address (v / 12 << 4).
 *
 * lsdj-alloc(FileUpgradeClearRange): BUSYB=26-62
 * -- register b held from the second loop's pointer cast to the end: v and
 * every temporary of the three statements stay in memory (docs/toolchain.md,
 * "The phantom register").
 */
void FileUpgradeClearRange(void)
{
    UBYTE i;
    UBYTE *p;
    UBYTE j;
    char v;
    
    UBYTE t;

    i = 0;
    FillMemory((UBYTE *)0xDA00, 0, 0x80);
    do {
        t = FileUpgradeReadByte(i);
        if (t < 0x80) {
            p = (UBYTE *)0xDA00 + (UWORD)t;
            *p = 1;
        }
    } while (++i);
    do {
        p = (UBYTE *)0xDA00 + (UWORD)i;
        if (*p) {
            j = 0;
            while (!(j == 0x10)) {
                v = FarCallArgument2(6, 0x56AA, i, j);
                v = v / 12;
                v <<= 4;
                FarCallArgument3(6, 0x5655, i, j, v);
                j++;
            }
        }
    } while (++i != 0x80);
}

/*
 * v11 -> v12: noise instruments (type 3) used to take their shape from the
 * phrase note. For every phrase, each step played by a noise instrument has
 * its note rewritten to the instrument's byte 4 with the high nibble moved by
 * the note's octave relative to octave 3 (clamped at 0 and F). A step with
 * instrument $40 (none) clears the value; one above $40 keeps the previous
 * step's.
 */
void FileUpgradeV11(void)
{
    UBYTE types[0x40];
    UBYTE instrument;
    UWORD p;
    UWORD address;
    UBYTE v;
    UBYTE j;
    UBYTE n;

    instrument = 0;
    p = 0xA000;
    address = 0xB000;
    while (instrument != 0x40) {
        types[instrument] = ReadInstrumentByte(instrument, 0);
        instrument++;
    }
    while (address != 0xBFF0) {
        v = 0;
        j = 0;
        ReadSongBytesW(0, p, wSongReadBuffer);
        ReadSongBytesW(3, address, wUsedTableMap);
        for (; j != 0x10; j = j + 1) {
            instrument = wUsedTableMap[j];
            if (instrument == 0x40) {
                v = 0;
            } else if (instrument < 0x40) {
                if (types[instrument] == 3)
                    v = ReadInstrumentByteS(instrument, 4);
                else
                    v = 0;
            }
            if (v) {
                n = wSongReadBuffer[j];
                if (n) {
                    n = n - 1;
                    n = n / 12;
                    n = n - 3;
                    while (n & 0x80) {
                        n = n + 1;
                        if (v & 0xF0)
                            v = v - 0x10;
                    }
                    while (n) {
                        n = n - 1;
                        if ((v & 0xF0) != 0xF0)
                            v = v + 0x10;
                    }
                    WriteSongByte(0, p + j, v);
                }
            }
        }
        address += 0x10;
        p += 0x10;
    }
}

UBYTE FileUpgradeRemapBits(UBYTE v)
{
    UWORD r;

    r = FileUpgradeRemapTable[v & 7];
    return (v & 0xF0) | r;
}

void FileUpgradeV12Pulse(void)
{
    FileWriteInstrumentByte(1, FileUpgradeRemapBits(FileReadInstrumentByte(1)));
    FileWriteInstrumentByte(9, FileUpgradeRemapBits(FileReadInstrumentByte(9)));
    FileWriteInstrumentByte(10, FileUpgradeRemapBits(FileReadInstrumentByte(10)));
}

void FileUpgradeV12(void)
{
    FileForEachInstrumentType(0, FileUpgradeV12Pulse);
    FileForEachInstrumentType(3, FileUpgradeV12Pulse);
}

void FileUpgradeV13(void)
{
    UBYTE *command;

    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (command = (UBYTE *)0xB680; command != (UBYTE *)0xB880; command++)
        if (*command == 0x0E)
            FileUpgradeRetrigRate(command + 0x200);
    for (command = (UBYTE *)0xBA80; command != (UBYTE *)0xBC80; command++)
        if (*command == 0x0E)
            FileUpgradeRetrigRate(command + 0x200);
    rRAMB = 2;
    for (command = (UBYTE *)0xA000; command != (UBYTE *)0xAFF0; command++)
        if (*command == 0x0E)
            FileUpgradeRetrigRate(command + 0xFF0);
    rRAMG = 0;
    _asm ei _endasm;
}

UBYTE FileUpgradeClampNibble(UBYTE v)
{
    UBYTE r;

    if ((v & 0x0F) > 1)
        r = v - 1;
    else
        r = v;
    return r;
}

void FileUpgradeV14Pulse(void)
{
    FileWriteInstrumentByte(1, FileUpgradeClampNibble(FileReadInstrumentByte(1)));
    FileWriteInstrumentByte(9, FileUpgradeClampNibble(FileReadInstrumentByte(9)));
    FileWriteInstrumentByte(10, FileUpgradeClampNibble(FileReadInstrumentByte(10)));
}

void FileUpgradeV14(void)
{
    FileForEachInstrumentType(0, FileUpgradeV14Pulse);
    FileForEachInstrumentType(3, FileUpgradeV14Pulse);
}

void FileUpgradeV15Wave(void)
{
    FileWriteInstrumentByte(3, FileReadInstrumentByte(2) & 0xF0);
}

void FileUpgradeV15(void)
{
    FileForEachInstrumentType(1, FileUpgradeV15Wave);
}

void FileUpgradeV16(void)
{
    UWORD address;
    UBYTE v;
    UBYTE *q;

    address = 0xB690;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    while (address != (UWORD)0xB890) {
        v = address;
        if ((v & 0x0F) != 0 && (*(UBYTE *)address & 0x0F) == 0x0F) {
            address--;
            q = (UBYTE *)address;
            v = *q;
            if ((v & 0x0F) < 0x0E)
                *q = v + 1;
            address++;
        }
        address++;
    }
    rRAMG = 0;
    _asm ei _endasm;
}

void FileUpgradeV17(void)
{
    UWORD address;
    UBYTE v;

    for (address = 0xBEBF; address != (UWORD)0xBFBF; address += 0x10) {
        v = ReadSongByte(1, address);
        WriteSongByte(1, address, -(v >> 4));
        WriteSongByte(1, address + 1, -(v & 0x0F));
    }
}

void FileUpgradeV17bPulse(void)
{
    FileWriteInstrumentByte(11, (FileReadInstrumentByte(7) << 1) & 0x78);
}

void FileUpgradeV17b(void)
{
    FileForEachInstrumentType(0, FileUpgradeV17bPulse);
}

/*
 * v18 -> v19: the same pass as v11 over every phrase step played by a noise
 * instrument, now remapping the note itself: notes below $20 become 0, the
 * rest index NoiseNoteRemapTable by (note - $20)'s high nibble and low three
 * bits, plus $3C when bit 3 is clear.
 */
void FileUpgradeV18(void)
{
    UBYTE types[0x40];
    UBYTE instrument;
    UWORD p;
    UWORD address;
    UBYTE v;
    UBYTE j;
    UBYTE n;
    UBYTE m;

    instrument = 0;
    p = 0xA000;
    address = 0xB000;
    while (instrument != 0x40) {
        types[instrument] = ReadInstrumentByte(instrument, 0);
        instrument++;
    }
    while (address != 0xBFF0) {
        v = 0;
        j = 0;
        ReadSongBytesW(0, p, wSongReadBuffer);
        ReadSongBytesW(3, address, wUsedTableMap);
        for (; j != 0x10; j = j + 1) {
            instrument = wUsedTableMap[j];
            if (instrument == 0x40) {
                v = 0;
            } else if (instrument < 0x40) {
                v = types[instrument] == 3;
            }
            if (v) {
                n = wSongReadBuffer[j];
                m = 0;
                if (n) {
                    if (!(n < 0x20)) {
                        n = n - 0x20;
                        m = NoiseNoteRemapTable[(n & 7) + ((n & 0xF0) >> 1)];
                        if (!(n & 8))
                            m = m + 0x3C;
                    }
                    WriteSongByte(0, p + j, m);
                }
            }
        }
        address += 0x10;
        p += 0x10;
    }
}

void FileUpgradeV19(void)
{
    UWORD source, destination;

    source = 0xBDD0;
    destination = 0xBDD0;
    while (destination != (UWORD)0xBDFA) {
        WriteSongByte(0, destination++, ReadSongByte(0, source++));
        WriteSongByte(0, destination++, ReadSongByte(0, source++));
        WriteSongByte(0, destination++, ReadSongByte(0, source++));
        source++;
    }
}

void FileUpgradeV21(void)
{
    UBYTE v;
    UWORD q, address;

    for (address = 0xBEB2; address != (UWORD)0xBFB2; address += 16) {
        q = address + 4;
        v = ReadSongByte(1, q);
        if (v)
            WriteSongByte(1, q, 3);
    }
}

void FileUpgradeSong(void)
{
    UBYTE version;

    version = ReadSongByte(3, 0xBFFF);
    if (!(version < 0x16))
        return;
    wVramCursor = 0x98E2;
    DrawString("UPGRADING...   ");
    while (version < 0x16) {
        switch (version) {
        case 0: FileUpgradeV0(); break;
        case 1: FileUpgradeV1(); break;
        case 2: FileUpgradeV2(); break;
        case 3: FileUpgradeV3(); break;
        case 4: FileUpgradeV4(); break;
        case 5: FileUpgradeV5(); break;
        case 6: FileUpgradeV6(); break;
        case 7: FileUpgradeV7(); break;
        case 8: FileUpgradeV8(); break;
        case 9: FileUpgradeV9(); break;
        case 10: FileUpgradeV10(); break;
        case 11: FileUpgradeV11(); FileUpgradeClearRange(); break;
        case 12: FileUpgradeV12(); break;
        case 13: FileUpgradeV13(); break;
        case 14: FileUpgradeV14(); break;
        case 15: FileUpgradeV15(); break;
        case 16: FileUpgradeV16(); break;
        case 17: FileUpgradeV17(); FileUpgradeV17b(); break;
        case 18: FileUpgradeV18(); break;
        case 19: FileUpgradeV19(); break;
        case 21: FileUpgradeV21(); break;
        }
        version++;
    }
    WriteSongByte(3, 0xBFFF, 0x16);
}
