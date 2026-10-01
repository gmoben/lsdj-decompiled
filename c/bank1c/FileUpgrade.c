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
void FileCheckByte6973(UBYTE *p);          /* $1C:$6973 */

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
            FileCheckByte6973(p + 0x200);
    for (p = (UBYTE *)0xBA80; p != (UBYTE *)0xBC80; p++)
        if (*p == 0x0E)
            FileCheckByte6973(p + 0x200);
    rRAMB = 2;
    for (p = (UBYTE *)0xA000; p != (UBYTE *)0xAFF0; p++)
        if (*p == 0x0E)
            FileCheckByte6973(p + 0xFF0);
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
