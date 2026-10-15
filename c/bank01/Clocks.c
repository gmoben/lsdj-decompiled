/*
 * The clocks -- LSDj 9.4.0, ROM bank $01.
 *
 * The PLAY clock and the WORKED time.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

extern UBYTE wPlayMinutes;                  /* $C552 */
extern UBYTE wPlaySeconds;                  /* $C553 */
extern UBYTE wPlayFrames;                   /* $C6A8 */
extern volatile UBYTE wProjectDrawScratch;  /* $C9A7; volatile: the ROM re-reads it for the `++` */

/* $4FEC: per-frame PLAY clock: wPlayFrames counts to 60 (one frame skipped
   every 4 s), then seconds/minutes; redraws m:ss on the project screen */
void UpdatePlayClock(void)
{
    if (!wPlayMode) {
        wPlayFrames = 0;
        return;
    }
    wPlayFrames++;
    if (wPlayFrames != 60)
        return;
    wPlaySeconds++;
    if (wPlaySeconds == 60) {
        wPlaySeconds = 0;
        wPlayMinutes++;
    }
    wPlayFrames = 0;
    if ((wPlaySeconds & 3) == 0)
        wPlayFrames = 1;
    if (wPlayMode && wCurrentScreen == SCREEN_PROJECT) {
        hBgDigitPalette = 4;
        hBgPalette = 1;
        wVramCursor = BG_MAP(5, 16);
        printf("%d:", (UWORD)wPlayMinutes);
        wProjectDrawScratch = 0x0C;
        if (wPlayMinutes > 9)
            wProjectDrawScratch++;
        if (wPlaySeconds < 10)
            DrawStringIndexed('0');
        printf("%d", (UWORD)wPlaySeconds);
    }
}

/* $50A0: apply one work-time minute: song WORKED h:mm ($BFB2/$BFB3) and the
   total d:hh:mm ($BFB6-$BFB8) in SRAM bank 1 */
void UpdateWorkClock(void)
{
    UBYTE v;

    v = ReadSongByte(1, SramPropertyWorkTime + 1);
    v++;
    if (v == 60) {
        WriteSongByte(1, SramPropertyWorkTime + 1, 0);
        v = ReadSongByte(1, SramPropertyWorkTime);
        v++;
        WriteSongByte(1, SramPropertyWorkTime, v);
    } else {
        WriteSongByte(1, SramPropertyWorkTime + 1, v);
    }
    v = ReadSongByte(1, SramPropertyTotalTime + 2);
    v++;
    if (v == 60) {
        WriteSongByte(1, SramPropertyTotalTime + 2, 0);
        v = ReadSongByte(1, SramPropertyTotalTime + 1);
        v++;
        if (v == 24) {
            WriteSongByte(1, SramPropertyTotalTime + 1, 0);
            v = ReadSongByte(1, SramPropertyTotalTime);
            v++;
            WriteSongByte(1, SramPropertyTotalTime, v);
        } else {
            WriteSongByte(1, SramPropertyTotalTime + 1, v);
        }
    } else {
        WriteSongByte(1, SramPropertyTotalTime + 2, v);
    }
}
