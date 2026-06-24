; LSDj runtime RAM variables (WRAM/HRAM), as recovered so far.
;
; These are *runtime* state (cursors, counters, sync, screen), distinct from the
; working song in SRAM (see constants/song.inc) and the hardware registers (see
; include/hardware.inc). Reference file: addresses are confirmed in bank 0 /
; bank $1B; names are provisional where noted. Not yet INCLUDEd by the build.

IF !DEF(RAM_INC)
DEF RAM_INC EQU 1

; --- Screen / UI state ------------------------------------------------------
DEF wCurrentScreen   EQU $C401  ; 0-14; indexes the screen draw/update dispatch
DEF wIsCGB           EQU $C402  ; set at boot when running on a Game Boy Color
DEF wProjectCursor   EQU $C566  ; Project screen cursor/selection (with $C567)
DEF wProjectCursor2  EQU $C567

; --- Drawing helpers (used by DrawString/DrawHexByte/... in bank 0) ----------
DEF wStringSrc       EQU $C564  ; source pointer for DrawString (16-bit)
DEF wVramCursor      EQU $C574  ; VRAM tilemap destination ($98xx-$9Bxx, 16-bit)
DEF wDebugTrapped    EQU $C7AF  ; set by DebugTrap so it fires only once

; --- Playback / timing ------------------------------------------------------
DEF wTimerReload     EQU $C578  ; tempo; copied to rTIMA each VBlank
DEF wSoundTick       EQU $C8F4  ; sound/playback tick counter (TimerHandler)

; --- Link cable sync (see docs/sync.md) -------------------------------------
DEF wSyncMode        EQU $CB66  ; 0=OFF 1=LSDJ 3=MIDI 5=KEYBD 6=ANA.IN 7=AN.OUT
DEF wSyncMode2       EQU $CB67  ; secondary sync config (default 6)
DEF wSyncPosition    EQU $CBDB  ; sync position/index
DEF wSyncTxByte      EQU $CBDC  ; byte to transmit (master)
DEF wSyncRxByte      EQU $CBDD  ; last byte received (<$FD data, >=$FD transport)
DEF wSyncState       EQU $CBDE  ; serial state machine (0 idle/1 handshake/2 data)

; --- Boot identity ----------------------------------------------------------
DEF wBootHwId        EQU $CBD9  ; boot A register (DMG/CGB id), saved at init
DEF wBootAgbId       EQU $CBDA  ; boot B register (AGB id)

; --- Buffers ----------------------------------------------------------------
DEF wOamBuffer       EQU $DD00  ; OAM shadow buffer (DMA source)

; --- HRAM -------------------------------------------------------------------
DEF hCurrentBank     EQU $FF8E  ; shadow of the selected ROM bank ($2000)
DEF hDrawParamLo     EQU $FF8C  ; scratch passed to screen draw helpers
DEF hDrawParamHi     EQU $FF8D

ENDC
