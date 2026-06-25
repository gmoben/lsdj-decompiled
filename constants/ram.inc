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
DEF wGroove          EQU $C417  ; current groove number being edited (0-31, Groove screen)
DEF wChain           EQU $C418  ; current chain number being edited (0-127, Chain screen)
DEF wTable           EQU $C41B  ; current table number being edited (0-31, Table screen)
DEF wSynth           EQU $C42A  ; current softsynth number being edited (0-15, Synth screen)

; --- Drawing helpers (used by DrawString/DrawHexByte/... in bank 0) ----------
DEF wStringSrc       EQU $C564  ; source pointer for DrawString (16-bit)
DEF wVramCursor      EQU $C573  ; VRAM tilemap dest ($98xx-$9Bxx, 16-bit LE; $C574=hi); draw helpers auto-increment it
DEF wPhrase          EQU $C414  ; current phrase number being edited ($FF=none, Phrase screen)
DEF wInstrument      EQU $C576  ; current instrument number (index into instruments[64][16])
DEF wStrBuffer       EQU $C6A9  ; shared text work buffer (StrCopy destination), used across banks
DEF wDebugTrapped    EQU $C7AF  ; set by DebugTrap so it fires only once

; --- Playback / timing ------------------------------------------------------
DEF wTimerReload     EQU $C578  ; tempo; copied to rTIMA each VBlank
DEF wSoundTick       EQU $C8F4  ; sound/playback tick counter (TimerHandler)

; --- Sound-engine channel state (struct-of-arrays, 4 channels: PU1/PU2/WAV/NOI)
; Confirmed from a live RAM dump while playing: the driver keeps per-channel
; 16-bit *data* pointers into the SRAM song ($A000-$BFFF) -- NOT code pointers.
; (So the audio engine, like the screens, uses static dispatch + RAM data ptrs;
; LSDj has no RAM-indirect code dispatch. See docs/reversal.md.)
; Engine = bank $02; the current channel index (0-3) lives in HRAM (hSoundCh).
; hSoundCh maps to the GB hardware channels: 0=PU1 (rAUD1), 1=PU2 (rAUD2),
; 2=WAV/KIT (rAUD3, plays WAVE+KIT instruments), 3=NOI (rAUD4). The per-channel
; computed frequency near $C0F2 is written to rAUDxLOW/HIGH each frame.
DEF wChanState       EQU $C0E0  ; start of the per-channel byte-state arrays
DEF wChanActive      EQU $C0E0  ; [4] per-channel active flag; ProcessChannel runs only if nonzero
DEF wChanNote        EQU $C170  ; [4] current note per channel (low 7 bits; high bit = flag)
DEF wChanReadPtr     EQU $C110  ; [4] phrase read cursor: note col (bank0) / command col (bank2), same offset
DEF wChanCmdValPtr   EQU $C128  ; [4] command-value cursor (bank2 phrase_fx_val, $AFxx)
DEF wChanInstrPtr    EQU $C140  ; [4] instrument cursor (bank3 phrase_instruments, $B0xx)
DEF wChanCmd         EQU $C365  ; [4] command read for the current step, per channel
DEF wChanCmdVal      EQU $C36B  ; [4] command value for the current step, per channel

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
DEF hSoundCh         EQU $FF8B  ; sound engine current channel index (0-3); indexes wChan* arrays
DEF hCurrentBank     EQU $FF8E  ; shadow of the selected ROM bank ($2000)
DEF hDrawParamLo     EQU $FF8C  ; scratch passed to screen draw helpers
DEF hDrawParamHi     EQU $FF8D

ENDC

; --- Main loop ---------------------------------------------------------------
DEF wRedrawPending   EQU $C56C  ; MainLoop: nonzero -> run ScreenUpdateDispatch this frame
DEF wSynthRenderIdx  EQU $C9F0  ; MainLoop idle: which softsynth wave to render (0-15)

; --- Input (joypad queue: ReadJoypad enqueues, GetNextButtons dequeues) -------
DEF wButtonRing      EQU $CE00  ; ring buffer of recent button states (for key-repeat + buffered input)
DEF wButtonReadIdx   EQU $C56F  ; GetNextButtons read cursor into wButtonRing
DEF wButtonWriteIdx  EQU $C570  ; ReadJoypad write cursor into wButtonRing
