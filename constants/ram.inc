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

; --- Link cable sync (see docs/sync.md) -------------------------------------
DEF wFrameSkip       EQU $CB6F  ; SndRunFrame: frames to skip before the next sound tick (tempo/groove)
DEF wTickRepeat      EQU $CB70  ; SndRunFrame: extra sound ticks to run this frame (tempo/groove)
DEF wSyncMode        EQU $CB66  ; 0=OFF 1=LSDJ 3=MIDI 5=KEYBD 6=ANA.IN 7=AN.OUT
DEF wSyncMode2       EQU $CB67  ; secondary sync config (default 6); also the AN.OUT pulse period (TICKS/STEP) reloaded into wAnalogOutTimer
DEF wAnalogOutTimer  EQU $CB68  ; AN.OUT clock pulse-period countdown (SyncSendAnalogClock): reloaded from wSyncMode2; while >= 3 it emits $FF (pulse high) else $00; SyncStopTransport sets it to 3
DEF wSyncTick        EQU $CB69  ; sync tick counter: $ff = idle; counts up and at 12 fires the sync pulse (L1E93).
                                ; Verified live: $ff while LSDJ sync is idle (no partner)
DEF wSyncRun         EQU $CB6B  ; sync running flag: 1 = sync clock active, 0 = stopped (set by the sync start/stop
                                ; handlers; also gated to the SONG screen in LSDJ mode). Verified live: 0 while idle
DEF wSyncHandshake   EQU $CB6C  ; set =1 before SendSyncHandshake (handshake pending); cleared on sync stop
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
