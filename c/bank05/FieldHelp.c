/*
 * Field help lines -- LSDj 9.4.0, ROM bank $05: the instrument screen's
 * one-line help for the field under the cursor.
 *
 * MATCHING C (period GBDK 2.95-3 / SDCC 2.2.1 + tools/match/lsdj.peep);
 * `make match` verifies every function here against the ROM.
 */

#include "lsdj.h"

/* The help strings follow ShowInstrumentFieldHelp's code ($05:$6D25..$7207). The
   ten that several instrument types share are stored by constant address
   (`ld (hl),#lo; inc hl; ld (hl),#hi`), the others by symbol (`ld de,#sym`),
   so the first group is spelled as addresses here. */
#define TxtHelpEnvStage1   ((char *)0x6D25)
#define TxtHelpEnvStage2   ((char *)0x6D72)
#define TxtHelpEnvStage3   ((char *)0x6DBF)
#define TxtHelpPan         ((char *)0x6E0C)
#define TxtHelpLength      ((char *)0x6E21)
#define TxtHelpPitchMode   ((char *)0x6E35)
#define TxtHelpTransposeOk ((char *)0x6EB5)
#define TxtHelpCmdRate     ((char *)0x6EC7)
#define TxtHelpTableMode   ((char *)0x6EF1)
#define TxtHelpVolume      ((char *)0x6F21)

extern char TxtHelpInstrType[];             /* $05:$6F38 */
extern char TxtHelpPulseWidth[];            /* $05:$6F6C */
extern char TxtHelpPu1Sweep[];              /* $05:$6F85 */
extern char TxtHelpTransposePu2[];          /* $05:$6FB9 */
extern char TxtHelpPuDetune[];              /* $05:$6FCB */
extern char TxtHelpDetune[];                /* $05:$6FE4 */
extern char TxtHelpWaveToPlay[];            /* $05:$6FF3 */
extern char TxtHelpSynthToPlay[];           /* $05:$7000 */
extern char TxtHelpWavePlayMode[];          /* $05:$7023 */
extern char TxtHelpTicksPerStep[];          /* $05:$70B5 */
extern char TxtHelpNumSteps[];              /* $05:$70C0 */
extern char TxtHelpFirstLoopStep[];         /* $05:$70D0 */
extern char TxtHelpKitLeft[];               /* $05:$70E3 */
extern char TxtHelpKitRight[];              /* $05:$70F5 */
extern char TxtHelpPitchShift[];            /* $05:$7108 */
extern char TxtHelpStartLoop[];             /* $05:$7114 */
extern char TxtHelpSoundLength[];           /* $05:$712E */
extern char TxtHelpKitLoopMode[];           /* $05:$7149 */
extern char TxtHelpSpeed[];                 /* $05:$718F */
extern char TxtHelpSampleMix[];             /* $05:$719F */
extern char TxtHelpSafeMode[];              /* $05:$71CE */
extern char TxtHelpVibShape[];              /* $05:$71F4 */

/*
 * The instrument screen's help line for the field under the cursor, by
 * instrument type (0 pulse, 1 wave, 2 kit, 3 noise); the TYPE row has one
 * string for all. The wave PLAY field depends on whether byte 9 selects a
 * synth.
 */
void ShowInstrumentFieldHelp(UBYTE type)
{
    char *s = 0;

    if (wInstrumentField == 1) {
        s = TxtHelpInstrType;
    } else {
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case 2: s = TxtHelpEnvStage1; break;
            case 3: s = TxtHelpEnvStage2; break;
            case 4: s = TxtHelpEnvStage3; break;
            case 5: s = TxtHelpPulseWidth; break;
            case 6: s = TxtHelpPan; break;
            case 7: s = TxtHelpLength; break;
            case 8: s = TxtHelpPu1Sweep; break;
            case 9: s = TxtHelpPitchMode; break;
            case 10: s = TxtHelpTransposeOk; break;
            case 11: s = TxtHelpTransposePu2; break;
            case 12: s = TxtHelpPuDetune; break;
            case 13: s = TxtHelpCmdRate; break;
            case 14: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case 2: s = TxtHelpVolume; break;
            case 3: s = TxtHelpPan; break;
            case 4: s = TxtHelpPitchMode; break;
            case 5: s = TxtHelpTransposeOk; break;
            case 6: s = TxtHelpDetune; break;
            case 7: s = TxtHelpCmdRate; break;
            case 9:
                if (ReadInstrumentByte(wInstrument, 9) == 0)
                    s = TxtHelpWaveToPlay;
                else
                    s = TxtHelpSynthToPlay;
                break;
            case 8: s = TxtHelpWavePlayMode; break;
            case 10: s = TxtHelpTicksPerStep; break;
            case 11: s = TxtHelpNumSteps; break;
            case 12: s = TxtHelpFirstLoopStep; break;
            case 13: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case 2: s = TxtHelpKitLeft; break;
            case 3: s = TxtHelpKitRight; break;
            case 4: s = TxtHelpVolume; break;
            case 5: s = TxtHelpPan; break;
            case 6: s = TxtHelpPitchMode; break;
            case 7: s = TxtHelpPitchShift; break;
            case 8:
            case 9: s = TxtHelpStartLoop; break;
            case 10:
            case 11: s = TxtHelpSoundLength; break;
            case 12:
            case 13: s = TxtHelpKitLoopMode; break;
            case 14: s = TxtHelpSpeed; break;
            case 15: s = TxtHelpSampleMix; break;
            case 16: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case 2: s = TxtHelpEnvStage1; break;
            case 3: s = TxtHelpEnvStage2; break;
            case 4: s = TxtHelpEnvStage3; break;
            case 5: s = TxtHelpPan; break;
            case 6: s = TxtHelpLength; break;
            case 7: s = TxtHelpSafeMode; break;
            case 8: s = TxtHelpVibShape; break;
            case 9: s = TxtHelpTransposeOk; break;
            case 10: s = TxtHelpCmdRate; break;
            case 11: s = TxtHelpTableMode; break;
            }
            break;
        }
    }
    if (s)
        ShowMessage(s);
}
