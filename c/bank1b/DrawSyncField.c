/*
 * DrawSyncField -- LSDj 9.4.0, ROM bank $1B:$58B1-$59B6 (262 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank1b/DrawSyncField.c --target DrawSyncField
 *
 * The Project screen's SYNC row: the mode name at tilemap $9885, then at $98A0
 * either the sub-setting label ("PS/2 DELAY " for KEYBD, "TICKS/STEP " for the
 * analog modes) followed by wSyncMode2 in hex, or a blank; on CGB the
 * attribute row is set with ColorField.
 *
 * Exercises: a sparse `switch` (SDCC emits a `cp` chain, `or a,a` for case 0,
 * and LSDj's compiler loads the operand once), plain-symbol string arguments
 * (a `sym + offset` argument would go through bc, so the six names are six
 * symbols, not one table),
 * if/else with the else placed after the if-body, a pointer local chosen by a
 * condition, and 4-byte-argument calls. The second half of this function was
 * labelled as a separate routine (`DrawSyncModeField`) in the symbol map until
 * this match showed the `push bc` there to be an argument push.
 */

#include "lsdj.h"

extern char TxtSyncOff[];                   /* $1B:$59B7 "OFF   " -- six 7-byte strings, each its own symbol */
extern char TxtSyncLsdj[];                  /* $1B:$59BE */
extern char TxtSyncMidi[];                  /* $1B:$59C5 */
extern char TxtSyncKeybd[];                 /* $1B:$59CC */
extern char TxtSyncAnaIn[];                 /* $1B:$59D3 */
extern char TxtSyncAnOut[];                 /* $1B:$59DA */
extern char TxtSyncPs2[];                   /* $1B:$59E1 "PS/2 DELAY " */
extern char TxtSyncTicksStep[];             /* $1B:$59ED "TICKS/STEP " */
extern char TxtBlank13[];                   /* $1B:$59F9 */

void DrawSyncField(void)
{
    char *s;

    wVramCursor = (UBYTE *)0x9885;
    switch (wSyncMode) {
    case 0: DrawString(TxtSyncOff); break;
    case 1: DrawString(TxtSyncLsdj); break;
    case 3: DrawString(TxtSyncMidi); break;
    case 5: DrawString(TxtSyncKeybd); break;
    case 6: DrawString(TxtSyncAnaIn); break;
    case 7: DrawString(TxtSyncAnOut); break;
    }
    wVramCursor = (UBYTE *)0x98A0;
    hBgDigitPal = 4;
    hBgPal = 1;
    if (ProjSyncModeIs567()) {
        if (wSyncMode == 5)
            s = TxtSyncPs2;
        else
            s = TxtSyncTicksStep;
        DrawString(s);
        hBgDigitPal = wShadedDigitPal;
        hBgPal = wShadedPal;
        DrawHexByte(wSyncMode2);
        if (hGameboyType)
            ColorField(0x0B, 5, 2, 1);
    } else {
        DrawString(TxtBlank13);
        if (hGameboyType)
            ColorField(0x0B, 5, 2, 0);
    }
}
