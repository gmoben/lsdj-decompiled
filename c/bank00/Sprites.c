/*
 * The sprite helpers -- LSDj 9.4.0, ROM bank $00:$30DD-$3260.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Everything writes the OAM shadow at $DD00 (wOamBuf, DMA-copied while
 * wOamDirty is set). The clears are literal-pointer stores (`ld de,#addr;
 * ld (de),a`; track_a drops the repeated `ld a,#0x00`). SetOamByte adds the
 * shadow's address to its cast offset, `(UBYTE *)offset + 0xDD00`, which
 * SDCC does in place in bc with the constant second, as the ROM does:
 * `wOamBuf + offset` adds the address first, and `offset + 0xDD00` on the
 * stack operand goes through `add hl,de`. Its test is `v == *p` (`*p == v`
 * is SDCC's `cp a,a` miscompile).
 */

#include "lsdj.h"

extern UBYTE wOamDirty;                     /* $CBD1 */

void ClearSprites012(void)
{
    *(UBYTE *)0xDD00 = 0;
    *(UBYTE *)0xDD04 = 0;
    *(UBYTE *)0xDD08 = 0;
}

void ClearMarkerSprites(void)
{
    *(UBYTE *)0xDD0C = 0;
    *(UBYTE *)0xDD10 = 0;
    *(UBYTE *)0xDD14 = 0;
    *(UBYTE *)0xDD18 = 0;
    *(UBYTE *)0xDD1C = 0;
    *(UBYTE *)0xDD20 = 0;
    *(UBYTE *)0xDD24 = 0;
    *(UBYTE *)0xDD28 = 0;
    *(UBYTE *)0xDD2D = 0;
    *(UBYTE *)0xDD31 = 0;
    *(UBYTE *)0xDD35 = 0;
    *(UBYTE *)0xDD39 = 0;
    *(UBYTE *)0xDD3D = 0;
    *(UBYTE *)0xDD41 = 0;
    *(UBYTE *)0xDD45 = 0;
    *(UBYTE *)0xDD49 = 0;
    *(UBYTE *)0xDD0F = 0;
    *(UBYTE *)0xDD13 = 0;
    *(UBYTE *)0xDD17 = 0;
    *(UBYTE *)0xDD1B = 0;
    *(UBYTE *)0xDD1F = 0;
    *(UBYTE *)0xDD23 = 0;
    *(UBYTE *)0xDD27 = 0;
    *(UBYTE *)0xDD2B = 0;
    wOamDirty = 1;
}

void SetOamByte(UWORD offset, UBYTE v)
{
    UBYTE *p;

    p = (UBYTE *)offset + 0xDD00;             /* wOamBuf */
    if (v == *p)
        return;
    *p = v;
    wOamDirty = 1;
}

void PositionWavSprites(void)
{
    SetOamByte(0, 0x68);
    SetOamByte(4, 0x68);
    SetOamByte(8, 0x68);
}

void ClearSpriteY(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0C, 0);
}

void SetSpriteX(UBYTE sprite, UBYTE col)
{
    SetOamByte(sprite * 4 + 0x0D, (col << 3) + 8);
}

void SetSpriteY(UBYTE sprite, UBYTE row)
{
    if (row > 0x0F)
        row = 0xFC;
    SetOamByte(sprite * 4 + 0x0C, (row << 3) + 0x20);
}

void SetSpriteTile3(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0E, 3);
}

void SetSpriteTile4(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0E, 4);
}

void SetSpriteAttr(UBYTE sprite, UBYTE attr)
{
    SetOamByte(sprite * 4 + 0x0F, attr);
}
