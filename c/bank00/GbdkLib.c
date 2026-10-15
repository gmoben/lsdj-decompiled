/*
 * GBDK 2.95 library code -- LSDj 9.4.0, ROM bank $00.
 *
 * MATCHING C, but not LSDj's: these routines come from GBDK's own library
 * build, compiled with SDCC 2.2.1's stock peephole rules only (strlen keeps
 * the `or a,a` after `or a,(hl)` that LSDj's rules drop). The marker
 * @stock-rules makes the harness skip lsdj.peep and the tracking passes for
 * this file; its compiler's word compares were LSDj's, so gb_cmp16 still
 * applies (abs's `num < 0` is `ld hl,#0x8000; add hl,de`). The sources are
 * gbdk-2.95-2's gbdk-lib/libc under the ROM's names; printf.c's number and
 * format printers call the console's putchar and puts directly, where
 * 2.95-2 passes an emitter.
 */

#define GBDKLIB_C           /* printf takes `...` here */
#include "lsdj.h"

/* gbdk-lib/include/asm/gbz80/stdarg.h */
typedef char * va_list;
#define va_start(list, last)	list = (char *)&last + sizeof(last)
#define va_arg(list, type)	*((type *)((list += sizeof(type)) - sizeof(type)))
#define va_end(list)

/* libc/abs.c */
int abs(int number) nonbanked
{
  if(number < 0)
    return -number;
  else
    return number;
}

/* libc/printf.c's _printn: u in `base`, a minus first when issigned and u
   is negative as an int, the leading digits by recursion */
void printn(unsigned u, unsigned base, char issigned) nonbanked
{
    if (issigned && ((int)u < 0)) {
	DrawStringIndexed('-');
	u = (unsigned)-((int)u);
    }
    if (u >= base)
	printn(u/base, base, 0);
    DrawStringIndexed("0123456789ABCDEF"[u%base]);
}

/* libc/printf.c's _printf: %c, %u, %d, %x and %s */
void printf(char *format, ...) nonbanked
{
    va_list va;
    va_start(va, format);

    while (*format) {
	if (*format == '%') {
	    switch (*++format) {
	    case 'c': {
		char c = va_arg(va, char);
		DrawStringIndexed(c);
		break;
	    }
	    case 'u':
		{
		    unsigned u = va_arg(va, unsigned);
		    printn(u, 10, 0);
		    break;
		}
	    case 'd':
		{
		    unsigned u = va_arg(va, unsigned);
		    printn(u, 10, 1);
		    break;
		}
	    case 'x':
		{
		    unsigned u = va_arg(va, unsigned);
		    printn(u, 16, 0);
		    break;
		}
	    case 's':
		{
		    char *s = va_arg(va, char *);
		    DrawString(s);
		    break;
		}
	    }
	}
	else {
	    DrawStringIndexed(*format);
	}
	format++;
    }
}

/* libc/strlen.c: Return length of string */
int StringLength(const char *s) nonbanked
{
    int i;

    i = 0;
    while(*s++)
	i++;
    return i;
}
