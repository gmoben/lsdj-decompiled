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

    if (wInstrumentField == INSTRUMENT_FIELD_TYPE) {
        s = TxtHelpInstrType;
    } else {
        switch (type) {
        case INSTRUMENT_PULSE:
            switch (wInstrumentField) {
            case PULSE_FIELD_ENVELOPE1: s = TxtHelpEnvStage1; break;
            case PULSE_FIELD_ENVELOPE2: s = TxtHelpEnvStage2; break;
            case PULSE_FIELD_ENVELOPE3: s = TxtHelpEnvStage3; break;
            case PULSE_FIELD_WAVE: s = TxtHelpPulseWidth; break;
            case PULSE_FIELD_OUTPUT: s = TxtHelpPan; break;
            case PULSE_FIELD_LENGTH: s = TxtHelpLength; break;
            case PULSE_FIELD_SWEEP: s = TxtHelpPu1Sweep; break;
            case PULSE_FIELD_PITCH: s = TxtHelpPitchMode; break;
            case PULSE_FIELD_TRANSPOSE: s = TxtHelpTransposeOk; break;
            case PULSE_FIELD_PU2_TRANSPOSE: s = TxtHelpTransposePu2; break;
            case PULSE_FIELD_FINETUNE: s = TxtHelpPuDetune; break;
            case PULSE_FIELD_COMMAND_RATE: s = TxtHelpCmdRate; break;
            case PULSE_FIELD_TABLE_ADVANCE: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_WAVE:
            switch (wInstrumentField) {
            case WAVE_FIELD_VOLUME: s = TxtHelpVolume; break;
            case WAVE_FIELD_OUTPUT: s = TxtHelpPan; break;
            case WAVE_FIELD_PITCH: s = TxtHelpPitchMode; break;
            case WAVE_FIELD_TRANSPOSE: s = TxtHelpTransposeOk; break;
            case WAVE_FIELD_FINETUNE: s = TxtHelpDetune; break;
            case WAVE_FIELD_COMMAND_RATE: s = TxtHelpCmdRate; break;
            case WAVE_FIELD_WAVE:
                if (ReadInstrumentByte(wInstrument, 9) == 0)
                    s = TxtHelpWaveToPlay;
                else
                    s = TxtHelpSynthToPlay;
                break;
            case WAVE_FIELD_PLAY: s = TxtHelpWavePlayMode; break;
            case WAVE_FIELD_SPEED: s = TxtHelpTicksPerStep; break;
            case WAVE_FIELD_LENGTH: s = TxtHelpNumSteps; break;
            case WAVE_FIELD_LOOP_POSITION: s = TxtHelpFirstLoopStep; break;
            case WAVE_FIELD_TABLE_ADVANCE: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_KIT:
            switch (wInstrumentField) {
            case KIT_FIELD_KIT1: s = TxtHelpKitLeft; break;
            case KIT_FIELD_KIT2: s = TxtHelpKitRight; break;
            case KIT_FIELD_VOLUME: s = TxtHelpVolume; break;
            case KIT_FIELD_OUTPUT: s = TxtHelpPan; break;
            case KIT_FIELD_PITCH: s = TxtHelpPitchMode; break;
            case KIT_FIELD_FINETUNE: s = TxtHelpPitchShift; break;
            case KIT_FIELD_OFFSET1:
            case KIT_FIELD_OFFSET2: s = TxtHelpStartLoop; break;
            case KIT_FIELD_LENGTH1:
            case KIT_FIELD_LENGTH2: s = TxtHelpSoundLength; break;
            case KIT_FIELD_LOOP1:
            case KIT_FIELD_LOOP2: s = TxtHelpKitLoopMode; break;
            case KIT_FIELD_SPEED: s = TxtHelpSpeed; break;
            case KIT_FIELD_CLIP: s = TxtHelpSampleMix; break;
            case KIT_FIELD_TABLE_ADVANCE: s = TxtHelpTableMode; break;
            }
            break;
        case INSTRUMENT_NOISE:
            switch (wInstrumentField) {
            case NOISE_FIELD_ENVELOPE1: s = TxtHelpEnvStage1; break;
            case NOISE_FIELD_ENVELOPE2: s = TxtHelpEnvStage2; break;
            case NOISE_FIELD_ENVELOPE3: s = TxtHelpEnvStage3; break;
            case NOISE_FIELD_OUTPUT: s = TxtHelpPan; break;
            case NOISE_FIELD_LENGTH: s = TxtHelpLength; break;
            case NOISE_FIELD_PITCH: s = TxtHelpSafeMode; break;
            case NOISE_FIELD_VIBRATO_SHAPE: s = TxtHelpVibShape; break;
            case NOISE_FIELD_TRANSPOSE: s = TxtHelpTransposeOk; break;
            case NOISE_FIELD_COMMAND_RATE: s = TxtHelpCmdRate; break;
            case NOISE_FIELD_TABLE_ADVANCE: s = TxtHelpTableMode; break;
            }
            break;
        }
    }
    if (s)
        ShowMessage(s);
}
