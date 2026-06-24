; LSDj working-song memory map (32 KiB), as RGBDS constants.
;
; The working song lives in SRAM banks 0-3 ($A000-$BFFF). SONG_* values are
; offsets within the flat 32 KiB buffer; see docs/data_model.md for the SRAM
; banking. Reference file: INCLUDE it when carving song-access code.
; (Cross-checked with liblsdj/pylsdj; kit location confirmed in-ROM.)

IF !DEF(SONG_INC)
DEF SONG_INC EQU 1

; --- Counts -----------------------------------------------------------------
DEF NUM_PHRASES        EQU 255
DEF NUM_CHAINS         EQU 128
DEF NUM_SONG_STEPS     EQU 256   ; sequence length
DEF NUM_INSTRUMENTS    EQU 64
DEF NUM_TABLES         EQU 32
DEF NUM_SYNTHS         EQU 16
DEF NUM_GROOVES        EQU 32
DEF NUM_WORDS          EQU 42
DEF NUM_WAVES          EQU 256   ; 16 synths * 16 waves
DEF STEPS_PER_PHRASE   EQU 16
DEF STEPS_PER_CHAIN    EQU 16
DEF STEPS_PER_TABLE    EQU 16
DEF STEPS_PER_GROOVE   EQU 16
DEF FRAMES_PER_WAVE    EQU 32    ; nibbles
DEF INSTRUMENT_SIZE    EQU 16
DEF SYNTH_SIZE         EQU 16

; --- Offsets within the 32 KiB song buffer ----------------------------------
DEF SONG_PHRASE_NOTES        EQU $0000  ; [255][16]
DEF SONG_BOOKMARKS           EQU $0FF0  ; [64]
DEF SONG_GROOVES             EQU $1090  ; [32][16]
DEF SONG_SEQUENCE            EQU $1290  ; [256] x {pu1,pu2,wav,noi}
DEF SONG_TABLE_ENVELOPES     EQU $1690  ; [32][16]
DEF SONG_WORDS               EQU $1890  ; [42][16][2] speech allophones
DEF SONG_WORD_NAMES          EQU $1DD0  ; [42][4]
DEF SONG_MEM_INIT_FLAG_1     EQU $1E78  ; 'rb'
DEF SONG_INSTRUMENT_NAMES    EQU $1E7A  ; [64][5]
DEF SONG_TABLE_ALLOC         EQU $2020  ; [32]
DEF SONG_INSTR_ALLOC         EQU $2040  ; [64]
DEF SONG_CHAIN_PHRASES       EQU $2080  ; [128][16]
DEF SONG_CHAIN_TRANSPOSES    EQU $2880  ; [128][16]
DEF SONG_INSTRUMENTS         EQU $3080  ; [64][16]
DEF SONG_TABLE_TRANSPOSES    EQU $3480  ; [32][16]
DEF SONG_TABLE_CMD1          EQU $3680  ; fx[32][16] + val[32][16]
DEF SONG_TABLE_CMD2          EQU $3A80  ; fx[32][16] + val[32][16]
DEF SONG_MEM_INIT_FLAG_2     EQU $3E80  ; 'rb'
DEF SONG_PHRASE_ALLOC        EQU $3E82  ; 256 bits
DEF SONG_CHAIN_ALLOC         EQU $3EA2  ; 128 bits
DEF SONG_SYNTH_PARAMS        EQU $3EB2  ; [16][16]
DEF SONG_PROPERTIES          EQU $3FB2  ; clock/tempo/tune/sync/font/colorset/...
DEF SONG_PHRASE_FX           EQU $4000  ; [255][16] command column
DEF SONG_PHRASE_FX_VAL       EQU $4FF0  ; [255][16] command values
DEF SONG_WAVE_FRAMES         EQU $6000  ; [16][16][32 nibbles]
DEF SONG_PHRASE_INSTRUMENTS  EQU $7000  ; [255][16] instrument column
DEF SONG_MEM_INIT_FLAG_3     EQU $7FF0  ; 'rb'
DEF SONG_FORMAT_VERSION      EQU $7FFF

; --- SRAM banking: song offset -> (bank, $A000-relative address) ------------
DEF SRAM_SONG_BASE     EQU $A000   ; song banks are mapped here ($A000-$BFFF)
DEF SRAM_BANK_SIZE     EQU $2000
; e.g. SONG_CHAIN_PHRASES ($2080) -> SRAM bank 1, CPU $A080.

ENDC
