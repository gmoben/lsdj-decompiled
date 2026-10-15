/*
 * The sprite helpers -- LSDj 9.4.0, ROM bank $00:$30DD-$3260.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 *
 * Everything writes the OAM shadow at $DD00 (wOamBuffer, DMA-copied while
 * wOamDirty is set). The clears store to OamShadow's fields, which SDCC
 * folds to literal addresses (`ld de,#addr; ld (de),a`; track_a drops the
 * repeated `ld a,#0x00`). SetOamByte adds the
 * shadow's address to its cast offset, `(UBYTE *)offset + 0xDD00`, which
 * SDCC does in place in bc with the constant second, as the ROM does:
 * `wOamBuffer + offset` adds the address first, and `offset + 0xDD00` on the
 * stack operand goes through `add hl,de`. Its test is `v == *p` (`*p == v`
 * is SDCC's `cp a,a` miscompile).
 */

#include "lsdj.h"

extern UBYTE wOamDirty;                     /* $CBD1 */

/* $30DD: zero OAM Y of sprites 0,1,2 (wOamBuffer +0/+4/+8) */
void ClearSprites012(void)
{
    OamShadow[0].y = 0;
    OamShadow[1].y = 0;
    OamShadow[2].y = 0;
}

/* $30EC: hide the marker sprites 3-10 (Y and attr = 0) and the cursor
   sprites 11-18 (X = 0); set wOamDirty */
void ClearMarkerSprites(void)
{
    /* the eight marks are sprites 3-10, the eight cursor sprites 11-18 */
    OamShadow[3].y = 0;
    OamShadow[4].y = 0;
    OamShadow[5].y = 0;
    OamShadow[6].y = 0;
    OamShadow[7].y = 0;
    OamShadow[8].y = 0;
    OamShadow[9].y = 0;
    OamShadow[10].y = 0;
    OamShadow[11].x = 0;
    OamShadow[12].x = 0;
    OamShadow[13].x = 0;
    OamShadow[14].x = 0;
    OamShadow[15].x = 0;
    OamShadow[16].x = 0;
    OamShadow[17].x = 0;
    OamShadow[18].x = 0;
    OamShadow[3].prop = 0;
    OamShadow[4].prop = 0;
    OamShadow[5].prop = 0;
    OamShadow[6].prop = 0;
    OamShadow[7].prop = 0;
    OamShadow[8].prop = 0;
    OamShadow[9].prop = 0;
    OamShadow[10].prop = 0;
    wOamDirty = 1;
}

/* $3154: write `v` to byte `offset` of the OAM buffer (wOamBuffer) if it
   differs, and mark it for the next frame's OAM DMA (wOamDirty) */
void SetOamByte(UWORD offset, UBYTE v)
{
    UBYTE *p;

    p = (UBYTE *)offset + 0xDD00;             /* wOamBuffer */
    if (v == *p)
        return;
    *p = v;
    wOamDirty = 1;
}

/* $3176: set OAM Y=$68 for sprites 0,1,2 -- the WAV channel's on-screen wave
   display (right edge, tiles $00-$02); only when the WAV channel is active
   (ResetScreenSprites) */
void PositionWavSprites(void)
{
    SetOamByte(0, 0x68);
    SetOamByte(4, 0x68);
    SetOamByte(8, 0x68);
}

/* $319E: hide `sprite`: its OAM Y = 0 */
void ClearSpriteY(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0C, 0);
}

/* $31B5: put `sprite` at tile column `column`: OAM X = column * 8 + 8 */
void SetSpriteX(UBYTE sprite, UBYTE column)
{
    SetOamByte(sprite * 4 + 0x0D, (column << 3) + 8);
}

/* $31E3: put `sprite` at tile row `row`: OAM Y = row * 8 + $20, off the
   screen ($FC) past row 15 */
void SetSpriteY(UBYTE sprite, UBYTE row)
{
    if (row > 0x0F)
        row = 0xFC;
    SetOamByte(sprite * 4 + 0x0C, (row << 3) + 0x20);
}

/* $321C: `sprite` shows tile 3 */
void SetSpriteTile3(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0E, 3);
}

/* $3233: `sprite` shows tile 4 */
void SetSpriteTile4(UBYTE sprite)
{
    SetOamByte(sprite * 4 + 0x0E, 4);
}

/* $324A: set `sprite`'s OAM attribute byte to `attribute` */
void SetSpriteAttribute(UBYTE sprite, UBYTE attribute)
{
    SetOamByte(sprite * 4 + 0x0F, attribute);
}
