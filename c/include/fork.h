/*
 * For a fork's new C, compiled with a modern SDCC (a file that says @sdcc4:
 * tools/match/sdcc4.py, docs/forking.md): the types, and the names of
 * LSDj's it is likely to use. LSDj's own C uses lsdj.h, in SDCC 2.2.1's
 * dialect.
 *
 * Every name here is an RGBDS symbol of the build (include/hardware.inc,
 * constants/ram.inc, src/bankNN.sym): the C refers to it, the linker puts
 * the address. A new variable goes in constants/ram.inc; declare it here
 * or in the file, `extern`.
 */

#ifndef FORK_H
#define FORK_H

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned int UWORD;
typedef signed int WORD;

/* hardware registers, include/hardware.inc's names (plain memory: SDCC 4
   loads them with `ld a, [rNR51]`) */
extern volatile UBYTE rNR50;                /* master volume, left and right */
extern volatile UBYTE rNR51;                /* each channel's left and right */
extern volatile UBYTE rNR52;                /* the APU's and the channels' on bits */

/* the sound engine's: the channel it is on and, as a command's handler
   runs, the command's value */
extern volatile UBYTE hSoundChannel;        /* 0 PU1, 1 PU2, 2 WAV, 3 NOI */
extern volatile UBYTE hCommandValue;

#endif
