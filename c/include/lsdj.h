/*
 * lsdj.h -- shared declarations for the matching C (Track 2).
 *
 * Every name here is a real LSDj symbol (src/bankNN.sym, constants/ram.inc,
 * include/hardware.inc, or the SRAM map in tools/gen_sym.py); the harness
 * links each extern at that address. Prototypes are part of the match -- an
 * argument's width or a return type changes the code SDCC emits -- so a
 * signature is settled here once, by the functions that call it. Data that
 * only one module uses (its strings and tables) is declared in that module.
 *
 * How LSDj's source spelled things, as far as the bytes tell:
 *   - `extern volatile sfr UBYTE` for the $FFxx registers and the HRAM
 *     variables it accesses with ldh; a plain extern compiles to the 3-byte
 *     `ld a,(nnnn)` form. hGameboyType is read that way (SDCC 2.2.1 crashes
 *     on `if (sfr)`, so LSDj's source must have declared it plainly).
 *   - The working song in SRAM is addressed with numeric constants, not
 *     linker symbols: SDCC swaps a *literal* left operand of `+` to the
 *     right, an extern's address it does not (NextUsedInstrument tells them
 *     apart), so the SramXxx names below are literal-pointer macros.
 *   - Banked calls go through FarCall/FarCallArgument (bank 0, hand-written asm).
 *     Most call sites pass the function itself (`ld bc,#fn`); others pass
 *     it cast through an integer, which SDCC loads byte-wise (`ld c,#<fn;
 *     ld b,#>fn`) -- FAR_FN(fn) reproduces those. FarCallArgument's address
 *     parameter is an integer (its call sites cast with (UWORD)).
 */

#ifndef LSDJ_H
#define LSDJ_H

#ifdef __clang__  /* editor tooling only: SDCC 2.2.1 keyword (never set by SDCC) */
#define sfr
#endif

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned int UWORD;     /* SDCC: int is 16 bits */
typedef int WORD;

/* --- constants --------------------------------------------------------------
 * A literal's spelling is part of the match: SDCC 2.2.1 types a hex or octal
 * constant unsigned and a decimal one signed (constVal in SDCCval.c). */

/* wCurrentScreen (constants/screens.inc) */
#define SCREEN_NONE          0
#define SCREEN_PHRASE        1
#define SCREEN_GROOVE        2
#define SCREEN_CHAIN         3
#define SCREEN_SONG          4
#define SCREEN_TABLE         5
#define SCREEN_INSTRUMENT    6
#define SCREEN_NAME_EDITOR   7      /* OpenNameEditor */
#define SCREEN_WAVE          8
#define SCREEN_PROJECT       9
#define SCREEN_SYNTH         10
#define SCREEN_SPEECH        11     /* the speech instrument's word list */
#define SCREEN_WORD          12
#define SCREEN_FILE          13
#define SCREEN_HELP          14

/* the channels: a song column, hSoundChannel */
#define CHANNEL_PU1          0
#define CHANNEL_PU2          1
#define CHANNEL_WAV          2
#define CHANNEL_NOI          3
#define CHANNELS             4

/* the commands, in CommandMnemonics' order */
#define COMMAND_NONE         0
#define COMMAND_A            1
#define COMMAND_B            2
#define COMMAND_C            3
#define COMMAND_D            4
#define COMMAND_E            5
#define COMMAND_F            6
#define COMMAND_G            7
#define COMMAND_H            8
#define COMMAND_K            9
#define COMMAND_L            10
#define COMMAND_M            11
#define COMMAND_O            12
#define COMMAND_P            13
#define COMMAND_R            14
#define COMMAND_S            15
#define COMMAND_T            16
#define COMMAND_V            17
#define COMMAND_W            18
#define COMMAND_Z            19

/* an instrument's type, its byte 0 (InstrumentTypeNames) */
#define INSTRUMENT_PULSE     0
#define INSTRUMENT_WAVE      1
#define INSTRUMENT_KIT       2
#define INSTRUMENT_NOISE     3

/* an empty cell: no chain, phrase or instrument (the screens' "--") */
#define EMPTY                0xFF

/* 64 instruments, then the speech instrument, locked to $40 */
#define INSTRUMENTS          0x40
#define INSTRUMENT_SPEECH    0x40

/* an instrument's 16 bytes (the manual's Appendix C): the bytes every type
 * shares, the ones some types share, then each type's own */
#define INSTRUMENT_BYTE_TYPE           0
#define INSTRUMENT_BYTE_ENVELOPE1      1    /* pulse, noise */
#define INSTRUMENT_BYTE_LENGTH         3    /* pulse, noise: bit 6 limited, 5-0 */
#define INSTRUMENT_BYTE_FLAGS          5    /* pitch, transpose, table speed, vibrato (kit: the loops) */
#define INSTRUMENT_BYTE_TABLE          6    /* bit 5 on, 4-0 the table */
#define INSTRUMENT_BYTE_OUTPUT         7    /* bits 1-0 (pulse: the wave in 7-6) */
#define INSTRUMENT_BYTE_COMMAND_RATE   8    /* pulse, wave, noise */
#define INSTRUMENT_BYTE_ENVELOPE2      9    /* pulse, noise */
#define INSTRUMENT_BYTE_ENVELOPE3      10   /* pulse, noise */
#define PULSE_BYTE_PU2_TRANSPOSE       2
#define PULSE_BYTE_SWEEP               4
#define PULSE_BYTE_FINETUNE            11
#define WAVE_BYTE_VOLUME               1    /* bits 6-5 */
#define WAVE_BYTE_LOOP_POSITION        2    /* the low nibble, inverted */
#define WAVE_BYTE_WAVE                 3    /* the wave; the synth in bits 7-4 */
#define WAVE_BYTE_PLAY                 9
#define WAVE_BYTE_LENGTH               10   /* $0F - the length */
#define WAVE_BYTE_SPEED                11   /* the speed - 3 */
#define WAVE_BYTE_FINETUNE             12
#define KIT_BYTE_VOLUME                1    /* bits 6-5 */
#define KIT_BYTE_KIT1                  2    /* bits 5-0; 6 half speed, 7 loop 1 at attack */
#define KIT_BYTE_LENGTH1               3
#define KIT_BYTE_FINETUNE              8
#define KIT_BYTE_KIT2                  9    /* bits 5-0; 7 loop 2 at attack */
#define KIT_BYTE_CLIP                  10
#define KIT_BYTE_LENGTH2               11
#define KIT_BYTE_OFFSET1               12
#define KIT_BYTE_OFFSET2               13
#define NOISE_BYTE_PITCH               2    /* bit 0: free/safe */
#define NOISE_BYTE_LAST_NOTE           4

/* wInstrumentField: the instrument screen's fields, per type (InstrumentFieldX
 * has their places; ShowInstrumentFieldHelp their help) */
#define INSTRUMENT_FIELD_NAME          0
#define INSTRUMENT_FIELD_TYPE          1
#define PULSE_FIELD_ENVELOPE1          2
#define PULSE_FIELD_ENVELOPE2          3
#define PULSE_FIELD_ENVELOPE3          4
#define PULSE_FIELD_WAVE               5
#define PULSE_FIELD_OUTPUT             6
#define PULSE_FIELD_LENGTH             7
#define PULSE_FIELD_SWEEP              8
#define PULSE_FIELD_PITCH              9
#define PULSE_FIELD_TRANSPOSE          10
#define PULSE_FIELD_PU2_TRANSPOSE      11
#define PULSE_FIELD_FINETUNE           12
#define PULSE_FIELD_COMMAND_RATE       13
#define PULSE_FIELD_TABLE_ADVANCE      14
#define PULSE_FIELD_TABLE              15
#define WAVE_FIELD_VOLUME              2
#define WAVE_FIELD_OUTPUT              3
#define WAVE_FIELD_PITCH               4
#define WAVE_FIELD_TRANSPOSE           5
#define WAVE_FIELD_FINETUNE            6
#define WAVE_FIELD_COMMAND_RATE        7
#define WAVE_FIELD_PLAY                8
#define WAVE_FIELD_WAVE                9
#define WAVE_FIELD_SPEED               10
#define WAVE_FIELD_LENGTH              11
#define WAVE_FIELD_LOOP_POSITION       12
#define WAVE_FIELD_TABLE_ADVANCE       13
#define WAVE_FIELD_TABLE               14
#define KIT_FIELD_KIT1                 2
#define KIT_FIELD_KIT2                 3
#define KIT_FIELD_VOLUME               4
#define KIT_FIELD_OUTPUT               5
#define KIT_FIELD_PITCH                6
#define KIT_FIELD_FINETUNE             7
#define KIT_FIELD_OFFSET1              8
#define KIT_FIELD_OFFSET2              9
#define KIT_FIELD_LENGTH1              10
#define KIT_FIELD_LENGTH2              11
#define KIT_FIELD_LOOP1                12
#define KIT_FIELD_LOOP2                13
#define KIT_FIELD_SPEED                14
#define KIT_FIELD_CLIP                 15
#define KIT_FIELD_TABLE_ADVANCE        16
#define KIT_FIELD_TABLE                17
#define NOISE_FIELD_ENVELOPE1          2
#define NOISE_FIELD_ENVELOPE2          3
#define NOISE_FIELD_ENVELOPE3          4
#define NOISE_FIELD_OUTPUT             5
#define NOISE_FIELD_LENGTH             6
#define NOISE_FIELD_PITCH              7
#define NOISE_FIELD_VIBRATO_SHAPE      8
#define NOISE_FIELD_TRANSPOSE          9
#define NOISE_FIELD_COMMAND_RATE       10
#define NOISE_FIELD_TABLE_ADVANCE      11
#define NOISE_FIELD_TABLE              12

/* wProjectCursor: the PROJECT screen's fields, top to bottom (ProjectFieldX
 * has their places) */
#define PROJECT_FIELD_TEMPO                  0
#define PROJECT_FIELD_TRANSPOSE              1
#define PROJECT_FIELD_SYNC                   2
#define PROJECT_FIELD_SYNC_PARAMETER         3
#define PROJECT_FIELD_CLONE                  4
#define PROJECT_FIELD_LOOK                   5
#define PROJECT_FIELD_KEY_DELAY_REPEAT       6
#define PROJECT_FIELD_PRELISTEN              7
#define PROJECT_FIELD_HELP                   8
#define PROJECT_FIELD_CLEAN_SONG_DATA        9
#define PROJECT_FIELD_CLEAN_INSTRUMENT_DATA  10
#define PROJECT_FIELD_LOAD_SAVE_SONG         11

/* wSyncMode (2 and 4 are no mode: the PROJECT screen steps over them) */
#define SYNC_OFF             0
#define SYNC_LSDJ            1
#define SYNC_MIDI            3
#define SYNC_KEYBOARD        5
#define SYNC_ANALOG_IN       6
#define SYNC_ANALOG_OUT      7

/* wPhraseColumn: a kit instrument's left note column (its first kit; hidden
 * for the other types), the note (a kit's right note column), the
 * instrument, the command and its value */
#define PHRASE_COLUMN_LEFT_NOTE    0
#define PHRASE_COLUMN_NOTE         1
#define PHRASE_COLUMN_INSTRUMENT   2
#define PHRASE_COLUMN_COMMAND      3
#define PHRASE_COLUMN_VALUE        4

/* wTableColumn: VOL TSP CMD VAL CMD VAL */
#define TABLE_COLUMN_VOLUME        0
#define TABLE_COLUMN_TRANSPOSE     1
#define TABLE_COLUMN_COMMAND1      2
#define TABLE_COLUMN_VALUE1        3
#define TABLE_COLUMN_COMMAND2      4
#define TABLE_COLUMN_VALUE2        5
#define TABLE_COLUMNS              6

/* wCursorMode: nothing copied, the clipboard holding a cut or copy (paste
 * takes it), or a selection being marked */
#define CURSOR_NORMAL        1
#define CURSOR_COPIED        2
#define CURSOR_SELECTING     4

/* wButtons: the joypad bits, GBDK's names; buttons held together are their
 * `|`, which SDCC folds to the one constant */
#define J_RIGHT   0x01
#define J_LEFT    0x02
#define J_UP      0x04
#define J_DOWN    0x08
#define J_A       0x10
#define J_B       0x20
#define J_SELECT  0x40
#define J_START   0x80
/* a cell of the background map ($9800, 32 cells a row): the screen's x, y */
#define BG_MAP(x, y) (0x9800 + (y) * 32 + (x))

/* the OAM shadow ($DD00, wOamBuffer) as GBDK's sprite records */
typedef struct { UBYTE y, x, tile, prop; } OamEntry;
#define OamShadow ((OamEntry *)0xDD00)

/* fixed addresses the code uses as numbers (constants/ram.inc names them) */
#define SRAM_START            0xA000   /* the SRAM window */
#define SRAM_END              0xC000
#define ROW_BUFFER_COLUMN(k)  (0xC46D + (k) * 16)   /* wSongReadBuffer, wRowBufferColumn2.. */
#define KIT_MIX_TABLE         0xD000   /* wKitMixTable */
#define KIT_MIXER_CODE        0xD480   /* wKitMixerCode */
#define DECOMPRESS_BUFFER     0xDA00   /* wDecompressBuffer */

/* a kit's header, in its ROM bank at $4000 (tools/kits.py) */
#define KIT_SAMPLE_BOUNDARIES 0x4000
#define KIT_NAME              0x4052
#define KIT_SAMPLES           0x4060   /* the first boundary: where the samples start */

/* ProcessInput's code for SELECT tapped alone: LEFT and RIGHT are never held
 * together */
#define J_SELECT_TAP (J_SELECT | J_LEFT | J_RIGHT)

/* --- hardware and HRAM ---------------------------------------------------- */

extern volatile sfr UBYTE VBK_REG;          /* $FF4F rVBK */
extern volatile sfr UBYTE hBgPalette;       /* $FF8C */
extern volatile sfr UBYTE hBgDigitPalette;  /* $FF8D */
extern volatile sfr UBYTE hCurrentBank;     /* $FF8E */
extern UBYTE hGameboyType;                  /* $FF90 */
extern volatile UBYTE rROMB0;               /* $2000, the MBC5 ROM bank register */
/* volatile: SoundSpeechStep's `rRAMG = 0x0A; x = *p; rRAMG = 0;` keeps both
 * stores, which SDCC drops as dead for a plain global */
extern volatile UBYTE rRAMG;                /* $0000, MBC5 SRAM enable ($0A on) */
extern volatile UBYTE rRAMB;                /* $4000, MBC5 SRAM bank */

/* GBDK's SWITCH_ROM_MBC1 is `*(UBYTE *)0x2000 = n`, which SDCC compiles through
 * de (`ld de,#0x2000; ld a,#n; ld (de),a`); the ROM has the named-variable form
 * (`ld hl,#0x2000; ld (hl),#n`), so LSDj wrote the register as a variable. It
 * also keeps the HRAM shadow the far-call helpers rely on. */
#define SWITCH_ROM(n) hCurrentBank = (n); rROMB0 = (n)

/* --- WRAM ----------------------------------------------------------------- */

extern UBYTE wCurrentScreen;                /* $C401 */
extern UBYTE wIsCgb;                        /* $C402 */
extern UBYTE wButtonsSame;                  /* $C408: 1 if the buttons are unchanged since last frame */
extern UBYTE wSelectionLeft;                      /* $C40D */
extern UBYTE wCursorMode;                   /* $C413 */
extern UBYTE wPhrase;                       /* $C414 */
extern UBYTE wPhraseRow;                    /* $C416 */
extern UBYTE wSongColumn;                      /* $C41D: song cursor column (channel) */
extern UBYTE wSongRow;                      /* $C41E: song cursor row */
extern UBYTE wInstrumentField;                   /* $C427: instrument screen cursor row */
extern UBYTE wSpeechRow;              /* $C429 */
extern char wSampleNoteString[];            /* $C43A: [7] shared name buffer */
extern UBYTE wSongReadBuffer[];                /* $C46D, [64] the song window (16 rows x 4 channels) */
extern UBYTE wUsedTableMap[];               /* $C47D, [32] tables referenced by instruments; also a scratch buffer */
extern UBYTE wPlayMode;                     /* $C4D2 */
extern UBYTE wTempo;                        /* $C529, 0-39 stand for 256-295 */
extern UBYTE wTempoMode;                    /* $C52A: 0 = BPM, 1-3 = the 2X/3X/6X sync speeds */
extern UBYTE wShadedDigitPalette;               /* $C566 */
extern UBYTE wShadedPalette;                    /* $C567 */
extern UBYTE wHighlightDigitPalette;               /* $C568 */
extern UBYTE wHighlightPalette;                    /* $C569 */
extern UBYTE *wStringSource;                   /* $C564, the character-to-tile map DrawString reads */
extern UBYTE AsciiCharacterMap[];                /* $0:$086F, text from ' '; wStringSource = AsciiCharacterMap - 0x20 */
extern UBYTE IdentityCharacterMap[];             /* $0:$08C8, raw tile indices */
extern UBYTE *wVramCursor;                  /* $C573 */
extern UBYTE wInstrument;                   /* $C576, the instrument being edited */
extern UBYTE wStringBuffer[];                  /* $C6A9, 20 chars + NUL */
extern UBYTE wMessageTimer;                     /* $C7A9 */
extern UBYTE *wMessageStringPointer;                   /* $C7AA */
extern UBYTE wMessagePanelShown;                /* $C7AC */
extern UBYTE wInstrumentEditLatch;               /* $C906: instrument screen edit latch */
extern char *wChainScratchPointer;              /* $C948 */
extern UBYTE wSyncMode;                     /* $CB66 */
extern UBYTE wSyncMode2;                    /* $CB67 */

/* --- the working song in SRAM (docs/data_model.md; tools/gen_sym.py) ------- */
/* A region's address is a number, SRAM_X, and its bytes a pointer, SramX: a
 * literal pointer, which SDCC swaps to the right of `+` as LSDj's code did.
 * SRAM is four banks at $A000: the same address is a different region in
 * each, so a name says which bank it is in. */

#define SRAM_PHRASE_NOTES                  0xA000   /* bank 0, song +$0000 */
#define SRAM_BOOKMARKS                     0xAFF0   /* bank 0, song +$0FF0 */
#define SRAM_GROOVES                       0xB090   /* bank 0, song +$1090 */
#define SRAM_SEQUENCE                      0xB290   /* bank 0, song +$1290 */
#define SRAM_TABLE_ENVELOPES               0xB690   /* bank 0, song +$1690 */
#define SRAM_WORDS                         0xB890   /* bank 0, song +$1890 */
#define SRAM_WORD_NAMES                    0xBDD0   /* bank 0, song +$1DD0 */
#define SRAM_INITIALIZED_MARKER1           0xBE78   /* bank 0, song +$1E78 */
#define SRAM_INSTRUMENT_NAMES              0xBE7A   /* bank 0, song +$1E7A */
#define SRAM_TABLE_ALLOCATION              0xA020   /* bank 1, song +$2020 */
#define SRAM_INSTRUMENT_ALLOCATION         0xA040   /* bank 1, song +$2040 */
#define SRAM_CHAIN_PHRASES                 0xA080   /* bank 1, song +$2080 */
#define SRAM_CHAIN_TRANSPOSES              0xA880   /* bank 1, song +$2880 */
#define SRAM_INSTRUMENTS                   0xB080   /* bank 1, song +$3080 */
#define SRAM_TABLE_TRANSPOSES              0xB480   /* bank 1, song +$3480 */
#define SRAM_TABLE_COMMANDS1               0xB680   /* bank 1, song +$3680 */
#define SRAM_TABLE_COMMAND_VALUES1         0xB880   /* bank 1, song +$3880 */
#define SRAM_TABLE_COMMANDS2               0xBA80   /* bank 1, song +$3A80 */
#define SRAM_TABLE_COMMAND_VALUES2         0xBC80   /* bank 1, song +$3C80 */
#define SRAM_INITIALIZED_MARKER2           0xBE80   /* bank 1, song +$3E80 */
#define SRAM_PHRASE_ALLOCATION             0xBE82   /* bank 1, song +$3E82 */
#define SRAM_CHAIN_ALLOCATION              0xBEA2   /* bank 1, song +$3EA2 */
#define SRAM_SYNTH_PARAMETERS              0xBEB2   /* bank 1, song +$3EB2 */
#define SRAM_PROPERTIES                    0xBFB2   /* bank 1, song +$3FB2 */
#define SRAM_PROPERTY_TEMPO                0xBFB4   /* bank 1, song +$3FB4 */
#define SRAM_PROPERTY_TRANSPOSE            0xBFB5   /* bank 1, song +$3FB5 */
#define SRAM_PROPERTY_TOTAL_TIME           0xBFB6   /* bank 1, song +$3FB6 */
#define SRAM_PROPERTY_KEY_DELAY            0xBFBA   /* bank 1, song +$3FBA */
#define SRAM_PROPERTY_KEY_REPEAT           0xBFBB   /* bank 1, song +$3FBB */
#define SRAM_PROPERTY_FONT                 0xBFBC   /* bank 1, song +$3FBC */
#define SRAM_PROPERTY_SYNC_MODE            0xBFBD   /* bank 1, song +$3FBD */
#define SRAM_PROPERTY_COLOR_SET            0xBFBE   /* bank 1, song +$3FBE */
#define SRAM_PROPERTY_SYNC_PARAMETER       0xBFBF   /* bank 1, song +$3FBF */
#define SRAM_PROPERTY_CLONE_MODE           0xBFC0   /* bank 1, song +$3FC0 */
#define SRAM_PROPERTY_FILE_CHANGED         0xBFC1   /* bank 1, song +$3FC1 */
#define SRAM_PROPERTY_POWER_SAVE           0xBFC2   /* bank 1, song +$3FC2 */
#define SRAM_PROPERTY_PRELISTEN            0xBFC3   /* bank 1, song +$3FC3 */
#define SRAM_PROPERTY_COLUMN_INSTRUMENTS   0xBFC6   /* bank 1, song +$3FC6 */
#define SRAM_PROPERTY_TEMPO_MODE           0xBFCC   /* bank 1, song +$3FCC */
#define SRAM_PHRASE_COMMANDS               0xA000   /* bank 2, song +$4000 */
#define SRAM_PHRASE_COMMAND_VALUES         0xAFF0   /* bank 2, song +$4FF0 */
#define SRAM_WAVE_FRAMES                   0xA000   /* bank 3, song +$6000 */
#define SRAM_PHRASE_INSTRUMENTS            0xB000   /* bank 3, song +$7000 */
#define SRAM_INITIALIZED_MARKER3           0xBFF0   /* bank 3, song +$7FF0 */
#define SRAM_PROPERTY_WORK_TIME            SRAM_PROPERTIES   /* hours, minutes */

#define SramPhraseNotes                ((UBYTE *)SRAM_PHRASE_NOTES)
#define SramBookmarks                  ((UBYTE *)SRAM_BOOKMARKS)
#define SramGrooves                    ((UBYTE *)SRAM_GROOVES)
#define SramSequence                   ((UBYTE *)SRAM_SEQUENCE)
#define SramTableEnvelopes             ((UBYTE *)SRAM_TABLE_ENVELOPES)
#define SramWords                      ((UBYTE *)SRAM_WORDS)
#define SramWordNames                  ((UBYTE *)SRAM_WORD_NAMES)
#define SramInitializedMarker1         ((UBYTE *)SRAM_INITIALIZED_MARKER1)
#define SramInstrumentNames            ((UBYTE *)SRAM_INSTRUMENT_NAMES)
#define SramTableAllocation            ((UBYTE *)SRAM_TABLE_ALLOCATION)
#define SramInstrumentAllocation       ((UBYTE *)SRAM_INSTRUMENT_ALLOCATION)
#define SramChainPhrases               ((UBYTE *)SRAM_CHAIN_PHRASES)
#define SramChainTransposes            ((UBYTE *)SRAM_CHAIN_TRANSPOSES)
#define SramInstruments                ((UBYTE *)SRAM_INSTRUMENTS)
#define SramTableTransposes            ((UBYTE *)SRAM_TABLE_TRANSPOSES)
#define SramTableCommands1             ((UBYTE *)SRAM_TABLE_COMMANDS1)
#define SramTableCommandValues1        ((UBYTE *)SRAM_TABLE_COMMAND_VALUES1)
#define SramTableCommands2             ((UBYTE *)SRAM_TABLE_COMMANDS2)
#define SramTableCommandValues2        ((UBYTE *)SRAM_TABLE_COMMAND_VALUES2)
#define SramInitializedMarker2         ((UBYTE *)SRAM_INITIALIZED_MARKER2)
#define SramPhraseAllocation           ((UBYTE *)SRAM_PHRASE_ALLOCATION)
#define SramChainAllocation            ((UBYTE *)SRAM_CHAIN_ALLOCATION)
#define SramSynthParameters            ((UBYTE *)SRAM_SYNTH_PARAMETERS)
#define SramProperties                 ((UBYTE *)SRAM_PROPERTIES)
#define SramPropertyTempo              ((UBYTE *)SRAM_PROPERTY_TEMPO)
#define SramPropertyTranspose          ((UBYTE *)SRAM_PROPERTY_TRANSPOSE)
#define SramPropertyTotalTime          ((UBYTE *)SRAM_PROPERTY_TOTAL_TIME)
#define SramPropertyKeyDelay           ((UBYTE *)SRAM_PROPERTY_KEY_DELAY)
#define SramPropertyKeyRepeat          ((UBYTE *)SRAM_PROPERTY_KEY_REPEAT)
#define SramPropertyFont               ((UBYTE *)SRAM_PROPERTY_FONT)
#define SramPropertySyncMode           ((UBYTE *)SRAM_PROPERTY_SYNC_MODE)
#define SramPropertyColorSet           ((UBYTE *)SRAM_PROPERTY_COLOR_SET)
#define SramPropertySyncParameter      ((UBYTE *)SRAM_PROPERTY_SYNC_PARAMETER)
#define SramPropertyCloneMode          ((UBYTE *)SRAM_PROPERTY_CLONE_MODE)
#define SramPropertyFileChanged        ((UBYTE *)SRAM_PROPERTY_FILE_CHANGED)
#define SramPropertyPowerSave          ((UBYTE *)SRAM_PROPERTY_POWER_SAVE)
#define SramPropertyPrelisten          ((UBYTE *)SRAM_PROPERTY_PRELISTEN)
#define SramPropertyColumnInstruments  ((UBYTE *)SRAM_PROPERTY_COLUMN_INSTRUMENTS)
#define SramPropertyTempoMode          ((UBYTE *)SRAM_PROPERTY_TEMPO_MODE)
#define SramPhraseCommands             ((UBYTE *)SRAM_PHRASE_COMMANDS)
#define SramPhraseCommandValues        ((UBYTE *)SRAM_PHRASE_COMMAND_VALUES)
#define SramWaveFrames                 ((UBYTE *)SRAM_WAVE_FRAMES)
#define SramPhraseInstruments          ((UBYTE *)SRAM_PHRASE_INSTRUMENTS)
#define SramInitializedMarker3         ((UBYTE *)SRAM_INITIALIZED_MARKER3)
#define SramPropertyWorkTime           ((UBYTE *)SRAM_PROPERTY_WORK_TIME)

/* --- bank 2: the sound engine ----------------------------------------------- */

/* Far-call targets, declared as the banked-call routines take them; bank 2's
   own units (SOUND_BANK) define them with their real types, and a unit that
   calls one directly with bank 2 paged in (SND_DIRECT: Main.c's
   ProcessInput) declares it so itself. */
#if !defined(SOUND_BANK) && !defined(SND_DIRECT)
void SoundGetChainPhrase(void);               /* $02:$6F65 (far): chain_phrases[chain][row] */
#endif

/* --- bank 0 ---------------------------------------------------------------- */

/* runtime (hand-written asm) */
void DebugTrap(void);                                   /* `ld b,b` + rst $38 assert */
void FarCall(UBYTE bank, void (*function)(void));
void FarCallArgument(UBYTE bank, void (*function)(void), UBYTE argument);    /* FAR_FN(f) for the byte-wise form */

/* far calls with arguments or a byte result (C: c/bank00/FarCall.c) */
UBYTE FarCallArgumentByte(UBYTE bank, UWORD function, UBYTE argument);         /* returns fn's byte */
UBYTE FarCallArgument2(UBYTE bank, UWORD function, UBYTE a, UBYTE b);
UBYTE FarCallArgument2P(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b);  /* the same routine taking FAR_FN(f) (match_function.py aliases it) */
void FarCallArgument3(UBYTE bank, UWORD function, UBYTE a, UBYTE b, UBYTE c);
void FarCallArgument3P(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b, UBYTE c);  /* the same routine taking FAR_FN(f) (match_function.py aliases it) */
UBYTE FarCallByteU(UBYTE bank, UBYTE (*function)(void));  /* FarCallByte for a callee declared returning UBYTE: `ld bc,#f` (aliased) */
#ifndef FARCALL_C
/* their callers pass a function; FarCall.c defines them taking a UWORD */
void FarCallArgument2V(UBYTE bank, void (*function)(void), UBYTE a, UBYTE b);
UBYTE FarCallByte(UBYTE bank, void (*function)(void));    /* returns fn's byte result */
#endif
#define FAR_FN(f) ((void (*)(void))(UWORD)(f))

/* song data access */
#ifdef SONG_BYTE_PTR
UBYTE ReadSongByte(UBYTE bank, UBYTE *address);    /* bank 3 declares the address a pointer */
#else
UBYTE ReadSongByte(UBYTE bank, UWORD address);
#endif
void WriteSongByte(UBYTE bank, UWORD address, UBYTE value);
UBYTE ReadSongByteRaw(UBYTE bank, UWORD address);  /* no range asserts */
void ReadSongBytes(UBYTE bank, UBYTE *source, UBYTE *destination);  /* 16 bytes */
void WriteInstrumentByte(UBYTE instrument, UBYTE field, UBYTE value);
UBYTE ReadInstrumentByte(UBYTE instrument, UBYTE field);
UBYTE ReadCurrentInstrumentByte(UBYTE field);
UBYTE ReadSequenceByte(UBYTE channel, UBYTE row);
void ReadKitName(UBYTE kit);                            /* -> wSampleNoteString */
UBYTE KitExists(UBYTE kit);

/* drawing */
void set_bkg_tiles(UBYTE x, UBYTE y, UBYTE w, UBYTE h, unsigned char *tiles);
void DrawString(char *s);
void DrawStringIndexed(UBYTE c);
void DrawStringTile20(void);
void DrawByte(UBYTE v);
void DrawHexByte(UBYTE v);
void DrawTempoField(void);
UWORD GetTempoTimerValue(void);                 /* the tempo as shown (or a speed code) */
void DrawTempo(UWORD tempo);
#ifndef GBDKLIB_C       /* GbdkLib.c defines it with `...` */
void printf(char *fmt, UWORD v);            /* printf-style, varargs */
#endif
void printf2(char *fmt, UWORD a, UWORD b);  /* the same routine with two values (match_function.py aliases it) */
void SetVramCursor(UBYTE x, UBYTE y);                  /* sets wVramCursor (asm) */
void ClearMarkerSprites(void);
char *strcpy(char *dest, char *source);
void FillMemory(UBYTE *dest, UBYTE value, UWORD count);
void memcpy(UBYTE *destination, UBYTE *source, UWORD n);

/* misc */
void SortBytePair(UBYTE *a, UBYTE *b);               /* swap so *a <= *b */
UBYTE ReadClipboardCell(UBYTE column, UBYTE row);         /* wClipboardBuffer[col*42 + row] */
void WriteClipboardCell(UBYTE column, UBYTE row, UBYTE v);
void LoadSynthShape(char *source);              /* bank $1E seed bytes -> wSynthShapeBuffer */

/* --- the screens' draw entry points (DispatchScreenDraw, bank 0) ----------- */

void RedrawScreen(void);                    /* $24DD: full repaint of the current screen */
void ClearBgTilemap(void);                  /* $17FB: tile 0 over the visible BG map */
extern UBYTE CommandMnemonics[];                 /* $0824: "-ABCDEFGHKLMOPRSTVWZ", the command letters */
void SetSpriteY(UBYTE sprite, UBYTE row);   /* play-position marker sprites */
void ClearSpriteY(UBYTE sprite);
void SetSpriteX(UBYTE sprite, UBYTE column);
void SetSpriteTile3(UBYTE sprite);
void SetSpriteTile4(UBYTE sprite);
void SetSpriteAttribute(UBYTE sprite, UBYTE attribute);
void SetOamByte(UWORD offset, UBYTE v);         /* wOamBuffer[offset] = v if changed */
void ClearSprites012(void);
void PositionWavSprites(void);

void DrawPhraseScreen(void);        /* $04:$4CE5 */
void DrawGrooveScreen(void);        /* $1D:$41DA */
void DrawChainScreen(void);         /* $06:$5947 */
void DrawSongScreen(void);          /* $07:$4574 */
void DrawTableScreen(void);         /* $05:$4762 */
void DrawInstrumentScreen(void);    /* $03:$4DE7 */
void DrawWaveScreen(void);          /* $06:$4F18 */
void DrawProjectScreen(void);       /* $1B:$5C17 */
void DrawSynthScreen(void);         /* $1D:$68AB */
void DrawWordScreen(void);          /* $06:$41A2 */
void DrawFileScreen(void);          /* $1C:$57BB */
void DrawHelpScreen(void);          /* $1B:$4A62 */

/* --- bank 1 ---------------------------------------------------------------- */

void ShowMessage(char *text);
void DrawMessageBar(void);
void DrawTopBar(void);
void DrawScreenPaletteMap(void);
void DrawOnOffName(UBYTE v);
void DrawSpeedModeName(UBYTE v);
void DrawSpeedGlyph(UBYTE v);
void CloneInstrumentTable(void);

/* --- bank 3: instrument screen --------------------------------------------- */

void WriteCurrentInstrumentByte(UBYTE field, UBYTE value);
void DrawInstrumentCursor(void);
void EraseInstrumentCursor(void);
UBYTE FindFreeSynth(void);
UBYTE SynthWavesModified(UBYTE synth);
void PreviousInstrumentTableMode(void);
UBYTE GetWaveLoopPosition(void);
UBYTE GetInstrumentType3(void);
void WriteKitNumber(UBYTE byte, UBYTE kit);
void InstrumentEnvelopeChanged(void);
void EnsureKitExists(void);
void CloneSynth(void);
void SetInstrumentTableOn(void);
UBYTE IsInstrumentTableOn(void);
UBYTE GetWavePlayMode(void);
UBYTE WavePlayIsManual(void);
void SetInstrumentPitchByte(UBYTE v);
void PreviousUsedInstrument(void);
UBYTE GetWaveLength(void);
UBYTE GetWaveSpeed(void);
UBYTE GetVibratoShape(void);
void SetVibratoShape(char v);               /* the shape byte is signed here: its OR reads it through a cast */

/* --- bank 5 ---------------------------------------------------------------- */
void RestoreSelectionState(void);           /* $05:$747D (far) */

void DrawMapTopRow(void);

/* --- bank $1B: project screen ---------------------------------------------- */

void UpdateInstrumentEnvelope(void);
void ColorField(UBYTE a, UBYTE b, UBYTE c, UBYTE d);
UBYTE SyncModeHasParameter(void);

/* --- bank $1F -------------------------------------------------------------- */

void DrawInstrumentTableField(void);
void DrawInstrumentTableMode(void);
UBYTE InstrumentEnvelopeHasSecond(void);
UBYTE InstrumentEnvelopeHasThird(void);
UBYTE FindFreeTable(void);
void DrawEnvelopeFields(void);

#endif /* LSDJ_H */
