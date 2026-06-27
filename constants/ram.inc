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
DEF wClipboardType   EQU $C411  ; SCREEN_* data-type held in the cut/copy clipboard (0 = empty). Set by
                                ; CUT (A+B) / COPY on each screen to that screen's id, checked by PASTE
                                ; (SEL+A). Verified in emulator: 0 -> $04 after cutting a SONG chain (and it
                                ; is unchanged by navigation) -- NOT the drawn screen.
DEF wProjectCursor   EQU $C566  ; Project screen cursor/selection (with $C567)
DEF wProjectCursor2  EQU $C567
DEF wGroove          EQU $C417  ; current groove number being edited (0-31, Groove screen)
DEF wChain           EQU $C418  ; current chain number being edited (0-127, Chain screen)
DEF wTable           EQU $C41B  ; current table number being edited (0-31, Table screen)
DEF wSynth           EQU $C42A  ; current softsynth number being edited (0-15, Synth screen)

; Cut/copy SELECTION rectangle in grid coords ($C40D-$C410), then wClipboardType.
; Set to the cursor cell on CUT (A+B) / COPY; read by cut/copy/paste/clone. Verified
; in emulator: (wSelLeft,wSelTop) = cursor (col,row), (wSelRight,wSelBottom) = far
; corner (e.g. SONG cut at col 2 -> 2,0,3,1; PHRASE cut at col1/row7 -> 1,7,3,8).
; These were mislabeled wCursorX/wCursorY -- they are the selection's X1/X2 *columns*,
; NOT a live cursor. The live edit cursor is per-screen (col/row): SONG wSongCol/wSongRow,
; CHAIN wChainCol/wChainRow, PHRASE wPhraseCol/wPhraseRow, etc.
DEF wSelLeft         EQU $C40D  ; selection rect left column (x1)
DEF wSelTop          EQU $C40E  ; selection rect top row (y1)
DEF wSelRight        EQU $C40F  ; selection rect right column (x2, exclusive)
DEF wSelBottom       EQU $C410  ; selection rect bottom row (y2, exclusive)
DEF wPhraseCol       EQU $C415  ; Phrase editor cursor column (0=NOTE 1=INSTR 2=CMD-type 3=CMD-param)
DEF wPhraseRow       EQU $C416  ; active phrase step/row (0-15); Phrase cursor row + playback position (bank $02)
; Per-screen edit cursors (col/row), verified live in the emulator by moving the cursor
; and watching the byte change. SONG columns are the 4 channels (PU1/PU2/WAV/NOI).
DEF wChainCol        EQU $C419  ; CHAIN editor cursor column (0=phrase, 1=transpose); LEFT/RIGHT move it
DEF wChainRow        EQU $C41A  ; CHAIN editor cursor row (0-15); DOWN/UP move it
DEF wSongScroll      EQU $C41C  ; SONG screen scroll = top visible row (on-screen row = wSongRow - wSongScroll);
                                ; verified: cursor row 23 -> scroll 8 (16-row window). SongCursorPageUp=0/PageDown=$B0
DEF wSongCol         EQU $C41D  ; SONG cursor column = channel (0=PU1 1=PU2 2=WAV 3=NOI); indexes per-channel song
                                ; arrays (e.g. [$C453+col]). RIGHT/LEFT move it. Also reused as scratch by some draws
DEF wSongRow         EQU $C41E  ; SONG cursor row (0-255); DOWN/UP move it (drives wSongScroll)
DEF wInstrField      EQU $C427  ; Instrument editor: current parameter/field index (reset to 0 on screen draw)
DEF wTableCol        EQU $C92F  ; Table editor: current column/cursor state (bit7 = special)
DEF wTableRow        EQU $C930  ; Table editor: current row cursor (0-15); pairs with wTableCol
; Table editor cell base pointers (16-bit) into the working table command data;
; TableGetCellA/B read [ptr + wTable*32 + row] to fetch a CMD1/CMD2 cell byte.
DEF wTableCellAPtr   EQU $C447  ; 16-bit base ptr for TABLE command column A (CMD1); TableGetCellA
DEF wTableCellBPtr   EQU $C449  ; 16-bit base ptr for TABLE command column B (CMD2); TableGetCellB
DEF wGrooveRow       EQU $C9D4  ; Groove editor: current step/row cursor (0-15)
DEF wSynthCursor     EQU $C442  ; Synth editor: current parameter/cursor index
DEF wPlayMode        EQU $C4D2  ; sound playback mode: 0=stopped, nonzero=playing (value = play scope;
                                ; verified in emulator: 4 when started from SONG, 1 from PHRASE). Main
                                ; loop runs SndRunFrame only when nonzero.
DEF wRetrigger       EQU $C4CD  ; sound retrigger/hop flag (SndTick: nonzero -> rerun the channel chain this tick)
; Pitch-modulation accumulators $C4D4-$C4E3 (current channel): two 16-bit sweep
; pairs. Acc A=$C4D4 steps by +$10 into $C4D6, clamped to limits $C4DC/$C4DE;
; Acc B=$C4D8 steps into $C4DA, clamped to $C4E0/$C4E2 (see SndStepAcc*/SndClampAcc*).
DEF wPitchModA       EQU $C4D4  ; pitch-mod sweep accumulator A (16-bit)
DEF wPitchModB       EQU $C4D8  ; pitch-mod sweep accumulator B (16-bit)
; --- song (de)compression cursors (bank $1C; see docs/save_format.md) ----------
DEF wCompBank        EQU $C979  ; SRAM bank used for compressed block I/O
DEF wCompSrc         EQU $C97B  ; compressed-stream read pointer (decompress input), 16-bit
DEF wCompDst         EQU $C97D  ; (de)compression output pointer into the working song, 16-bit
DEF wDecompPos       EQU $C983  ; cursor into the $DA00 decompress scratch buffer

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
DEF wFxCommand       EQU $C8C4  ; current FX command index (0-$12) being processed for the channel; argument in
                                ; hFxValue. Set from wChanCmd, dispatched via the jump table at $47A2 (DispatchFxCommand)

; --- Sound-engine channel state (struct-of-arrays, 4 channels: PU1/PU2/WAV/NOI)
; Confirmed from a live RAM dump while playing: the driver keeps per-channel
; 16-bit *data* pointers into the SRAM song ($A000-$BFFF) -- NOT code pointers.
; (So the audio engine, like the screens, uses static dispatch + RAM data ptrs;
; LSDj has no RAM-indirect code dispatch. See docs/reversal.md.)
; Engine = bank $02; the current channel index (0-3) lives in HRAM (hSoundCh).
; hSoundCh maps to the GB hardware channels: 0=PU1 (rAUD1), 1=PU2 (rAUD2),
; 2=WAV/KIT (rAUD3, plays WAVE+KIT instruments), 3=NOI (rAUD4). The per-channel
; computed frequency (wChanFreq) is written to rAUDxLOW/HIGH each frame.
DEF wChanFreq        EQU $C0F4  ; [4] 16-bit GB period per channel; = wNotePeriods[note+xpose] -> rAUDxLOW/HIGH
DEF wNoteTranspose   EQU $C4D3  ; transpose offset added to the note before the wNotePeriods lookup
DEF wNotePeriods     EQU $CF28  ; runtime 16-bit GB note-period table (computed for tuning); NoteToPeriod indexes it
DEF wSoundParams     EQU $C0D0  ; raw 16-byte instrument copied here on note-on from SONG_INSTRUMENTS
                                ; ($3080 = SRAM bank1 $B080 + instr*16); SetupChannelSound then
                                ; distributes its bytes to the rAUDx register shadows
; Per-channel sound state arrays (all [4] = PU1/PU2/WAV/NOI), derived from live
; RAM dumps cross-checked with the working-song SRAM map (see docs/save_format.md).
DEF wChanModFlag     EQU $C0A0  ; [4] modulation (vibrato/sweep) active flag
DEF wChanModType     EQU $C0A4  ; [4] modulation type
; Vibrato state, per channel, from the V command (VXY: X=speed, Y=depth) in FxCmd_V.
; For the WAV channel (ch 2) the type/phase slots are seeded from the instrument by
; SetupSoundWave (which writes base+2 = channel 2's slot).
DEF wChanVibSpeed    EQU $C314  ; [4] 16-bit signed vibrato increment (V high nibble via table; negated by dir)
DEF wChanVibDepth    EQU $C324  ; [4] vibrato depth (V low nibble via table $7D81)
DEF wChanVibType     EQU $C328  ; [4] vibrato type/shape (compared to 1 in FxCmd_V)
DEF wChanVibPhase    EQU $C32C  ; [4] vibrato direction/phase (gates the speed negation with wChanVibType)
DEF wChanCmdStep     EQU $C34E  ; [4] per-tick step/phase counter for the cycling commands chord (FxCmd_C) and
                                ; wave-frame (FxCmd_F): reset by those handlers, incremented each tick by WriteChannelFreq
DEF wChanVibShape    EQU $C35F  ; [4] vibrato waveform select: FxCmd_V picks table $7DB1 (nonzero) vs $7D91 (zero).
                                ; For the WAV channel (ch2) seeded from the instrument's vib setting by SetupSoundWave
DEF wChanActiveInstr EQU $C0C8  ; [4] instrument currently loaded into the sound engine per channel ($FF=none);
                                ; set by SndLoadChannelInstr, gates note processing in ProcessChannel/NoteToPeriod.
                                ; Distinct from wChanInstr ($C210), the *sequenced* instrument from the phrase.
DEF wChanNote        EQU $C0CC  ; [4] current note value per channel
DEF wChanState       EQU $C0E0  ; start of the per-channel byte-state arrays
DEF wChanActive      EQU $C0E0  ; [4] per-channel active flag; ProcessChannel runs only if nonzero
DEF wChanRestart     EQU $C0E4  ; [4] per-channel (re)start request; SndChanStateMachine sets, SndStartChannel consumes
DEF wChanOutNote     EQU $C0E8  ; [4] effective note used for frequency: bank0 L1BA7 looks up the period as
                                ; wNotePeriods[wChanOutNote + $7F + offset]. Set = wChanNote each tick by
                                ; WriteChannelFreq; read/adjusted by the pitch-slide command (FxCmd_L)
DEF wChanNoteTrigger EQU $C0F0  ; [4] per-channel "advance to next note step" flag: set nonzero when the note
                                ; countdown expires (SndReadChanState/SndReadChainEnd), consumed by ProcessChannel
                                ; which clears it and reads+plays the next note/command. Verified live (watchpoint).
DEF wChanFxSlots     EQU $C2F0  ; [12] per-channel active FX-command slots (3 slots x 4 channels, column-major)
DEF wChanChainTrans  EQU $C3FC  ; [4] per-channel chain-transpose shadow (from SndReadChainTranspose)
; Sequencer position (song row -> chain -> phrase -> note step). Verified against a
; live RAM dump + the SRAM map (docs/save_format.md):
DEF wChanSongRow     EQU $C200  ; [4] current song row, top of hierarchy (AdvanceSongRow walks this)
DEF wChanChain       EQU $C170  ; [4] current chain number per channel (CHAIN_ASSIGNMENTS[songRow][ch])
DEF wChanChainRow    EQU $C17C  ; [4] current row within the chain (0-15)
DEF wChanPhrase      EQU $C168  ; [4] current phrase = CHAIN_PHRASES[chain][chainRow] (set by ReadChainPhrase)
DEF wChanPhraseRow   EQU $C16C  ; [4] current note step within the phrase (0-15)
DEF wChanNextPhrase  EQU $C100  ; [4] next chain row's phrase, prefetched
DEF wChanNextSongRow EQU $C2FC  ; [4] next song row, prefetched
DEF wChanReadPtr     EQU $C110  ; [4] 16-bit SRAM cursor; PHRASE_NOTES (bank0) / PHRASE_COMMANDS (bank2),
                                ; same offset $A000+phrase*16+step, selected by SRAM bank
DEF wChanCmdValPtr   EQU $C128  ; [4] 16-bit cursor into PHRASE_COMMAND_VALUES (bank2 $AFF0+)
DEF wChanInstrPtr    EQU $C140  ; [4] 16-bit cursor into PHRASE_INSTRUMENTS (bank3 $B000+)
DEF wChanGroove      EQU $C150  ; [4] current groove per channel
DEF wChanGrooveRow   EQU $C158  ; [4] current row within the groove
DEF wChanTablePlayed EQU $C154  ; [4] flag: channel has played its table
DEF wChanNoteCountdown EQU $C160 ; [4] ticks until the next note step
DEF wChanTableCountdown EQU $C164 ; [4] ticks until the next table step
DEF wChanTable       EQU $C204  ; [4] current table per channel
DEF wChanTableRow1   EQU $C208  ; [4] table command-1 playback row
DEF wChanTableRow2   EQU $C20C  ; [4] table command-2 playback row
DEF wChanInstr       EQU $C210  ; [4] current instrument number per channel
DEF wChanChord       EQU $C218  ; [4] chord command value (CXY: X/Y semitone offsets); 0 = off. Set by FxCmd_C
; Current-channel table-playback cursors (16-bit SRAM addresses):
DEF wTableTransposePtr EQU $C10E ; -> table_transposes
DEF wTableCmd1Ptr    EQU $C11E  ; -> table_cmd1
DEF wTableCmd2Ptr    EQU $C126  ; -> table_cmd2
DEF wTableCmd1ValPtr EQU $C136  ; -> table_cmd1 values
DEF wTableCmd2ValPtr EQU $C13E  ; -> table_cmd2 values
DEF wChanCmd         EQU $C365  ; [4] command read for the current step, per channel
DEF wChanCmdVal      EQU $C36B  ; [4] command value for the current step, per channel
; Current-channel state pointers (16-bit; low byte seeded by InitSoundStatePtrs,
; high byte set on channel select). These match the InitSoundStatePtrs cluster
; ($C8DF=$7C->wChanChainRow, etc.):
DEF wPtrChainRow     EQU $C8DE  ; -> wChanChainRow[curChan]
DEF wPtrPhraseRow    EQU $C8E0  ; -> wChanPhraseRow[curChan]
DEF wPtrNoteCountdown EQU $C8E2 ; -> wChanNoteCountdown[curChan]
DEF wPtrGroove       EQU $C8E4  ; -> wChanGroove[curChan]
DEF wPtrGrooveRow    EQU $C8E6  ; -> wChanGrooveRow[curChan]
DEF wPtrPhrase       EQU $C8EA  ; -> wChanPhrase[curChan]
DEF wPtrNote         EQU $C8EC  ; -> wChanNote[curChan]
; Per-channel GB-sound register shadows, written to hardware by TriggerNote:
; Pitch-bend / slide state (per channel, 16-bit). The command handlers set a
; target + rate; the freq code (bank0 L19A6) steps the current pitch toward the
; target each tick by the rate and clamps on arrival. Reset on note-on.
DEF wChanPitchTarget EQU $C248  ; [4] 16-bit pitch-bend target period (0 = no bend active); set by FxCmd_L
DEF wChanPitchRate   EQU $C2D0  ; [4] 16-bit signed per-tick pitch step toward the target (sign in hi byte
                                ; $C2D1); set by ApplyPitchBend from the FX value; cleared on note-on
DEF wChanEnv         EQU $C2D8  ; [4] envelope value -> rAUDxENV (ch2/WAV = wave level rAUD3LEVEL)
DEF wChanLenDuty     EQU $C2DC  ; [4] length/duty value -> rAUDxLEN
DEF wChanFreqHi      EQU $C2E0  ; [4] freq-high + trigger/length-enable bits -> rAUDxHIGH
DEF wPU1Sweep        EQU $C2E4  ; PU1 sweep value -> rAUD1SWEEP

; --- Link cable sync (see docs/sync.md) -------------------------------------
DEF wFrameSkip       EQU $CB6F  ; SndRunFrame: frames to skip before the next sound tick (tempo/groove)
DEF wTickRepeat      EQU $CB70  ; SndRunFrame: extra sound ticks to run this frame (tempo/groove)
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
DEF hOamRefresh      EQU $FF80  ; OAM-refresh flag
DEF hSoundCh         EQU $FF8B  ; sound engine current channel index (0-3); indexes wChan* arrays
DEF hBgPalette       EQU $FF8C  ; CGB BG-map attribute palette for draws
DEF hBgDigitPalette  EQU $FF8D  ; CGB BG-map palette for digits
DEF hCurrentBank     EQU $FF8E  ; shadow of the selected ROM bank ($2000)
DEF hFxValue         EQU $FF8F  ; FX command value, staged before each FxCmd_* call (the handler's arg)
DEF hGameboyType     EQU $FF90  ; 0=DMG 1=CGB; read by draw code for palettes
DEF hOscilloscope    EQU $FFA0  ; oscilloscope sample buffer

ENDC

; --- Main loop ---------------------------------------------------------------
DEF wRedrawPending   EQU $C56C  ; MainLoop: nonzero -> run ScreenUpdateDispatch this frame
DEF wSynthRenderIdx  EQU $C9F0  ; MainLoop idle: which softsynth wave to render (0-15)

; --- Input (joypad queue: ReadJoypad enqueues, GetNextButtons dequeues) -------
DEF wButtons         EQU $C575  ; current button state (read 111x by every screen); bits below
DEF wButtonRing      EQU $CE00  ; ring buffer of recent button states (for key-repeat + buffered input)
DEF wButtonReadIdx   EQU $C56F  ; GetNextButtons read cursor into wButtonRing
DEF wButtonWriteIdx  EQU $C570  ; ReadJoypad write cursor into wButtonRing
; Key-repeat (autorepeat). ProcessInput ($11BE/$11C2) copies the two settings into
; the working timers on a fresh press, decrements the delay timer each frame, and
; only re-emits a held button once it reaches 0. Shown on PROJECT as "KEY DELAY/
; REPEAT 7/3". Verified in emulator: wKeyDelay=0 disables repeat (timer dec's 0 ->
; $FF => ~255-frame delay); wKeyRepeat sets only the post-delay rate (does not gate
; the first repeat). Changing the leading "7" is what turns repeat off.
DEF wKeyDelayTimer   EQU $C406  ; working countdown to the first repeat (reloads from wKeyDelay)
DEF wKeyRepeatTimer  EQU $C407  ; working countdown between repeats (reloads from wKeyRepeat)
DEF wKeyDelay        EQU $C52B  ; KEY DELAY setting: frames before a held button first repeats (0 = off)
DEF wKeyRepeat       EQU $C52C  ; KEY REPEAT setting: frames between repeats after the initial delay
; PROJECT-screen settings (working copies of song properties; loaded from SRAM
; $BFxx at screen entry -- see SONG_PROPERTIES). Verified in emulator by editing
; the PROJECT line and watching the byte + on-screen label change.
DEF wColorSet        EQU $C52F  ; LOOK colorset/palette (e.g. AIRY/KIWI); from SRAM $BFBE; clamped <=3 on DMG
DEF wCloneMode       EQU $C531  ; CLONE mode: 0=DEEP, 1=SLIM; from SRAM $BFC0
; Button bits in wButtons -- DIRECTIONS in the low nibble, BUTTONS in the high
; nibble (ReadJoypad $18BF builds dirs | (buttons<<4)). Verified against the code
; AND live emulator input (Down->$08, A(x)->$10, B(z)->$20, Select->$40, Start->$80):
DEF BTN_RIGHT        EQU 0
DEF BTN_LEFT         EQU 1
DEF BTN_UP           EQU 2
DEF BTN_DOWN         EQU 3
DEF BTN_A            EQU 4
DEF BTN_B            EQU 5
DEF BTN_SELECT       EQU 6
DEF BTN_START        EQU 7
