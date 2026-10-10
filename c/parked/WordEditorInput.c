/* Parked attempt for WordEditorInput ($06:$431D): 15 rows from the ROM, see c/parked.txt.
   Link with --define wWordClipPtrB=0xC939 ($C939 is a byte there and the clip pointer in the cut).
   Byte-identical under the debug compiler with BUSYC=1-93 (register c held by a phantom from the
   function's first instruction through the cut's second ReadWordCell result), or equivalently
   SPILLSYM=iTemp37 SPILLAT=iTemp0:94 (v never in a register, y evicted before the cut's third read). */
#include "lsdj.h"

extern UBYTE wWordClipCell0[];              /* $C935 */
extern UBYTE wWordClipCell1[];              /* $C936 */
extern UBYTE *wWordClipPtr;                 /* $C939 */
extern UBYTE wWordClipPtrB;                 /* $C939 as a byte */
extern UBYTE wClipboardBuf[];               /* $C57A */
extern UBYTE wWordEditRow;                  /* $C937 */
extern UBYTE wWordEditCol;                  /* $C938 */
extern UBYTE wClipboardType;                /* $C411 */
extern UBYTE wAPressEnabled;                /* $C409 */
extern UBYTE wBHeld;                        /* $C40B */
extern UBYTE wButtons;                      /* $C575 */
UBYTE ChainScaleValue(UBYTE v);
void DrawSampleNameCell(UBYTE v);
UBYTE ReadWordCell(void);
void WriteWordCell(UBYTE v);
void ChainDrawWaveCell(void);
void ChainDrawWaveCellHw(void);
void DrawWaveCellPair(void);
void ReadKitSampleName(UBYTE kit, UBYTE sample);
void MapMoveLeft(void);
void MapMoveUp(void);
void MapMoveDown(void);
void ToggleSongPlayback(void);
void TogglePlayback(void);
UBYTE ModU8(UBYTE a, UBYTE b);

UBYTE WordEditorInput(void)
{
    UBYTE v;
    UBYTE col;
    UBYTE y;

    hBgDigitPalette = wSelectDigitPal;
    hBgPalette = wSelectPal;
    y = wWordEditRow + 2;
    GetTilemapAddr(wWordEditCol * 4 + 2, y);
    switch (wButtons) {
    case 0x04:
        if (wWordEditRow) {
            ChainDrawWaveCell();
            wWordEditRow--;
            ChainDrawWaveCellHw();
        }
        return 1;
    case 0x08:
        DrawWaveCellPair();
        return 1;
    case 0x02:
        ChainDrawWaveCell();
        wWordEditCol = 0;
        ChainDrawWaveCellHw();
        return 1;
    case 0x01:
        ChainDrawWaveCell();
        wWordEditCol = 1;
        ChainDrawWaveCellHw();
        return 1;
    case 0x30:
        if (!wAPressEnabled)
            return 0;
        {
        UBYTE t;
        v = wWordEditCol;
        if (wClipboardType != 0x0C && wCursorMode == 4)
            DebugTrap();
        wClipboardType = 0x0C;
        wCursorMode = 2;
        wWordEditCol = 0;
        wWordClipCell0[0] = ReadWordCell();
        wClipboardBuf[0] = ReadWordCell();
        wWordEditCol = 1;
        wWordClipPtr = wWordClipCell0 + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        wWordClipPtr = wClipboardBuf + 1;
        t = ReadWordCell();
        *wWordClipPtr = t;
        WriteWordCell(0);
        wWordEditCol = 0;
        WriteWordCell(0);
        wWordEditCol = v;
        DrawWordScreen();
        return 1;
        }
    case 0x50:
        if (wClipboardType != 0x0C)
            return 0;
        v = wWordEditCol;
        wWordEditCol = 0;
        WriteWordCell(wClipboardBuf[0]);
        wWordEditCol = 1;
        WriteWordCell(wClipboardBuf[1]);
        wWordEditCol = v;
        DrawWordScreen();
        DrawWaveCellPair();
        return 1;
    case 0x10:
        if (!wAPressEnabled)
            return 0;
        col = wWordEditCol;
        if (!wWordEditCol) {
            if (!ReadWordCell() && wWordClipCell0[0]) {
                WriteWordCell(wWordClipCell0[0]);
                wWordEditCol = 1;
                WriteWordCell(wWordClipCell1[0]);
                GetTilemapAddr(2, y);
                hBgDigitPalette = 4;
                hBgPalette = 1;
                v = wWordClipCell0[0];
                v--;
                wWordClipPtrB = v % 15 + 1;
                ReadKitSampleName(ChainScaleValue(v), wWordClipPtrB);
                DrawString(wSampleNoteString);
                DrawStringTile20();
                hBgDigitPalette = wShadowDigitPal;
                hBgPalette = wShadowPal;
                DrawHexByte(wWordClipCell1[0]);
                wWordEditCol = col;
                ChainDrawWaveCellHw();
                return 1;
            }
            wWordEditCol = col;
        } else if (!ReadWordCell()) {
            WriteWordCell(wWordClipCell1[0]);
            GetTilemapAddr(6, y);
            hBgDigitPalette = wSelectDigitPal;
            hBgPalette = wSelectPal;
            DrawHexByte(wWordClipCell1[0]);
            return 1;
        }
        return 0;
    case 0x11:
        v = ReadWordCell();
        if (wWordEditCol) {
            if (v < 0xFF)
                v++;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else if (v != 0x3B) {
            ReadKitSampleName(ChainScaleValue(v), v % 15 + 1);
            v++;
            wWordClipCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case 0x12:
        v = ReadWordCell();
        if (wWordEditCol) {
            if (v)
                v--;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else if (v > 1) {
            v -= 2;
            ReadKitSampleName(ChainScaleValue(v), v % 15 + 1);
            v++;
            wWordClipCell0[0] = v;
            DrawString(wSampleNoteString);
        }
        WriteWordCell(v);
        return 1;
    case 0x14:
        v = ReadWordCell() + 0x10;
        if (wWordEditCol) {
            if (v < 0x10)
                return 0;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else {
            if (v > 0x3B)
                v = 0x3B;
            DrawSampleNameCell(v);
        }
        WriteWordCell(v);
        return 1;
    case 0x18:
        v = ReadWordCell() - 0x10;
        if (wWordEditCol) {
            if (v >= 0xF0)
                return 0;
            DrawHexByte(v);
            wWordClipCell1[0] = v;
        } else {
            if (v > 0x80)
                v = 1;
            DrawSampleNameCell(v);
        }
        WriteWordCell(v);
        return 1;
    case 0x22:
        if (wSpeechCursorRow) {
            wSpeechCursorRow--;
            DrawWordScreen();
        }
        return 1;
    case 0x21:
        if ((UWORD)wSpeechCursorRow < 13) {
            wSpeechCursorRow++;
            DrawWordScreen();
        }
        return 1;
    case 0x28:
        wSpeechCursorRow -= 12;
        if (wSpeechCursorRow > 0x80)
            wSpeechCursorRow = 0;
        DrawWordScreen();
        return 1;
    case 0x24:
        wSpeechCursorRow += 12;
        if (wSpeechCursorRow >= 14)
            wSpeechCursorRow = 13;
        DrawWordScreen();
        return 1;
    case 0x42:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveLeft);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x44:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveUp);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0x48:
        if (wButtonsSame)
            break;
        FarCall(5, MapMoveDown);
        FarCall(4, FAR_FN(RedrawScreen));
        return 1;
    case 0xC0:
        if (wButtonsSame)
            return 1;
        FarCall(2, FAR_FN(ToggleSongPlayback));
        return 1;
    case 0x80:
        if (wButtonsSame || wBHeld)
            return 1;
        FarCall(2, FAR_FN(TogglePlayback));
        return 1;
    }
    return 0;
}
