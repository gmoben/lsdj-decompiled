/*
 * Song-format upgrades -- LSDj 9.4.0, ROM bank $1C: loading an older .sav
 * steps its working song through FileUpgradeV0..V17b, each patching the
 * instruments of one or more types through a per-instrument callback
 * (FileForEachInstrType sets wFileInstrIdx and calls it).
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wFileInstrIdx;                 /* $C9A1, the instrument being upgraded */
extern UBYTE FileUpgradeRemapTable[];       /* $1C:$68F1, [8] */
extern UBYTE NoiseNoteRemapTable[];         /* $1C:$6E3C, [112] */

void FileForEachInstrType(UBYTE type, void (*fn)());
UBYTE FileReadInstrByte(UBYTE field);
void FileWriteInstrByte(UBYTE field, UBYTE val);
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
void ReadSongBytesW(UBYTE bank, UWORD src, UBYTE *dst);  /* ReadSongBytes taking a word address (aliased by match_function.py) */
char ReadInstrByteS(UBYTE instr, UBYTE field);  /* ReadInstrByte returning char (aliased): the cast to UBYTE keeps the call result in its own slot */

UWORD FileCalcCompressedSize(void)
{
    UWORD n;

    n = ReadSongByte(1, 0xBFB2);
    n -= ReadSongByte(1, 0xBFCA);
    n *= 0x3C;
    n += ReadSongByte(1, 0xBFB3);
    n -= ReadSongByte(1, 0xBFCB);
    return n;
}

UBYTE FileReadInstrByte(UBYTE field)
{
    return ReadInstrByte(wFileInstrIdx, field);
}

void FileWriteInstrByte(UBYTE field, UBYTE val)
{
    WriteInstrByte(wFileInstrIdx, field, val);
}

void FileForEachInstrType(UBYTE type, void (*fn)())
{
    for (wFileInstrIdx = 0; wFileInstrIdx < 0x40; wFileInstrIdx++) {
        if (FileReadInstrByte(0) == type)
            (*fn)();
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

    a = FileReadInstrByte(2);
    f = FileReadInstrByte(5);
    b = FileReadInstrByte(9);
    la = a & 0x20;
    lb = b & 0x20;
    a &= ~(UWORD)0x20;
    b &= ~(UWORD)0x20;
    a += 8;
    b += 8;
    FileWriteInstrByte(2, a);
    FileWriteInstrByte(9, b);
    if (la)
        f |= 0x40;
    if (lb)
        f |= 0x20;
    FileWriteInstrByte(5, f);
}

void FileUpgradeV0(void)
{
    FileForEachInstrType(2, FileUpgradeV0Kit);
}

void FileUpgradeV1Kit(void)
{
    UBYTE a, b;

    a = FileReadInstrByte(2);
    b = FileReadInstrByte(9);
    a -= 8;
    b -= 8;
    FileWriteInstrByte(2, a);
    FileWriteInstrByte(9, b);
}

void FileUpgradeV1(void)
{
    FileForEachInstrType(2, FileUpgradeV1Kit);
}

/* Version 2 to 3: every table envelope byte ($B690-$B88F) whose low
   nibble is neither 0 nor F gains one. */
void FileUpgradeV2(void)
{
    UWORD p;
    UBYTE v, lo;

    for (p = 0xB690; p != (UWORD)0xB890; p++) {
        v = ReadSongByte(0, p);
        lo = v & 0x0F;
        if (lo && lo != 0x0F)
            WriteSongByte(0, p, v + 1);
    }
}

/* Version 3 to 4, pulse and wave instruments: byte 5's bits 2-1 move on.
   2 sets bit 4; 4 clears them and sets bit 4; 6 leaves 2 in them and sets
   bit 4. The `(char)` reads are the ROM's `ld b,c; ld a,b` copies, and the
   `& 0xF9` stays in a for the `|` (peep.py's chained_bitwise). */
void FileUpgradeV3Pulse(void)
{
    UBYTE v;

    v = FileReadInstrByte(5);
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
    FileWriteInstrByte(5, v);
}

/* The same for kits: 2 sets bit 4, 4 sets bit 7, then bits 2-1 are cleared
   and bit 0 set. */
void FileUpgradeV3Kit(void)
{
    UBYTE v;

    v = FileReadInstrByte(5);
    switch ((char)v & 6) {
    case 2:
        v |= 0x10;
        break;
    case 4:
        v |= 0x80;
        break;
    }
    v = ((char)v & 0xF9) | 1;
    FileWriteInstrByte(5, v);
}

void FileUpgradeV3(void)
{
    FileForEachInstrType(0, FileUpgradeV3Pulse);
    FileForEachInstrType(1, FileUpgradeV3Pulse);
    FileForEachInstrType(2, FileUpgradeV3Kit);
}

void FileUpgradeV4(void)
{
    UWORD p;
    UBYTE v;

    for (p = 0xBEB4; p != (UWORD)0xBFB4; p += 0x10) {
        v = ReadSongByte(1, p);
        v |= v << 4;
        WriteSongByte(1, p, v);
    }
}

void FileUpgradeV5Wave(void)
{
    UBYTE v;

    v = FileReadInstrByte(14);
    FileWriteInstrByte(10, v >> 4);
    FileWriteInstrByte(11, v & 0x0F);
}

void FileUpgradeV5(void)
{
    FileForEachInstrType(1, FileUpgradeV5Wave);
}

void FileUpgradeV6Wave(void)
{
    UBYTE v;

    v = FileReadInstrByte(10);
    FileWriteInstrByte(10, 0x0F - v);
    v = FileReadInstrByte(11);
    FileWriteInstrByte(11, v - 3);
}

void FileUpgradeV6(void)
{
    FileForEachInstrType(1, FileUpgradeV6Wave);
}

void FileUpgradeV7(void)
{
    UBYTE *p;
    UBYTE v;

    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (p = (UBYTE *)0xB680; p != (UBYTE *)0xB880; p++) {
        v = *p;
        if (v > 1)
            *p = v + 1;
    }
    for (p = (UBYTE *)0xBA80; p != (UBYTE *)0xBC80; p++)
        if (*p > 1)
            (*p)++;
    rRAMB = 2;
    for (p = (UBYTE *)0xA000; p != (UBYTE *)0xAFF0; p++)
        if (*p > 1)
            (*p)++;
    rRAMG = 0;
    _asm ei _endasm;
}

void FileUpgradeV8Wave(void)
{
    FileWriteInstrByte(2, 0x0F ^ FileReadInstrByte(2));
}

void FileUpgradeV8(void)
{
    FileForEachInstrType(1, FileUpgradeV8Wave);
}

void FileUpgradeV9Wave(void)
{
    FileWriteInstrByte(9, (FileReadInstrByte(9) + 1) & 3);
}

void FileUpgradeV9(void)
{
    FileForEachInstrType(1, FileUpgradeV9Wave);
}

void FileUpgradeV10Pulse(void)
{
    UBYTE v;

    v = FileReadInstrByte(1) & 0x0F;
    if (v > 8)
        v = 0xF0;
    else
        v = 0;
    FileWriteInstrByte(9, v);
}

void FileUpgradeV10(void)
{
    FileForEachInstrType(0, FileUpgradeV10Pulse);
    FileForEachInstrType(3, FileUpgradeV10Pulse);
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
 * each used chain's 16 transposes through bank 6's ChainReadTranspose /
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
    MemSet((UBYTE *)0xDA00, 0, 0x80);
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
                v = FarCallArg2(6, 0x56AA, i, j);
                v = v / 12;
                v <<= 4;
                FarCallArg3(6, 0x5655, i, j, v);
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
    UBYTE i;
    UWORD p;
    UWORD q;
    UBYTE v;
    UBYTE j;
    UBYTE n;

    i = 0;
    p = 0xA000;
    q = 0xB000;
    while (i != 0x40) {
        types[i] = ReadInstrByte(i, 0);
        i++;
    }
    while (q != 0xBFF0) {
        v = 0;
        j = 0;
        ReadSongBytesW(0, p, wSongReadBuf);
        ReadSongBytesW(3, q, wUsedTableMap);
        for (; j != 0x10; j = j + 1) {
            i = wUsedTableMap[j];
            if (i == 0x40) {
                v = 0;
            } else if (i < 0x40) {
                if (types[i] == 3)
                    v = ReadInstrByteS(i, 4);
                else
                    v = 0;
            }
            if (v) {
                n = wSongReadBuf[j];
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
        q += 0x10;
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
    FileWriteInstrByte(1, FileUpgradeRemapBits(FileReadInstrByte(1)));
    FileWriteInstrByte(9, FileUpgradeRemapBits(FileReadInstrByte(9)));
    FileWriteInstrByte(10, FileUpgradeRemapBits(FileReadInstrByte(10)));
}

void FileUpgradeV12(void)
{
    FileForEachInstrType(0, FileUpgradeV12Pulse);
    FileForEachInstrType(3, FileUpgradeV12Pulse);
}

void FileUpgradeV13(void)
{
    UBYTE *p;

    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    for (p = (UBYTE *)0xB680; p != (UBYTE *)0xB880; p++)
        if (*p == 0x0E)
            FileUpgradeRetrigRate(p + 0x200);
    for (p = (UBYTE *)0xBA80; p != (UBYTE *)0xBC80; p++)
        if (*p == 0x0E)
            FileUpgradeRetrigRate(p + 0x200);
    rRAMB = 2;
    for (p = (UBYTE *)0xA000; p != (UBYTE *)0xAFF0; p++)
        if (*p == 0x0E)
            FileUpgradeRetrigRate(p + 0xFF0);
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
    FileWriteInstrByte(1, FileUpgradeClampNibble(FileReadInstrByte(1)));
    FileWriteInstrByte(9, FileUpgradeClampNibble(FileReadInstrByte(9)));
    FileWriteInstrByte(10, FileUpgradeClampNibble(FileReadInstrByte(10)));
}

void FileUpgradeV14(void)
{
    FileForEachInstrType(0, FileUpgradeV14Pulse);
    FileForEachInstrType(3, FileUpgradeV14Pulse);
}

void FileUpgradeV15Wave(void)
{
    FileWriteInstrByte(3, FileReadInstrByte(2) & 0xF0);
}

void FileUpgradeV15(void)
{
    FileForEachInstrType(1, FileUpgradeV15Wave);
}

void FileUpgradeV16(void)
{
    UWORD p;
    UBYTE v;
    UBYTE *q;

    p = 0xB690;
    _asm di _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    while (p != (UWORD)0xB890) {
        v = p;
        if ((v & 0x0F) != 0 && (*(UBYTE *)p & 0x0F) == 0x0F) {
            p--;
            q = (UBYTE *)p;
            v = *q;
            if ((v & 0x0F) < 0x0E)
                *q = v + 1;
            p++;
        }
        p++;
    }
    rRAMG = 0;
    _asm ei _endasm;
}

void FileUpgradeV17(void)
{
    UWORD p;
    UBYTE v;

    for (p = 0xBEBF; p != (UWORD)0xBFBF; p += 0x10) {
        v = ReadSongByte(1, p);
        WriteSongByte(1, p, -(v >> 4));
        WriteSongByte(1, p + 1, -(v & 0x0F));
    }
}

void FileUpgradeV17bPulse(void)
{
    FileWriteInstrByte(11, (FileReadInstrByte(7) << 1) & 0x78);
}

void FileUpgradeV17b(void)
{
    FileForEachInstrType(0, FileUpgradeV17bPulse);
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
    UBYTE i;
    UWORD p;
    UWORD q;
    UBYTE v;
    UBYTE j;
    UBYTE n;
    UBYTE m;

    i = 0;
    p = 0xA000;
    q = 0xB000;
    while (i != 0x40) {
        types[i] = ReadInstrByte(i, 0);
        i++;
    }
    while (q != 0xBFF0) {
        v = 0;
        j = 0;
        ReadSongBytesW(0, p, wSongReadBuf);
        ReadSongBytesW(3, q, wUsedTableMap);
        for (; j != 0x10; j = j + 1) {
            i = wUsedTableMap[j];
            if (i == 0x40) {
                v = 0;
            } else if (i < 0x40) {
                v = types[i] == 3;
            }
            if (v) {
                n = wSongReadBuf[j];
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
        q += 0x10;
        p += 0x10;
    }
}

void FileUpgradeV19(void)
{
    UWORD p, q;

    p = 0xBDD0;
    q = 0xBDD0;
    while (q != (UWORD)0xBDFA) {
        WriteSongByte(0, q++, ReadSongByte(0, p++));
        WriteSongByte(0, q++, ReadSongByte(0, p++));
        WriteSongByte(0, q++, ReadSongByte(0, p++));
        p++;
    }
}

void FileUpgradeV21(void)
{
    UBYTE v;
    UWORD q, p;

    for (p = 0xBEB2; p != (UWORD)0xBFB2; p += 16) {
        q = p + 4;
        v = ReadSongByte(1, q);
        if (v)
            WriteSongByte(1, q, 3);
    }
}

void FileUpgradeSong(void)
{
    UBYTE v;

    v = ReadSongByte(3, 0xBFFF);
    if (!(v < 0x16))
        return;
    wVramCursor = 0x98E2;
    DrawString("UPGRADING...   ");
    while (v < 0x16) {
        switch (v) {
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
        v++;
    }
    WriteSongByte(3, 0xBFFF, 0x16);
}
