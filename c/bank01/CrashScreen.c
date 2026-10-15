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

void DrawCrashScreen(void)
{
    int entry;

    ClearBgTilemap();
    hBgDigitPalette = 4;
    hBgPalette = 1;
    LCDC_REG &= ~2;                 /* sprites off */
    wVramCursor = (UBYTE *)0x9800;
    DrawString("DONT PANIC!");
    wVramCursor = (UBYTE *)0x9840;
    DrawString("PLEASE MAIL THIS");
    wVramCursor = (UBYTE *)0x9860;
    DrawString("DATA TO INFO AT");
    wVramCursor = (UBYTE *)0x9880;
    DrawString("LITTLESOUNDDJ.COM");
    wVramCursor = (UBYTE *)0x98C1;
    DrawString("ROM:");
    DrawHexByte(hCurrentBank);
    wVramCursor = (UBYTE *)0x9907;
    DrawString("STK:");
    DrawHexByte(wCrashSP >> 8);
    DrawHexByte(wCrashSP);
    wVramCursor = (UBYTE *)0x9967;
    DrawString("PLY:");
    DrawByte(wPlayMode);
    wVramCursor = (UBYTE *)0x9987;
    DrawString("CVW:");
    DrawByte(wCurrentScreen);
    if (hGameboyType) {
        wVramCursor = (UBYTE *)0x99A7;
        DrawString("CGB");
    }
    if (wBootAgbId) {
        wVramCursor = (UBYTE *)0x99C7;
        DrawString("GBA");
    }
    wVramCursor = (UBYTE *)0x9A07;
    DrawString("SYN:");
    DrawByte(wSyncMode);
    if (wDebugTrapped) {
        wVramCursor = (UBYTE *)0x9A27;
        DrawString("AST");
    }
    for (entry = 0; entry < 10; entry++) {
        SetVramCursor(1, entry + 8);
        DrawHexByte(wCrashStack[(entry << 1) + 1]);
        DrawHexByte(wCrashStack[entry << 1]);
    }
    wVramCursor = (UBYTE *)0x98C9;
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
