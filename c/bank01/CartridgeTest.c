/*
 * The cartridge test -- LSDj 9.4.0, ROM bank $01.
 *
 * The boot CARTRIDGE TEST screen (the ROM checksum and the SRAM self-test),
 * the working song's SRAM format and the 'jk' marker check.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wColorSet;
UBYTE CheckRomChecksum(void);               /* $00:$1FD5 */
UBYTE TestSramLow(void);
void initrand(UWORD seed);              /* bank 0 */
UBYTE randw(void);                       /* bank 0, a 16-bit LCG; the SRAM test uses the low byte */
void ClearSramWindow(void);                 /* $01:$7DE9, asm: zero the SRAM window */
void ClearBookmarks(void);                  /* $07:$643D */
extern UBYTE DefaultWaveFrameB1[];          /* $01:$40EA, [16] */
UBYTE TestSram(void);                       /* bank 0 */

UBYTE TestSramLow(void)
{
    UBYTE bank;
    UBYTE * volatile p;                     /* volatile: the ROM keeps p on the stack */
    char done;
    UBYTE v;
    char ok;

    initrand(0x1234);
    rRAMG = 0x0A;
    for (bank = 0; bank < 4; bank++) {
        p = (UBYTE *)0xA000;
        rRAMB = bank;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = randw();
            *p = v;
            p = p + 1;
        }
    }
    initrand(0x1234);
    for (bank = 0; bank < 4; bank++) {
        p = (UBYTE *)0xA000;
        rRAMB = bank;
        for (;;) {
            done = p == (UBYTE *)0xC000;
            if (done)
                break;
            v = *p;
            ok = v == randw();
            if (!ok)
                return 0;
            p++;
        }
    }
    rRAMG = 0;
    return 1;
}

UBYTE TestSramHigh(void)
{
    UBYTE bank;

    rRAMG = 0x0A;
    for (bank = 4; bank < 0x10; bank++) {
        rRAMB = bank;
        if (TestSram())
            return 0;
    }
    rRAMG = 0;
    return 1;
}

void DrawCartTest(void)
{
    wVramCursor = (UBYTE *)BG_MAP(0, 1);
    DrawString("CARTRIDGE TEST");
    wVramCursor = (UBYTE *)BG_MAP(0, 3);
    DrawString("ROM...");
    if (FarCallByte(0, FAR_FN(CheckRomChecksum))) {
        DrawString("OK!");
    } else {
        DrawString("FAIL!");
        wVramCursor = (UBYTE *)BG_MAP(0, 4);
        DrawString("TRY CLEANING PINS");
        wVramCursor = (UBYTE *)BG_MAP(0, 5);
        DrawString("OR FLASH AGAIN.");
        for (;;)
            ;
    }
    wVramCursor = (UBYTE *)BG_MAP(0, 4);
    DrawString("SRAM...");
    if (TestSramLow() && TestSramHigh()) {
        DrawString("OK!");
    } else {
        DrawString("FAIL!");
        wVramCursor = (UBYTE *)BG_MAP(0, 5);
        DrawString("TRY CLEANING PINS");
        wVramCursor = (UBYTE *)BG_MAP(0, 6);
        DrawString("OR REPLACE BATTERY.");
        for (;;)
            ;
    }
}

/* $45D1: format the SRAM song: zero every bank, default the grooves (6/6),
   the sequence, the word names "W-0".."W-x" (digits then letters), the
   instrument names, the synth parameters, the wave frames and the
   properties, with the 'rb' markers and $BFFF = $16. p is a plain
   pointer: it owns bc in the allocator's books and is the operand of
   its own copies and reads, which therefore spill themselves (the word
   loop's `*p += ...`); s2 is a volatile byte so the third saved
   property byte is stored while the other two keep b and c. */
void FormatSramSong(void)
{
    UBYTE *p;
    UBYTE i;
    volatile UBYTE s2;
    UBYTE s0, s1;

    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 0;
    ClearSramWindow();
    FarCall(7, FAR_FN(ClearBookmarks));
    *(UBYTE *)0xBE78 = 'r';
    *(UBYTE *)0xBE79 = 'b';
    for (p = (UBYTE *)0xB090; p != (UBYTE *)0xB290; ) {
        *p = 6;
        p++;
        *p = 6;
        p += 0x0F;
    }
    FillMemory((UBYTE *)0xB290, 0xFF, 0x400);
    p = (UBYTE *)0xBDD0;
    i = 0;
    do {
        *p = 'W';
        p++;
        *p = '-';
        p++;
        *p = i;
        *p += i < 10 ? '0' : '7';
        p++;
        i++;
    } while (p != (UBYTE *)0xBDFA);
    rRAMB = 1;
    s0 = *(UBYTE *)0xBFB6;
    s1 = *(UBYTE *)0xBFB7;
    s2 = *(UBYTE *)0xBFB8;
    ClearSramWindow();
    FillMemory((UBYTE *)0xBFC6, 0xFF, 4);
    *(UBYTE *)0xBFB6 = s0;
    *(UBYTE *)0xBFB7 = s1;
    *(UBYTE *)0xBFB8 = s2;
    *(UBYTE *)0xBFC3 = 1;
    rRAMB = 3;
    ClearSramWindow();
    FillMemory((UBYTE *)0xB000, 0xFF, 0xFF0);
    rRAMB = 2;
    ClearSramWindow();
    _asm
        di
    _endasm;
    rRAMG = 0x0A;
    rRAMB = 1;
    *(UBYTE *)0xBE80 = 'r';
    *(UBYTE *)0xBE81 = 'b';
    *(UBYTE *)0xBFB4 = 0x80;
    *(UBYTE *)0xBFBA = 7;
    *(UBYTE *)0xBFBB = 2;
    wColorSet = 0;
    FillMemory((UBYTE *)0xA080, 0xFF, 0x800);
    for (p = (UBYTE *)0xBEB2; p != (UBYTE *)0xBFB2; p += 0x10) {
        p[5] = 0x10;
        p[9] = 0x10;
        p[6] = 0xFF;
        p[10] = 0xFF;
    }
    rRAMB = 3;
    *(UBYTE *)0xBFF0 = 'r';
    *(UBYTE *)0xBFF1 = 'b';
    for (p = (UBYTE *)0xA000; p < (UBYTE *)0xB000; p += 0x10)
        memcpy((void *)p, DefaultWaveFrameB1, 0x10);
    *(UBYTE *)0xBFFF = 0x16;
    _asm
        ei
    _endasm;
    rRAMG = 0;
}

UBYTE CheckJkMarker(void)
{
    if (ReadSongByte(4, (UBYTE *)0xA13E) != 'j')
        return 0;
    if (ReadSongByte(4, (UBYTE *)0xA13F) != 'k')
        return 0;
    return 1;
}
