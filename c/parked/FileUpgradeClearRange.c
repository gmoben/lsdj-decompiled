#include "lsdj.h"

UBYTE FileUpgradeReadByte(UBYTE i);

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
