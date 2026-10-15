/* Bank 0: the far calls that pass arguments or return the callee's byte.
   Each saves the current ROM bank, switches to the callee's, calls it
   through a function pointer cast from the UWORD address (the cast's copy
   is the `ld b,(hl); inc hl; ld c,(hl)`; SDCC's call through a pointer
   pushes the return label and jumps with `jp (hl)`), then switches back.
   The callee's byte comes back in e, untouched by the bank restore.
   FarCall and FarCallArg are hand-written. */

#define FARCALL_C       /* FarCallByte and FarCallArg2V take a UWORD here */
#include "lsdj.h"

/* $0BFE: call fn in ROM bank `bank`, returning its byte */

UBYTE FarCallByte(UBYTE bank, UWORD fn)
{
    UBYTE old, r;

    old = hCurrentBank;
    SWITCH_ROM(bank);
    r = ((UBYTE (*)(void))fn)();
    SWITCH_ROM(old);
    return r;
}

/* $0C29: call fn(arg) in ROM bank `bank`, returning its byte */

UBYTE FarCallArgByte(UBYTE bank, UWORD fn, UBYTE arg)
{
    UBYTE old, r;

    old = hCurrentBank;
    SWITCH_ROM(bank);
    r = ((UBYTE (*)(UBYTE))fn)(arg);
    SWITCH_ROM(old);
    return r;
}

/* $0C5A: call fn(a, b) in ROM bank `bank` */

void FarCallArg2V(UBYTE bank, UWORD fn, UBYTE a, UBYTE b)
{
    UBYTE old;

    old = hCurrentBank;
    SWITCH_ROM(bank);
    ((void (*)(UBYTE, UBYTE))fn)(a, b);
    SWITCH_ROM(old);
}

/* $0C8F: call fn(a, b) in ROM bank `bank`, returning its byte */

UBYTE FarCallArg2(UBYTE bank, UWORD fn, UBYTE a, UBYTE b)
{
    UBYTE old, r;

    old = hCurrentBank;
    SWITCH_ROM(bank);
    r = ((UBYTE (*)(UBYTE, UBYTE))fn)(a, b);
    SWITCH_ROM(old);
    return r;
}

/* $0CC5: call fn(a, b, c) in ROM bank `bank` */

void FarCallArg3(UBYTE bank, UWORD fn, UBYTE a, UBYTE b, UBYTE c)
{
    UBYTE old;

    old = hCurrentBank;
    SWITCH_ROM(bank);
    ((void (*)(UBYTE, UBYTE, UBYTE))fn)(a, b, c);
    SWITCH_ROM(old);
}
