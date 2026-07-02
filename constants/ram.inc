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
DEF wSampleNoteString EQU $C43A ; shared text buffer for a sample/kit note name (Phrase/Chain/Song note draws)
DEF wTableCol        EQU $C92F  ; Table editor: current column/cursor state (bit7 = special)
DEF wTableRow        EQU $C930  ; Table editor: current row cursor (0-15); pairs with wTableCol
DEF wDefaultNotes     EQU $C904  ; table/array of default note values (InstrGetField0)
DEF wHelpCursorRow   EQU $C934  ; HELP/word screen cursor row (ProjHelpSelectPrev/Next/OpenSelected)
DEF wWordEditRow     EQU $C937  ; Word/speech editor: cursor row within the current word (inc/dec by nav); AccessWord indexes word data at +wWordEditRow*2
DEF wWordEditCol     EQU $C938  ; Word/speech editor: cursor column (0/1) within the row; AccessWord byte offset within the 2-byte unit
DEF wPlayMarkerTimer EQU $C950  ; Song screen: free-running blink counter incremented each SongUpdatePlayMarker; bit 3 (and $08) is the play-marker on/off phase
; Table editor cell base pointers (16-bit) into the working table command data;
; TableGetCellA/B read [ptr + wTable*32 + row] to fetch a CMD1/CMD2 cell byte.
DEF wTableCellAPtr   EQU $C447  ; 16-bit base ptr for TABLE command column A (CMD1); TableGetCellA
DEF wTableCellBPtr   EQU $C449  ; 16-bit base ptr for TABLE command column B (CMD2); TableGetCellB
DEF wGrooveRow       EQU $C9D4  ; Groove editor: current step/row cursor (0-15)
DEF wSynthScreen     EQU $C441  ; Synth editor: current synth screen/sub-state (pairs with wSynthCursor); Synth* owned
DEF wSynthCursor     EQU $C442  ; Synth editor: current parameter/cursor index
DEF wPlayMode        EQU $C4D2  ; sound playback mode: 0=stopped, nonzero=playing (value = play scope;
                                ; verified in emulator: 4 when started from SONG, 1 from PHRASE). Main
                                ; loop runs SndRunFrame only when nonzero.
DEF wRetrigger       EQU $C4CD  ; sound retrigger/hop flag (SndTick: nonzero -> rerun the channel chain this tick)
; Pitch-modulation accumulators $C4D4-$C4E3 (current channel): two 16-bit sweep
; pairs. Acc A=$C4D4 steps by +$10 into $C4D6, clamped to limits $C4DC/$C4DE;
; Acc B=$C4D8 steps into $C4DA, clamped to $C4E0/$C4E2 (see SndStepAcc*/SndClampAcc*).
DEF wPitchModA       EQU $C4D4  ; pitch-mod sweep accumulator A (16-bit). NB: OVERLAID -- the oscilloscope consumer
                                ; (bank00 $03d8, gated by wOscActive) reuses $C4D4-$C4F9 as wave-display pointers,
                                ; reloading its read pointer from here. Same RAM, two mutually-exclusive features.
DEF wPitchModB       EQU $C4D8  ; pitch-mod sweep accumulator B (16-bit); also the B-side oscilloscope start ptr (overlay)
; Oscilloscope wave-display (bank00 $03d8): streams wave data from a banked pointer (16 bytes/frame) to
; hOscilloscope. These vars are oscilloscope-only (not used by the pitch-mod stepper):
DEF wOscActive       EQU $C8F6  ; oscilloscope active bits: bit0 = channel A, bit1 = channel B
DEF wOscBankA        EQU $C4F4  ; ROM bank selected (-> $2000) to read channel A's wave data for the oscilloscope
DEF wOscBankB        EQU $C4F5  ; ROM bank for channel B's oscilloscope wave data
DEF wOscLoopA        EQU $C4E4  ; channel A loop flag: 0 -> stop (clear wOscActive bit0); else reload read ptr to start
DEF wOscLoopB        EQU $C4E5  ; channel B loop flag
; --- song (de)compression cursors (bank $1C; see docs/save_format.md) ----------
DEF wFileScroll       EQU $C99B  ; File screen: file-list scroll offset (top visible index). Screen row = wFileCursorRestrict
                                 ; - wFileScroll, bounded to 0..F (16 visible rows; DebugTrap otherwise). FileDrawFieldChecked
DEF wFileCursorRestrict EQU $C999 ; File screen: restricts/limits the file-list cursor (File* draw/validate). Cross-checked
DEF wIsSelectingFile  EQU $C995  ; File screen: nonzero while choosing a file from the list (FileDrawStr)
DEF wFileRedrawCount  EQU $C992  ; File screen: count of fields still to redraw; while >0, FileDrawField is called
                                 ; once per field and it decrements to 0 (entry value 1/2 also selects which fields)
DEF wCompBank        EQU $C979  ; SRAM bank used for compressed block I/O
DEF wCompSrc         EQU $C97B  ; compressed-stream read pointer (decompress input), 16-bit
DEF wCompDst         EQU $C97D  ; (de)compression output pointer into the working song, 16-bit
DEF wCompByte        EQU $C97F  ; current byte being processed in song (de)compression: loaded via ReadSongByte and
                                ; compared against RLE markers ($20, $ff, run counts); set to $ff as a sentinel
DEF wDecompPos       EQU $C983  ; cursor into the $DA00 decompress scratch buffer

; --- Drawing helpers (used by DrawString/DrawHexByte/... in bank 0) ----------
DEF wStringSrc       EQU $C564  ; source pointer for DrawString (16-bit)
DEF wVramCursor      EQU $C573  ; VRAM tilemap dest ($98xx-$9Bxx, 16-bit LE; $C574=hi); draw helpers auto-increment it
DEF wPhrase          EQU $C414  ; current phrase number being edited ($FF=none, Phrase screen)
DEF wInstrument      EQU $C576  ; current instrument number (index into instruments[64][16])
DEF wStrBuffer       EQU $C6A9  ; shared text work buffer (StrCopy destination), used across banks
DEF wSongReadBuf     EQU $C46D  ; shared scratch buffer that ReadSongBytes fills with song data (its address is passed
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
DEF wFrameCounter    EQU $C553  ; per-frame counter (verified: increments every frame; `inc [hl]`), overflows into
                                ; wFrameCounterHi; consumed as `&$03` (4-phase UI animation) and `cp $0a`; reset on sound stop
DEF wFrameCounterHi  EQU $C552  ; high byte of wFrameCounter (incremented when wFrameCounter wraps)
DEF wSyncSendAccum   EQU $C956  ; 16-bit serial/sync send-clock accumulator (TimerHandler, gated by $cb6e==1): -=8 each
                                ; timer tick; on underflow adds wSyncSendRate and calls SendByteStep. Reset on sound stop
DEF wSyncSendRate    EQU $C954  ; 16-bit reload added to wSyncSendAccum on underflow (sets the serial/sync byte-send rate)
DEF wIdleHalt        EQU $C68B  ; power-save idle/halt flag: set when MainLoop idle-halts (wIdleFrames>7); while set, VBlankHandler only ticks the work clock; cleared by activity (work-clock wrap, SerialHandler) or input
DEF wIdleFrames      EQU $C68C  ; frames since last activity (redraw/work/sync/input reset it to 0); when it exceeds 7, MainLoop enters the idle halt
DEF wWorkTimeAccum   EQU $C69D  ; 16-bit sub-second accumulator for the song work-time clock; wraps every $0e00 to add a
                                ; whole-second tick to wWorkTimeTicks (verified: c69d advances during playback)
DEF wWorkTimeTicks   EQU $C577  ; pending whole-second ticks for the work-time clock; bank01 $50a0 applies each to the
                                ; song clock in SRAM (seconds @ $bfb3 carry to minutes @ $bfb2 at 60) and decrements this
DEF wWavMode         EQU $C500  ; WAV channel wave-source mode, set from the wave instrument in SetupSoundWave;
                                ; 0 gates the wave-frame updater off (`ret z`), 1/2 select modes
DEF wSndDataPtr      EQU $C8E9  ; 16-bit working pointer the sound command/table reader (SndReadCmdStream) walks;
                                ; pointed at &wChanTableRow1[ch] (stride $04) or instrument-mode data (stride $40)
DEF wSndDataStride   EQU $C8FB  ; advance stride for wSndDataPtr ($04 = per-channel arrays, $40 = instrument mode)
DEF wFxChannel       EQU $C527  ; channel (0-3) the command/FX/table engine is processing (set from hSoundCh); some
                                ; FX OR in bit2/bit3 as mode flags (tested via `and $fc` before using it as an index)
DEF wChanCmdRate     EQU $C8C6  ; [4] command/table step rate = instrument COMMAND_RATE (byte 8); reload value for the countdown
DEF wChanCmdCountdown EQU $C8CA ; [4] ticks until the next command/table step; SndRunTableStep decrements it, and on 0
                                ; reloads it from wChanCmdRate and runs the step (SndDecodeCmdNibbles / SndTableStepDispatch)
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
DEF wSongTranspose   EQU $C444  ; global/project TRANSPOSE (working copy of the SRAM song property); SndTransposeNote
                                ; adds it to every note; displayed/edited on the PROJECT screen (ProjDrawHex)
DEF wTransposeDirty  EQU $C445  ; transpose edit/commit flag: set by the PROJECT screen after refreshing wSongTransposeDisp; when set, ProcessInput commits wSongTransposeDisp -> wSongTranspose and saves it to SRAM ($BFB5), then clears it
DEF wSongTransposeDisp EQU $C446 ; displayed form of the song transpose (ProjDrawHex / ProcessInput)
DEF wNoteTranspose   EQU $C4D3  ; PU2 instrument transpose (from instrument PU2_TRANSPOSE param), added before the period lookup
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
DEF wChanVibAccum    EQU $C308  ; [4] 16-bit per-channel vibrato LFO phase accumulator (advanced by wChanVibSpeed each step; high byte indexes the vib waveform via SndCalcVibOffset -> wChanVibOffset)
DEF wChanVibSpeed    EQU $C314  ; [4] 16-bit signed vibrato increment (V high nibble via table; negated by dir)
DEF wChanVibOffset   EQU $C31C  ; [4] 16-bit current vibrato pitch displacement; L1BA7 adds it to the period
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
DEF wChanWaveTrig     EQU $C35C ; [4] byte5 bit7: ApplyPitchBend triggers a wave (SndTriggerWave) instead of bending when set
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
DEF wChanNextPhrase  EQU $C100  ; [4] next chain row's phrase, prefetched
DEF wChanNextSongRow EQU $C2FC  ; [4] next song row, prefetched
DEF wChanReadPtr     EQU $C110  ; [4] 16-bit SRAM cursor; PHRASE_NOTES (bank0) / PHRASE_COMMANDS (bank2),
                                ; same offset $A000+phrase*16+step, selected by SRAM bank
DEF wChanCmdValPtr   EQU $C128  ; [4] 16-bit cursor into PHRASE_COMMAND_VALUES (bank2 $AFF0+)
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
DEF wChanNoteStep    EQU $C21C  ; [4] per-channel step counter since note-on (reset to 0 by SetupSound*); incremented each tick, with the first values (0/1/2) gating note-onset behavior
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
; (addresses verified against the authoritative pointer-init block in bank0 @ $3C6F)
DEF wPtrChainRow     EQU $C8DF  ; -> wChanChainRow[curChan]
DEF wPtrPhraseRow    EQU $C8E1  ; -> wChanPhraseRow[curChan]
DEF wPtrNoteCountdown EQU $C8E3 ; -> wChanNoteCountdown[curChan]
DEF wPtrGroove       EQU $C8E5  ; -> wChanGroove[curChan]
DEF wPtrGrooveRow    EQU $C8E7  ; -> wChanGrooveRow[curChan]
DEF wPtrPhrase       EQU $C8EB  ; -> wChanPhrase[curChan]
DEF wPtrNote         EQU $C8ED  ; -> wChanNote[curChan]
DEF wPtrTableStep    EQU $C8FC  ; -> wChanTableStep[curChan]
; Per-channel GB-sound register shadows, written to hardware by TriggerNote:
; Pitch-bend / slide state (per channel, 16-bit). The command handlers set a
; target + rate; the freq code (bank0 L19A6) steps the current pitch toward the
; target each tick by the rate and clamps on arrival. Reset on note-on.
DEF wChanPitchTarget EQU $C248  ; [4] 16-bit pitch-bend target period (0 = no bend active); set by FxCmd_L
DEF wChanPitchRate   EQU $C2D0  ; [4] 16-bit signed per-tick pitch step toward the target (sign in hi byte
                                ; $C2D1); set by ApplyPitchBend from the FX value; cleared on note-on
DEF wChanPitchCur    EQU $C337  ; [4] 16-bit current pitch value (default $0080) that L19A6 slides toward
                                ; wChanPitchTarget by wChanPitchRate each tick, clamping on arrival; reset by WriteChannelFreq
DEF wChanFinetune    EQU $C696  ; per-pulse-channel finetune (PU1 $C696, PU2 $C697); L1BA7 subtracts it as a
                                ; fine (low-byte) pitch adjustment in the period calc. From instrument finetune param (byte $B)
DEF wChanPeriodTablePtr EQU $C356 ; [4] 16-bit per-channel pointer to the note-period table for pitch interpolation
                                ; (L1B28). NoteToPeriod sets it to wNotePeriods ($CF28), or an alt table when
                                ; wChanSlideNoteSrc is set. Pitched channels only (ch0-2; noise excluded)
DEF wChanEnv         EQU $C2D8  ; [4] envelope value -> rAUDxENV (ch2/WAV = wave level rAUD3LEVEL)
; Per-channel software-envelope working struct, 7 bytes/channel (channel N at base + N*7:
; PU1 $CBBC, PU2 $CBC3, WAV $CBCA; set up by SetupSoundPU1/PU2/...). The three
; stage bytes are the instrument ENV params (bytes 1/9/A); SndTableEnv takes a stage's
; volume (hi nibble) into wChanEnv and steps the fade (lo nibble). Verified live + static.
; (The +N*7 slot for noise would land on $CBD1, but that byte is wOamDirty below -- the
; noise channel has no software-envelope struct here.)
DEF wOamDirty        EQU $CBD1  ; OAM shadow dirty flag: timer ($185F) calls hOamRefresh (OAM DMA) when nonzero; set by OAM-buffer writers (SetOAMByte, screen redraws)
DEF wChanEnvStage1   EQU $CBBC  ; env stage 1: volume (hi nibble) / fade (lo nibble); from instrument byte 1
DEF wChanEnvStage2   EQU $CBBD  ; env stage 2 (from instrument byte 9)
DEF wChanEnvStage3   EQU $CBBE  ; env stage 3 (from instrument byte $A)
DEF wChanLenDuty     EQU $C2DC  ; [4] length/duty value -> rAUDxLEN
DEF wChanFreqHi      EQU $C2E0  ; [4] freq-high + trigger/length-enable bits -> rAUDxHIGH
DEF wPU1Sweep        EQU $C2E4  ; PU1 sweep value -> rAUD1SWEEP
DEF wFxNoiseNote     EQU $C2E7  ; noise channel's note (note-1) captured during FX-command setup (ch3); used as the base in SndClampNoteRange note math and the noise FX commands (mirrors $C4FD)

; --- Link cable sync (see docs/sync.md) -------------------------------------
DEF wFrameSkip       EQU $CB6F  ; SndRunFrame: frames to skip before the next sound tick (tempo/groove)
DEF wTickRepeat      EQU $CB70  ; SndRunFrame: extra sound ticks to run this frame (tempo/groove)
DEF wSyncMode        EQU $CB66  ; 0=OFF 1=LSDJ 3=MIDI 5=KEYBD 6=ANA.IN 7=AN.OUT
DEF wSyncMode2       EQU $CB67  ; secondary sync config (default 6); also the AN.OUT pulse period (TICKS/STEP) reloaded into wAnalogOutTimer
DEF wAnalogOutTimer  EQU $CB68  ; AN.OUT clock pulse-period countdown (SyncSendAnalogClock): reloaded from wSyncMode2; while >= 3 it emits $FF (pulse high) else $00; SyncStopTransport sets it to 3
DEF wSyncInPulses    EQU $CB77  ; AN.IN: received clock-pulse counter (incremented per nonzero rSB in SyncReceiveAnalogClock), reset relative to wSyncMode2 (TICKS/STEP) when a sequencer tick advances
DEF wSyncInGate      EQU $CB78  ; AN.IN: when nonzero, SyncReceiveAnalogClock skips processing the incoming pulse (gate/hold)
DEF wSyncTick        EQU $CB69  ; sync tick counter: $ff = idle; counts up and at 12 fires the sync pulse (L1E93).
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
DEF wTileLoadSrc     EQU $CBE2  ; [16-bit] current tile-load request: source pointer to the 16-byte tile graphics
DEF wTileSlotAlloc   EQU $CBE4  ; running VRAM tile-slot allocator: stored as each request's id, advanced by its tile count
DEF wTileReqTable    EQU $CBE6  ; tile-load request queue base: up to 6 entries x 4 bytes (source ptr + params), scanned by LoadTilesToVram

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
; Input edge/modifier state (set by ProcessInput / L2B8D, read by the screens):
DEF wButtonsSame     EQU $C408  ; 1 if wButtons unchanged since last frame, 0 if changed; ProcessInput gates the
DEF wAPressEnabled    EQU $C409  ; 1 = an A-press is allowed (set on button release, cleared while a button is held). ProcessInput
                                ; key-repeat timer reload on it (changed -> reload). Verified by tracing ProcessInput
DEF wSelectHeld      EQU $C40A  ; nonzero while SELECT is held alone (set by L2B8D); read by screens as the SELECT modifier
DEF wBHeld           EQU $C40B  ; nonzero while B is held alone (set by L2B8D); e.g. disables song-cursor left/right
DEF wAHeld           EQU $C40C  ; nonzero while A is held alone (set by L2B8D)
DEF wCursorButtonHold EQU $C467 ; cursor button-hold state (ProcessInput / editor cursors)
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
DEF wNameEditBuf      EQU $C431 ; 5-char name being edited (instr/file/word/kit-sample)
DEF wFont            EQU $C52E  ; UI font selection 0-2 (project screen FONT; picks the TilesFontN blocks)
DEF wKbdInstrument   EQU $CB93  ; instrument played by the PS/2 keyboard (shown with name on the song screen)
DEF wColInstrument   EQU $C468  ; last-used instrument per channel column (4 bytes, set by SetColInstrument)

; --- session-evidenced RAM (readers named in parentheses) ---
DEF wColChain        EQU $C44F  ; chain under cursor per song column, 4 bytes (SongEnterChain / IsSongChainUsed)
DEF wColPhrase       EQU $C453  ; phrase per song column, 4 bytes (AllocNextFreePhrase scan start)
DEF wUsedTableMap    EQU $C47D  ; 32-slot used-tables map (BuildUsedTablesMap)
DEF wTopBarStr       EQU $C555  ; top-bar/title string buffer (DrawTopBar draws it directly)
DEF wClockFrame60    EQU $C6A8  ; 60-frame divider feeding the TOTAL WORK clock (UpdateWorkClock)
DEF wHelpStrBuf      EQU $C7B0  ; 20-byte staged help/scroll string (UiDrawHelpStr)
DEF wHelpSrcPtr      EQU $C7C4  ; source pointer of the staged help string, 2 bytes
DEF wPlayChanFirst   EQU $C8DD  ; first channel to start playing (SndStartChannels)
DEF wPlayChanLast    EQU $C8DE  ; last channel to start playing
DEF wSavedScreen     EQU $C8FE  ; wCurrentScreen saved around song-context stop (StopAllPlayback)
DEF wFileBlockCounts EQU $C959  ; per-file block counts, 32 slots (CountFileBlocks)
DEF wFileNameBuf     EQU $C987  ; 8-char filename buffer (CopyFileName)
DEF wProjStrBuf      EQU $C998  ; project-screen field string buffer (DrawProjTextField, CloseNameEditor)
DEF wProjCursor      EQU $C9A4  ; project-screen cursor row (ProjCalcFieldTile index)
DEF wSynthBufA       EQU $CAB8  ; 64-byte synth wave work buffer A (ClearSynthWaveBufs)
DEF wSynthBufB       EQU $CAF8  ; 64-byte synth wave work buffer B
DEF wOverwriteStart  EQU $CB61  ; first wave frame for synth OVERWRITE (SynthOverwriteWaves)
DEF wOverwriteEnd    EQU $CB62  ; last wave frame for synth OVERWRITE
DEF wKbdQueueHead    EQU $CB7D  ; PS/2 keyboard byte-queue head (KbdReadByte)
DEF wLinkQueueHead   EQU $CB80  ; link-port ring buffer head (ProcessLinkQueue)
DEF wLinkQueueBuf    EQU $CB81  ; link-port ring buffer (ProcessLinkQueue)
DEF wLfoStatePU1     EQU $CBBF  ; LFO state block, PU1 (SndSetupLfoPU1)
DEF wLfoStatePU2     EQU $CBC6  ; LFO state block, PU2 (SndSetupLfoPU2)
DEF wLfoStateWavNoi  EQU $CBCD  ; LFO state block, WAV/NOI (SndSetupLfoWavNoi)
DEF wSavedSelBottom  EQU $CBD7  ; saved wSelBottom (Save/RestoreSelectionState)
DEF wDecompBuf       EQU $DA00  ; 256-byte block staging buffer (FileWriteCompressedSram)
DEF wSpriteDefs      EQU $DD00  ; sprite definition/staging entries, 4-byte stride (ClearOamBuffer init)
DEF wBlockDstPage    EQU $C97E  ; SRAM dest page hi byte for block save ($a0-$bf; FlushBlockToSram)
DEF wBlockDstBank    EQU $C97C  ; SRAM dest bank counter for block save (bumped on page wrap)

; --- array elements / region bytes (auto-derived; every RAM byte the ROM
; touches carries a symbol so the debugger never shows _RAM_) ---
DEF wChanModFlagPu2   EQU $C0A1  ; wChanModFlag[1] = Pu2 channel
DEF wChanModFlagWav   EQU $C0A2  ; wChanModFlag[2] = Wav channel
DEF wChanModFlagNoi   EQU $C0A3  ; wChanModFlag[3] = Noi channel
DEF wChanModType_p4 EQU $C0A8  ; wChanModType+4
DEF wChanModType_p24 EQU $C0BC  ; wChanModType+24
DEF wChanModType_p32 EQU $C0C4  ; wChanModType+32
DEF wChanNoteWav      EQU $C0CE  ; wChanNote[2] = Wav channel
DEF wChanNoteNoi      EQU $C0CF  ; wChanNote[3] = Noi channel
DEF wSoundParams_p1 EQU $C0D1  ; wSoundParams+1
DEF wSoundParams_p2 EQU $C0D2  ; wSoundParams+2
DEF wSoundParams_p3 EQU $C0D3  ; wSoundParams+3
DEF wSoundParams_p5 EQU $C0D5  ; wSoundParams+5
DEF wSoundParams_p6 EQU $C0D6  ; wSoundParams+6
DEF wSoundParams_p7 EQU $C0D7  ; wSoundParams+7
DEF wSoundParams_p8 EQU $C0D8  ; wSoundParams+8
DEF wSoundParams_p10 EQU $C0DA  ; wSoundParams+10
DEF wSoundParams_p12 EQU $C0DC  ; wSoundParams+12
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
DEF wChanFreq_p1 EQU $C0F5  ; wChanFreq+1
DEF wChanFreq_p2 EQU $C0F6  ; wChanFreq+2
DEF wChanFreq_p3 EQU $C0F7  ; wChanFreq+3
DEF wChanFreq_p4 EQU $C0F8  ; wChanFreq+4
DEF wChanFreq_p5 EQU $C0F9  ; wChanFreq+5
DEF wChanFreq_p8 EQU $C0FC  ; wChanFreq+8
DEF wChanNextPhrase_p8 EQU $C108  ; wChanNextPhrase+8
DEF wChanNextPhrase_p10 EQU $C10A  ; wChanNextPhrase+10
DEF wChanNextPhrase_p12 EQU $C10C  ; wChanNextPhrase+12
DEF wChanReadPtr_p2 EQU $C112  ; wChanReadPtr+2
DEF wChanReadPtr_p4 EQU $C114  ; wChanReadPtr+4
DEF wChanReadPtr_p6 EQU $C116  ; wChanReadPtr+6
DEF wChanReadPtr_p8 EQU $C118  ; wChanReadPtr+8
DEF wChanReadPtr_p10 EQU $C11A  ; wChanReadPtr+10
DEF wChanReadPtr_p12 EQU $C11C  ; wChanReadPtr+12
DEF wTableCmd1Ptr_p2 EQU $C120  ; wTableCmd1Ptr+2
DEF wTableCmd1Ptr_p4 EQU $C122  ; wTableCmd1Ptr+4
DEF wTableCmd1Ptr_p6 EQU $C124  ; wTableCmd1Ptr+6
DEF wChanCmdValPtr_p8 EQU $C130  ; wChanCmdValPtr+8
DEF wTableCmd1ValPtr_p2 EQU $C138  ; wTableCmd1ValPtr+2
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
DEF wChanChain_p4 EQU $C174  ; wChanChain+4
DEF wChanChain_p5 EQU $C175  ; wChanChain+5
DEF wChanChain_p6 EQU $C176  ; wChanChain+6
DEF wChanChain_p8 EQU $C178  ; wChanChain+8
DEF wChanChainRow_p1 EQU $C17D  ; wChanChainRow+1
DEF wChanChainRow_p2 EQU $C17E  ; wChanChainRow+2
DEF wChanChainRow_p3 EQU $C17F  ; wChanChainRow+3
DEF wChanChainRow_p4 EQU $C180  ; wChanChainRow+4
DEF wChanChainRow_p36 EQU $C1A0  ; wChanChainRow+36
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
DEF wChanInstrPu1Hi EQU $C211  ; wChanInstr[0] hi byte (Pu1)
DEF wChanInstrPu2 EQU $C212  ; wChanInstr[1]  (Pu2)
DEF wChanInstrPu2Hi EQU $C213  ; wChanInstr[1] hi byte (Pu2)
DEF wChanInstrWav EQU $C214  ; wChanInstr[2]  (Wav)
DEF wChanChordPu2     EQU $C219  ; wChanChord[1] = Pu2 channel
DEF wChanChordWav     EQU $C21A  ; wChanChord[2] = Wav channel
DEF wChanChordNoi     EQU $C21B  ; wChanChord[3] = Noi channel
DEF wChanNoteStep_p1 EQU $C21D  ; wChanNoteStep+1
DEF wChanNoteStep_p2 EQU $C21E  ; wChanNoteStep+2
DEF wChanNoteStep_p3 EQU $C21F  ; wChanNoteStep+3
DEF wChanNoteStep_p4 EQU $C220  ; wChanNoteStep+4
DEF wChanNoteStep_p5 EQU $C221  ; wChanNoteStep+5
DEF wChanNoteStep_p6 EQU $C222  ; wChanNoteStep+6
DEF wChanNoteStep_p7 EQU $C223  ; wChanNoteStep+7
DEF wChanNoteStep_p8 EQU $C224  ; wChanNoteStep+8
DEF wChanPitchTarget_p2 EQU $C24A  ; wChanPitchTarget+2
DEF wChanPitchTarget_p4 EQU $C24C  ; wChanPitchTarget+4
DEF wChanPitchTarget_p8 EQU $C250  ; wChanPitchTarget+8
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
DEF wFxNoiseNote_p1 EQU $C2E8  ; wFxNoiseNote+1
DEF wFxNoiseNote_p2 EQU $C2E9  ; wFxNoiseNote+2
DEF wFxNoiseNote_p3 EQU $C2EA  ; wFxNoiseNote+3
DEF wFxNoiseNote_p4 EQU $C2EB  ; wFxNoiseNote+4
DEF wFxNoiseNote_p5 EQU $C2EC  ; wFxNoiseNote+5
DEF wFxNoiseNote_p6 EQU $C2ED  ; wFxNoiseNote+6
DEF wFxNoiseNote_p7 EQU $C2EE  ; wFxNoiseNote+7
DEF wFxNoiseNote_p8 EQU $C2EF  ; wFxNoiseNote+8
DEF wChanNextSongRowPu1Hi EQU $C2FD  ; wChanNextSongRow[0] hi byte (Pu1)
DEF wChanNextSongRowPu2 EQU $C2FE  ; wChanNextSongRow[1]  (Pu2)
DEF wChanNextSongRowPu2Hi EQU $C2FF  ; wChanNextSongRow[1] hi byte (Pu2)
DEF wChanNextSongRowWav EQU $C300  ; wChanNextSongRow[2]  (Wav)
DEF wChanVibAccum_p1 EQU $C309  ; wChanVibAccum+1
DEF wChanVibAccum_p2 EQU $C30A  ; wChanVibAccum+2
DEF wChanVibAccum_p3 EQU $C30B  ; wChanVibAccum+3
DEF wChanVibAccum_p4 EQU $C30C  ; wChanVibAccum+4
DEF wChanVibAccum_p5 EQU $C30D  ; wChanVibAccum+5
DEF wChanVibAccum_p6 EQU $C30E  ; wChanVibAccum+6
DEF wChanVibAccum_p7 EQU $C30F  ; wChanVibAccum+7
DEF wChanVibAccum_p8 EQU $C310  ; wChanVibAccum+8
DEF wChanVibAccum_p9 EQU $C311  ; wChanVibAccum+9
DEF wChanVibAccum_p10 EQU $C312  ; wChanVibAccum+10
DEF wChanVibAccum_p11 EQU $C313  ; wChanVibAccum+11
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
DEF wChanTableStep_p1 EQU $C340  ; wChanTableStep+1
DEF wChanTableStep_p2 EQU $C341  ; wChanTableStep+2
DEF wChanTableStep_p3 EQU $C342  ; wChanTableStep+3
DEF wChanTableStep_p4 EQU $C343  ; wChanTableStep+4
DEF wChanTableStep_p5 EQU $C344  ; wChanTableStep+5
DEF wChanTableStep_p6 EQU $C345  ; wChanTableStep+6
DEF wChanTableStep_p8 EQU $C347  ; wChanTableStep+8
DEF wChanTableStep_p9 EQU $C348  ; wChanTableStep+9
DEF wChanTableStep_p10 EQU $C349  ; wChanTableStep+10
DEF wChanTableStep_p11 EQU $C34A  ; wChanTableStep+11
DEF wChanTableStep_p12 EQU $C34B  ; wChanTableStep+12
DEF wChanTableStep_p13 EQU $C34C  ; wChanTableStep+13
DEF wChanTableStep_p14 EQU $C34D  ; wChanTableStep+14
DEF wChanCmdStepPu2   EQU $C34F  ; wChanCmdStep[1] = Pu2 channel
DEF wChanCmdStepWav   EQU $C350  ; wChanCmdStep[2] = Wav channel
DEF wChanPanPu2       EQU $C353  ; wChanPan[1] = Pu2 channel
DEF wChanPanWav       EQU $C354  ; wChanPan[2] = Wav channel
DEF wChanPanNoi       EQU $C355  ; wChanPan[3] = Noi channel
DEF wChanPeriodTablePtr_p2 EQU $C358  ; wChanPeriodTablePtr+2
DEF wChanPeriodTablePtr_p4 EQU $C35A  ; wChanPeriodTablePtr+4
DEF wChanWaveTrigPu2  EQU $C35D  ; wChanWaveTrig[1] = Pu2 channel
DEF wChanWaveTrigWav  EQU $C35E  ; wChanWaveTrig[2] = Wav channel
DEF wChanVibShapePu1Hi EQU $C360  ; wChanVibShape[0] hi byte (Pu1)
DEF wChanVibShapePu2 EQU $C361  ; wChanVibShape[1]  (Pu2)
DEF wChanVibShapePu2Hi EQU $C362  ; wChanVibShape[1] hi byte (Pu2)
DEF wChanVibShapeWav EQU $C363  ; wChanVibShape[2]  (Wav)
DEF wChanVibShapeWavHi EQU $C364  ; wChanVibShape[2] hi byte (Wav)
DEF wChanCmdWav EQU $C369  ; wChanCmd[2]  (Wav)
DEF wChanCmdWavHi EQU $C36A  ; wChanCmd[2] hi byte (Wav)
DEF wChanCmdValPu2 EQU $C36D  ; wChanCmdVal[1]  (Pu2)
DEF wChanCmdValPu2Hi EQU $C36E  ; wChanCmdVal[1] hi byte (Pu2)
DEF wChanCmdValWav EQU $C36F  ; wChanCmdVal[2]  (Wav)
DEF wChanCmdValWavHi EQU $C370  ; wChanCmdVal[2] hi byte (Wav)
DEF wChanPitchModActivePu2  EQU $C3FA  ; wChanPitchModActive[1] = Pu2 channel
DEF wChanPitchModActiveWav  EQU $C3FB  ; wChanPitchModActive[2] = Wav channel
DEF wIsCGB_p1 EQU $C403  ; wIsCGB+1
DEF wIsCGB_p2 EQU $C404  ; wIsCGB+2
DEF wClipboardType_p1 EQU $C412  ; wClipboardType+1
DEF wSongRow_p1 EQU $C41F  ; wSongRow+1
DEF wSongRow_p2 EQU $C420  ; wSongRow+2
DEF wSongRow_p3 EQU $C421  ; wSongRow+3
DEF wSongRow_p4 EQU $C422  ; wSongRow+4
DEF wSongRow_p5 EQU $C423  ; wSongRow+5
DEF wNameEditBuf_p1 EQU $C432  ; wNameEditBuf+1
DEF wNameEditBuf_p2 EQU $C433  ; wNameEditBuf+2
DEF wNameEditBuf_p3 EQU $C434  ; wNameEditBuf+3
DEF wNameEditBuf_p4 EQU $C435  ; wNameEditBuf+4
DEF wNameEditBuf_p5 EQU $C436  ; wNameEditBuf+5
DEF wSampleNoteString_p1 EQU $C43B  ; wSampleNoteString+1
DEF wSampleNoteString_p2 EQU $C43C  ; wSampleNoteString+2
DEF wSampleNoteString_p3 EQU $C43D  ; wSampleNoteString+3
DEF wSampleNoteString_p4 EQU $C43E  ; wSampleNoteString+4
DEF wSampleNoteString_p5 EQU $C43F  ; wSampleNoteString+5
DEF wSampleNoteString_p6 EQU $C440  ; wSampleNoteString+6
DEF wSynthCursor_p1 EQU $C443  ; wSynthCursor+1
DEF wTableCellAPtr_p1 EQU $C448  ; wTableCellAPtr+1
DEF wTableCellBPtr_p1 EQU $C44A  ; wTableCellBPtr+1
DEF wTableCellBPtr_p2 EQU $C44B  ; wTableCellBPtr+2
DEF wTableCellBPtr_p3 EQU $C44C  ; wTableCellBPtr+3
DEF wTableCellBPtr_p4 EQU $C44D  ; wTableCellBPtr+4
DEF wTableCellBPtr_p5 EQU $C44E  ; wTableCellBPtr+5
DEF wColChain_p1 EQU $C450  ; wColChain+1
DEF wColChain_p2 EQU $C451  ; wColChain+2
DEF wColChain_p3 EQU $C452  ; wColChain+3
DEF wColPhrase_p1 EQU $C454  ; wColPhrase+1
DEF wColPhrase_p2 EQU $C455  ; wColPhrase+2
DEF wColPhrase_p3 EQU $C456  ; wColPhrase+3
DEF wColInstrument_p3 EQU $C46B  ; wColInstrument+3
DEF wColInstrument_p4 EQU $C46C  ; wColInstrument+4
DEF wUsedTableMap_p16 EQU $C48D  ; wUsedTableMap+16
DEF wRetrigger_p1 EQU $C4CE  ; wRetrigger+1
DEF wRetrigger_p2 EQU $C4CF  ; wRetrigger+2
DEF wRetrigger_p3 EQU $C4D0  ; wRetrigger+3
DEF wRetrigger_p4 EQU $C4D1  ; wRetrigger+4
DEF wPitchModA_p1 EQU $C4D5  ; wPitchModA+1
DEF wPitchModA_p2 EQU $C4D6  ; wPitchModA+2
DEF wPitchModA_p3 EQU $C4D7  ; wPitchModA+3
DEF wPitchModB_p1 EQU $C4D9  ; wPitchModB+1
DEF wPitchModB_p2 EQU $C4DA  ; wPitchModB+2
DEF wPitchModB_p3 EQU $C4DB  ; wPitchModB+3
DEF wPitchModB_p4 EQU $C4DC  ; wPitchModB+4
DEF wPitchModB_p5 EQU $C4DD  ; wPitchModB+5
DEF wPitchModB_p6 EQU $C4DE  ; wPitchModB+6
DEF wPitchModB_p7 EQU $C4DF  ; wPitchModB+7
DEF wPitchModB_p8 EQU $C4E0  ; wPitchModB+8
DEF wPitchModB_p9 EQU $C4E1  ; wPitchModB+9
DEF wPitchModB_p10 EQU $C4E2  ; wPitchModB+10
DEF wPitchModB_p11 EQU $C4E3  ; wPitchModB+11
DEF wOscLoopB_p1 EQU $C4E6  ; wOscLoopB+1
DEF wOscLoopB_p2 EQU $C4E7  ; wOscLoopB+2
DEF wOscLoopB_p3 EQU $C4E8  ; wOscLoopB+3
DEF wOscLoopB_p4 EQU $C4E9  ; wOscLoopB+4
DEF wOscLoopB_p5 EQU $C4EA  ; wOscLoopB+5
DEF wOscLoopB_p6 EQU $C4EB  ; wOscLoopB+6
DEF wOscLoopB_p7 EQU $C4EC  ; wOscLoopB+7
DEF wOscLoopB_p8 EQU $C4ED  ; wOscLoopB+8
DEF wOscLoopB_p9 EQU $C4EE  ; wOscLoopB+9
DEF wOscLoopB_p10 EQU $C4EF  ; wOscLoopB+10
DEF wOscLoopB_p11 EQU $C4F0  ; wOscLoopB+11
DEF wOscLoopB_p12 EQU $C4F1  ; wOscLoopB+12
DEF wOscLoopB_p13 EQU $C4F2  ; wOscLoopB+13
DEF wOscLoopB_p14 EQU $C4F3  ; wOscLoopB+14
DEF wOscBankB_p1 EQU $C4F6  ; wOscBankB+1
DEF wOscBankB_p2 EQU $C4F7  ; wOscBankB+2
DEF wOscBankB_p3 EQU $C4F8  ; wOscBankB+3
DEF wOscBankB_p4 EQU $C4F9  ; wOscBankB+4
DEF wOscBankB_p5 EQU $C4FA  ; wOscBankB+5
DEF wOscBankB_p6 EQU $C4FB  ; wOscBankB+6
DEF wOscBankB_p7 EQU $C4FC  ; wOscBankB+7
DEF wOscBankB_p8 EQU $C4FD  ; wOscBankB+8
DEF wOscBankB_p9 EQU $C4FE  ; wOscBankB+9
DEF wOscBankB_p10 EQU $C4FF  ; wOscBankB+10
DEF wWavMode_p1 EQU $C501  ; wWavMode+1
DEF wWavMode_p33 EQU $C521  ; wWavMode+33
DEF wWavMode_p34 EQU $C522  ; wWavMode+34
DEF wWavMode_p35 EQU $C523  ; wWavMode+35
DEF wWavMode_p36 EQU $C524  ; wWavMode+36
DEF wWavMode_p37 EQU $C525  ; wWavMode+37
DEF wColorSet_p1 EQU $C530  ; wColorSet+1
DEF wTopBarStr_p13 EQU $C562  ; wTopBarStr+13
DEF wSelectPal_p1 EQU $C56A  ; wSelectPal+1
DEF wSelectPal_p2 EQU $C56B  ; wSelectPal+2
DEF wRedrawPending_p1 EQU $C56D  ; wRedrawPending+1
DEF wButtonWriteIdx_p1 EQU $C571  ; wButtonWriteIdx+1
DEF wButtonWriteIdx_p2 EQU $C572  ; wButtonWriteIdx+2
DEF wVramCursor_p1 EQU $C574  ; wVramCursor+1
DEF wTimerReload_p1 EQU $C579  ; wTimerReload+1
DEF wTimerReload_p2 EQU $C57A  ; wTimerReload+2
DEF wTimerReload_p3 EQU $C57B  ; wTimerReload+3
DEF wIdleFrames_p2 EQU $C68E  ; wIdleFrames+2
DEF wIdleFrames_p3 EQU $C68F  ; wIdleFrames+3
DEF wIdleFrames_p4 EQU $C690  ; wIdleFrames+4
DEF wIdleFrames_p5 EQU $C691  ; wIdleFrames+5
DEF wIdleFrames_p6 EQU $C692  ; wIdleFrames+6
DEF wIdleFrames_p8 EQU $C694  ; wIdleFrames+8
DEF wIdleFrames_p9 EQU $C695  ; wIdleFrames+9
DEF wChanFinetunePu1Hi EQU $C697  ; wChanFinetune[0] hi byte (Pu1)
DEF wChanFinetunePu2 EQU $C698  ; wChanFinetune[1]  (Pu2)
DEF wChanFinetuneWav EQU $C69A  ; wChanFinetune[2]  (Wav)
DEF wChanFinetuneWavHi EQU $C69B  ; wChanFinetune[2] hi byte (Wav)
DEF wChanFinetuneNoi EQU $C69C  ; wChanFinetune[3]  (Noi)
DEF wWorkTimeAccum_p2 EQU $C69F  ; wWorkTimeAccum+2
DEF wMsgStrPtr_p2 EQU $C7AC  ; wMsgStrPtr+2
DEF wMsgStrPtr_p3 EQU $C7AD  ; wMsgStrPtr+3
DEF wHelpStrBuf_p1 EQU $C7B1  ; wHelpStrBuf+1
DEF wHelpStrBuf_p2 EQU $C7B2  ; wHelpStrBuf+2
DEF wHelpStrBuf_p3 EQU $C7B3  ; wHelpStrBuf+3
DEF wHelpStrBuf_p4 EQU $C7B4  ; wHelpStrBuf+4
DEF wHelpStrBuf_p5 EQU $C7B5  ; wHelpStrBuf+5
DEF wHelpStrBuf_p6 EQU $C7B6  ; wHelpStrBuf+6
DEF wHelpStrBuf_p7 EQU $C7B7  ; wHelpStrBuf+7
DEF wHelpStrBuf_p8 EQU $C7B8  ; wHelpStrBuf+8
DEF wHelpStrBuf_p9 EQU $C7B9  ; wHelpStrBuf+9
DEF wHelpStrBuf_p10 EQU $C7BA  ; wHelpStrBuf+10
DEF wHelpStrBuf_p11 EQU $C7BB  ; wHelpStrBuf+11
DEF wHelpStrBuf_p12 EQU $C7BC  ; wHelpStrBuf+12
DEF wHelpStrBuf_p13 EQU $C7BD  ; wHelpStrBuf+13
DEF wHelpStrBuf_p14 EQU $C7BE  ; wHelpStrBuf+14
DEF wHelpStrBuf_p15 EQU $C7BF  ; wHelpStrBuf+15
DEF wHelpStrBuf_p16 EQU $C7C0  ; wHelpStrBuf+16
DEF wHelpStrBuf_p17 EQU $C7C1  ; wHelpStrBuf+17
DEF wHelpStrBuf_p18 EQU $C7C2  ; wHelpStrBuf+18
DEF wHelpStrBuf_p19 EQU $C7C3  ; wHelpStrBuf+19
DEF wHelpSrcPtr_p1 EQU $C7C5  ; wHelpSrcPtr+1
DEF wFxCommand_p1 EQU $C8C5  ; wFxCommand+1
DEF wChanCmdRatePu2   EQU $C8C7  ; wChanCmdRate[1] = Pu2 channel
DEF wChanCmdRateWav   EQU $C8C8  ; wChanCmdRate[2] = Wav channel
DEF wChanCmdRateNoi   EQU $C8C9  ; wChanCmdRate[3] = Noi channel
DEF wChanCmdCountdown_p1 EQU $C8CB  ; wChanCmdCountdown+1
DEF wChanCmdCountdown_p2 EQU $C8CC  ; wChanCmdCountdown+2
DEF wChanCmdCountdown_p3 EQU $C8CD  ; wChanCmdCountdown+3
DEF wChanCmdCountdown_p16 EQU $C8DA  ; wChanCmdCountdown+16
DEF wChanCmdCountdown_p17 EQU $C8DB  ; wChanCmdCountdown+17
DEF wChanCmdCountdown_p18 EQU $C8DC  ; wChanCmdCountdown+18
DEF wPtrNote_p2 EQU $C8EF  ; wPtrNote+2
DEF wPtrNote_p4 EQU $C8F1  ; wPtrNote+4
DEF wPtrNote_p5 EQU $C8F2  ; wPtrNote+5
DEF wOscActive_p1 EQU $C8F7  ; wOscActive+1
DEF wOscActive_p2 EQU $C8F8  ; wOscActive+2
DEF wOscActive_p3 EQU $C8F9  ; wOscActive+3
DEF wOscActive_p4 EQU $C8FA  ; wOscActive+4
DEF wDefaultNotes_p1 EQU $C905  ; wDefaultNotes+1
DEF wDefaultNotes_p2 EQU $C906  ; wDefaultNotes+2
DEF wDefaultNotes_p3 EQU $C907  ; wDefaultNotes+3
DEF wDefaultNotes_p4 EQU $C908  ; wDefaultNotes+4
DEF wDefaultNotes_p5 EQU $C909  ; wDefaultNotes+5
DEF wDefaultNotes_p6 EQU $C90A  ; wDefaultNotes+6
DEF wDefaultNotes_p7 EQU $C90B  ; wDefaultNotes+7
DEF wDefaultNotes_p8 EQU $C90C  ; wDefaultNotes+8
DEF wDefaultNotes_p41 EQU $C92D  ; wDefaultNotes+41
DEF wDefaultNotes_p42 EQU $C92E  ; wDefaultNotes+42
DEF wTableRow_p1 EQU $C931  ; wTableRow+1
DEF wTableRow_p2 EQU $C932  ; wTableRow+2
DEF wTableRow_p3 EQU $C933  ; wTableRow+3
DEF wHelpCursorRow_p1 EQU $C935  ; wHelpCursorRow+1
DEF wHelpCursorRow_p2 EQU $C936  ; wHelpCursorRow+2
DEF wWordEditCol_p1 EQU $C939  ; wWordEditCol+1
DEF wWordEditCol_p3 EQU $C93B  ; wWordEditCol+3
DEF wWordEditCol_p4 EQU $C93C  ; wWordEditCol+4
DEF wWordEditCol_p13 EQU $C945  ; wWordEditCol+13
DEF wWordEditCol_p14 EQU $C946  ; wWordEditCol+14
DEF wWordEditCol_p15 EQU $C947  ; wWordEditCol+15
DEF wWordEditCol_p16 EQU $C948  ; wWordEditCol+16
DEF wWordEditCol_p18 EQU $C94A  ; wWordEditCol+18
DEF wWordEditCol_p19 EQU $C94B  ; wWordEditCol+19
DEF wWordEditCol_p20 EQU $C94C  ; wWordEditCol+20
DEF wWordEditCol_p21 EQU $C94D  ; wWordEditCol+21
DEF wWordEditCol_p22 EQU $C94E  ; wWordEditCol+22
DEF wWordEditCol_p23 EQU $C94F  ; wWordEditCol+23
DEF wPlayMarkerTimer_p1 EQU $C951  ; wPlayMarkerTimer+1
DEF wPlayMarkerTimer_p2 EQU $C952  ; wPlayMarkerTimer+2
DEF wPlayMarkerTimer_p3 EQU $C953  ; wPlayMarkerTimer+3
DEF wSyncSendRate_p1 EQU $C955  ; wSyncSendRate+1
DEF wSyncSendAccum_p1 EQU $C957  ; wSyncSendAccum+1
DEF wSyncSendAccum_p2 EQU $C958  ; wSyncSendAccum+2
DEF wCompBank_p1 EQU $C97A  ; wCompBank+1
DEF wCompByte_p1 EQU $C980  ; wCompByte+1
DEF wCompByte_p2 EQU $C981  ; wCompByte+2
DEF wCompByte_p3 EQU $C982  ; wCompByte+3
DEF wDecompPos_p1 EQU $C984  ; wDecompPos+1
DEF wFileNameBuf_p8 EQU $C98F  ; wFileNameBuf+8
DEF wFileNameBuf_p9 EQU $C990  ; wFileNameBuf+9
DEF wFileRedrawCount_p1 EQU $C993  ; wFileRedrawCount+1
DEF wFileRedrawCount_p2 EQU $C994  ; wFileRedrawCount+2
DEF wIsSelectingFile_p1 EQU $C996  ; wIsSelectingFile+1
DEF wIsSelectingFile_p2 EQU $C997  ; wIsSelectingFile+2
DEF wFileCursorRestrict_p1 EQU $C99A  ; wFileCursorRestrict+1
DEF wFileScroll_p1 EQU $C99C  ; wFileScroll+1
DEF wFileScroll_p2 EQU $C99D  ; wFileScroll+2
DEF wFileScroll_p6 EQU $C9A1  ; wFileScroll+6
DEF wFileScroll_p7 EQU $C9A2  ; wFileScroll+7
DEF wFileScroll_p8 EQU $C9A3  ; wFileScroll+8
DEF wProjCursor_p1 EQU $C9A5  ; wProjCursor+1
DEF wProjCursor_p2 EQU $C9A6  ; wProjCursor+2
DEF wProjCursor_p3 EQU $C9A7  ; wProjCursor+3
DEF wProjCursor_p4 EQU $C9A8  ; wProjCursor+4
DEF wProjCursor_p5 EQU $C9A9  ; wProjCursor+5
DEF wProjCursor_p6 EQU $C9AA  ; wProjCursor+6
DEF wProjCursor_p7 EQU $C9AB  ; wProjCursor+7
DEF wProjCursor_p16 EQU $C9B4  ; wProjCursor+16
DEF wProjCursor_p17 EQU $C9B5  ; wProjCursor+17
DEF wProjCursor_p18 EQU $C9B6  ; wProjCursor+18
DEF wProjCursor_p19 EQU $C9B7  ; wProjCursor+19
DEF wProjCursor_p20 EQU $C9B8  ; wProjCursor+20
DEF wProjCursor_p21 EQU $C9B9  ; wProjCursor+21
DEF wProjCursor_p22 EQU $C9BA  ; wProjCursor+22
DEF wProjCursor_p23 EQU $C9BB  ; wProjCursor+23
DEF wProjCursor_p24 EQU $C9BC  ; wProjCursor+24
DEF wProjCursor_p25 EQU $C9BD  ; wProjCursor+25
DEF wProjCursor_p26 EQU $C9BE  ; wProjCursor+26
DEF wProjCursor_p27 EQU $C9BF  ; wProjCursor+27
DEF wProjCursor_p28 EQU $C9C0  ; wProjCursor+28
DEF wProjCursor_p29 EQU $C9C1  ; wProjCursor+29
DEF wProjCursor_p30 EQU $C9C2  ; wProjCursor+30
DEF wProjCursor_p31 EQU $C9C3  ; wProjCursor+31
DEF wProjCursor_p32 EQU $C9C4  ; wProjCursor+32
DEF wProjCursor_p33 EQU $C9C5  ; wProjCursor+33
DEF wProjCursor_p34 EQU $C9C6  ; wProjCursor+34
DEF wProjCursor_p35 EQU $C9C7  ; wProjCursor+35
DEF wProjCursor_p36 EQU $C9C8  ; wProjCursor+36
DEF wProjCursor_p37 EQU $C9C9  ; wProjCursor+37
DEF wProjCursor_p38 EQU $C9CA  ; wProjCursor+38
DEF wProjCursor_p39 EQU $C9CB  ; wProjCursor+39
DEF wProjCursor_p40 EQU $C9CC  ; wProjCursor+40
DEF wProjCursor_p41 EQU $C9CD  ; wProjCursor+41
DEF wProjCursor_p46 EQU $C9D2  ; wProjCursor+46
DEF wGrooveRow_p1 EQU $C9D5  ; wGrooveRow+1
DEF wGrooveRow_p18 EQU $C9E6  ; wGrooveRow+18
DEF wGrooveRow_p20 EQU $C9E8  ; wGrooveRow+20
DEF wGrooveRow_p21 EQU $C9E9  ; wGrooveRow+21
DEF wGrooveRow_p22 EQU $C9EA  ; wGrooveRow+22
DEF wGrooveRow_p23 EQU $C9EB  ; wGrooveRow+23
DEF wGrooveRow_p24 EQU $C9EC  ; wGrooveRow+24
DEF wGrooveRow_p25 EQU $C9ED  ; wGrooveRow+25
DEF wGrooveRow_p26 EQU $C9EE  ; wGrooveRow+26
DEF wGrooveRow_p27 EQU $C9EF  ; wGrooveRow+27
DEF wSynthRenderIdx_p1 EQU $C9F1  ; wSynthRenderIdx+1
DEF wSynthRenderIdx_p2 EQU $C9F2  ; wSynthRenderIdx+2
DEF wSynthBufB_p64 EQU $CB38  ; wSynthBufB+64
DEF wOverwriteEnd_p1 EQU $CB63  ; wOverwriteEnd+1
DEF wOverwriteEnd_p3 EQU $CB65  ; wOverwriteEnd+3
DEF wSyncTick_p1 EQU $CB6A  ; wSyncTick+1
DEF wSyncHandshake_p1 EQU $CB6D  ; wSyncHandshake+1
DEF wSyncRxPos_p1 EQU $CB72  ; wSyncRxPos+1
DEF wSyncRxPos_p2 EQU $CB73  ; wSyncRxPos+2
DEF wSyncRxPos_p3 EQU $CB74  ; wSyncRxPos+3
DEF wSyncRxPos_p4 EQU $CB75  ; wSyncRxPos+4
DEF wSyncRxPos_p5 EQU $CB76  ; wSyncRxPos+5
DEF wSyncInGate_p1 EQU $CB79  ; wSyncInGate+1
DEF wSyncInGate_p2 EQU $CB7A  ; wSyncInGate+2
DEF wSyncInGate_p3 EQU $CB7B  ; wSyncInGate+3
DEF wSyncInGate_p4 EQU $CB7C  ; wSyncInGate+4
DEF wKbdHeldNote_p1 EQU $CB7F  ; wKbdHeldNote+1
DEF wKbdTranspose_p1 EQU $CB92  ; wKbdTranspose+1
DEF wKbdInstrument_p1 EQU $CB94  ; wKbdInstrument+1
DEF wKbdInstrument_p2 EQU $CB95  ; wKbdInstrument+2
DEF wKbdInstrument_p3 EQU $CB96  ; wKbdInstrument+3
DEF wKbdInstrument_p4 EQU $CB97  ; wKbdInstrument+4
DEF wKbdInstrument_p6 EQU $CB99  ; wKbdInstrument+6
DEF wKbdInstrument_p7 EQU $CB9A  ; wKbdInstrument+7
DEF wKbdInstrument_p8 EQU $CB9B  ; wKbdInstrument+8
DEF wKbdInstrument_p9 EQU $CB9C  ; wKbdInstrument+9
DEF wKbdInstrument_p10 EQU $CB9D  ; wKbdInstrument+10
DEF wKbdInstrument_p11 EQU $CB9E  ; wKbdInstrument+11
DEF wKbdInstrument_p12 EQU $CB9F  ; wKbdInstrument+12
DEF wKbdInstrument_p13 EQU $CBA0  ; wKbdInstrument+13
DEF wKbdInstrument_p14 EQU $CBA1  ; wKbdInstrument+14
DEF wKbdInstrument_p15 EQU $CBA2  ; wKbdInstrument+15
DEF wKbdInstrument_p16 EQU $CBA3  ; wKbdInstrument+16
DEF wKbdInstrument_p17 EQU $CBA4  ; wKbdInstrument+17
DEF wKbdInstrument_p25 EQU $CBAC  ; wKbdInstrument+25
DEF wKbdInstrument_p33 EQU $CBB4  ; wKbdInstrument+33
DEF wLfoStatePU1_p1 EQU $CBC0  ; wLfoStatePU1+1
DEF wLfoStatePU1_p2 EQU $CBC1  ; wLfoStatePU1+2
DEF wLfoStatePU1_p3 EQU $CBC2  ; wLfoStatePU1+3
DEF wLfoStatePU1_p4 EQU $CBC3  ; wLfoStatePU1+4
DEF wLfoStatePU1_p5 EQU $CBC4  ; wLfoStatePU1+5
DEF wLfoStatePU1_p6 EQU $CBC5  ; wLfoStatePU1+6
DEF wLfoStatePU2_p1 EQU $CBC7  ; wLfoStatePU2+1
DEF wLfoStatePU2_p2 EQU $CBC8  ; wLfoStatePU2+2
DEF wLfoStatePU2_p3 EQU $CBC9  ; wLfoStatePU2+3
DEF wLfoStatePU2_p4 EQU $CBCA  ; wLfoStatePU2+4
DEF wLfoStatePU2_p5 EQU $CBCB  ; wLfoStatePU2+5
DEF wLfoStatePU2_p6 EQU $CBCC  ; wLfoStatePU2+6
DEF wLfoStateWavNoi_p1 EQU $CBCE  ; wLfoStateWavNoi+1
DEF wLfoStateWavNoi_p2 EQU $CBCF  ; wLfoStateWavNoi+2
DEF wLfoStateWavNoi_p3 EQU $CBD0  ; wLfoStateWavNoi+3
DEF wSavedSelBottom_p1 EQU $CBD8  ; wSavedSelBottom+1
DEF wTileLoadSrc_p1 EQU $CBE3  ; wTileLoadSrc+1
DEF wTileSlotAlloc_p1 EQU $CBE5  ; wTileSlotAlloc+1
DEF wTileReqTable_p17 EQU $CBF7  ; wTileReqTable+17
DEF wTileReqTable_p18 EQU $CBF8  ; wTileReqTable+18
DEF wTileReqTable_p19 EQU $CBF9  ; wTileReqTable+19
DEF wTileReqTable_p20 EQU $CBFA  ; wTileReqTable+20
DEF wTileReqTable_p21 EQU $CBFB  ; wTileReqTable+21
DEF wTileReqTable_p22 EQU $CBFC  ; wTileReqTable+22
DEF wDecompBuf_p64 EQU $DA40  ; wDecompBuf+64
DEF wSpriteDefs_p1 EQU $DD01  ; wSpriteDefs+1
DEF wSpriteDefs_p4 EQU $DD04  ; wSpriteDefs+4
DEF wSpriteDefs_p5 EQU $DD05  ; wSpriteDefs+5
DEF wSpriteDefs_p6 EQU $DD06  ; wSpriteDefs+6
DEF wSpriteDefs_p8 EQU $DD08  ; wSpriteDefs+8
DEF wSpriteDefs_p9 EQU $DD09  ; wSpriteDefs+9
DEF wSpriteDefs_p10 EQU $DD0A  ; wSpriteDefs+10
DEF wSpriteDefs_p12 EQU $DD0C  ; wSpriteDefs+12
DEF wSpriteDefs_p15 EQU $DD0F  ; wSpriteDefs+15
DEF wSpriteDefs_p16 EQU $DD10  ; wSpriteDefs+16
DEF wSpriteDefs_p19 EQU $DD13  ; wSpriteDefs+19
DEF wSpriteDefs_p20 EQU $DD14  ; wSpriteDefs+20
DEF wSpriteDefs_p23 EQU $DD17  ; wSpriteDefs+23
DEF wSpriteDefs_p24 EQU $DD18  ; wSpriteDefs+24
DEF wSpriteDefs_p27 EQU $DD1B  ; wSpriteDefs+27
DEF wSpriteDefs_p28 EQU $DD1C  ; wSpriteDefs+28
DEF wSpriteDefs_p31 EQU $DD1F  ; wSpriteDefs+31
DEF wSpriteDefs_p32 EQU $DD20  ; wSpriteDefs+32
DEF wSpriteDefs_p35 EQU $DD23  ; wSpriteDefs+35
DEF wSpriteDefs_p36 EQU $DD24  ; wSpriteDefs+36
DEF wSpriteDefs_p39 EQU $DD27  ; wSpriteDefs+39
DEF wSpriteDefs_p40 EQU $DD28  ; wSpriteDefs+40
DEF wSpriteDefs_p43 EQU $DD2B  ; wSpriteDefs+43
DEF wSpriteDefs_p45 EQU $DD2D  ; wSpriteDefs+45
DEF wSpriteDefs_p49 EQU $DD31  ; wSpriteDefs+49
DEF wSpriteDefs_p53 EQU $DD35  ; wSpriteDefs+53
DEF wSpriteDefs_p57 EQU $DD39  ; wSpriteDefs+57
DEF wSpriteDefs_p61 EQU $DD3D  ; wSpriteDefs+61
DEF hOamRefresh_p1 EQU $FF81  ; hOamRefresh+1
DEF hOscilloscope_p1 EQU $FFA1  ; hOscilloscope+1
DEF hOscilloscope_p2 EQU $FFA2  ; hOscilloscope+2
DEF hOscilloscope_p3 EQU $FFA3  ; hOscilloscope+3
DEF hOscilloscope_p4 EQU $FFA4  ; hOscilloscope+4
DEF hOscilloscope_p5 EQU $FFA5  ; hOscilloscope+5
DEF hOscilloscope_p6 EQU $FFA6  ; hOscilloscope+6
DEF hOscilloscope_p7 EQU $FFA7  ; hOscilloscope+7
DEF hOscilloscope_p8 EQU $FFA8  ; hOscilloscope+8
DEF hOscilloscope_p9 EQU $FFA9  ; hOscilloscope+9
DEF hOscilloscope_p10 EQU $FFAA  ; hOscilloscope+10
DEF hOscilloscope_p11 EQU $FFAB  ; hOscilloscope+11
DEF hOscilloscope_p12 EQU $FFAC  ; hOscilloscope+12
DEF hOscilloscope_p13 EQU $FFAD  ; hOscilloscope+13
DEF hOscilloscope_p14 EQU $FFAE  ; hOscilloscope+14
DEF hOscilloscope_p15 EQU $FFAF  ; hOscilloscope+15
DEF hOscilloscope_p16 EQU $FFB0  ; hOscilloscope+16
DEF hOscilloscope_p17 EQU $FFB1  ; hOscilloscope+17
DEF hOscilloscope_p18 EQU $FFB2  ; hOscilloscope+18
DEF hOscilloscope_p19 EQU $FFB3  ; hOscilloscope+19
DEF hOscilloscope_p20 EQU $FFB4  ; hOscilloscope+20
DEF hOscilloscope_p41 EQU $FFC9  ; hOscilloscope+41

; --- orphan RAM addresses from the debugger export, named from their readers ---
DEF wNoteOctave      EQU $C693  ; octave/fine index stored by NoteToPeriod; indexes wNoteFineTable-1 (WriteChannelFreq)
DEF wChanTableBufWav EQU $C1C0  ; WAV channel 32-byte running-table buffer ($ff-filled at note setup, SetupSoundWave)
DEF wChanTableBufNoi EQU $C1E0  ; NOI channel 32-byte running-table buffer (SetupSoundNoise)
DEF wSelHoldFrames   EQU $C676  ; frames SELECT has been held (ProcessInput resets on other input)
DEF wSelHoldFrames2  EQU $C677  ; second SELECT-hold counter byte
DEF wFxWaveParam     EQU $C8C2  ; W command value latch (lo nibble; consumed via SndSetKitParams)
DEF wFxWaveParam2    EQU $C8C3  ; W command companion latch
DEF wSynthRenderState EQU $CB3A ; synth render params copied at buffer init (3 words: +0/+2/+4)
DEF wSynthRenderState_p2 EQU $CB3C ; synth render param word 2
DEF wSynthRenderState_p4 EQU $CB3E ; synth render param word 3
DEF wWaveEditX       EQU $CB60  ; wave-editor pen X: (column*4) & $f0 (SynthWriteWaveCol)
DEF wNotePeriodLo    EQU $CD28  ; RAM copy of the note->period table (NoteToPeriod indexes by note)
DEF wNoteFineTable   EQU $CD94  ; RAM copy of the finetune/delta table (WriteChannelFreq, index wNoteOctave-1)
DEF wMasterVolTable  EQU $D400  ; master-volume transition LUT (FxCmd_M indexes by rAUDVOL & $70)
DEF wOscBufEnd       EQU $DA4F  ; last byte of the $DA00 buffer (OrBitsIntoDaBuffer bound; buffer reused by save staging)
DEF wSpriteDefs_p65  EQU $DD41  ; wSpriteDefs+65
DEF wSpriteDefs_p69  EQU $DD45  ; wSpriteDefs+69
DEF wSpriteDefs_p73  EQU $DD49  ; wSpriteDefs+73
DEF wStackTop        EQU $DFFF  ; top of WRAM / initial stack area (Start clears $20 bytes downward)
