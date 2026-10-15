/*
 * DrawChanStatus -- LSDj 9.4.0, ROM bank $1F:$6F14-$7057 (324 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank1f/DrawChanStatus.c --target DrawChanStatus
 *
 * The song screen's channel status cell at (16, ch + 6): MUTE / L / R while
 * the channel is muted or panned, else the channel letter, with blanks drawn
 * once after a MUTE went away. The two CGB attribute rows are `const` data
 * and sit right behind the function in the ROM, so this is its own unit
 * (SDCC 2.2.1 emits a unit's const data after its first function).
 */

#include "lsdj.h"

extern UBYTE wChanOutput[];                /* $C0A0, [4] */
extern UBYTE wChanOutputOverride[];                /* $C0A4, [4] */
extern UBYTE wChanMuteShown[];              /* $C9CE, [4] */
extern UBYTE *wChanMuteShownPtr;            /* $C9D2 */
int StrLength(const char *s);

const UBYTE ChanStatusAttrSel[4] = {3, 3, 3, 3};
const UBYTE ChanStatusAttrNorm[4] = {0, 1, 1, 1};

void DrawChanStatus(UBYTE ch)
{
    UBYTE y;
    char *s;
    UBYTE c;

    y = ch + 6;
    GetTilemapAddr(0x10, y);
    c = ch;
    if (wChanOutput[c] & 0x80) {
        s = "MUTE";
        hBgDigitPal = wSelectDigitPal;
        hBgPal = wSelectPal;
        switch (wChanOutputOverride[c]) {
        case 0:
            wChanMuteShown[c] = 1;
            break;
        case 1:
            s = "L";
            break;
        case 2:
            s = "R";
            break;
        }
        DrawString(s);
        if (hGameboyType) {
            VBK_REG = 1;
            set_bkg_tiles(0x10, y, StrLength(s), 1, ChanStatusAttrSel);
            VBK_REG = 0;
        }
    } else {
        hBgDigitPal = 4;
        hBgPal = 1;
        DrawStringIndexed("12WN"[c]);
        wChanMuteShownPtr = &wChanMuteShown[c];
        if (*wChanMuteShownPtr) {
            *wChanMuteShownPtr = 0;
            hBgDigitPal = wShadowDigitPal;
            hBgPal = wShadowPal;
            DrawString("   ");
        }
        if (hGameboyType) {
            VBK_REG = 1;
            set_bkg_tiles(0x10, y, 4, 1, ChanStatusAttrNorm);
            VBK_REG = 0;
        }
    }
}

void DrawAllChanStatus(void)
{
    DrawChanStatus(0);
    DrawChanStatus(1);
    DrawChanStatus(2);
    DrawChanStatus(3);
}
