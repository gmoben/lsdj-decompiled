/*
 * DrawChannelStatus -- LSDj 9.4.0, ROM bank $1F:$6F14-$7057 (324 bytes)
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep). Verify:
 *
 *     tools/match/match_function.py --c c/bank1f/DrawChannelStatus.c --target DrawChannelStatus
 *
 * The song screen's channel status cell at (16, ch + 6): MUTE / L / R while
 * the channel is muted or panned, else the channel letter, with blanks drawn
 * once after a MUTE went away. The two CGB attribute rows are `const` data
 * and sit right behind the function in the ROM, so this is its own unit
 * (SDCC 2.2.1 emits a unit's const data after its first function).
 */

#include "lsdj.h"

extern UBYTE wChannelOutput[];              /* $C0A0, [4] */
extern UBYTE wChannelOutputOverride[];      /* $C0A4, [4] */
extern UBYTE wChannelMuteShown[];           /* $C9CE, [4] */
extern UBYTE *wChannelMuteShownPointer;     /* $C9D2 */
int StringLength(const char *s);

const UBYTE ChanStatusAttrSel[4] = {3, 3, 3, 3};
const UBYTE ChanStatusAttrNorm[4] = {0, 1, 1, 1};

/* $6F14: draw channel status cell at (16, ch+6) from wChannelOutput[ch] */
void DrawChannelStatus(UBYTE channel)
{
    UBYTE y;
    char *s;
    UBYTE c;

    y = channel + 6;
    SetVramCursor(0x10, y);
    c = channel;
    if (wChannelOutput[c] & 0x80) {
        s = "MUTE";
        hBgDigitPalette = wHighlightDigitPalette;
        hBgPalette = wHighlightPalette;
        switch (wChannelOutputOverride[c]) {
        case 0:
            wChannelMuteShown[c] = 1;
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
            set_bkg_tiles(0x10, y, StringLength(s), 1, ChanStatusAttrSel);
            VBK_REG = 0;
        }
    } else {
        hBgDigitPalette = 4;
        hBgPalette = 1;
        DrawStringIndexed("12WN"[c]);
        wChannelMuteShownPointer = &wChannelMuteShown[c];
        if (*wChannelMuteShownPointer) {
            *wChannelMuteShownPointer = 0;
            hBgDigitPalette = wShadedDigitPalette;
            hBgPalette = wShadedPalette;
            DrawString("   ");
        }
        if (hGameboyType) {
            VBK_REG = 1;
            set_bkg_tiles(0x10, y, 4, 1, ChanStatusAttrNorm);
            VBK_REG = 0;
        }
    }
}

/* $7072: draw the 4 channel status cells (DrawChannelStatus 0-3) */
void DrawAllChannelStatus(void)
{
    DrawChannelStatus(0);
    DrawChannelStatus(1);
    DrawChannelStatus(2);
    DrawChannelStatus(3);
}
