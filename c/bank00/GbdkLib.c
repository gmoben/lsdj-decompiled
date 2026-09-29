/*
 * GBDK 2.95 library code -- LSDj 9.4.0, ROM bank $00.
 *
 * MATCHING C, but not LSDj's: these routines come from GBDK's own library
 * build, compiled with SDCC 2.2.1's stock peephole rules only (strlen keeps
 * the `or a,a` after `or a,(hl)` that LSDj's rules drop). The marker
 * @stock-rules makes the harness skip lsdj.peep and the tracking passes for
 * this file. The sources are gbdk-2.95-2's gbdk-lib/libc under the ROM's
 * names.
 */

#include "lsdj.h"

/* libc/strlen.c: Return length of string */
int StringLength(const char *s) nonbanked
{
    int i;

    i = 0;
    while(*s++)
	i++;
    return i;
}
