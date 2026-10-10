#define SONG_BYTE_PTR
#include "lsdj.h"

extern UBYTE InstrFieldX[][18];
extern UBYTE InstrFieldY[][18];
extern UBYTE InstrFieldCounts[];
extern UBYTE wInstrFieldCount;              /* $C905 */
extern UBYTE wNameEditBuf[];                /* $C431 */
extern char InstrTypeNames[];               /* $03:$4FC4 */
void DrawSpeechInstrScreen(void);           /* $1F:$7108 */
void DrawAllChanStatus(void);               /* $1F:$7072 */
void DrawPulseInstr(void);
void DrawWaveInstr(void);
void DrawKitInstr(void);
void DrawNoiseInstr(void);
void CopyInstrName(void);                   /* $01 */
void InstrScreenFrame(void);
void FarCallArgW(UBYTE bank, UWORD fn, UBYTE arg);
void FarCallW(UBYTE bank, UWORD fn);
UBYTE MulU8(UBYTE a, UBYTE b);

void DrawInstrumentScreen(void)
{
    UBYTE i;
    UBYTE type;
    UWORD fn;

    if (wInstrument > 0x40)
        DebugTrap();
    if (wInstrument == 0x40) {
        FarCall(0x1F, FAR_FN(DrawSpeechInstrScreen));
        return;
    }
    FarCallArgW(0x1F, 0x492A, wInstrument);
    type = ReadCurInstrByte(0);
    if (type >= 4) {
        WriteCurrentInstrByte(0, 0);
        type = 0;
    }
    wInstrFieldCount = InstrFieldCounts[type];
    if (wInstrField >= wInstrFieldCount)
        wInstrField = wInstrFieldCount - 1;
    InstrScreenFrame();
    hBgDigitPalette = wShadowDigitPal;
    hBgPalette = wShadowPal;
    GetTilemapAddr(InstrFieldX[type][0], InstrFieldY[type][0]);
    FarCallArg(1, FAR_FN(CopyInstrName), wInstrument);
    for (i = 0; i < 5; i++) {
        UBYTE c = wNameEditBuf[i];
        if (!c)
            c = ' ';
        DrawStringIndexed(c);
    }
    GetTilemapAddr(InstrFieldX[type][1], InstrFieldY[type][1]);
    DrawString(InstrTypeNames + type * 6);
    switch (type) {
    case 0:
        fn = (UWORD)DrawPulseInstr;
        break;
    case 1:
        fn = (UWORD)DrawWaveInstr;
        break;
    case 3:
        fn = (UWORD)DrawNoiseInstr;
        break;
    case 2:
        fn = (UWORD)DrawKitInstr;
        break;
    }
    FarCallW(0x1F, fn);
    FarCall(0x1F, FAR_FN(DrawAllChanStatus));
    if (hGameboyType) {
        i = wInstrField;
        wInstrField = 0;
        while (wInstrField < wInstrFieldCount) {
            EraseInstrCursor();
            wInstrField++;
        }
        wInstrField = i;
    }
    DrawInstrCursor();
    InstrEnvelopeChanged();
}
