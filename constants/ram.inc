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
DEF wDrawStep        EQU $C405  ; multi-frame screen-redraw step: each screen's per-frame draw reads it, draws that step's part (cp 0/1/...), then increments; reset to 0 restarts a full redraw. Shared by all editor screens.
DEF wCursorMode      EQU $C413  ; editor cursor mode (read across screens; cp $04 commonly). Cross-checked
DEF wClipboardType   EQU $C411  ; SCREEN_* data-type held in the cut/copy clipboard (0 = empty). Set by
                                ; CUT (A+B) / COPY on each screen to that screen's id, checked by PASTE
                                ; (SEL+A). Verified in emulator: 0 -> $04 after cutting a SONG chain (and it
                                ; is unchanged by navigation) -- NOT the drawn screen.
; CGB BG-map attribute palettes copied to hBgDigitPalette/hBgPalette before a draw.
; Two contexts: dimmed/shadowed ($C566/7) and selected/highlighted ($C568/9). Verified:
; the draw helper copies $C568->hBgDigitPalette and $C569->hBgPalette. (These four were
; previously mislabeled wProjectCursor -- they are draw palette config, not a cursor.)
DEF wShadowDigitPal  EQU $C566  ; BG digit-palette attribute, dimmed/normal context
DEF wShadowPal       EQU $C567  ; BG tile-palette attribute, dimmed/normal context
DEF wSelectDigitPal  EQU $C568  ; BG digit-palette attribute, selected/highlighted context (-> hBgDigitPalette)
DEF wSelectPal       EQU $C569  ; BG tile-palette attribute, selected/highlighted context (-> hBgPalette)
DEF wGroove          EQU $C417  ; current groove number being edited (0-31, Groove screen)
DEF wChain           EQU $C418  ; current chain number being edited (0-127, Chain screen)
DEF wTable           EQU $C41B  ; current table number being edited (0-31, Table screen)
DEF wSynth           EQU $C42A  ; current softsynth number being edited (0-15, Synth screen)
DEF wWaveCursor      EQU $C42B  ; WAVE editor horizontal cursor = wave-frame position (verified: ranges 0..F as
                                ; LEFT/RIGHT move it); x4 in the draw for frame addressing. NB: not wSynth+1

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
DEF wPhraseCol       EQU $C415  ; Phrase editor cursor column (1=NOTE 2=INSTR 3=CMD-type 4=CMD-param; col 0 unreachable -- verified live via tools/emu/song.py)
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
DEF wInstrField      EQU $C427  ; Instrument editor: current parameter/field index (reset to 0 on screen draw) = cursor row
DEF wInstrCursorCol  EQU $C428  ; Instrument editor cursor column (pairs with wInstrField as the row)
DEF wSpeechCursorRow EQU $C429  ; Speech (word) instrument editor cursor row (DrawWordScreen)
; On-screen keyboard for entering the project/song name (ProjDrawNameField):
DEF wKbdLetterPos    EQU $C42C  ; keyboard: index of the letter being edited in the name
DEF wKbdCursorCol    EQU $C42D  ; keyboard cursor column
DEF wKbdCursorRow    EQU $C42E  ; keyboard cursor row
DEF wSampleNoteString EQU $C43A  ; [7] shared text buffer for a sample/kit note name (Phrase/Chain/Song note draws)
DEF wTableCol        EQU $C92F  ; Table editor: current column/cursor state (bit7 = special)
DEF wTableRow        EQU $C930  ; Table editor: current row cursor (0-15); pairs with wTableCol
DEF wDefaultNotes     EQU $C904  ; table/array of default note values (InstrScreenInput / InstrParamByte10 / InstrParamTableSet2; reset in ResetWorkState)
DEF wHelpCursorRow   EQU $C934  ; HELP/word screen cursor row (ProjHelpSelectPrev/Next/OpenSelected)
DEF wWordEditRow     EQU $C937  ; Word/speech editor: cursor row within the current word (inc/dec by nav); ReadWordCell indexes word data at +wWordEditRow*2
DEF wWordEditCol     EQU $C938  ; Word/speech editor: cursor column (0/1) within the row; ReadWordCell byte offset within the 2-byte unit
DEF wPlayMarkerTimer EQU $C950  ; Song screen: free-running blink counter incremented each SongUpdatePlayMarker; bit 3 (and $08) is the play-marker on/off phase
; Table editor column bases (addresses held as 16-bit integers): the selected
; command column ($B680 CMD1 or $BA80 CMD2, SRAM bank 1) and its value column
; ($B880 / $BC80); the accessors use [base + wTable*16 + step].
DEF wTableCellAPtr   EQU $C447  ; [1] 16-bit base of the table command column (CMD1/CMD2); TableReadCmd, TableWriteCmd
DEF wTableCellBPtr   EQU $C449  ; [1] 16-bit base of its value column; TableReadVal, TableWriteVal
DEF wGrooveRow       EQU $C9D4  ; Groove editor: current step/row cursor (0-15)
DEF wSynthScreen     EQU $C441  ; Synth editor: current synth screen/sub-state (pairs with wSynthCursor); Synth* owned
DEF wSynthCursor     EQU $C442  ; Synth editor: current parameter/cursor index
DEF wPlayMode        EQU $C4D2  ; sound playback mode: 0=stopped, nonzero=playing (value = play scope;
                                ; verified in emulator: 4 when started from SONG, 1 from PHRASE). Main
                                ; loop runs SndRunFrame only when nonzero.
DEF wPlayRestartPending EQU $C4CD  ; first tick after (re)start: send sync byte, seed RNG, skip stepping
; Pitch-modulation accumulators $C4D4-$C4E3 (current channel): two 16-bit sweep
; pairs. Acc A=$C4D4 steps by +$10 into $C4D6, clamped to limits $C4DC/$C4DE;
; Acc B=$C4D8 steps into $C4DA, clamped to $C4E0/$C4E2 (see SndStepAcc*/SndClampAcc*).
DEF wKit1LoopStart   EQU $C4D4  ; kit voice 1 loop-window start ptr lo (sample start + OFFSET*16; S cmd slides, latched into read ptr at wrap)
                                ; (bank00 $03d8, gated by wKitVoicesActive) streams via the wKit1*/wKit2* state,
                                ; reloading its read pointer from here. Same RAM, two mutually-exclusive features.
DEF wKit2LoopStart   EQU $C4D8  ; kit voice 2 loop-window start ptr lo
; Kit sample streaming (TimerHandler, bank00 $03d8): each tick an active kit voice copies 16 sample bytes
; from its banked ROM read pointer into hWavUploadBuf (-> WAV RAM), then loops (reload from wKit1LoopStart/wKit2LoopStart)
; or stops at its loop end. (First mislabeled an "oscilloscope"/hOscilloscope.) Kit-streamer only, not
; used by the pitch-mod stepper:
DEF wKitVoicesActive EQU $C8F6  ; kit streaming active bits: bit0 = voice 1, bit1 = voice 2 (TimerHandler gates)
DEF wKit1Bank        EQU $C4F4  ; kit voice 1 active ROM bank (selected before streaming)
DEF wKit2Bank        EQU $C4F5  ; kit voice 2 active ROM bank (speech: $19+n allophone bank)
DEF wKit1LoopEnabled EQU $C4E4  ; kit voice 1 loop flag (from kit bank loop-bits / instr B5 bit6)
DEF wKit2LoopEnabled EQU $C4E5  ; kit voice 2 loop flag
; --- song (de)compression cursors (bank $1C; see docs/save_format.md) ----------
DEF wFileScroll       EQU $C99B  ; File screen: file-list scroll offset (top visible index). Screen row = wFileCursor
                                 ; - wFileScroll, bounded to 0..F (16 visible rows; DebugTrap otherwise). FileDrawFieldChecked
DEF wFileCursor      EQU $C999  ; file-list cursor index (0-$1F)
DEF wFilePromptActive EQU $C995  ; YES/NO prompt shown
DEF wFileField       EQU $C992  ; file-screen field cursor (0-2)
                                 ; once per field and it decrements to 0 (entry value 1/2 also selects which fields)
DEF wCompBank        EQU $C979  ; SRAM bank used for compressed block I/O
DEF wCompSrcHi       EQU $C97B  ; compressed-data source ptr hi
DEF wCompDst         EQU $C97D  ; (de)compression output pointer into the working song, 16-bit
DEF wCompByte        EQU $C97F  ; current byte being processed in song (de)compression: loaded via ReadSongByte and
                                ; compared against RLE markers ($20, $ff, run counts); set to $ff as a sentinel
DEF wDecompPos       EQU $C983  ; cursor into the $DA00 decompress scratch buffer
DEF wWorkSongPage    EQU $C986  ; ReadWorkingSongByte's cached SRAM page (song address high byte; $FF = none)

; --- Drawing helpers (used by DrawString/DrawHexByte/... in bank 0) ----------
DEF wStringSrc       EQU $C564  ; source pointer for DrawString (16-bit)
DEF wVramCursor      EQU $C573  ; [1] 16-bit VRAM tilemap dest ($98xx-$9Bxx, 16-bit LE; $C574=hi); draw helpers auto-increment it
DEF wPhrase          EQU $C414  ; current phrase number being edited ($FF=none, Phrase screen)
DEF wInstrument      EQU $C576  ; current instrument number (index into instruments[64][16])
DEF wStrBuffer       EQU $C6A9  ; shared text work buffer (strcpy destination), used across banks
DEF wSongReadBuf     EQU $C46D  ; [64] the song screen's window: 16 visible rows x 4 channels (SongReadRow, four ReadSongBytes of 16); also ReadSongBytes' shared scratch destination
                                ; in across banks); editors read a row/set (e.g. groove steps, cp $10) into it then render
DEF wMsgTimer        EQU $C7A9  ; frame countdown for the transient message (set 10/50/100/150, dec'd; gates its draw)
DEF wMsgStrPtr       EQU $C7AA  ; 16-bit pointer to a transient on-screen message string (set to wStrBuffer / the
                                ; command-help strings). While nonzero the main screen-redraw dispatcher skips a full redraw
DEF wDebugTrapped    EQU $C7AF  ; set by DebugTrap so it fires only once

; --- Playback / timing ------------------------------------------------------
DEF wLiveMode        EQU $C528  ; SONG-screen LIVE mode flag, 0=SONG / 1=LIVE (verified: SELECT+LEFT on SONG
                                ; toggles it, title + nav letter switch SONG<->LIVE). Boolean toggle at bank07 $4eda;
                                ; the playback engine branches on it. Cleared by SndInitPlayState
DEF wTempoMode       EQU $C52A  ; tempo mode (song property $BFCC): 0 = numeric BPM (wTempo), 1/2/3 = special tempo presets with fixed CGB/DMG timer values (GetTempoTimerValue / DrawTempo)
DEF wTempo           EQU $C529  ; song tempo in BPM (verified: PROJECT TEMPO 124->134 on edit). Also set by the
                                ; T command (FxCmd_T). wTimerReload is derived from it
DEF wTimerReload     EQU $C578  ; timer reload derived from wTempo; copied to rTIMA each VBlank
DEF wSoundTick       EQU $C8F4  ; sound/playback tick counter (TimerHandler)
DEF wWavLevel        EQU $C8F3  ; WAV channel (ch 2) output level: staged here, then written to rAUD3LEVEL + wChanEnv+2
DEF wSndUpdateFlags  EQU $C8F5  ; current channel's per-frame sound-update gate: nonzero -> run the vibrato/pitch-mod
                                ; stepper, wave-trigger and freq-write (each `or a,[hl]; ret z`); reset on channel
                                ; setup, bit7 set on note trigger, bit1 tested
DEF wPlaySeconds     EQU $C553  ; PLAY clock seconds (0-59; UpdatePlayClock advances it every 60 frames, carries into
                                ; wPlayMinutes); shown as "PLAY m:ss" on the project screen; reset on sound stop
DEF wPlayMinutes     EQU $C552  ; PLAY clock minutes (shown before the colon; >9 widens the field)
DEF wSyncSendAccum   EQU $C956  ; [1] 16-bit 16-bit serial/sync send-clock accumulator (TimerHandler, gated by $cb6e==1): -=8 each
                                ; timer tick; on underflow adds wSyncSendRate and calls SendByteStep. Reset on sound stop
DEF wSyncSendRate    EQU $C954  ; [1] 16-bit 16-bit reload added to wSyncSendAccum on underflow (sets the serial/sync byte-send rate)
DEF wIdleHalt        EQU $C68B  ; power-save idle/halt flag: set when MainLoop idle-halts (wIdleFrames>7); while set, VBlankHandler only ticks the work clock; cleared by activity (work-clock wrap, SerialHandler) or input
DEF wIdleFrames      EQU $C68C  ; frames since last activity (redraw/work/sync/input reset it to 0); when it exceeds 7, MainLoop enters the idle halt
DEF wWorkTimeAccum   EQU $C69D  ; 16-bit frame accumulator for the song work-time clock; wraps every $0e00 frames
                                ; (one minute) to add a tick to wWorkTimeTicks (verified: c69d advances during playback)
DEF wWorkTimeTicks   EQU $C577  ; pending whole-minute ticks for the work-time clock; UpdateWorkClock applies each to the
                                ; song clock in SRAM (minutes @ $bfb3 carry to hours @ $bfb2 at 60) and decrements this
DEF wWavPlayMode     EQU $C500  ; wav synth play mode (1 = stop at list end, 4 = manual; kit/off = 0)
                                ; 0 gates the wave-frame updater off (`ret z`), 1/2 select modes
DEF wSndDataPtr      EQU $C8E9  ; 16-bit working pointer the sound command/table reader (SndReadCmdStream) walks;
                                ; pointed at &wChanTableRow1[ch] (stride $04) or instrument-mode data (stride $40)
DEF wSndDataStride   EQU $C8FB  ; advance stride for wSndDataPtr ($04 = per-channel arrays, $40 = instrument mode)
DEF wFxChannel       EQU $C527  ; channel (0-3) the command/FX/table engine is processing (set from hSoundCh); some
                                ; FX OR in bit2/bit3 as mode flags (tested via `and $fc` before using it as an index)
DEF wChanCmdRate     EQU $C8C6  ; [4] command/table step rate = instrument COMMAND_RATE (byte 8); reload value for the countdown
DEF wChanCmdCountdown EQU $C8CA ; [4] ticks until the next command/table step; SndRunTableStep decrements it, and on 0
                                ; reloads it from wChanCmdRate and runs the step (SndDecodeCmdNibbles / SndNoiChordStep)
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
DEF wChanFreq        EQU $C0F4  ; channel frequency shadow, PU1 lo (rAUD1LOW)
DEF wSongTranspose   EQU $C444  ; global/project TRANSPOSE (working copy of the SRAM song property); SndTransposeNote
                                ; adds it to every note; displayed/edited on the PROJECT screen (ProjectScreenInput)
DEF wTransposeDirty  EQU $C445  ; transpose edit/commit flag: set by the PROJECT screen after refreshing wSongTransposeDisp; when set, ProcessInput commits wSongTransposeDisp -> wSongTranspose and saves it to SRAM ($BFB5), then clears it
DEF wSongTransposeDisp EQU $C446 ; displayed form of the song transpose (ProjectScreenInput / ProcessInput)
DEF wNoteTranspose   EQU $C4D3  ; PU2 instrument transpose (from instrument PU2_TRANSPOSE param), added before the period lookup
DEF wNotePeriods     EQU $CF28  ; runtime 16-bit GB note-period table (computed for tuning); NoteToPeriod indexes it
DEF wCurInstr        EQU $C0D0  ; current instrument record byte 0 = type (0 pulse, 1 wav, 2 kit, 3 noise); 16B staged from SramInstruments $B080+instr*16
                                ; ($3080 = SRAM bank1 $B080 + instr*16); SetupChannelSound then
                                ; distributes its bytes to the rAUDx register shadows
; Per-channel sound state arrays (all [4] = PU1/PU2/WAV/NOI), derived from live
; RAM dumps cross-checked with the working-song SRAM map (see docs/save_format.md).
DEF wChanModFlag     EQU $C0A0  ; [4] modulation (vibrato/sweep) active flag
DEF wChanModType     EQU $C0A4  ; [4] modulation type
; Vibrato state, per channel, from the V command (VXY: X=speed, Y=depth) in FxCmd_V.
; For the WAV channel (ch 2) the type/phase slots are seeded from the instrument by
; SetupSoundWave (which writes base+2 = channel 2's slot).
DEF wChanVibAccum    EQU $C308  ; [4] 16-bit per-channel vibrato LFO phase accumulator (advanced by wChanVibSpeed each step; high byte indexes the vib waveform via SndCalcVibOffset -> wChanVibOffset)
DEF wChanVibSpeed    EQU $C314  ; [4] 16-bit signed vibrato increment (V high nibble via table; negated by dir)
DEF wChanVibOffset   EQU $C31C  ; [4] 16-bit current vibrato pitch displacement; SndWriteFreqPU1 adds it to the period
                                ; when vibrato is active (wChanVibSpeed != 0). Verified live (residual $FFD8 on a vib channel)
DEF wChanVibDepth    EQU $C324  ; [4] vibrato depth (V low nibble via table $7D81)
DEF wChanVibType     EQU $C328  ; [4] vibrato type/shape (compared to 1 in FxCmd_V)
DEF wChanVibPhase    EQU $C32C  ; [4] vibrato direction/phase (gates the speed negation with wChanVibType)
DEF wChanPan         EQU $C352  ; [4] panning: instrument OUTPUT param (byte 7, L=bit0/R=bit1); NoteToPeriod passes it
                                ; to SndWritePanning (-> rNR51). The duty bits (6-7) of the same byte feed wChanLenDuty
DEF wChanCmdStep     EQU $C34E  ; [4] per-tick step/phase counter for the cycling commands chord (FxCmd_C) and
                                ; wave-frame (FxCmd_F): reset by those handlers, incremented each tick by WriteChannelFreq
DEF wChanVibShape    EQU $C35F  ; [4] vibrato waveform select: FxCmd_V picks table $7DB1 (nonzero) vs $7D91 (zero).
                                ; For the WAV channel (ch2) seeded from the instrument's vib setting by SetupSoundWave
; Per-channel instrument feature flags, decoded from instrument byte 5 by SetupSoundPU1
; (ch0) / SetupSoundWave (ch2, writes base+2). The same byte yields wChanVibShape/Phase/
; Type (bits 4/0/1-2). Named by their verified engine effect (the bit each consumer gates):
DEF wChanTableStep    EQU $C33F ; [4] byte5 bit3 = instrument TABLE-STEP param; gates the table-setup branch in SetupSoundPU1
DEF wChanSlideNoteSrc EQU $C330 ; [4] byte5 bit6: FxCmd_L slide reads the alt note source ($C0EC vs wChanOutNote) when set
DEF wChanTranspose    EQU $C333 ; [4] byte5 bit5 = instrument TRANSPOSE param (bit set = transpose OFF). When set,
                                ; SndTransposeNote skips adding wChanChainTrans to the note. Verified in our code.
DEF wChanWaveTrig     EQU $C35C ; [4] byte5 bit7: when set, ApplyPitchBend jumps to SndUpdateWavFreq instead of bending
DEF wChanActiveInstr EQU $C0C8  ; [4] instrument currently loaded into the sound engine per channel ($FF=none);
                                ; set by SndLoadChannelInstr, gates note processing in ProcessChannel/NoteToPeriod.
                                ; Distinct from wChanInstr ($C210), the *sequenced* instrument from the phrase.
DEF wChanNote        EQU $C0CC  ; [4] current note value per channel
DEF wChanState       EQU $C0E0  ; start of the per-channel byte-state arrays
DEF wChanActive      EQU $C0E0  ; [4] per-channel active flag; ProcessChannel runs only if nonzero
DEF wChanRestart     EQU $C0E4  ; [4] per-channel (re)start request; SndLiveStartQueued sets, SndStartChannel consumes
DEF wChanOutNote     EQU $C0E8  ; [4] effective note used for frequency: bank0 SndWriteFreqPU1 looks up the period as
                                ; wNotePeriods[wChanOutNote + $7F + offset]. Set = wChanNote each tick by
                                ; WriteChannelFreq; read/adjusted by the pitch-slide command (FxCmd_L)
DEF wChanNoteTrigger EQU $C0F0  ; [4] per-channel "advance to next note step" flag: set nonzero when the note
                                ; countdown expires (SndReadChanState/SndReadChainEnd), consumed by ProcessChannel
                                ; which clears it and reads+plays the next note/command. Verified live (watchpoint).
DEF wFxSlotCmdTimer  EQU $C230  ; [12] per-FX-slot command-step countdown (SndRunTableColumn, indexed by wFxChannel); when it hits 0, SndRunFxSlot runs the slot's command
DEF wFxSlotEnvTimer  EQU $C23C  ; [12] per-FX-slot envelope-step countdown (SndRunTableColumn); when it hits 0, SndTableEnv runs the table envelope step
DEF wChanFxSlots     EQU $C2F0  ; [12] per-channel active FX-command slots (3 slots x 4 channels, column-major)
DEF wChanPitchModActive EQU $C3F9 ; [3] per-channel "pitch is modulating" flag (PU1/PU2/WAV); set by SndUpdatePitchModFlag*, gates per-frame SndStepPitchVib+SndWriteFreq at $0584
DEF wChanChainTrans  EQU $C3FC  ; [4] per-channel chain-transpose shadow (from SndReadChainTranspose)
DEF wRng             EQU $C400  ; RNG state (verified: random value every frame, song stopped & no input);
                                ; read-modify-written (ld hl,wRng / sub a,[hl] / ld [hl],e). Used by the probability
                                ; commands (B "maybe play note", H "maybe hop", R retrig). NB: not wChanChainTrans+4
; Sequencer position (song row -> chain -> phrase -> note step). Verified against a
; live RAM dump + the SRAM map (docs/save_format.md):
DEF wChanSongRow     EQU $C200  ; [4] current song row, top of hierarchy (AdvanceSongRow walks this)
DEF wChanChain       EQU $C170  ; [4] current chain number per channel (CHAIN_ASSIGNMENTS[songRow][ch])
DEF wChanChainRow    EQU $C17C  ; [4] current row within the chain (0-15)
DEF wChanPhrase      EQU $C168  ; [4] current phrase = CHAIN_PHRASES[chain][chainRow] (set by ReadChainPhrase)
DEF wChanPhraseRow   EQU $C16C  ; [4] current note step within the phrase (0-15)
DEF wChanRowQueue    EQU $C100  ; queued chain-row value, PU1 (consumed at phrase end: moved to [$C1xx + wPtrChainRow], then cleared)
DEF wChanNextSongRow EQU $C2FC  ; [4] next song row, prefetched
DEF wChanStopQueued     EQU $C304  ; [4] live mode: stop the channel at its chain's end (SndLiveStopRange sets it, SndLiveChainEnd acts on it, the song screen blinks its marker); cleared by SndDeactivateChannel, SndStartChannels, SyncTickLSDJ
DEF wPhraseCmdPtr    EQU $C110  ; SRAM read ptr: phrase FX command, PU1 (SndReadPhraseCmd; init $0140)
                                ; same offset $A000+phrase*16+step, selected by SRAM bank
DEF wPhraseCmdValPtr EQU $C128  ; SRAM read ptr: phrase FX value, PU1
DEF wChanInstrPtr    EQU $C140  ; [4] 16-bit cursor into PHRASE_INSTRUMENTS (bank3 $B000+)
DEF wChanRetrigTimer EQU $C148  ; [4] per-channel retrigger countdown; decremented each sound tick, and on 0 reloads from wChanRetrigRate and retriggers the channel (rewrite NRx2 envelope + trigger note via rAUDxHIGH|$80)
DEF wChanRetrigRate  EQU $C14C  ; [4] per-channel retrigger period (0 = no retrigger); reloaded into wChanRetrigTimer
DEF wChanGroove      EQU $C150  ; [4] current groove per channel
DEF wChanGrooveRow   EQU $C158  ; [4] current row within the groove
DEF wChanTablePlayed EQU $C154  ; [4] flag: channel has played its table
DEF wChanNoteCountdown EQU $C160 ; [4] ticks until the next note step
DEF wChanTableCountdown EQU $C164 ; [4] ticks until the next table step
DEF wChanTable       EQU $C204  ; [4] current table per channel
DEF wChanTableRow1   EQU $C208  ; [4] table command-1 playback row
DEF wChanTableRow2   EQU $C20C  ; [4] table command-2 playback row
DEF wChanChordPhase  EQU $C21C  ; chord arp phase counter, PU1 (0/1/2: which chord nibble is active; SndDecodeCmdNibbles)
DEF wChanInstr       EQU $C210  ; [4] current instrument number per channel
DEF wChanChord       EQU $C218  ; [4] chord command value (CXY: X/Y semitone offsets); 0 = off. Set by FxCmd_C
; Current-channel table-playback cursors (16-bit SRAM addresses):
DEF wTableNotePtrNoi EQU $C10E  ; SRAM read ptr: table NOTE column, NOI
DEF wTableCmd1PtrNoi EQU $C11E  ; SRAM read ptr: table CMD1 column, NOI
DEF wTableCmd2PtrNoi EQU $C126  ; SRAM read ptr: table CMD2 column, NOI
DEF wTableCmd1ValPtrNoi EQU $C136  ; SRAM read ptr: table CMD1 value, NOI
DEF wTableCmd2ValPtrNoi EQU $C13E  ; SRAM read ptr: table CMD2 value, NOI
DEF wChanCmd         EQU $C365  ; [6] command read for the current step: the 4 phrase channels, then the channel's table CMD1 and CMD2 (slots 4, 5)
DEF wChanCmdVal      EQU $C36B  ; [6] its value, same slots (Z's digits while Z runs: SndZRepeat)
DEF wLastCmd        EQU $C371  ; [68] the last command that was not Z or H, for Z to repeat: the 4 phrase channels,
                                ; then each table's CMD1 (4 + table) and CMD2 ($24 + table)
DEF wLastCmdVal     EQU $C3B5  ; [68] its value, same slots
; Current-channel state pointers (16-bit; low byte seeded by InitSoundStatePtrs,
; high byte set on channel select). These match the InitSoundStatePtrs cluster
; ($C8DF=$7C->wChanChainRow, etc.):
; (addresses verified against the authoritative pointer-init block in bank0 @ $3C6F)
DEF wPtrChainRow     EQU $C8DF  ; -> wChanChainRow[curChan]
DEF wPtrPhraseRow    EQU $C8E1  ; -> wChanPhraseRow[curChan]
DEF wPtrNoteCountdown EQU $C8E3 ; -> wChanNoteCountdown[curChan]
DEF wPtrGroove       EQU $C8E5  ; -> wChanGroove[curChan]
DEF wPtrGrooveRow    EQU $C8E7  ; -> wChanGrooveRow[curChan]
DEF wPtrPhrase       EQU $C8EB  ; -> wChanPhrase[curChan]
DEF wPtrNoteSrc      EQU $C8ED  ; ptr to current channel's note-column SRAM-ptr slot (wPhraseNotePtr + ch*2; repointed in table mode)
DEF wPtrTableStep    EQU $C8FC  ; -> wChanTableStep[curChan]
; Per-channel GB-sound register shadows, written to hardware by TriggerNote:
; Pitch-bend / slide state (per channel, 16-bit). The command handlers set a
; target + rate; the freq code (bank0 SndStepPitchVibPU1) steps the current pitch toward the
; target each tick by the rate and clamps on arrival. Reset on note-on.
DEF wChanPitchTarget EQU $C248  ; [4] 16-bit pitch-bend target period (0 = no bend active); set by FxCmd_L
DEF wChanPitchRate   EQU $C2D0  ; [4] 16-bit signed per-tick pitch step toward the target (sign in hi byte
                                ; $C2D1); set by ApplyPitchBend from the FX value; cleared on note-on
DEF wChanPitchCur    EQU $C337  ; [4] 16-bit current pitch value (default $0080) that SndStepPitchVibPU1 slides toward
                                ; wChanPitchTarget by wChanPitchRate each tick, clamping on arrival; reset by WriteChannelFreq
DEF wChanFinetune    EQU $C696  ; per-pulse-channel finetune (PU1 $C696, PU2 $C697); SndWriteFreqPU1 subtracts it as a
                                ; fine (low-byte) pitch adjustment in the period calc. From instrument finetune param (byte $B)
DEF wChanPeriodTablePtr EQU $C356 ; [4] 16-bit per-channel pointer to the note-period table for pitch interpolation
                                ; (SndInterpPeriod). NoteToPeriod sets it to wNotePeriods ($CF28), or an alt table when
                                ; wChanSlideNoteSrc is set. Pitched channels only (ch0-2; noise excluded)
DEF wChanEnv         EQU $C2D8  ; [4] envelope value -> rAUDxENV (ch2/WAV = wave level rAUD3LEVEL)
; Per-channel software-envelope working struct, 7 bytes/channel (channel N at base + N*7:
; PU1 $CBBC, PU2 $CBC3, WAV $CBCA; set up by SetupSoundPU1/PU2/...). The three
; stage bytes are the instrument ENV params (bytes 1/9/A); SndTableEnv takes a stage's
; volume (hi nibble) into wChanEnv and steps the fade (lo nibble). Verified live + static.
; (The +N*7 slot for noise would land on $CBD1, but that byte is wOamDirty below -- the
; noise channel has no software-envelope struct here.)
DEF wOamDirty        EQU $CBD1  ; OAM shadow dirty flag: timer ($185F) calls hOamRefresh (OAM DMA) when nonzero; set by OAM-buffer writers (SetOAMByte, screen redraws)
DEF wEnvNode1Pu1     EQU $CBBC  ; envelope node 1, PU1: vol hi-nibble / fade-speed lo-nibble (SndInitEnvelope)
DEF wEnvNode2Pu1     EQU $CBBD  ; envelope node 2, PU1 (stage 1 target/speed)
DEF wEnvNode3Pu1     EQU $CBBE  ; envelope node 3, PU1 (stage 2 target/speed)
DEF wChanLenDuty     EQU $C2DC  ; [4] length/duty value -> rAUDxLEN
DEF wChanFreqHi      EQU $C2E0  ; [4] freq-high + trigger/length-enable bits -> rAUDxHIGH
DEF wChanSweep       EQU $C2E4  ; [4] per channel: PU1's sweep (-> rAUD1SWEEP); NOI's [3] is its note (wNoiNote, set from wNoiseNoteLatch on a retrig)
DEF wNoiNote         EQU $C2E7  ; current noise note number (wraps 0..$77, drives SetNoisePoly)

; --- Link cable sync (see docs/sync.md) -------------------------------------
DEF wFrameSkip       EQU $CB6F  ; SndRunFrame: frames to skip before the next sound tick (tempo/groove)
DEF wTickRepeat      EQU $CB70  ; SndRunFrame: extra sound ticks to run this frame (tempo/groove)
DEF wSyncMode        EQU $CB66  ; 0=OFF 1=LSDJ 3=MIDI 5=KEYBD 6=ANA.IN 7=AN.OUT
DEF wSyncMode2       EQU $CB67  ; secondary sync config (default 6); also the AN.OUT pulse period (TICKS/STEP) reloaded into wAnalogOutTimer
DEF wAnalogOutTimer  EQU $CB68  ; AN.OUT clock pulse-period countdown (SyncSendAnalogClock): reloaded from wSyncMode2; while >= 3 it emits $FF (pulse high) else $00; SyncStopTransport sets it to 3
DEF wSyncInPulses    EQU $CB77  ; AN.IN: received clock-pulse counter (incremented per nonzero rSB in SyncReceiveAnalogClock), reset relative to wSyncMode2 (TICKS/STEP) when a sequencer tick advances
DEF wSyncInGate      EQU $CB78  ; AN.IN: when nonzero, SyncReceiveAnalogClock skips processing the incoming pulse (gate/hold)
DEF wSyncTick        EQU $CB69  ; sync tick counter: $ff = idle; counts up and at 12 fires the sync pulse (DispatchSyncClock).
                                ; Verified live: $ff while LSDJ sync is idle (no partner)
DEF wSyncRun         EQU $CB6B  ; sync running flag: 1 = sync clock active, 0 = stopped (set by the sync start/stop
                                ; handlers; also gated to the SONG screen in LSDJ mode). Verified live: 0 while idle
DEF wSyncHandshake   EQU $CB6C  ; set =1 before SendSyncHandshake (handshake pending); cleared on sync stop
DEF wSyncRxPos       EQU $CB71  ; received sync position/row: ProcessSyncByte stores an incoming <$FD value here, and SyncSeekSongPos seeks playback to it (LSDJ mode)
DEF wSyncSendGate    EQU $CB6E  ; TimerHandler sync transport: 1 = run the wSyncSendAccum byte-send clock; >=2 = alt path
DEF wTileLoadNow     EQU $CBDB  ; tile-loader flag, bit1 = copy tiles immediately vs defer (set to 2 by ResetTileLoader, checked by LoadTilesToVram). NOT a sync position: only ever 0/2, and the GetTileLoadNow getter ($34F0) is dead code.
DEF wSyncTxByte      EQU $CBDC  ; byte to transmit (master)
DEF wSyncRxByte      EQU $CBDD  ; last byte received (<$FD data, >=$FD transport)
DEF wSyncState       EQU $CBDE  ; serial state machine (0 idle/1 handshake/2 data)

; --- dynamic VRAM tile loader (LoadTilesToVram / CopyTileData / ResetTileLoader) ---
DEF wTileLoadSlot    EQU $CBE1  ; current tile-load request: destination VRAM tile slot (CopyTileData writes $9000 + slot*16)
DEF wTileLoadSrc     EQU $CBE2  ; [1] 16-bit [16-bit] current tile-load request: source pointer to the 16-byte tile graphics
DEF wTileSlotAlloc   EQU $CBE4  ; running VRAM tile-slot allocator: stored as each request's id, advanced by its tile count
DEF wTileReqTable    EQU $CBE6  ; entry-0 source ptr of the tile-load request table (scan anchor: LoadTilesToVram walks 6 entries, stride 3)

; --- Boot identity ----------------------------------------------------------
DEF wBootHwId        EQU $CBD9  ; boot A register (DMG/CGB id), saved at init
DEF wBootAgbId       EQU $CBDA  ; boot B register (AGB id)

; --- Buffers ----------------------------------------------------------------
DEF wOamBuffer       EQU $DD00  ; [160] OAM shadow: 40 sprites x 4 bytes, DMA-copied by OAMDMARoutine

; --- HRAM -------------------------------------------------------------------
DEF hOamRefresh      EQU $FF80  ; OAM-refresh flag
DEF hSoundCh         EQU $FF8B  ; sound engine current channel index (0-3); indexes wChan* arrays
DEF hBgPalette       EQU $FF8C  ; CGB BG-map attribute palette for draws
DEF hBgDigitPalette  EQU $FF8D  ; CGB BG-map palette for digits
DEF hCurrentBank     EQU $FF8E  ; shadow of the selected ROM bank ($2000)
DEF hFxValue         EQU $FF8F  ; FX command value, staged before each FxCmd_* call (the handler's arg)
DEF hGameboyType     EQU $FF90  ; 0=DMG 1=CGB; read by draw code for palettes
DEF hWavUploadBuf    EQU $FFA0  ; [16] staged waveform pop-streamed into WAV RAM $FF30 (TimerHandler/LoadWaveRam)

ENDC

; --- Main loop ---------------------------------------------------------------
DEF wRedrawPending   EQU $C56C  ; MainLoop: nonzero -> run ScreenUpdateDispatch this frame
DEF wSynthRenderIdx  EQU $C9F0  ; MainLoop idle: which softsynth wave to render (0-15)

; --- Input (joypad queue: ReadJoypad enqueues, GetNextButtons dequeues) -------
DEF wButtons         EQU $C575  ; current button state (read 111x by every screen); bits below
DEF wButtonRing      EQU $CE00  ; ring buffer of recent button states (for key-repeat + buffered input)
DEF wPrelisten       EQU $C56E  ; PRELISTEN on/off setting (verified: poking 0 -> PROJECT shows OFF on redraw). Project setting
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
; Input edge/modifier state (set by ProcessInput / HandleButtonEvent, read by the screens):
DEF wButtonsSame     EQU $C408  ; 1 if wButtons unchanged since last frame, 0 if changed; ProcessInput gates the
DEF wAPressEnabled    EQU $C409  ; 1 = an A-press is allowed (set on button release, cleared while a button is held). ProcessInput
                                ; key-repeat timer reload on it (changed -> reload). Verified by tracing ProcessInput
DEF wSelectHeld      EQU $C40A  ; nonzero while SELECT is held alone (set by HandleButtonEvent); read by screens as the SELECT modifier
DEF wBHeld           EQU $C40B  ; nonzero while B is held alone (set by HandleButtonEvent); e.g. disables song-cursor left/right
DEF wAHeld           EQU $C40C  ; nonzero while A is held alone (set by HandleButtonEvent)
DEF wCursorButtonHold EQU $C467 ; cursor button-hold state (ProcessInput / editor cursors)
DEF wKeyDelay        EQU $C52B  ; KEY DELAY setting: frames before a held button first repeats (0 = off)
DEF wKeyRepeat       EQU $C52C  ; KEY REPEAT setting: frames between repeats after the initial delay
; PROJECT-screen settings (working copies of song properties; loaded from SRAM
; $BFxx at screen entry -- see SONG_PROPERTIES). Verified in emulator by editing
; the PROJECT line and watching the byte + on-screen label change.
DEF wColorSet        EQU $C52F  ; LOOK colorset/palette (e.g. AIRY/KIWI); from SRAM $BFBE; clamped <=3 on DMG
DEF wCloneMode       EQU $C531  ; CLONE mode: 0=DEEP, 1=SLIM; from SRAM $BFC0
DEF wClonePhraseMap  EQU $C532  ; [32] SongCloneChain scratch: [0..15] the source chain's phrases, [16..31] their clones (a phrase repeated in the chain is cloned once)
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

; --- screen map (SEL+direction navigation, bank 5 $4000-$42B2) ---
DEF wMapCol          EQU $C8FF  ; screen-map column 0-4 (middle row: SONG CHAIN PHRASE INSTR TABLE)
DEF wMapRow          EQU $C900  ; screen-map row 0-2 (0=PROJECT/WAVE/SYNTH, 1=main, 2=GROOVE)
DEF wMapProjectCol   EQU $C901  ; column where the PROJECT map entry currently sits
DEF wMapSynthCol     EQU $C902  ; column where the WAVE/SYNTH map entry currently sits
DEF wMapMoveDir      EQU $C903  ; last map move direction: 1=up 2=right 3=down 4=left
; --- selection save area (bank 5 Save/RestoreSelectionState) ---
DEF wSavedCursorMode EQU $CBD2  ; saved wCursorMode across screen-map navigation
DEF wSavedClipType   EQU $CBD3  ; saved wClipboardType
DEF wSavedSelLeft    EQU $CBD4  ; saved wSelLeft
DEF wSavedSelTop     EQU $CBD5  ; saved wSelTop
DEF wSavedSelRight   EQU $CBD6  ; saved wSelRight
; --- PS/2 keyboard (bank 7) ---
DEF wKbdHeldNote     EQU $CB7E  ; note currently held on the PS/2 keyboard (0=none)
DEF wKbdTranspose    EQU $CB91  ; keyboard octave/transpose offset added to KeyboardNoteMap notes

; --- name editor (bank 1b OpenNameEditor) ---
DEF wNameReturnScreen EQU $C42F ; screen to return to when the name editor closes
DEF wNameEditIsInstr  EQU $C430 ; nonzero = editing an instrument name
DEF wNameEditBuf     EQU $C431  ; [5] 5-char name being edited (instr/file/word/kit-sample)
DEF wFont            EQU $C52E  ; UI font selection 0-2 (project screen FONT; picks the TilesFontN blocks)
DEF wKbdInstrument   EQU $CB93  ; instrument played by the PS/2 keyboard (shown with name on the song screen)
DEF wColInstrument   EQU $C468  ; last-used instrument per channel column (4 bytes, set by SetColInstrument)

; --- session-evidenced RAM (readers named in parentheses) ---
DEF wColChain        EQU $C44F  ; chain under cursor per song column, 4 bytes (SongCloneChain / IsSongChainUsed)
DEF wColPhrase       EQU $C453  ; phrase per song column, 4 bytes (AllocNextFreePhrase scan start)
DEF wUsedTableMap    EQU $C47D  ; [32] 32-slot used-tables map (BuildUsedTablesMap); overlaps wSongReadBuf + 16
DEF wRowBufCol2      EQU $C48D  ; [16] shared row buffer, third column (phrase: commands; table: command 1; song: 3rd channel)
DEF wRowBufCol3      EQU $C49D  ; [16] shared row buffer, fourth column (phrase: command values; table: value 1)
DEF wRowBufCol4      EQU $C4AD  ; [16] shared row buffer, fifth column (table: command 2)
DEF wRowBufCol5      EQU $C4BD  ; [16] shared row buffer, sixth column (table: value 2)
DEF wTopBarStr       EQU $C555  ; top-bar/title string buffer (DrawTopBar draws it directly)
DEF wPlayFrames      EQU $C6A8  ; PLAY clock frame divider (UpdatePlayClock: 60 frames = 1 s, restarted at 1 every 4th
                                ; second to track the 59.73 Hz frame rate); 0 while stopped
DEF wHelpStrBuf      EQU $C7B0  ; [20] help/status string buffer (UiDrawHelpStr unrolled copy; wHelpSrcPtr follows)
DEF wHelpSrcPtr      EQU $C7C4  ; [1] 16-bit source pointer of the staged help string, 2 bytes
DEF wClipboardSave   EQU $C7C6  ; [252] ResetEditorState keeps wClipboardBuf here across ResetWorkState
DEF wPlayChanFirst   EQU $C8DD  ; first channel to start playing (SndStartChannels)
DEF wPlayChanLast    EQU $C8DE  ; last channel to start playing
DEF wSavedScreen     EQU $C8FE  ; wCurrentScreen, saved while ToggleSongPlayback poses as the song screen
DEF wFileBlockCounts EQU $C959  ; per-file block counts, 32 slots (CountFileBlocks)
DEF wFileNameBuf     EQU $C987  ; 8-char filename buffer (CopyFileName)
DEF wFileListActive  EQU $C998  ; cursor is inside the file list (not a string buffer)
DEF wProjCursor      EQU $C9A4  ; project-screen cursor row (ProjCursorFieldWidth index)
DEF wSynthBufA       EQU $CAB8  ; [64] 64-byte synth wave work buffer A (ClearSynthWaveBufs)
DEF wSynthBufB       EQU $CAF8  ; [64] 64-byte synth wave work buffer B
DEF wOverwriteStart  EQU $CB61  ; nonzero while the synth OVERWRITE? prompt is open (OverwritePromptInput clears it)
DEF wOverwriteEnd    EQU $CB62  ; the prompt's choice: 0 = left (accept: SynthClearFlag), 1 = right
DEF wLinkQueueCount EQU $CB7D  ; bytes waiting in the wLinkQueueBuf ring (ProcessLinkQueue appends, LinkDequeueByte pops)
DEF wLinkQueueHead   EQU $CB80  ; link-port ring buffer head (ProcessLinkQueue)
DEF wLinkQueueBuf    EQU $CB81  ; link-port ring buffer (ProcessLinkQueue)
DEF wEnvStagePu1     EQU $CBBF  ; current envelope stage 0-2, PU1 (UpdateEnvPU1 advances)
DEF wEnvStagePu2     EQU $CBC6  ; current envelope stage 0-2, PU2
DEF wEnvStageNoi     EQU $CBCD  ; current envelope stage 0-2, NOI (WAV has no envelope)
DEF wSavedSelBottom  EQU $CBD7  ; saved wSelBottom (Save/RestoreSelectionState)
DEF wDecompBuf       EQU $DA00  ; [256] 256-byte block staging buffer (FileWriteCompressedSram)
DEF wSpriteWavY      EQU $DD00  ; WAV display sprite 0 Y (ClearSprites012 / PositionWavSprites)
DEF wBlockDstPage    EQU $C97E  ; SRAM dest page hi byte for block save ($a0-$bf; FlushBlockToSram)
DEF wBlockDstBank    EQU $C97C  ; SRAM dest bank counter for block save (bumped on page wrap)

; --- array elements / region bytes (auto-derived; every RAM byte the ROM
; touches carries a symbol so the debugger never shows _RAM_) ---
DEF wChanModFlagPu2   EQU $C0A1  ; wChanModFlag[1] = Pu2 channel
DEF wChanModFlagWav   EQU $C0A2  ; wChanModFlag[2] = Wav channel
DEF wChanModFlagNoi   EQU $C0A3  ; wChanModFlag[3] = Noi channel
DEF wChanMVolPending EQU $C0A8  ; [4] M-cmd value latched | $80 per channel (handler $2885; exact consumer TBD)
DEF wPhraseNotePtr   EQU $C0BC  ; SRAM read ptr (16-bit): note column, PU1 -- phrase/table row source (reader: base+ch*2)
DEF wChanStepCountdown EQU $C0C4  ; steps until next note read, PU1 ($ff = halted; via wPtrStepCountdown)
DEF wChanNoteWav      EQU $C0CE  ; wChanNote[2] = Wav channel
DEF wChanNoteNoi      EQU $C0CF  ; wChanNote[3] = Noi channel
DEF wCurInstrB1      EQU $C0D1  ; instrument byte 1: pulse = envelope; wav/kit = volume (-> wWavLevel)
DEF wCurInstrB2      EQU $C0D2  ; instrument byte 2: pulse = transpose enable/flags (-> wNoteTranspose)
DEF wCurInstrB3      EQU $C0D3  ; instrument byte 3 (-> wChanFreqHi* on setup)
DEF wCurInstrB5      EQU $C0D5  ; instrument byte 5
DEF wCurInstrB6      EQU $C0D6  ; instrument byte 6
DEF wCurInstrB7      EQU $C0D7  ; instrument byte 7: pan (bits 0-1) | length bits (-> wChanPan/wChanLenDuty)
DEF wCurInstrB8      EQU $C0D8  ; instrument byte 8: table cmd rate (-> wChanCmdRate)
DEF wCurInstrB10     EQU $C0DA  ; instrument byte 10: envelope node 3 (pulse)
DEF wCurInstrB12     EQU $C0DC  ; instrument byte 12
DEF wChanActivePu2    EQU $C0E1  ; wChanActive[1] = Pu2 channel
DEF wChanActiveWav    EQU $C0E2  ; wChanActive[2] = Wav channel
DEF wChanActiveNoi    EQU $C0E3  ; wChanActive[3] = Noi channel
DEF wChanRestartPu2   EQU $C0E5  ; wChanRestart[1] = Pu2 channel
DEF wChanRestartWav   EQU $C0E6  ; wChanRestart[2] = Wav channel
DEF wChanRestartNoi   EQU $C0E7  ; wChanRestart[3] = Noi channel
DEF wChanOutNotePu1Hi EQU $C0E9  ; wChanOutNote[0] hi byte (Pu1)
DEF wChanOutNotePu2 EQU $C0EA  ; wChanOutNote[1]  (Pu2)
DEF wChanOutNotePu2Hi EQU $C0EB  ; wChanOutNote[1] hi byte (Pu2)
DEF wChanOutNoteWav EQU $C0EC  ; wChanOutNote[2]  (Wav)
DEF wChanFreqPu1Hi   EQU $C0F5  ; channel frequency shadow, PU1 hi (|$80 -> rAUD1HIGH on retrigger)
DEF wChanFreqPu2     EQU $C0F6  ; channel frequency shadow, PU2 lo
DEF wChanFreqPu2Hi   EQU $C0F7  ; channel frequency shadow, PU2 hi
DEF wChanFreqWav     EQU $C0F8  ; channel frequency shadow, WAV lo (rAUD3LOW)
DEF wChanFreqWavHi   EQU $C0F9  ; channel frequency shadow, WAV hi
DEF wChanGrooveTicks EQU $C0FC  ; ticks left in groove step, PU1: 0 -> wChanNoteTrigger + advance groove (SndCountdownStep)
DEF wTableNotePtr    EQU $C108  ; SRAM read ptr: table NOTE column, PU1 (lo nibble = table row; hop cmd writes it, SndTableHop)
DEF wTableNotePtrPu2 EQU $C10A  ; SRAM read ptr: table NOTE column, PU2
DEF wTableNotePtrWav EQU $C10C  ; SRAM read ptr: table NOTE column, WAV
DEF wPhraseCmdPtrPu2 EQU $C112  ; SRAM read ptr: phrase FX command, PU2
DEF wPhraseCmdPtrWav EQU $C114  ; SRAM read ptr: phrase FX command, WAV
DEF wPhraseCmdPtrNoi EQU $C116  ; SRAM read ptr: phrase FX command, NOI
DEF wTableCmd1Ptr    EQU $C118  ; SRAM read ptr: table CMD1 column, PU1 (SndReadTableCmd1)
DEF wTableCmd1PtrPu2 EQU $C11A  ; SRAM read ptr: table CMD1 column, PU2
DEF wTableCmd1PtrWav EQU $C11C  ; SRAM read ptr: table CMD1 column, WAV
DEF wTableCmd2Ptr    EQU $C120  ; SRAM read ptr: table CMD2 column, PU1 (SndReadTableCmd2)
DEF wTableCmd2PtrPu2 EQU $C122  ; SRAM read ptr: table CMD2 column, PU2
DEF wTableCmd2PtrWav EQU $C124  ; SRAM read ptr: table CMD2 column, WAV
DEF wTableCmd1ValPtr EQU $C130  ; SRAM read ptr: table CMD1 value, PU1
DEF wTableCmd2ValPtr EQU $C138  ; SRAM read ptr: table CMD2 value, PU1
DEF wChanRetrigTimerPu2  EQU $C149  ; wChanRetrigTimer[1] = Pu2 channel
DEF wChanRetrigTimerWav  EQU $C14A  ; wChanRetrigTimer[2] = Wav channel
DEF wChanRetrigTimerNoi  EQU $C14B  ; wChanRetrigTimer[3] = Noi channel
DEF wChanRetrigRatePu2  EQU $C14D  ; wChanRetrigRate[1] = Pu2 channel
DEF wChanRetrigRateWav  EQU $C14E  ; wChanRetrigRate[2] = Wav channel
DEF wChanRetrigRateNoi  EQU $C14F  ; wChanRetrigRate[3] = Noi channel
DEF wChanGroovePu2    EQU $C151  ; wChanGroove[1] = Pu2 channel
DEF wChanGrooveWav    EQU $C152  ; wChanGroove[2] = Wav channel
DEF wChanGrooveNoi    EQU $C153  ; wChanGroove[3] = Noi channel
DEF wChanTablePlayedPu2  EQU $C155  ; wChanTablePlayed[1] = Pu2 channel
DEF wChanTablePlayedWav  EQU $C156  ; wChanTablePlayed[2] = Wav channel
DEF wChanTablePlayedNoi  EQU $C157  ; wChanTablePlayed[3] = Noi channel
DEF wChanGrooveRowPu1Hi EQU $C159  ; wChanGrooveRow[0] hi byte (Pu1)
DEF wChanGrooveRowPu2 EQU $C15A  ; wChanGrooveRow[1]  (Pu2)
DEF wChanGrooveRowPu2Hi EQU $C15B  ; wChanGrooveRow[1] hi byte (Pu2)
DEF wChanGrooveRowWav EQU $C15C  ; wChanGrooveRow[2]  (Wav)
DEF wChanGrooveRowWavHi EQU $C15D  ; wChanGrooveRow[2] hi byte (Wav)
DEF wChanGrooveRowNoi EQU $C15E  ; wChanGrooveRow[3]  (Noi)
DEF wChanGrooveRowNoiHi EQU $C15F  ; wChanGrooveRow[3] hi byte (Noi)
DEF wChanTableCountdownPu2  EQU $C165  ; wChanTableCountdown[1] = Pu2 channel
DEF wChanTableCountdownWav  EQU $C166  ; wChanTableCountdown[2] = Wav channel
DEF wChanTableCountdownNoi  EQU $C167  ; wChanTableCountdown[3] = Noi channel
DEF wChanPhraseRowPu2  EQU $C16D  ; wChanPhraseRow[1] = Pu2 channel
DEF wChanPhraseRowWav  EQU $C16E  ; wChanPhraseRow[2] = Wav channel
DEF wChanPhraseRowNoi  EQU $C16F  ; wChanPhraseRow[3] = Noi channel
DEF wChanNoteOffset  EQU $C174  ; semitone offset added to the output note at freq computation, PU1 (SndWriteFreqPU1)
DEF wChanNoteOffsetPu2 EQU $C175  ; semitone note offset, PU2
DEF wChanNoteOffsetWav EQU $C176  ; semitone note offset, WAV
DEF wChanDelayTicks  EQU $C178  ; [4] accumulated D-cmd delay per channel (groove advance subtracts)
DEF wTableHopCounters EQU $C180  ; [32] table H-hop repeat counters, PU1 slice: 2 cols x 16 rows ($FF = unset)
DEF wTableHopCountersPu2 EQU $C1A0  ; [32] table H-hop repeat counters, PU2 slice
DEF wChanSongRowPu2   EQU $C201  ; wChanSongRow[1] = Pu2 channel
DEF wChanSongRowWav   EQU $C202  ; wChanSongRow[2] = Wav channel
DEF wChanSongRowNoi   EQU $C203  ; wChanSongRow[3] = Noi channel
DEF wChanTablePu2     EQU $C205  ; wChanTable[1] = Pu2 channel
DEF wChanTableWav     EQU $C206  ; wChanTable[2] = Wav channel
DEF wChanTableNoi     EQU $C207  ; wChanTable[3] = Noi channel
DEF wChanTableRow1Pu2  EQU $C209  ; wChanTableRow1[1] = Pu2 channel
DEF wChanTableRow1Wav  EQU $C20A  ; wChanTableRow1[2] = Wav channel
DEF wChanTableRow1Noi  EQU $C20B  ; wChanTableRow1[3] = Noi channel
DEF wChanTableRow2Pu2  EQU $C20D  ; wChanTableRow2[1] = Pu2 channel
DEF wChanTableRow2Wav  EQU $C20E  ; wChanTableRow2[2] = Wav channel
DEF wChanTableRow2Noi  EQU $C20F  ; wChanTableRow2[3] = Noi channel
DEF wChanInstrPu2 EQU $C211  ; wChanInstr[1] (PU2)
DEF wChanInstrWav EQU $C212  ; wChanInstr[2] (WAV)
DEF wChanInstrNoi EQU $C213  ; wChanInstr[3] (NOI)
DEF wChanHopCount EQU $C214  ; [4] H's hops left per channel: the high digit, counted down to the last hop (SndPhraseHop)
DEF wChanChordPu2     EQU $C219  ; wChanChord[1] = Pu2 channel
DEF wChanChordWav     EQU $C21A  ; wChanChord[2] = Wav channel
DEF wChanChordNoi     EQU $C21B  ; wChanChord[3] = Noi channel
DEF wChanChordPhasePu2 EQU $C21D  ; chord arp phase counter, PU2
DEF wChanChordPhaseWav EQU $C21E  ; chord arp phase counter, WAV
DEF wChanChordPhaseNoi EQU $C21F  ; chord arp phase counter, NOI
DEF wChanTableLatch  EQU $C220  ; table number latched at note-on, PU1
DEF wChanTableLatchPu2 EQU $C221  ; table number latched at note-on, PU2
DEF wChanTableLatchWav EQU $C222  ; table number latched at note-on, WAV
DEF wChanTableLatchNoi EQU $C223  ; table number latched at note-on, NOI
DEF wChanEnvVolOffset EQU $C224  ; signed volume delta (<<4) applied to the env-node triplet, PU1 (SndFxParsePitchVal -> SndInitEnvelope)
DEF wChanPitchTargetPu2 EQU $C24A  ; pitch slide target (16-bit), PU2
DEF wChanPitchTargetWav EQU $C24C  ; pitch slide target (16-bit), WAV
DEF wChanPitchSlots  EQU $C250  ; [128] per-channel 16-byte pitch-FX state block, reset to $80 center (8x FillChan16; layout TBD)
DEF wChanPitchRatePu1Hi EQU $C2D1  ; wChanPitchRate[0] hi byte (Pu1)
DEF wChanPitchRatePu2 EQU $C2D2  ; wChanPitchRate[1]  (Pu2)
DEF wChanPitchRatePu2Hi EQU $C2D3  ; wChanPitchRate[1] hi byte (Pu2)
DEF wChanPitchRateWav EQU $C2D4  ; wChanPitchRate[2]  (Wav)
DEF wChanPitchRateWavHi EQU $C2D5  ; wChanPitchRate[2] hi byte (Wav)
DEF wChanEnvPu2       EQU $C2D9  ; wChanEnv[1] = Pu2 channel
DEF wChanEnvWav       EQU $C2DA  ; wChanEnv[2] = Wav channel
DEF wChanEnvNoi       EQU $C2DB  ; wChanEnv[3] = Noi channel
DEF wChanLenDutyPu2   EQU $C2DD  ; wChanLenDuty[1] = Pu2 channel
DEF wChanFreqHiPu2    EQU $C2E1  ; wChanFreqHi[1] = Pu2 channel
DEF wChanFreqHiNoi    EQU $C2E3  ; wChanFreqHi[3] = Noi channel
DEF wChanTableActive EQU $C2E8  ; table engaged at note-on, PU1 (set 1 in Setup* .storeChanTable)
DEF wChanTableActivePu2 EQU $C2E9  ; table engaged at note-on, PU2
DEF wChanTableActiveWav EQU $C2EA  ; table engaged at note-on, WAV
DEF wChanTableActiveNoi EQU $C2EB  ; table engaged at note-on, NOI
DEF wChanTableFresh  EQU $C2EC  ; table freshly (re)started, PU1: buffers FF-filled, rows reset
DEF wChanTableFreshPu2 EQU $C2ED  ; table freshly restarted, PU2
DEF wChanTableFreshWav EQU $C2EE  ; table freshly restarted, WAV
DEF wChanTableFreshNoi EQU $C2EF  ; table freshly restarted, NOI
DEF wChanNextSongRowPu1Hi EQU $C2FD  ; wChanNextSongRow[0] hi byte (Pu1)
DEF wChanNextSongRowPu2 EQU $C2FE  ; wChanNextSongRow[1]  (Pu2)
DEF wChanNextSongRowPu2Hi EQU $C2FF  ; wChanNextSongRow[1] hi byte (Pu2)
DEF wChanLiveQueued  EQU $C300  ; [4] live mode: a song row is queued for this channel (SndLiveStartQueued; the play marker blinks faster while set)
DEF wChanVibAccumPu1Hi EQU $C309  ; vibrato phase accumulator hi, PU1 (16-bit += wChanVibSpeed)
DEF wChanVibAccumPu2 EQU $C30A  ; vibrato phase accumulator lo, PU2
DEF wChanVibAccumPu2Hi EQU $C30B  ; vibrato phase accumulator hi, PU2
DEF wChanVibAccumWav EQU $C30C  ; vibrato phase accumulator lo, WAV
DEF wChanVibAccumWavHi EQU $C30D  ; vibrato phase accumulator hi, WAV
DEF wChanVibAccumNoi EQU $C30E  ; vibrato phase accumulator lo, NOI
DEF wChanVibAccumNoiHi EQU $C30F  ; vibrato phase accumulator hi, NOI
DEF wChanVibWaveOfs  EQU $C310  ; vib waveform base offset (type*$40), PU1 (SndCalcVibOffset)
DEF wChanVibWaveOfsPu2 EQU $C311  ; vib waveform base offset, PU2
DEF wChanVibWaveOfsWav EQU $C312  ; vib waveform base offset, WAV
DEF wChanVibWaveOfsNoi EQU $C313  ; vib waveform base offset, NOI
DEF wChanVibSpeedPu1Hi EQU $C315  ; wChanVibSpeed[0] hi byte (Pu1)
DEF wChanVibSpeedPu2 EQU $C316  ; wChanVibSpeed[1]  (Pu2)
DEF wChanVibSpeedPu2Hi EQU $C317  ; wChanVibSpeed[1] hi byte (Pu2)
DEF wChanVibSpeedWav EQU $C318  ; wChanVibSpeed[2]  (Wav)
DEF wChanVibSpeedWavHi EQU $C319  ; wChanVibSpeed[2] hi byte (Wav)
DEF wChanVibSpeedNoi EQU $C31A  ; wChanVibSpeed[3]  (Noi)
DEF wChanVibSpeedNoiHi EQU $C31B  ; wChanVibSpeed[3] hi byte (Noi)
DEF wChanVibOffsetPu1Hi EQU $C31D  ; wChanVibOffset[0] hi byte (Pu1)
DEF wChanVibOffsetPu2 EQU $C31E  ; wChanVibOffset[1]  (Pu2)
DEF wChanVibOffsetPu2Hi EQU $C31F  ; wChanVibOffset[1] hi byte (Pu2)
DEF wChanVibOffsetWav EQU $C320  ; wChanVibOffset[2]  (Wav)
DEF wChanVibOffsetWavHi EQU $C321  ; wChanVibOffset[2] hi byte (Wav)
DEF wChanVibDepthPu2  EQU $C325  ; wChanVibDepth[1] = Pu2 channel
DEF wChanVibDepthWav  EQU $C326  ; wChanVibDepth[2] = Wav channel
DEF wChanVibDepthNoi  EQU $C327  ; wChanVibDepth[3] = Noi channel
DEF wChanVibTypePu2   EQU $C329  ; wChanVibType[1] = Pu2 channel
DEF wChanVibTypeWav   EQU $C32A  ; wChanVibType[2] = Wav channel
DEF wChanVibTypeNoi   EQU $C32B  ; wChanVibType[3] = Noi channel
DEF wChanVibPhasePu2  EQU $C32D  ; wChanVibPhase[1] = Pu2 channel
DEF wChanVibPhaseWav  EQU $C32E  ; wChanVibPhase[2] = Wav channel
DEF wChanVibPhaseNoi  EQU $C32F  ; wChanVibPhase[3] = Noi channel
DEF wChanSlideNoteSrcPu2  EQU $C331  ; wChanSlideNoteSrc[1] = Pu2 channel
DEF wChanSlideNoteSrcWav  EQU $C332  ; wChanSlideNoteSrc[2] = Wav channel
DEF wChanTransposePu2  EQU $C334  ; wChanTranspose[1] = Pu2 channel
DEF wChanTransposeWav  EQU $C335  ; wChanTranspose[2] = Wav channel
DEF wChanTransposeNoi  EQU $C336  ; wChanTranspose[3] = Noi channel
DEF wChanPitchCurPu1Hi EQU $C338  ; wChanPitchCur[0] hi byte (Pu1)
DEF wChanPitchCurPu2 EQU $C339  ; wChanPitchCur[1]  (Pu2)
DEF wChanPitchCurPu2Hi EQU $C33A  ; wChanPitchCur[1] hi byte (Pu2)
DEF wChanPitchCurWav EQU $C33B  ; wChanPitchCur[2]  (Wav)
DEF wChanPitchCurWavHi EQU $C33C  ; wChanPitchCur[2] hi byte (Wav)
DEF wChanTableStepPu2 EQU $C340  ; table step row, PU2
DEF wChanTableStepWav EQU $C341  ; table step row, WAV
DEF wChanTableStepNoi EQU $C342  ; table step row, NOI
DEF wChanChordOffset EQU $C343  ; current chord transpose (semitones), PU1 -- cycled by wChanChordPhase from the C-cmd nibbles
DEF wChanChordOffsetPu2 EQU $C344  ; current chord transpose, PU2
DEF wChanChordOffsetWav EQU $C345  ; current chord transpose, WAV
DEF wChanTableTrans  EQU $C347  ; current table transpose, PU1 (added into wChanNoteOffset)
DEF wChanTableTransPu2 EQU $C348  ; current table transpose, PU2
DEF wChanTableTransWav EQU $C349  ; current table transpose, WAV
DEF wChanTableTransNoi EQU $C34A  ; current table transpose, NOI
DEF wChanTableTransPrev EQU $C34B  ; previous table transpose (change detect), PU1
DEF wChanTableTransPrevPu2 EQU $C34C  ; previous table transpose, PU2
DEF wChanTableTransPrevWav EQU $C34D  ; previous table transpose, WAV
DEF wChanCmdStepPu2   EQU $C34F  ; wChanCmdStep[1] = Pu2 channel
DEF wChanCmdStepWav   EQU $C350  ; wChanCmdStep[2] = Wav channel
DEF wChanPanPu2      EQU $C353  ; wChanPan[1] = PU2 channel
DEF wChanPanWav       EQU $C354  ; wChanPan[2] = Wav channel
DEF wChanPanNoi      EQU $C355  ; wChanPan[3] = NOI channel
DEF wChanPeriodTablePtrPu2 EQU $C358  ; period-table ptr, PU2
DEF wChanPeriodTablePtrWav EQU $C35A  ; period-table ptr, WAV (NOI has no period table)
DEF wChanWaveTrigPu2  EQU $C35D  ; wChanWaveTrig[1] = Pu2 channel
DEF wChanWaveTrigWav  EQU $C35E  ; wChanWaveTrig[2] = Wav channel
DEF wChanVibShapePu1Hi EQU $C360  ; wChanVibShape[0] hi byte (Pu1)
DEF wChanVibShapePu2 EQU $C361  ; wChanVibShape[1]  (Pu2)
DEF wNoiPSpeed EQU $C362  ; wChanVibShape[3]: noise has no vibrato, so the slot holds P's speed (applied as S every tick)
DEF wChanVibShapeWav EQU $C363  ; wChanVibShape[2]  (Wav)
DEF wChanVibShapeWavHi EQU $C364  ; wChanVibShape[2] hi byte (Wav)
DEF wTableCmd1       EQU $C369  ; current-channel table CMD1 command (scratch)
DEF wTableCmd2       EQU $C36A  ; current-channel table CMD2 command (scratch)
DEF wChanCmdValWav EQU $C36D  ; wChanCmdVal[2] (WAV)
DEF wChanCmdValNoi EQU $C36E  ; wChanCmdVal[3] (NOI)
DEF wTableCmd1Val    EQU $C36F  ; current-channel table CMD1 value (scratch)
DEF wTableCmd2Val    EQU $C370  ; current-channel table CMD2 value (scratch)
DEF wChanPitchModActivePu2  EQU $C3FA  ; wChanPitchModActive[1] = Pu2 channel
DEF wChanPitchModActiveWav  EQU $C3FB  ; wChanPitchModActive[2] = Wav channel
DEF wChainScopeDirty EQU $C403  ; re-resolve phrase under chain cursor into wColPhrase (chain screen sets)
DEF wSongScopeDirty  EQU $C404  ; re-resolve chain under song cursor into wColChain (song screen sets)
DEF wQuickMarking EQU $C412  ; 1 once SELECT+(B, B) extends a selection to a column, row or screen (quick-mark); 0 when a selection starts; kept across ResetEditorState
DEF wChanDispNote    EQU $C41F  ; [4] per-channel note pending for sample-name display (bank06 draws + clears)
DEF wChanDispNoteWav EQU $C421  ; pending display note, WAV (speech path writes)
DEF wChanDispNoteNoi EQU $C422  ; pending display note, NOI (SetNoisePoly tail)
DEF wChanDispNoteTimer EQU $C423  ; [4] sample-name display hold countdown ($19 frames)
DEF wUnusedC443  EQU $C443  ; set to 0 at reset, never read
DEF wUnusedC44B      EQU $C44B  ; set to 1 at init, never read
DEF wUnusedC44C      EQU $C44C  ; set to 1 at init, never read
DEF wUnusedC44D      EQU $C44D  ; set to 1 at init, never read
DEF wUnusedC44E      EQU $C44E  ; set to 1 at init, never read
DEF wColChainPu2     EQU $C450  ; chain under cursor, song column PU2
DEF wColChainWav     EQU $C451  ; chain under cursor, song column WAV
DEF wColChainNoi     EQU $C452  ; chain under cursor, song column NOI
DEF wColPhrasePu2    EQU $C454  ; phrase under cursor, song column PU2
DEF wColPhraseWav    EQU $C455  ; phrase under cursor, song column WAV
DEF wColPhraseNoi    EQU $C456  ; phrase under cursor, song column NOI
DEF wColInstrumentNoi EQU $C46B  ; instrument cache, song column NOI
DEF wUnusedC46C EQU $C46C  ; set at reset, never read
DEF wSpeechWord      EQU $C4CE  ; word being spoken (-> wChanNoteWav at speech note-on)
DEF wSpeechFrameTicks EQU $C4CF  ; frames left in current allophone frame; 0 -> stream next (SndSpeechStep)
DEF wSpeechPtr       EQU $C4D0  ; allophone stream read ptr lo (SramWords $B890 base)
DEF wSpeechPtrHi     EQU $C4D1  ; allophone stream read ptr hi
DEF wKit1LoopStartHi EQU $C4D5  ; kit voice 1 loop-window start hi
DEF wKit1LoopEnd     EQU $C4D6  ; kit voice 1 loop-window end ptr lo (stop/wrap comparator)
DEF wKit1LoopEndHi   EQU $C4D7  ; kit voice 1 loop-window end hi
DEF wKit2LoopStartHi EQU $C4D9  ; kit voice 2 loop-window start hi
DEF wKit2LoopEnd     EQU $C4DA  ; kit voice 2 loop-window end ptr lo
DEF wKit2LoopEndHi   EQU $C4DB  ; kit voice 2 loop-window end hi
DEF wKit1SampleStart EQU $C4DC  ; kit voice 1 raw sample start lo (lower clamp; note: start clamps are cross-wired between voices in ROM)
DEF wKit1SampleStartHi EQU $C4DD  ; kit voice 1 raw sample start hi
DEF wKit1SampleEnd   EQU $C4DE  ; kit voice 1 sample end lo (upper clamp for loop end)
DEF wKit1SampleEndHi EQU $C4DF  ; kit voice 1 sample end hi
DEF wKit2SampleStart EQU $C4E0  ; kit voice 2 raw sample start lo
DEF wKit2SampleStartHi EQU $C4E1  ; kit voice 2 raw sample start hi
DEF wKit2SampleEnd   EQU $C4E2  ; kit voice 2 sample end lo
DEF wKit2SampleEndHi EQU $C4E3  ; kit voice 2 sample end hi
DEF wKitHalfSpeed    EQU $C4E6  ; kit half-speed tick-skip phase ($40 = half speed, toggles $40<->$41)
DEF wKit1KeepAttack  EQU $C4E7  ; kit voice 1 LOOP=ATK: bit7 keeps attack, loop from OFFSET
DEF wKit2KeepAttack  EQU $C4E8  ; kit voice 2 LOOP=ATK flag
DEF wKitPitch        EQU $C4E9  ; kit PITCH byte: biases CH3 replay frequency (bit7 = base select)
DEF wKit1OffsetHi    EQU $C4EA  ; kit voice 1 OFFSET hi nibble (added to sample start, *16)
DEF wKit1OffsetLo    EQU $C4EB  ; kit voice 1 OFFSET lo (F cmd adjusts +/-hFxValue*32)
DEF wKit2OffsetHi    EQU $C4EC  ; kit voice 2 OFFSET hi
DEF wKit2OffsetLo    EQU $C4ED  ; kit voice 2 OFFSET lo
DEF wKit1LenDisabled EQU $C4EE  ; kit voice 1: instr LEN==0, skip end clamp
DEF wKit1LenHi       EQU $C4EF  ; kit voice 1 LEN hi nibble (caps loop end = start + LEN*16)
DEF wKit1LenLo       EQU $C4F0  ; kit voice 1 LEN lo
DEF wKit2LenDisabled EQU $C4F1  ; kit voice 2: LEN==0 flag (also 1 for speech)
DEF wKit2LenHi       EQU $C4F2  ; kit voice 2 LEN hi
DEF wKit2LenLo       EQU $C4F3  ; kit voice 2 LEN lo
DEF wKit1BankPending EQU $C4F6  ; kit voice 1 pending bank (committed at trigger; change clears active bit0)
DEF wKit2BankPending EQU $C4F7  ; kit voice 2 pending bank
DEF wKitDistType     EQU $C4F8  ; kit DIST/clip mode fed to the RAM dual-voice mixer at $D480
DEF wKit1ReadPtr     EQU $C4F9  ; kit voice 1 sample read ptr lo (16 bytes streamed per tick)
DEF wKit1ReadPtrHi   EQU $C4FA  ; kit voice 1 sample read ptr hi
DEF wKit2ReadPtr     EQU $C4FB  ; kit voice 2 sample read ptr lo
DEF wKit2ReadPtrHi   EQU $C4FC  ; kit voice 2 sample read ptr hi
DEF wNoiseNoteLatch  EQU $C4FD  ; noise note-1 latched at trigger; replayed via SetNoisePoly on cmd expiry
DEF wWavSynthRepeat  EQU $C4FE  ; wav instr byte3: synth start/repeat frame (seeds current frame)
DEF wWavSynthLength  EQU $C4FF  ; wav instr LENGTH nibble inverted (delta-list build bound)
DEF wWavFrameDeltas  EQU $C501  ; [32] signed wave-frame step list (from ROM table $7EAC, 0-terminated)
DEF wWavFrameDeltaPtr EQU $C521  ; wave-frame delta list cursor lo
DEF wWavFrameDeltaPtrHi EQU $C522  ; wave-frame delta list cursor hi
DEF wWavFrameDeltaLoop EQU $C523  ; saved delta-list reload ptr lo (list end, mode != once)
DEF wWavFrameDeltaLoopHi EQU $C524  ; saved delta-list reload ptr hi
DEF wWavFrameTimer   EQU $C525  ; wave-frame step countdown (reload = wFxWaveParam2; W cmd hi nibble sets)
DEF wMsgNoPause     EQU $C530  ; 1 while a message SELECT cannot pause (set at reset: the startup banner); cleared when a message ends, on redraw, on a live-mode switch (UiUpdateMessage)
DEF wTopBarMarkPtr   EQU $C562  ; 16-bit ptr to the unsaved-'!' char slot in the top bar
DEF wMuteSoloLatch    EQU $C56A  ; set to 1 after B+SELECT (mute) and 2 after B+START (solo) in HandleInputCombos; B alone releases it (SongScreenInput turns 2 into 1)
DEF wFsCheckResult   EQU $C56B  ; CheckFileMagicAll result (drives file-screen variant)
DEF wPartialRedrawDone EQU $C56D  ; partial row redraw done; ScreenUpdateDispatch skips full update
DEF wLiveStartCol    EQU $C571  ; LIVE queued start column ($FF = none; saves wLiveMode)
DEF wSavedLiveMode   EQU $C572  ; saved wLiveMode across queued LIVE start
DEF wTimerTickInc    EQU $C579  ; per-tick work-time increment ($0C CGB / $06 DMG)
DEF wClipboardBuf    EQU $C57A  ; [252] clipboard buffer, 6 columns of 42 cells (ReadClipboardCell/WriteClipboardCell index col*42 + row; word cut/paste uses first two bytes; link transfer indexes it)
DEF wFxSlideTargetNote EQU $C68E  ; slide (L cmd) destination note, clamped to $6C
DEF wFxSlideNote     EQU $C68F  ; slide base-note scratch (current note or wNoteOctave source)
DEF wFxSlidePitch    EQU $C690  ; slide start pitch lo (-> wChanPitchCur)
DEF wFxSlidePitchHi  EQU $C691  ; slide start pitch hi
DEF wNoiPitchSafe     EQU $C692  ; noise instrument byte 2, PITCH: nonzero = SAFE, 0 = FREE (manual C.4; SetupSoundNoise copies it); after a pitch change SAFE goes on to ProcessChannelTable, FREE returns for notes >= $3C
DEF wWavSynthFrame   EQU $C694  ; current wav-synth frame index (seeded from wWavSynthRepeat; W cmd value adds)
DEF wWavSynthEnd     EQU $C695  ; effective wav-synth frame count = min(instr LENGTH, wFxWaveParam)
DEF wChanFinetunePu1Hi EQU $C697  ; wChanFinetune[0] hi byte (Pu1)
DEF wChanFinetunePu2 EQU $C698  ; wChanFinetune[1]  (Pu2)
DEF wChanFinetuneWav EQU $C69A  ; wChanFinetune[2]  (Wav)
DEF wChanFinetuneWavHi EQU $C69B  ; wChanFinetune[2] hi byte (Wav)
DEF wWavUploadPending EQU $C69C  ; nonzero: the timer interrupt uploads hWavUploadBuf to wave RAM (SndModWavFreq; cleared after the upload)
DEF wTimerTailBusy   EQU $C69F  ; timer-tail reentrancy guard (scope bitmap copy work)
DEF wMsgPanelShown   EQU $C7AC  ; 4-line message panel shown (wMsgTimer=$64; special clear on expiry)
DEF wTopBarDrawScratch EQU $C7AD  ; 16-bit top-bar draw scratch (string ptr / remaining count)
DEF wNoiPSlideStart    EQU $C8C5  ; set by P on the noise channel; its first tick zeroes wNoiPitchFrac and clears it
DEF wChanCmdRatePu2   EQU $C8C7  ; wChanCmdRate[1] = Pu2 channel
DEF wChanCmdRateWav   EQU $C8C8  ; wChanCmdRate[2] = Wav channel
DEF wChanCmdRateNoi   EQU $C8C9  ; wChanCmdRate[3] = Noi channel
DEF wChanCmdCountdownPu2 EQU $C8CB  ; command countdown, PU2
DEF wChanCmdCountdownWav EQU $C8CC  ; command countdown, WAV
DEF wChanCmdCountdownNoi EQU $C8CD  ; command countdown, NOI
DEF wFxSlotRetrigRate EQU $C8CE ; [12] R's retrig period per FX slot: (second digit - 1) x (command rate + 1); $0F (R?0) retrigs once
DEF wNoiVibPrevOfs   EQU $C8DA  ; last applied noise vib offset (noise pitch is note-stepped)
DEF wNoiPitchFrac    EQU $C8DB  ; noise pitch-slide fractional accumulator (quarter-semitones)
DEF wNoiChordPrevOfs EQU $C8DC  ; last applied noise chord/arp offset
DEF wPtrStepCountdown EQU $C8EF  ; ptr to current channel's step-countdown byte (wChanStepCountdown + ch)
DEF wWavRamDirty     EQU $C8F1  ; reload WAV RAM + level at next WAV trigger (SndSetKitParams sets; trigger clears + LoadWaveRam)
DEF wNoiTableNotePrev EQU $C8F2  ; last table note applied on NOI (change detect via wTableNotePtrNoi)
DEF wSeqNoteLatch    EQU $C8F7  ; phrase-row note byte latch (after SndSplitPhraseNibbles)
DEF wTableHopDone    EQU $C8F8  ; per-tick table-hop guard (only one H per tick)
DEF wKbdNoteInjected EQU $C8F9  ; kbd note injected into phrase ptr this tick (clears wKbdNoteQueued)
DEF wKitNoteNibbleMask EQU $C8FA  ; masks which kit-voice nibbles of the note survive ($ff at chain end)
DEF wInstrFieldCount EQU $C905  ; instrument screen: fields on current row (wInstrField clamps to -1)
DEF wInstrEditLatch  EQU $C906  ; instrument screen edit latch (cleared on entry/input; role approximate)
DEF wPhraseColLast   EQU $C907  ; [4] the last value entered per phrase column, by wSongCol (PhraseResolveCursorNote; set to 1 at reset); [3] is wPhraseInstrCache
DEF wPhraseInstrCache EQU $C90A  ; phrase screen: ReadInstrByte cache during phrase note write
DEF wInstrIsKit      EQU $C90B  ; current instrument type == KIT cache (phrase screen)
DEF wPhraseDrawMode  EQU $C90C  ; phrase draw variant selector (jump-table $42A3 vs $429E)
DEF wPhraseRowFlags  EQU $C90D  ; [16] per-step state DrawPhraseScreen sets for the phrase grid: the instrument, $FE (none) / $FF (not a kit) -- on kit rows the left sample's kit bank (PhraseCheckColFD tests > $FD)
DEF wPhraseRowFlagsB EQU $C91D  ; [16] kit rows: the right sample's kit bank (DrawPhraseScreen)
DEF wPhraseNoteIsKitA EQU $C92D  ; phrase edit: current note is-kit cache
DEF wPhraseNoteIsKitB EQU $C92E  ; phrase edit: adjacent note is-kit cache
DEF wTableEnvCache   EQU $C931  ; table screen: cached TableReadEnv at cursor
DEF wTableTransCache EQU $C932  ; table screen: cached TableReadTrans at cursor
DEF wHelpOpenEntry   EQU $C933  ; currently open HELP entry (0 = none)
DEF wWordClipCell0   EQU $C935  ; word clipboard: allophone byte
DEF wWordClipCell1   EQU $C936  ; word clipboard: length byte
DEF wWordClipPtr     EQU $C939  ; 16-bit scratch ptr walking the 4-byte word clipboard (copy/paste)
DEF wWaveSelStart    EQU $C93B  ; wave editor selection anchor (frame index; pairs with wWaveCursor)
DEF wWaveSelEnd      EQU $C93C  ; wave editor selection other endpoint
DEF wWaveColEmpty    EQU $C93D  ; [8] wave screen: 1 per row of the column being drawn that takes the empty tile (DrawWaveColumn)
DEF wChainCellValue  EQU $C945  ; chain-screen phrase value under cursor (working copy -> DrawHexByte)
DEF wChainCellEmpty  EQU $C946  ; chain cursor cell reads $FF flag
DEF wTempoShadow EQU $C947  ; cached wTempo for the chain-screen tempo field (redraw-if-changed)
DEF wChainScratchPtr EQU $C948  ; 16-bit scratch ptr: DrawTileHexCell parks HexCellPrefixPtrs[v&3] (bank06 $75A6) here before DrawString
DEF wSongSelScroll   EQU $C94A  ; saved wSongScroll: song-screen selection anchor
DEF wSongSelRow      EQU $C94B  ; saved wSongRow: song-screen selection anchor
DEF wPlayStartChan   EQU $C94C  ; playback start channel (= wSongCol at play; seeds the engine)
DEF wPlayStartRow    EQU $C94D  ; playback start song row (seeds wChanNextSongRow)
DEF wSongPasteCol    EQU $C94E  ; saved wSongCol during song selection paste
DEF wSongPasteRow    EQU $C94F  ; saved wSongRow during song selection paste
DEF wSongAPressState EQU $C951  ; song-screen A-press/mark state machine (0/1/2)
DEF wSavedSongCol    EQU $C952  ; saved wSongCol around song-screen farcalls
DEF wSongCellEmpty   EQU $C953  ; song draw: current cell reads $FF latch
DEF wFileBlockIdx    EQU $C958  ; SRAM block iterator for save/scan (bank1c; not sync)
DEF wCompSrc         EQU $C97A  ; compressed-data source ptr lo (16-bit, += $100 per page)
DEF wFileFirstChar   EQU $C980  ; first name char of file being loaded (0 = empty slot)
DEF wCompProgress    EQU $C981  ; blocks-processed counter (progress display)
DEF wDecompOk        EQU $C982  ; decompress continue/success flag (cleared on OOB/overflow/mismatch)
DEF wBlockSinkPtr    EQU $C984  ; 16-bit handler ptr: FlushBlockToSram or VerifyBlockAgainstSram (jp hl)
DEF wFileNameTerm    EQU $C98F  ; NUL terminator after the 8-char filename
DEF wFileBlockCountPtr EQU $C990  ; 16-bit scratch ptr into wFileBlockCounts
DEF wFileDrawIdx     EQU $C993  ; absolute file index being drawn (wFileScroll + row, max $1F)
DEF wFileEraseArmed  EQU $C994  ; erase confirm armed (prompt-confirm -> EraseFile)
DEF wFilePromptNo    EQU $C996  ; prompt cursor (1 = NO/cancel, default)
DEF wFileLoadAltReturn EQU $C997  ; SELECT+A load: jump to screen $0D after
DEF wFileListAction  EQU $C99A  ; field the file list was opened from (0 load / 2 erase)
DEF wFileNameNavMode EQU $C99C  ; file list entered from the name field (typed-name navigation)
DEF wFileSramProps   EQU $C99D  ; [4] SRAM settings backup around FileLoadSlotInfo ($BFB6-B9)
DEF wFileInstrIdx    EQU $C9A1  ; instrument index iterator 0-$3F (FileForEachInstrType)
DEF wNameEditWrapLatch EQU $C9A2  ; name editor: cursor clamped at edge, blocks wrap while held
DEF wSramFsFlag      EQU $C9A3  ; CheckFileMagic64 result; gates FILE menu + props write (polarity TBD)
DEF wProjMenuActive  EQU $C9A5  ; project screen: bottom LOAD/SAVE menu focused
DEF wProjMenuItem    EQU $C9A6  ; project screen: menu item index (0/1)
DEF wProjDrawScratch EQU $C9A7  ; transient tile-value scratch (Proj/clock digit draw; also bank01)
DEF wProjDrawScratch2 EQU $C9A8  ; draw scratch (ProjDrawSyncField)
DEF wProjDrawScratch3 EQU $C9A9  ; draw scratch (ProjDrawSyncFieldHw)
DEF wWorkClockSubFrame EQU $C9AA  ; frame counter mod 128 feeding the WORK-time clock (TickWorkTimeClock)
DEF wTapTempoCount EQU $C9AB  ; project scroll/average queue fill count (0-8)
DEF wVramCopyScratch EQU $C9B4  ; VRAM byte-copy scratch (bank1e LoadFontTiles path)
DEF wTileSlotPtr     EQU $C9B5  ; 16-bit scratch ptr (LoadTilesDispatch -> RequestTileSlot)
DEF wTileSlotPtrHi   EQU $C9B6  ; hi byte of wTileSlotPtr
DEF wInstrRemapDirty EQU $C9B7  ; instrument remap table populated/dirty flag
DEF wInstrRemapTable EQU $C9B8  ; [16] instrument-slot remap/claim table ($FF = empty; InitRemapTable)
DEF wPhraseDedupScratch EQU $C9C8  ; scratch: IsPhraseUsedFar result (bank1f dedup)
DEF wPhraseDedupScratch2 EQU $C9C9  ; scratch: FindDupPhrase result
DEF wPhraseDedupScratch3 EQU $C9CA  ; scratch (bank1f phrase dedup)
DEF wPhraseDedupScratch4 EQU $C9CB  ; scratch (bank1f phrase dedup)
DEF wPhraseDedupScratch5 EQU $C9CC  ; scratch (bank1f phrase dedup)
DEF wDrawTextScratch EQU $C9CD  ; transient tile-value scratch for ColorField (bank1f)
DEF wChanMuteShown   EQU $C9CE  ; [4] per channel: 1 while the channel cell shows MUTE (DrawChanStatus clears it with blanks)
DEF wChanMuteShownPtr EQU $C9D2 ; DrawChanStatus's static pointer into wChanMuteShown
DEF wGrooveCacheValid EQU $C9D5  ; groove step-cache built flag (GrooveBuildCache sets, GrooveResetState clears)
DEF wGrooveCachePtr  EQU $C9E6  ; [1] 16-bit scratch ptr walking wGrooveStepCache (GrooveBuildCache)
DEF wSynthSegLen1    EQU $C9E8  ; synth param: segment length/fill count -> curve buf $CA74 (field TBD)
DEF wSynthWaveShape  EQU $C9E9  ; synth waveform shape type (jump-table selector, SynthRenderWaveType)
DEF wSynthSegLen2    EQU $C9EA  ; synth param: fill count -> curve buf $CA54 (field TBD)
DEF wSynthFilterType EQU $C9EB  ; synth filter type (2=clear, 3=SynthFilterPass)
DEF wSynthFilterCutoff EQU $C9EC  ; synth filter cutoff (value<<3 & $F8, SynthFilterPass)
DEF wSynthSignalType EQU $C9ED  ; synth source: 2 selects SynthShapeTableB; 0/1 select SynthShapeTableA; 3 reads a saved wave
DEF wSynthFilterParam EQU $C9EE  ; synth distortion mode: 0=CLIP, 1=FOLD, 2=WRAP (SynthComposeWave)
DEF wSynthSegLen3    EQU $C9EF  ; synth param: fill count -> curve buf $CA84 (field TBD)
DEF wSynthRenderActive EQU $C9F1  ; idle synth render in progress (cleared at frame $10)
DEF wSynthRenderDst  EQU $C9F2  ; 16-bit SRAM dest ptr for rendered waves ($A0xx)
DEF wSynthShapeBuf  EQU $C9F4  ; [32] words: signed seed samples from bank $1E (LoadSynthShape), accumulated into by SynthRenderWaveA/B
DEF wSynthBufPtr     EQU $CB38  ; 16-bit scratch ptr into wSynthBufA (render/smoothing)
DEF wSynthNavSide    EQU $CB63  ; synth screen: last horizontal nav direction (cursor column side)
DEF wSyncXferActive  EQU $CB65  ; link song-transfer in progress (skips ProcessSyncByte)
DEF wSyncStartRow    EQU $CB6A  ; saved/target song row for sync start (B-held save/restore)
DEF wSyncSerialIeMask EQU $CB6D  ; IE serial-bit mask ($08/$00) for the current sync mode
DEF wSendByteNest    EQU $CB72  ; SendByteStep nesting/pending counter (wrap -> HandleTooBusy)
DEF wAnaInStepLen    EQU $CB73  ; analog sync-in: pulses per step, 8.8 fixed lo (DivU16 quotient)
DEF wAnaInStepLenHi  EQU $CB74  ; analog sync-in step interval hi
DEF wAnaInStepAccum  EQU $CB75  ; analog sync-in progress accumulator lo (+= $0100 per pulse)
DEF wAnaInStepAccumHi EQU $CB76  ; analog sync-in progress accumulator hi
DEF wAnaInPulseCount EQU $CB79  ; analog sync-in pulses since resync
DEF wAnaInPrevPlayMode EQU $CB7A  ; analog sync-in: latched wPlayMode (0 -> emit extra step)
DEF wFullRedrawTimer EQU $CB7B  ; forces a full screen update every 32 frames despite partial redraws
DEF wKbdQueueTail    EQU $CB7C  ; PS/2 ring-buffer write index into wLinkQueueBuf (serial IRQ)
DEF wKbdPulseChans  EQU $CB7F  ; keyboard: the pulse channel(s) a pulse instrument plays on, 0 PU1 / 1 PU2 / 2 both (F8 cycles them; KbdLink.c)
DEF wKbdNoteQueued   EQU $CB92  ; queued PS/2 note/entry nibble-merge (0 = none; gates kbd trigger)
DEF wKbdArmed        EQU $CB94  ; keyboard note-active / instrument-armed flag
DEF wKbdInstrument2  EQU $CB95  ; keyboard voice-2 instrument number
DEF wKbdChanState    EQU $CB96  ; [4] per-sound-channel keyboard-active state
DEF wKbdChanStatePu2 EQU $CB97  ; keyboard-active state, PU2
DEF wKbdChanStateNoi EQU $CB99  ; keyboard-active state, NOI
DEF wKbdActionCh     EQU $CB9A  ; pending keyboard action target channel ($ff = none)
DEF wKbdChanTrigger  EQU $CB9B  ; [4] per-channel pending kbd note trigger (instr | $80)
DEF wKbdChanTriggerPu2 EQU $CB9C  ; pending kbd note trigger, PU2
DEF wKbdChanTriggerWav EQU $CB9D  ; pending kbd note trigger, WAV
DEF wKbdChanTriggerNoi EQU $CB9E  ; pending kbd note trigger, NOI
DEF wKbdCmdModifier  EQU $CB9F  ; kbd/link command modifier latch (ProcessLinkQueue)
DEF wKbdSustain      EQU $CBA0  ; kbd/link sustain/held latch ($80)
DEF wKbdCmdAccum     EQU $CBA1  ; kbd/link command accumulator
DEF wScopeDirty      EQU $CBA2  ; scope bitmap redraw-pending flag (VBlank uploads to VRAM $8000, then clears)
DEF wScopeRefresh    EQU $CBA3  ; scope refresh trigger (1 = redraw after WAV refresh, 2 = reinit)
DEF wScopeBitmap     EQU $CBA4  ; [8] scope tile bitmap row 0 (3 rows x 8 bytes; ChainUpdateScope draws)
DEF wScopeBitmapRow1 EQU $CBAC  ; scope tile bitmap row 1
DEF wScopeBitmapRow2 EQU $CBB4  ; scope tile bitmap row 2
DEF wEnvSpeedPu1     EQU $CBC0  ; active envelope fade speed, PU1 (current node lo-nibble)
DEF wEnvCounterPu1   EQU $CBC1  ; envelope fade countdown, PU1 (reloaded from wEnvSpeedPu1)
DEF wEnvTargetPu1    EQU $CBC2  ; envelope fade target volume, PU1 (FadeEnvStep b arg)
DEF wEnvNode1Pu2     EQU $CBC3  ; envelope node 1, PU2
DEF wEnvNode2Pu2     EQU $CBC4  ; envelope node 2, PU2
DEF wEnvNode3Pu2     EQU $CBC5  ; envelope node 3, PU2
DEF wEnvSpeedPu2     EQU $CBC7  ; active envelope fade speed, PU2
DEF wEnvCounterPu2   EQU $CBC8  ; envelope fade countdown, PU2
DEF wEnvTargetPu2    EQU $CBC9  ; envelope fade target volume, PU2
DEF wEnvNode1Noi     EQU $CBCA  ; envelope node 1, NOI
DEF wEnvNode2Noi     EQU $CBCB  ; envelope node 2, NOI
DEF wEnvNode3Noi     EQU $CBCC  ; envelope node 3, NOI
DEF wEnvSpeedNoi     EQU $CBCE  ; active envelope fade speed, NOI
DEF wEnvCounterNoi   EQU $CBCF  ; envelope fade countdown, NOI
DEF wEnvTargetNoi    EQU $CBD0  ; envelope fade target volume, NOI
DEF wEnvPlotX        EQU $CBD8  ; last plotted env-graph column 0-$3F (PlotEnvCurve -> wDecompBuf)
DEF wTileReqSlot0    EQU $CBE5  ; tile-load request table: 6 entries x [slot, ptrLo, ptrHi] at $CBE5-$CBF6 (this = entry-0 slot)
DEF wUnusedCBF7      EQU $CBF7  ; zeroed by ResetTileLoader, never read
DEF wUnusedCBF8      EQU $CBF8  ; zeroed by ResetTileLoader, never read
DEF wRngSeedHi       EQU $CBF9  ; RNG seed hi byte (Random16: seed = seed*17 + $5C93)
DEF wRngSeedLo       EQU $CBFA  ; RNG seed lo byte (SeedRandom16 sets)
DEF wUnusedCBFB      EQU $CBFB  ; set to $03 at display reinit, never read
DEF wUnusedCBFC      EQU $CBFC  ; set to $00 at display reinit, never read
DEF wSpriteWavX      EQU $DD01  ; WAV display sprite 0 X (=$90, ClearOamBuffer)
DEF wSpriteWav1Y     EQU $DD04  ; WAV display sprite 1 Y
DEF wSpriteWav1X     EQU $DD05  ; WAV display sprite 1 X (=$98)
DEF wSpriteWav1Tile  EQU $DD06  ; WAV display sprite 1 tile (=$01)
DEF wSpriteWav2Y     EQU $DD08  ; WAV display sprite 2 Y
DEF wSpriteWav2X     EQU $DD09  ; WAV display sprite 2 X (=$A0)
DEF wSpriteWav2Tile  EQU $DD0A  ; WAV display sprite 2 tile (=$02)
DEF wSpriteMark0Y    EQU $DD0C  ; sprite pool slot 3 Y: play markers / env graph (screen-multiplexed; ClearMarkerSprites)
DEF wSpriteMark0Attr EQU $DD0F  ; sprite pool slot 3 attr
DEF wSpriteMark1Y    EQU $DD10  ; sprite pool slot 4 Y
DEF wSpriteMark1Attr EQU $DD13  ; sprite pool slot 4 attr
DEF wSpriteMark2Y    EQU $DD14  ; sprite pool slot 5 Y
DEF wSpriteMark2Attr EQU $DD17  ; sprite pool slot 5 attr
DEF wSpriteMark3Y    EQU $DD18  ; sprite pool slot 6 Y
DEF wSpriteMark3Attr EQU $DD1B  ; sprite pool slot 6 attr
DEF wSpriteMark4Y    EQU $DD1C  ; sprite pool slot 7 Y
DEF wSpriteMark4Attr EQU $DD1F  ; sprite pool slot 7 attr
DEF wSpriteMark5Y    EQU $DD20  ; sprite pool slot 8 Y
DEF wSpriteMark5Attr EQU $DD23  ; sprite pool slot 8 attr
DEF wSpriteMark6Y    EQU $DD24  ; sprite pool slot 9 Y
DEF wSpriteMark6Attr EQU $DD27  ; sprite pool slot 9 attr
DEF wSpriteMark7Y    EQU $DD28  ; sprite pool slot 10 Y
DEF wSpriteMark7Attr EQU $DD2B  ; sprite pool slot 10 attr
DEF wSpriteCursor0X  EQU $DD2D  ; selection/edit-cursor column sprite 11 X (WaveSetSpriteColumns)
DEF wSpriteCursor1X  EQU $DD31  ; selection cursor sprite 12 X
DEF wSpriteCursor2X  EQU $DD35  ; selection cursor sprite 13 X
DEF wSpriteCursor3X  EQU $DD39  ; selection cursor sprite 14 X
DEF wSpriteCursor4X  EQU $DD3D  ; selection cursor sprite 15 X
DEF hWavFreqSaveLo   EQU $FFB0  ; saved WAV channel freq lo, restored to rAUD3LOW after wave upload
DEF hWavFreqSaveHi   EQU $FFB1  ; saved WAV channel freq hi -> rAUD3HIGH
DEF hSavedSPLo       EQU $FFB2  ; saved stack pointer lo (ld sp,hWavUploadBuf pop-stream trick)
DEF hSavedSPHi       EQU $FFB3  ; saved stack pointer hi
DEF hSavedAudTerm    EQU $FFB4  ; saved rAUDTERM (panning) across the AUD3 off/retrigger window

; --- orphan RAM addresses from the debugger export, named from their readers ---
DEF wNoteOctave      EQU $C693  ; octave/fine index stored by NoteToPeriod; indexes wNoteFineTable-1 (WriteChannelFreq)
DEF wTableHopCountersWav EQU $C1C0  ; [32] table H-hop repeat counters, WAV slice
DEF wTableHopCountersNoi EQU $C1E0  ; [32] table H-hop repeat counters, NOI slice
DEF wSelHoldFrames   EQU $C676  ; frames SELECT has been held (ProcessInput resets on other input)
DEF wAttrRowBuf     EQU $C677  ; [20] CGB attribute row buffer: ColorField fills it with the palette and set_bkg_tiles writes it to VRAM bank 1
DEF wFxWaveParam     EQU $C8C2  ; W command value latch (lo nibble; consumed via SndSetKitParams)
DEF wFxWaveParam2    EQU $C8C3  ; W command companion latch
DEF wSynthRenderState EQU $CB3A ; synth render params copied at buffer init (3 words: +0/+2/+4)
DEF wSynthWaveBuf   EQU $CA34  ; [32] rendered synth wave samples, one nibble each (SynthComposeWave; SynthWriteWaveCol packs them into SRAM)
DEF wSynthCurveSeg2  EQU $CA54  ; [16] per-wave interpolated synth parameter curves (SynthFillWaveRange from SynthLoadParams,
DEF wSynthCurveCutoff EQU $CA64 ;      read back per wave by RenderSynthWave into wSynthSegLen2/wSynthFilterCutoff/
DEF wSynthCurveSeg1  EQU $CA74  ;      wSynthSegLen1/wSynthSegLen3); the C addresses them as literal pointers
DEF wSynthCurveSeg3  EQU $CA84
DEF wSynthCurveAmp   EQU $CA94  ; [16] per-frame mix amplitude, interpolated from the two nibbles of synth parameter 2
DEF wSynthCurveLimit EQU $CAA4  ; [16] per-frame distortion-window control: synth parameters 13/14 biased by $0F
DEF wSynthRenderPtr  EQU $CB3C  ; 16-bit scratch ptr into the $CA34 wave scratch (render/smoothing)
DEF wSynthRenderVal  EQU $CB3E  ; 16-bit render scratch (wSynthBufA value, DivU16 operand)
DEF wSynthWaveTmp    EQU $CB40  ; [32] filter-pass scratch copy of wSynthWaveBuf (SynthFilterPass writes it, then memcpy back)
DEF wWaveEditX       EQU $CB60  ; wave-editor pen X: (column*4) & $f0 (SynthWriteWaveCol)
DEF wNotePeriodLo    EQU $CD28  ; RAM copy of the note->period table (NoteToPeriod indexes by note)
DEF wNoteFineTable   EQU $CD94  ; RAM copy of the finetune/delta table (WriteChannelFreq, index wNoteOctave-1)
DEF wMasterVolTable  EQU $D400  ; master-volume transition LUT (FxCmd_M indexes by rAUDVOL & $70)
DEF wDecompBufEnd    EQU $DA4F  ; end bound of wDecompBuf walks (OrBitsIntoDaBuffer)
DEF wSpriteCursor5X  EQU $DD41  ; selection cursor sprite 16 X
DEF wSpriteCursor6X  EQU $DD45  ; selection cursor sprite 17 X
DEF wSpriteCursor7X  EQU $DD49  ; selection cursor sprite 18 X
DEF wStackTop        EQU $DFFF  ; top of WRAM / initial stack area (Start clears $20 bytes downward)
DEF wPhraseNotePtrPu2 EQU $C0BE  ; SRAM read ptr: note column, PU2
DEF wPhraseNotePtrWav EQU $C0C0  ; SRAM read ptr: note column, WAV
DEF wPhraseNotePtrNoi EQU $C0C2  ; SRAM read ptr: note column, NOI
DEF wChanStepCountdownPu2 EQU $C0C5  ; steps until next note read, PU2
DEF wChanStepCountdownWav EQU $C0C6  ; steps until next note read, WAV
DEF wChanStepCountdownNoi EQU $C0C7  ; steps until next note read, NOI
DEF wChanGrooveTicksPu2 EQU $C0FD  ; ticks left in groove step, PU2
DEF wChanGrooveTicksWav EQU $C0FE  ; ticks left in groove step, WAV
DEF wChanGrooveTicksNoi EQU $C0FF  ; ticks left in groove step, NOI
DEF wCurInstrB4      EQU $C0D4  ; instrument byte 4
DEF wCurInstrB9      EQU $C0D9  ; instrument byte 9: envelope node 2 (pulse)
DEF wCurInstrB11     EQU $C0DB  ; instrument byte 11: finetune (-> wChanFinetune)
DEF wChanRowQueuePu2 EQU $C101  ; queued chain-row value, PU2
DEF wChanRowQueueWav EQU $C102  ; queued chain-row value, WAV
DEF wChanRowQueueNoi EQU $C103  ; queued chain-row value, NOI
DEF wChanChordOffsetNoi EQU $C346  ; current chord transpose, NOI
DEF wChanNoteOffsetNoi EQU $C177  ; semitone note offset, NOI
DEF wColInstrumentPu2 EQU $C469  ; instrument cache, song column PU2
DEF wColInstrumentWav EQU $C46A  ; instrument cache, song column WAV
DEF wChanEnvVolOffsetPu2 EQU $C225  ; env volume delta, PU2
DEF wChanEnvVolOffsetWav EQU $C226  ; env volume delta, WAV
DEF wChanEnvVolOffsetNoi EQU $C227  ; env volume delta, NOI
DEF wFileNameTmp     EQU $C6A0  ; 8-char filename scratch (memcpy'd from the file table)
DEF wGrooveStepCache EQU $C9D6  ; [16] groove-step display cache (GrooveBuildCache fills, DrawGrooveRow reads)
DEF wTapTempoBuf   EQU $C9AC  ; [8] sample queue averaged by TapTempoStep
