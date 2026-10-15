; LSDj screen indices -- the value in wCurrentScreen ($C401), used by
; DispatchScreenDraw ($254F) / DispatchScreenUpdate ($264B). See docs/editors.md.
;
; Identified by each screen's song-structure access and drawn strings; together
; they form LSDj's complete, canonical set of 12 screens. Indices 0/7/11 are
; unused/blank dispatch slots.

IF !DEF(SCREENS_INC)
DEF SCREENS_INC EQU 1

DEF SCREEN_NONE       EQU 0   ; no screen (no draw handler)
DEF SCREEN_PHRASE     EQU 1   ; bank $04:$4CE5  (draws phrase note/instr/cmd grid)
DEF SCREEN_GROOVE     EQU 2   ; bank $1D:$41DA
DEF SCREEN_CHAIN      EQU 3   ; bank $06:$5947
DEF SCREEN_SONG       EQU 4   ; bank $07:$4574  (draws 4-channel sequence)
DEF SCREEN_TABLE      EQU 5   ; bank $05:$4762
DEF SCREEN_INSTRUMENT EQU 6   ; bank $03:$4DE7  (reads instruments 119x; confirmed via live $C401=6)
DEF SCREEN_NAME_EDITOR EQU 7  ; the name editor (OpenNameEditor; no draw handler)
DEF SCREEN_WAVE       EQU 8   ; bank $06:$4F18
DEF SCREEN_PROJECT    EQU 9   ; bank $1B:$5C17
DEF SCREEN_SYNTH      EQU 10  ; bank $1D:$68AB
DEF SCREEN_SPEECH     EQU 11  ; the speech instrument's word list (no draw handler)
DEF SCREEN_WORD       EQU 12  ; bank $06:$41A2  (speech / words)
DEF SCREEN_FILE       EQU 13  ; bank $1C:$57BB
DEF SCREEN_HELP       EQU 14  ; bank $1B:$4A62

ENDC
