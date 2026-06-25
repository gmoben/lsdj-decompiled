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

DEF wCursorX         EQU $C40D  ; shared edit cursor X (column); reused by every screen
DEF wCursorY         EQU $C40F  ; shared edit cursor Y (row/step); reused by every screen

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
DEF wSoundParams     EQU $C0D0  ; staged sound-param buffer; SetupChannelSound copies the
                                ; instrument from SRAM here, then distributes to the rAUDx shadows
DEF wChanState       EQU $C0E0  ; start of the per-channel byte-state arrays
DEF wChanActive      EQU $C0E0  ; [4] per-channel active flag; ProcessChannel runs only if nonzero
; Sequencer position (song row -> chain -> phrase -> note step). Verified against a
; live RAM dump cross-checked with the working-song SRAM map (see docs/save_format.md):
DEF wChanSongRow     EQU $C200  ; [4] current song row, top of hierarchy (AdvanceSongRow walks this)
DEF wChanChain       EQU $C170  ; [4] current chain number per channel (CHAIN_ASSIGNMENTS[songRow][ch])
DEF wChanChainRow    EQU $C17C  ; [4] current row within the chain (0-15)
DEF wChanPhrase      EQU $C168  ; [4] current phrase = CHAIN_PHRASES[chain][chainRow] (set by ReadChainPhrase)
DEF wChanPhraseRow   EQU $C16C  ; [4] current note step within the phrase (0-15)
DEF wChanReadPtr     EQU $C110  ; [4] 16-bit SRAM cursor; PHRASE_NOTES (bank0) / PHRASE_COMMANDS (bank2),
                                ; same offset $A000+phrase*16+step, selected by SRAM bank
DEF wChanCmdValPtr   EQU $C128  ; [4] 16-bit cursor into PHRASE_COMMAND_VALUES (bank2 $AFF0+)
DEF wChanInstrPtr    EQU $C140  ; [4] 16-bit cursor into PHRASE_INSTRUMENTS (bank3 $B000+)
DEF wChanCmd         EQU $C365  ; [4] command read for the current step, per channel
DEF wChanCmdVal      EQU $C36B  ; [4] command value for the current step, per channel
; Per-channel GB-sound register shadows, written to hardware by TriggerNote:
DEF wChanEnv         EQU $C2D8  ; [4] envelope value -> rAUDxENV (ch2/WAV = wave level rAUD3LEVEL)
DEF wChanLenDuty     EQU $C2DC  ; [4] length/duty value -> rAUDxLEN
DEF wChanFreqHi      EQU $C2E0  ; [4] freq-high + trigger/length-enable bits -> rAUDxHIGH
DEF wPU1Sweep        EQU $C2E4  ; PU1 sweep value -> rAUD1SWEEP

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
DEF wButtons         EQU $C575  ; current button state (read 111x by every screen); bits below
DEF wButtonRing      EQU $CE00  ; ring buffer of recent button states (for key-repeat + buffered input)
DEF wButtonReadIdx   EQU $C56F  ; GetNextButtons read cursor into wButtonRing
DEF wButtonWriteIdx  EQU $C570  ; ReadJoypad write cursor into wButtonRing
; Button bits in wButtons (from ReadJoypad: buttons in low nibble, dirs in high):
DEF BTN_A            EQU 0
DEF BTN_B            EQU 1
DEF BTN_SELECT       EQU 2
DEF BTN_START        EQU 3
DEF BTN_RIGHT        EQU 4
DEF BTN_LEFT         EQU 5
DEF BTN_UP           EQU 6
DEF BTN_DOWN         EQU 7
