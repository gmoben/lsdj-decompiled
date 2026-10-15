/*
 * The crash screen -- LSDj 9.4.0, ROM bank $01.
 *
 * The crash entry and the DONT PANIC screen with the bank, the stack
 * pointer and the stack.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

void ClearBgMapAndAttributes(void);
extern volatile sfr UBYTE LCDC_REG;         /* $FF40 */

/* $794B: the crash entry: interrupts off, SRAM locked, then the stack dump
   and the crash screen */

void SaveCrashStack(void);                  /* asm: saves sp and 20 stack bytes, then DrawCrashScreen */

/* $794B: crash entry: di, SRAM locked (rRAMG = 0), SaveCrashStack */
void CrashHandler(void)
{
    _asm di _endasm;
    rRAMG = 0;
    SaveCrashStack();
}

/* $7955: the crash screen, reached from SaveCrashStack: "DONT PANIC!", the
   ROM bank, the stack pointer and 10 words from the stack, the play mode,
   the screen, the hardware, the sync mode and whether an assertion fired
   (AST), then the version; then it hangs */

extern volatile sfr UBYTE SCY_REG;          /* $FF42 */
extern volatile sfr UBYTE SCX_REG;          /* $FF43 */
extern UWORD wCrashSP;                      /* $C7C4 */
extern UBYTE wCrashStack[];                 /* $C7B0, [20] */
extern UBYTE wBootAgbId;                    /* $CBDA */
extern UBYTE wDebugTrapped;                 /* $C7AF */
extern char TxtVersion[];                   /* $01:$6F8E, "V9.4.0  (c) ..." */
void DebugNop(void);                        /* bank 0 */

/* $7955: the crash screen: "DONT PANIC!", the bank, the stack pointer and 10
   stack words, play mode, screen, hardware, sync, AST, the version; then
   hangs */
void DrawCrashScreen(void)
{
    int entry;

    ClearBgTilemap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    LCDC_REG &= ~2;                 /* sprites off */
    wVramCursor = (UBYTE *)BG_MAP(0, 0);
    DrawString("DONT PANIC!");
    wVramCursor = (UBYTE *)BG_MAP(0, 2);
    DrawString("PLEASE MAIL THIS");
    wVramCursor = (UBYTE *)BG_MAP(0, 3);
    DrawString("DATA TO INFO AT");
    wVramCursor = (UBYTE *)BG_MAP(0, 4);
    DrawString("LITTLESOUNDDJ.COM");
    wVramCursor = (UBYTE *)BG_MAP(1, 6);
    DrawString("ROM:");
    DrawHexByte(hCurrentBank);
    wVramCursor = (UBYTE *)BG_MAP(7, 8);
    DrawString("STK:");
    DrawHexByte(wCrashSP >> 8);
    DrawHexByte(wCrashSP);
    wVramCursor = (UBYTE *)BG_MAP(7, 11);
    DrawString("PLY:");
    DrawByte(wPlayMode);
    wVramCursor = (UBYTE *)BG_MAP(7, 12);
    DrawString("CVW:");
    DrawByte(wCurrentScreen);
    if (hGameboyType) {
        wVramCursor = (UBYTE *)BG_MAP(7, 13);
        DrawString("CGB");
    }
    if (wBootAgbId) {
        wVramCursor = (UBYTE *)BG_MAP(7, 14);
        DrawString("GBA");
    }
    wVramCursor = (UBYTE *)BG_MAP(7, 16);
    DrawString("SYN:");
    DrawByte(wSyncMode);
    if (wDebugTrapped) {
        wVramCursor = (UBYTE *)BG_MAP(7, 17);
        DrawString("AST");
    }
    for (entry = 0; entry < 10; entry++) {
        SetVramCursor(1, entry + 8);
        DrawHexByte(wCrashStack[(entry << 1) + 1]);
        DrawHexByte(wCrashStack[entry << 1]);
    }
    wVramCursor = (UBYTE *)BG_MAP(9, 6);
    DrawStringIndexed(TxtVersion[0]);
    DrawStringIndexed(TxtVersion[1]);
    DrawStringIndexed(TxtVersion[2]);
    DrawStringIndexed(TxtVersion[3]);
    DrawStringIndexed(TxtVersion[4]);
    DrawStringIndexed(TxtVersion[5]);
    SCY_REG = 0;
    SCX_REG = 0;
    DebugNop();
    ClearBgMapAndAttributes();
    while (1)
        ;
}
